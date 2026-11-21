// SPDX-License-Identifier: AGPL-3.0-only
// Loop Mode (the Move's manual, 12; docs/UI.md, "Sequencer"): Loop on a
// Play screen shows the selected pattern's bars on the steps, sixteen at a
// time (Shift + Left / Right the next sixteen).
//
//   Step                 selects the bar: Note Mode's steps show it after
//   Step held + Step     the loop from the first bar to the second
//   Step double press    a one-bar loop
//   Step held + wheel, Volume knob, Plus / Minus, Left / Right
//                        the bar's notes: length, velocity, transpose, nudge
//   Step held + Pad      the pad's note on every step of the bar
//   Copy + Step, then a Step   the bar copied there
//   Delete + Step        the bar's notes cleared
//   Left / Right         the bar before or after
//   Loop held + wheel    the length (the sequencer's); Loop or Back: out
//
// The loop from the first bar is the pattern's length (Melodee's patterns
// loop from their top); from a later bar it plays as an editor's loop.
#include "app/screens/common.h"
#include "app/sequencer.h"
#include "ui/seq_widgets.h"

namespace app::screens {
namespace {

using bindings::Job;

class LoopMode final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::Loop; }
    void enter() override {
        auto& seq = studio.sequencer();
        selected = int(seq.page() * seq.page_steps() * seq.pattern().q() / seq.bar_ticks());
        first = unsigned(selected) / 16 * 16;
        copied = -1;
    }
    bool job(Job job, const bindings::Use& use) override {
        auto& seq = studio.sequencer();
        auto& p = seq.pattern();
        unsigned t = studio.track(), n = seq.number(t);
        uint32_t bar = seq.bar_ticks();
        int held = use.held_step >= 0 ? int(first) + use.held_step : -1;
        auto held_span = [&] { return seq::bar_span(unsigned(held), bar); };
        const int lane = -1; // a bar's notes, every pad's (12.4)
        switch (job) {
        case Job::SelectBar: select(int(first) + use.index); return true;
        case Job::LoopRange:
            if (held < 0) return true;
            seq.set_loop(unsigned(std::min(held, int(first) + use.index)), unsigned(std::max(held, int(first) + use.index)) + 1);
            studio.say("LOOP " + loop_text());
            return true;
        case Job::OneBarLoop:
            seq.set_loop(first + unsigned(use.index), first + unsigned(use.index) + 1);
            studio.say("LOOP " + loop_text());
            return true;
        case Job::BarLength: if (held >= 0) { seq::add_length(p, held_span(), lane, use.delta * std::max(1, int(p.q()) / 10)); seq.sync(t, n); } return true;
        case Job::BarVelocity: if (held >= 0) { seq::add_velocity(p, held_span(), lane, use.delta); seq.sync(t, n); } return true;
        case Job::TransposeBar:
            if (held >= 0) { seq::transpose(p, held_span(), lane, use.control == bindings::Control::Plus ? 1 : -1, seq.kit()); seq.sync(t, n); }
            return true;
        case Job::NudgeBar:
            if (held >= 0) { seq::nudge(p, held_span(), lane, (use.control == bindings::Control::Right ? 1 : -1) * std::max(1, int(p.q()) / 10)); seq.sync(t, n); }
            return true;
        case Job::NoteOnBar: {
            int key = studio.stamp(unsigned(use.index)).key.load(std::memory_order_acquire); // its own key, on every step
            if (held >= 0 && key >= 0) { seq::toggle_key_every_step(p, held_span(), uint8_t(key), 100); seq.sync(t, n); }
            return true;
        }
        case Job::CopyBar: {
            int at = int(first) + use.index;
            if (copied < 0) { copied = at; studio.say("BAR " + std::to_string(at + 1) + " COPIED"); return true; }
            if (uint32_t(at) * bar >= p.length) seq::set_length(p, uint32_t(at + 1) * bar, studio.project().max_length());
            seq::copy_span(p, seq::bar_span(unsigned(copied), bar), seq::bar_span(unsigned(at), bar), lane);
            seq.sync(t, n);
            studio.say("PASTED ON BAR " + std::to_string(at + 1));
            return true;
        }
        case Job::ClearBar:
            seq::clear_span(p, seq::bar_span(first + unsigned(use.index), bar), lane);
            seq.sync(t, n);
            studio.say("BAR " + std::to_string(first + unsigned(use.index) + 1) + " CLEARED");
            return true;
        case Job::Bar: select(selected + (use.control == bindings::Control::Right ? 1 : -1)); return true;
        case Job::Bar16: {
            int to = int(first) + (use.control == bindings::Control::Right ? 16 : -16);
            if (to < 0 || to >= int(melodee::max_bars)) return true;
            first = unsigned(to);
            select(int(first));
            return true;
        }
        case Job::LoopMode: case Job::Back: studio.close(); return true;
        // The steps are bars here: Note Mode's step jobs (a Tap or Hold of the same keys) do nothing.
        case Job::ToggleStep: case Job::StepVelocity: case Job::StepLength: case Job::StepLengthTo: case Job::TransposeStep:
        case Job::TransposeStepOctave: case Job::NudgeStep: case Job::NudgeStepFine: case Job::NudgeStepWhole: case Job::StepMotion:
        case Job::StepTrig: case Job::NoteOnStep: case Job::CopyStep:
            return true;
        default: return false;
        }
    }
    void input(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Button && move::Button(input.index) == move::Button::Copy && !input.pressed) copied = -1;
    }
    void lights(move::Lights& lights) override {
        using namespace move;
        if (auto* play = studio.screen(home_screen(studio.kind())); play && play != this) play->lights(lights); // the pads as they play
        auto& seq = studio.sequencer();
        auto& p = seq.pattern();
        auto colour = ui::track_colour(studio.track());
        unsigned bars = seq::bars_of(p, seq.bar_ticks());
        unsigned from = seq.loop_from(studio.track(), seq.number(studio.track())) & 0xffff;
        int playing = playing_bar();
        bool pulse = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count() / 200 % 2;
        for (unsigned s = 0; s < step_count; ++s) {
            unsigned bar = first + s;
            uint8_t shown = bar >= bars ? colour::off : int(bar) == selected ? colour::white : bar >= from ? colour.bright : colour::dark_grey;
            if (int(bar) == playing && pulse) shown = colour.dim; // the playhead pulses (1.2.0)
            lights.step(s, shown);
        }
        lights.button(Button::Loop, white_bright);
        lights.button(Button::Back, white_dim);
        lights.button(Button::Left, selected > 0 ? white_dim : 0);
        lights.button(Button::Right, white_dim);
    }
    void draw(juce::Graphics& g) override {
        auto& seq = studio.sequencer();
        auto& p = seq.pattern();
        uint32_t bar = seq.bar_ticks();
        unsigned bars = seq::bars_of(p, bar);
        ui::rail(g, "LOOP", std::to_string(bars), "BARS");
        ui::seq::title(g, "PATTERN " + std::to_string(seq.number(studio.track()) + 1), std::to_string(first + 1) + "-" + std::to_string(first + 16));
        ui::seq::Bars shown;
        shown.count = std::max(bars, unsigned(selected) + 1);
        shown.first = first;
        unsigned packed = seq.loop_from(studio.track(), seq.number(studio.track()));
        shown.loop_from = packed & 0xffff;
        shown.loop_to = packed ? packed >> 16 : bars;
        shown.selected = selected;
        shown.playing = playing_bar();
        for (unsigned k = 0; k < 16; ++k)
            shown.notes[k] = uint8_t(std::min<size_t>(seq::notes_in(p, seq::bar_span(first + k, bar), seq.lane_key()).size(), 255));
        ui::seq::bar_blocks(g, ui::content(13, 38), shown);
        ui::detail(g, "BAR " + std::to_string(selected + 1) + " OF " + std::to_string(bars), loop_text());
    }
    std::string status() const override { return " loop_bar=" + std::to_string(selected + 1); }

private:
    using Clock = std::chrono::steady_clock;
    int selected = 0;   // the bar the steps show in Note Mode
    unsigned first = 0; // the first bar on the steps
    int copied = -1;    // Copy + a step: the bar copied

    void select(int bar) {
        auto& seq = studio.sequencer();
        bar = std::clamp(bar, 0, int(melodee::max_bars) - 1);
        selected = bar;
        first = unsigned(bar) / 16 * 16;
        // Note Mode's steps show the bar (its first page).
        uint32_t steps_a_page = seq.page_steps() * seq.pattern().q();
        seq.set_page(int(uint64_t(bar) * seq.bar_ticks() / std::max<uint32_t>(steps_a_page, 1)));
        studio.say("BAR " + std::to_string(bar + 1));
    }
    int playing_bar() const {
        if (!studio.playing()) return -1;
        int position = studio.host().position_of(studio.track());
        return position < 0 ? -1 : int(unsigned(position) / studio.sequencer().bar_ticks());
    }
    std::string loop_text() const {
        auto& seq = studio.sequencer();
        unsigned packed = seq.loop_from(studio.track(), seq.number(studio.track()));
        unsigned from = packed & 0xffff, to = packed ? packed >> 16 : seq::bars_of(seq.pattern(), seq.bar_ticks());
        return std::to_string(from + 1) + "-" + std::to_string(to);
    }
};

RegisterScreen loop("loop", make<LoopMode>, {{bindings::Job::LoopMode}});

} // namespace
} // namespace app::screens
