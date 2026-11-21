// SPDX-License-Identifier: AGPL-3.0-only
// The sequencer's settings and sheets (docs/UI.md, "Sequencer"):
//
//   Workflow  Shift + Step 3 (the Move's, 13): quantize amount, step grid
//             (the pattern's Q), count-in, Melodee's Rec Quantize, time
//             signature, a free take's longest, the metronome on Rec only,
//             BPM per pattern. The wheel walks the rows, a push edits one
//             (the wheel turns it), a push again or Back is done.
//   Repeat    Shift + Step 11 (the Move's, 11.6): Note Repeat on, and its
//             screen: the style (Off, Repeat; on a note track the arp's Up,
//             Down, Up-down, Random, Order), the rate, pressure as velocity;
//             the wheel turns the row the push picks. Shift + Step 11 again: off.
//   Capture   a capture played stopped (14.3): three tempos it fits, the
//             wheel picks (the pattern follows), push or Back done.
#include "app/screens/common.h"
#include "app/sequencer.h"
#include "melodee/engine/arp.h"

namespace app::screens {
namespace {

using bindings::Job;

class Workflow final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override { editing = false; }
    bool job(Job job, const bindings::Use& use) override {
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (editing) change(use.delta);
            else row = std::clamp(row + (use.delta > 0 ? 1 : -1), 0, rows - 1);
            return true;
        case Job::ListPush: editing = !editing; return true;
        case Job::Back: if (editing) editing = false; else studio.close(); return true;
        case Job::Workflow: studio.close(); return true; // Shift + Step 3 again
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        if (auto* play = studio.screen(home_screen(studio.kind()))) play->lights(lights);
        for (unsigned s = 0; s < move::step_count; ++s) lights.step(s, move::colour::off);
        lights.button(move::Button::Back, move::white_dim);
    }
    void draw(juce::Graphics& g) override {
        ui::rail(g, "WORK", "", editing ? "EDIT" : "SEQ");
        std::vector<std::string> lines;
        for (int r = 0; r < rows; ++r) {
            std::string name = names[r], value = value_of(r);
            if (editing && r == row) value = "[" + value + "]";
            int gap = std::max(1, 16 - int(name.size()) - int(value.size()));
            lines.push_back(name + std::string(size_t(gap), ' ') + value);
        }
        ui::list(g, ui::content(0, ui::height), lines, row, 1);
    }
    std::string status() const override { return " workflow_row=" + std::to_string(row) + " workflow_edit=" + std::to_string(int(editing)); }

private:
    static constexpr int rows = 8;
    static constexpr const char* names[rows] {"QUANTIZE", "STEP GRID", "COUNT-IN", "REC QUANTIZE", "TIME SIG", "MAX LENGTH", "METRONOME", "BPM/PATTERN"};
    int row = 0;
    bool editing = false;
    static constexpr unsigned lengths[] {0, 1, 2, 4, 8, 16, 32, 64};

    std::string value_of(int r) const {
        const auto& seq = studio.sequencer();
        const auto& work = seq.workflow;
        switch (r) {
        case 0: return std::to_string(work.quantize) + "%";
        case 1: return melodee::division_name(const_cast<Sequencer&>(seq).pattern().division);
        case 2: return work.count_in ? "ON" : "OFF";
        case 3: return work.rec_quantize ? "ON" : "OFF";
        case 4: { auto sig = studio.project().time_signature(); return std::to_string(sig.beats) + "/" + std::to_string(sig.unit); }
        case 5: return work.max_bars ? std::to_string(work.max_bars) + " BR" : "FREE";
        case 6: return work.click_rec_only ? "REC ONLY" : "ALWAYS";
        case 7: return studio.project().seq_tempo ? "ON" : "OFF";
        default: return {};
        }
    }
    void change(int delta) {
        auto& seq = studio.sequencer();
        auto& work = seq.workflow;
        switch (row) {
        case 0: work.quantize = unsigned(std::clamp(int(work.quantize) + delta * 5, 0, 100)); break;
        case 1: seq.set_division(delta); break;
        case 2: work.count_in = delta > 0; break;
        case 3: work.rec_quantize = delta > 0; break;
        case 4: seq.set_signature(delta); break;
        case 5: {
            int at = 0;
            for (int k = 0; k < 8; ++k) if (lengths[k] == work.max_bars) at = k;
            work.max_bars = lengths[std::clamp(at + delta, 0, 7)];
            break;
        }
        case 6: work.click_rec_only = delta > 0; break;
        case 7: seq.set_seq_tempo(delta > 0); break;
        default: break;
        }
        studio.changed();
    }
};

class Repeat final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override {
        const auto& track = studio.current();
        style = track.arp ? 2u + std::min<unsigned>(track.arp_mode, 4u) : 1u; // the arp's mode, else Repeat
        rate = track.repeat_rate % melodee::arp_rates;
        row = 1;
        if (!track.repeat && !track.arp) apply(true); // Shift + Step 11 turns it on (11.6)
    }
    bool job(Job job, const bindings::Use& use) override {
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (row == 0) style = unsigned(std::clamp(int(style) + (use.delta > 0 ? 1 : -1), 0, studio.sequencer().kit() ? 1 : 6));
            else if (row == 1) rate = unsigned(std::clamp(int(rate) + (use.delta > 0 ? 1 : -1), 0, int(melodee::arp_rates) - 1));
            else if (row == 2) pressure = use.delta > 0;
            apply(true);
            return true;
        case Job::ListPush: row = (row + 1) % 3; return true;
        case Job::Back: studio.close(); return true; // it stays on
        case Job::Repeat: apply(false); studio.close(); studio.say("REPEAT OFF"); return true; // Shift + Step 11 again
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        if (auto* play = studio.screen(home_screen(studio.kind()))) play->lights(lights);
        lights.button(move::Button::Back, move::white_dim);
    }
    void draw(juce::Graphics& g) override {
        const auto& track = studio.current();
        bool on = track.repeat || track.arp;
        ui::rail(g, "RPT", on ? "ON" : "OFF", "");
        move::draw_text(g, ui::content_x + 4, 2, melodee::arp_rate_name(rate), 3);
        static const char* styles[] {"OFF", "REPEAT", "ARP UP", "ARP DOWN", "ARP UP-DN", "ARP RAND", "ARP ORDER"};
        auto line = [](std::string name, std::string value) { return name + std::string(size_t(std::max(1, 16 - int(name.size()) - int(value.size()))), ' ') + value; };
        ui::list(g, ui::content(26, 30), {line("STYLE", styles[style]), line("RATE", melodee::arp_rate_name(rate)), line("PRESSURE", pressure ? "VEL" : "OFF")}, row, 1);
    }
    std::string status() const override { return " repeat_rate=" + std::to_string(rate) + " repeat_style=" + std::to_string(style); }

private:
    unsigned style = 0, rate = 4;
    int row = 1;
    bool pressure = false;
    void apply(bool on) {
        // Style 0 off, 1 Repeat, 2-6 the arp's modes (instruments' ARP page sets its octaves, gate, latch).
        studio.sequencer().set_repeat(studio.track(), on && style > 0, style > 0 ? style - 1 : 0, rate);
        studio.send({melodee::Command::Type::Repeat, uint8_t(studio.track()), 0, 5, int16_t(pressure)});
    }
};

class CaptureTempo final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override { chosen = 0; }
    bool job(Job job, const bindings::Use& use) override {
        auto& seq = studio.sequencer();
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (seq.guesses.empty()) return true;
            chosen = unsigned(std::clamp(int(chosen) + (use.delta > 0 ? 1 : -1), 0, int(seq.guesses.size()) - 1));
            seq.take_guess(chosen);
            return true;
        case Job::ListPush: case Job::Back: studio.close(); return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        if (auto* play = studio.screen(home_screen(studio.kind()))) play->lights(lights);
        lights.button(move::Button::Back, move::white_dim);
    }
    void draw(juce::Graphics& g) override {
        const auto& guesses = studio.sequencer().guesses;
        std::vector<std::string> rows;
        for (const auto& guess : guesses) {
            char text[24];
            std::snprintf(text, sizeof text, "%u.%u BPM", guess.tenths / 10, guess.tenths % 10);
            std::string left = text, right = std::to_string(guess.bars) + (guess.bars == 1 ? " BAR" : " BAR");
            rows.push_back(left + std::string(size_t(std::max(1, 16 - int(left.size()) - int(right.size()))), ' ') + right);
        }
        unsigned bpm = guesses.empty() ? studio.tempo() : guesses[chosen].tenths;
        ui::rail(g, "CAP", std::to_string(bpm / 10), "BPM");
        ui::list(g, ui::content(0, 50), rows, int(chosen), 1);
        ui::detail(g, "PUSH: OK");
    }

private:
    unsigned chosen = 0;
};

RegisterScreen workflow("workflow", make<Workflow>, {{bindings::Job::Workflow}}); // Shift + Step 3
RegisterScreen repeat("repeat", make<Repeat>, {{bindings::Job::Repeat}});         // Shift + Step 11
RegisterScreen capture("capture", make<CaptureTempo>);                           // a capture played stopped

} // namespace
} // namespace app::screens
