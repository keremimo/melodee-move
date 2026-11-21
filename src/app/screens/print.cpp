// SPDX-License-Identifier: AGPL-3.0-only
// PRINT (a track button held + Sampling; docs/UI.md "Print and freeze",
// Kerem's decision in docs/MOVE_CONTROLS.md): Melodee's PRINT sheet for that
// track. The pads are its patterns of the bank in view (Plus / Minus: the
// bank of 32), the ones with notes dim in the track's colour, the ones to
// print ice; a pad takes one out or back. Knob 1 TAP (Post FX: engine and
// chain; Pre FX: the engine alone), knob 2 TAIL (Loop, Ring, Cut). The wheel
// picks what its push does: FREEZE (the lit patterns printed, then the track
// plays them), DEEP FREEZE (every pattern with notes, Post FX, and the
// module let go), ALL, NONE, UNFREEZE (live again at once, the prints kept).
// While printing: the pattern and how far, the speed against real time; the
// track plays live until its prints are ready and switches on a bar line.
// Back calls the prints off (nothing changes), or leaves. The work is
// prints.h's.
#include "app/screens/common.h"
#include "app/prints.h"
#include "app/song_desk.h"
#include "ui/song_view.h"
#include <cmath>

namespace app::screens {
namespace {

using bindings::Job;
using melodee::PrintTail;
using melodee::PrintTap;

uint8_t ice() { return move::nearest_colour({150, 215, 255}); }
constexpr unsigned bank_size = 32;

class Print final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::Print; }

    void enter() override {
        auto& printer = studio.desk().printer();
        track = studio.track();
        bank = 0;
        action = 0;
        started = false;
        const auto& f = printer.table().freeze[track];
        chosen = {};
        if (f.frozen()) {
            for (unsigned p = 0; p < melodee::pattern_count; ++p) if (f.plays(p)) chosen[p / 64] |= uint64_t(1) << (p % 64);
            tap = f.tap;
            tail = f.tail;
        } else {
            chosen = printer.with_notes(track);
        }
    }

    bool job(Job job, const bindings::Use& use) override {
        auto& printer = studio.desk().printer();
        switch (job) {
        case Job::Back:
            if (printer.busy()) { printer.cancel(); studio.say("CALLED OFF"); return true; }
            studio.close();
            return true;
        case Job::PrintPattern: {
            if (printer.busy()) return true;
            unsigned p = bank * bank_size + unsigned(use.index);
            chosen[p / 64] ^= uint64_t(1) << (p % 64);
            return true;
        }
        case Job::PrintBank:
            bank = unsigned(std::clamp(int(bank) + (use.control == bindings::Control::Plus ? 1 : -1), 0, int(melodee::pattern_count / bank_size) - 1));
            return true;
        case Job::PrintTurn: action = std::clamp(action + use.delta, 0, int(actions().size()) - 1); return true;
        case Job::TurnKnob: case Job::TurnKnobFine:
            if (printer.busy() || !use.delta) return true;
            if (use.index == 0) tap = use.delta > 0 ? PrintTap::PreFx : PrintTap::PostFx;
            if (use.index == 1) tail = PrintTail(std::clamp(int(tail) + (use.delta > 0 ? 1 : -1), 0, 2));
            return true;
        case Job::PrintPush: push(); return true;
        default: return false;
        }
    }

    Knob knob(unsigned which) const override {
        if (which == 0) return {"TAP", tap == PrintTap::PreFx ? "PRE FX" : "POST FX", tap == PrintTap::PreFx ? 1.0f : 0.0f};
        if (which == 1) return {"TAIL", tail_text(), float(tail) / 2.0f};
        return {};
    }
    std::string knob_owner() const override { return "PRNT"; }

    void lights(move::Lights& lights) override {
        using namespace move;
        auto& printer = studio.desk().printer();
        auto with = printer.with_notes(track);
        auto progress = printer.progress();
        bool blink = std::chrono::steady_clock::now().time_since_epoch() / std::chrono::milliseconds(250) % 2;
        int playing = studio.playing() ? int(studio.host().pattern_of(track)) : -1;
        for (unsigned pad = 0; pad < pad_count; ++pad) {
            unsigned p = bank * bank_size + pad;
            bool notes = with[p / 64] >> (p % 64) & 1, lit = chosen[p / 64] >> (p % 64) & 1;
            uint8_t colour = notes ? ui::track_colour(track).dim : colour::off;
            if (progress.busy) {
                if (progress.is_done(p)) colour = ice();
                else if (int(p) == progress.pattern) colour = blink ? ice() : colour::white;
            } else if (lit && notes) {
                colour = ice();
            }
            if (!progress.busy && int(p) == playing && notes) colour = blink ? colour::white : colour;
            lights.pad(pad, colour);
        }
        for (unsigned step = 0; step < step_count; ++step) lights.step(step, colour::off);
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Copy, Button::Delete}) lights.button(button, 0);
        lights.button(Button::Up, bank + 1 < melodee::pattern_count / bank_size ? white_dim : 0);
        lights.button(Button::Down, bank ? white_dim : 0);
        lights.button(Button::Back, white_dim);
        lights.button(Button::Sample, progress.busy ? colour::red : colour::light_grey);
    }

    void draw(juce::Graphics& g) override {
        auto& printer = studio.desk().printer();
        auto progress = printer.progress();
        std::string owner = tag();
        if (progress.busy) {
            ui::rail(g, "PRNT", std::to_string(track + 1), std::to_string(progress.through + 1) + "/" + std::to_string(progress.count));
            move::draw_text(g, ui::content_x + 2, 1, "PRINTING");
            if (progress.pattern >= 0) move::draw_text(g, ui::content_x + 2, 11, melodee::print_pattern_name(unsigned(progress.pattern)), 3);
            ui::progress(g, {ui::content_x + 44, 13, ui::content_width - 46, 9}, progress.fraction);
            char speed[24];
            std::snprintf(speed, sizeof speed, "X%.0f REAL", progress.times_real);
            if (progress.times_real > 0) move::draw_text(g, ui::content_x + 44, 25, speed);
            if (progress.freeze) move::draw_text(g, ui::content_x + 2, 40, "THEN ON A BAR");
            char left[16];
            std::snprintf(left, sizeof left, "%.0fS", progress.seconds_left);
            ui::detail(g, "BACK: CALL OFF", progress.seconds_left > 0 ? left : "");
            return;
        }
        ui::rail(g, "PRNT", std::to_string(track + 1), owner);
        if (printer.frozen(track)) {
            g.setColour(juce::Colours::black);
            ui::snowflake(g, 8, 43);
            g.setColour(juce::Colours::white);
        }
        if (!printer.printable(track)) {
            move::draw_text(g, ui::content_x + 2, 4, studio.kind(track) == TrackKind::Midi ? "MIDI TRACKS:" : "NOTHING TO");
            move::draw_text(g, ui::content_x + 2, 14, studio.kind(track) == TrackKind::Midi ? "LATER" : "PRINT HERE");
            ui::detail(g, "BACK");
            return;
        }
        auto patterns = chosen_patterns();
        char room[24];
        std::snprintf(room, sizeof room, "ROOM  %.1f MB", printer.room(track, patterns, tail) / 1048576.0);
        move::draw_text(g, ui::content_x + 2, 1, std::string("TAP   ") + (tap == PrintTap::PreFx ? "PRE FX" : "POST FX"));
        move::draw_text(g, ui::content_x + 2, 10, "TAIL  " + tail_text());
        move::draw_text(g, ui::content_x + 2, 19, room);
        move::draw_text(g, ui::content_x + 2, 28, "CPU   " + std::to_string(printer.cpu_percent(track)) + "% NOW");
        g.fillRect(ui::content_x, 39, ui::content_width, 11);
        g.setColour(juce::Colours::black);
        move::draw_text_centred(g, ui::content_x, 41, ui::content_width, "< " + actions()[size_t(action)] + " >");
        g.setColour(juce::Colours::white);
        unsigned with = 0;
        auto bits = printer.with_notes(track);
        for (auto b : bits) with += unsigned(__builtin_popcountll(b));
        std::string error = progress.error.empty() ? "" : upper(progress.error).substr(0, 11);
        ui::detail(g, !error.empty() ? error : actions()[size_t(action)] == "DEEP FREEZE" ? "MODULE OFF" : "PATTERNS",
                   std::to_string(patterns.size()) + "/" + std::to_string(with));
    }

    std::string status() const override {
        return studio.top() == this ? " print_sheet=" + std::to_string(track + 1) + " print_action=" + std::to_string(action) : std::string();
    }

private:
    unsigned track = 0, bank = 0;
    int action = 0;
    bool started = false;
    std::array<uint64_t, 2> chosen {};
    PrintTap tap = PrintTap::PostFx;
    PrintTail tail = PrintTail::Loop;

    std::string tail_text() const { return tail == PrintTail::Ring ? "RING" : tail == PrintTail::Cut ? "CUT" : "LOOP"; }
    std::string tag() const {
        auto* module = const_cast<Studio&>(studio).module_track(track);
        if (module && module->module) return upper(module->module->info().abbrev).substr(0, 4);
        return kind_tag(studio.kind(track));
    }
    std::vector<std::string> actions() const {
        std::vector<std::string> list {"FREEZE", "DEEP FREEZE", "ALL", "NONE"};
        if (studio.desk().printer().frozen(track)) list.push_back("UNFREEZE");
        return list;
    }
    std::vector<uint8_t> chosen_patterns() const {
        std::vector<uint8_t> list;
        auto with = studio.desk().printer().with_notes(track);
        for (unsigned p = 0; p < melodee::pattern_count; ++p)
            if ((chosen[p / 64] & with[p / 64]) >> (p % 64) & 1) list.push_back(uint8_t(p));
        return list;
    }
    void push() {
        auto& printer = studio.desk().printer();
        if (printer.busy()) return;
        action = std::min(action, int(actions().size()) - 1);
        const std::string what = actions()[size_t(action)];
        if (what == "ALL") { chosen = printer.with_notes(track); return; }
        if (what == "NONE") { chosen = {}; return; }
        if (what == "UNFREEZE") {
            if (auto why = printer.unfreeze(track); !why.empty()) studio.say(why);
            studio.close();
            return;
        }
        Printer::Job job;
        job.track = track;
        job.patterns = chosen_patterns();
        job.tap = tap;
        job.tail = tail;
        job.freeze = true;
        job.deep = what == "DEEP FREEZE";
        if (job.patterns.empty() && !job.deep) { // none lit: as Melodee's B3 then, live again
            if (printer.frozen(track)) printer.unfreeze(track);
            studio.close();
            return;
        }
        if (auto why = printer.start(std::move(job)); !why.empty()) { studio.say(why); return; }
        started = true;
    }
};

RegisterScreen print("print", make<Print>, {{bindings::Job::Print}}); // a track button held + Sampling

// "Unfreeze track N?": a control that would change what a frozen track's
// prints are made of asks first (the app opens this). Push: live again (a
// deep-frozen one thaws first); Back: nothing changes.
class Unfreeze final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(Job job, const bindings::Use&) override {
        switch (job) {
        case Job::ListPush:
            if (auto why = studio.desk().printer().unfreeze(studio.track()); !why.empty()) studio.say(why);
            studio.close();
            return true;
        case Job::Back: studio.close(); return true;
        case Job::ListTurn: case Job::ListTurnFine: return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        using namespace move;
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Copy, Button::Delete}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }
    void draw(juce::Graphics& g) override {
        auto* module = studio.module_track();
        ui::rail(g, "FRZN", std::to_string(studio.track() + 1), module && module->module ? upper(module->module->info().abbrev).substr(0, 4) : kind_tag(studio.kind()));
        move::draw_text_centred(g, ui::content_x, 6, ui::content_width, "UNFREEZE", 2);
        move::draw_text_centred(g, ui::content_x, 26, ui::content_width, "TRACK " + std::to_string(studio.track() + 1) + "?", 2);
        ui::detail(g, "PUSH YES", "BACK NO");
    }
};

RegisterScreen unfreeze("unfreeze", make<Unfreeze>);

} // namespace
} // namespace app::screens
