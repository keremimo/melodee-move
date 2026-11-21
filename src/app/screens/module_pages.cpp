// SPDX-License-Identifier: AGPL-3.0-only
// A Schwung module's pages, entered (docs/UI.md, "A module's pages"; Kerem,
// 2026-11-21): Schwung's own screen code shows them (schwung_pages.h), and
// this screen is its window: Schwung's whole 128 x 64 screen, nothing of
// Melodee's drawn over it, and the Move's controls handed to it as Schwung's
// shim hands them to its shadow UI: the wheel, its push and Back as their
// CCs, the knobs as relative CCs and their touches as notes, Mute as its CC,
// the pads as notes (they play the track too). Shift is not sent: Schwung's
// code asks for it, and gets it held or locked. While Schwung's keyboard has
// the pads (its pad typing) they are its keys: their notes and pressure go
// to it alone, lit in its colours, and play nothing. Back at the top of the
// pages leaves for the Device View.
#include "app/screens/note_pages.h"

namespace app::screens {
namespace {

class ModulePages final : public Screen {
public:
    using Screen::Screen;

    // Entered from the Device View (sheets over the pages are pushed, so a
    // return from one is no entry): Schwung's pages start over.
    void enter() override {
        held.fill(false);
        if (SchwungPages* pages = studio.pages_of(studio.pages_target)) pages->enter();
    }

    // Gone with the pads still the keyboard's: given back to the track.
    void leave() override {
        if (SchwungPages* pages = studio.pages_of(studio.pages_target)) pages->release_pads();
    }

    bindings::Screen screen_kind() const override { return bindings::Screen::Canvas; }

    void input(const move::Input& input) override {
        using Kind = move::Input::Kind;
        if (input.kind == Kind::Pad && input.index < pad_count) held[input.index] = input.pressed;
        SchwungPages* pages = studio.pages_of(studio.pages_target);
        if (!pages || studio.top() != this) return;
        if (input.kind == Kind::Touch && input.index < 8) pages->midi(input.pressed ? 0x90 : 0x80, input.index, input.pressed ? 127 : 0);
        if (input.kind == Kind::Button && input.index == uint8_t(move::Button::Mute))
            pages->midi(0xb0, move::button_cc(move::Button::Mute), input.pressed ? 127 : 0);
        if (input.kind == Kind::Pad && input.index < pad_count)
            pages->midi(input.pressed ? 0x90 : 0x80, move::pad_note(input.index), input.pressed ? uint8_t(std::max<int>(1, input.value)) : 0);
        if (input.kind == Kind::PadPressure && input.index < pad_count && pages->pad_blocked()) // poly pressure: a key pressed harder
            pages->midi(0xa0, move::pad_note(input.index), uint8_t(std::clamp<int>(input.value, 0, 127)));
        leave_if_asked(*pages);
    }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        SchwungPages* pages = studio.pages_of(studio.pages_target);
        if (!pages) {
            if (job == Job::CanvasBack || job == Job::Back) { studio.close(); return true; }
            return job == Job::CanvasTurn || job == Job::CanvasPush;
        }
        // The keyboard's pads: no job of the track's under this screen.
        if (pages->pad_blocked() && (use.control == bindings::Control::Pad || use.control == bindings::Control::PadPressure)) return true;
        auto cc = [&](uint8_t number, uint8_t value) { pages->midi(0xb0, number, value); };
        auto detents = [&](uint8_t number, int delta) { // one message a detent, as the Move sends them
            for (int n = 0; n < std::min(std::abs(delta), 16); ++n) cc(number, delta > 0 ? 1 : 127);
        };
        bool mine = true;
        switch (job) {
        case Job::TurnKnob: case Job::TurnKnobFine:
            if (use.index >= 0 && use.index < 8 && use.delta) {
                detents(uint8_t(move::knob_first_cc + use.index), use.delta);
                studio.changed(); // the values are saved with the project
            }
            break;
        case Job::CanvasTurn: detents(move::main_knob_cc, use.delta); break;
        case Job::CanvasPush:
            cc(move::button_cc(move::Button::MainPush), 127);
            cc(move::button_cc(move::Button::MainPush), 0);
            break;
        case Job::CanvasBack:
            cc(move::button_cc(move::Button::Back), 127);
            cc(move::button_cc(move::Button::Back), 0);
            break;
        default: mine = false; break; // the pads, the octave: the track's Play screen under this one
        }
        leave_if_asked(*pages);
        return mine;
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        key_pads(studio, lights, held);
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim); // always a way up
        if (SchwungPages* pages = studio.pages_of(studio.pages_target)) { // the knob rings, as Schwung's pages light them
            for (unsigned n = 0; n < 8; ++n)
                if (int colour = pages->knob_rings()[n]; colour >= 0) lights.knob(n, uint8_t(colour));
            if (pages->pad_blocked()) // the keyboard's keys, in its colours
                for (unsigned n = 0; n < pad_count && n < pages->pad_colours().size(); ++n)
                    lights.pad(n, uint8_t(std::max(0, pages->pad_colours()[n])));
        }
    }

    Knob knob(unsigned) const override { return {}; } // Schwung's pages show their own

    std::string status() const override {
        SchwungPages* pages = const_cast<Studio&>(studio).pages_of(studio.pages_target);
        if (!pages) return " pages=-";
        const auto& s = pages->status();
        // The wheel is a door's or a picker's when Schwung's footer gives
        // it any verb but PAGE.
        bool wheel = !s.footer.empty() && s.footer[0].first == "JOG" && s.footer[0].second != "PAGE";
        return " pages=" + s.page + "," + s.kind + (s.canvas ? ",canvas" : "") + (s.picker ? ",picker" : "") + (wheel ? ",wheel" : "") +
               (s.typing ? ",typing" : "") + (s.warning ? ",warning" : "") + (s.view != "parampages" ? ",view=" + s.view : "");
    }

    void draw(juce::Graphics& g) override {
        SchwungPages* pages = studio.pages_of(studio.pages_target);
        if (!pages) {
            move::draw_text_centred(g, ui::content_x, 24, ui::content_width, "NO MODULE");
            ui::detail(g, "BACK: OUT");
            return;
        }
        pages->frame(g); // Schwung's screen, all of it
        leave_if_asked(*pages);
    }

private:
    std::array<bool, pad_count> held {};

    // Back at the top of Schwung's pages: out to the Device View. Remove
    // Module asked from them: out first, then gone.
    void leave_if_asked(SchwungPages& pages) {
        if (studio.remove_from_pages && *studio.remove_from_pages == studio.pages_target) {
            auto target = *studio.remove_from_pages;
            studio.remove_from_pages.reset();
            if (studio.top() == this) studio.close();
            studio.remove_module(target);
            return;
        }
        if (pages.left() && studio.top() == this) studio.close();
    }
};

RegisterScreen registered("module-pages", make<ModulePages>);

} // namespace
} // namespace app::screens
