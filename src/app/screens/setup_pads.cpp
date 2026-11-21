// SPDX-License-Identifier: AGPL-3.0-only
// Setup > PADS (docs/UI.md, "Pads"; docs/ui/pads.png): the pads' settings,
// here LAYOUT, Move rows or two MPC banks (app/pad_setting.h). Push edits
// it: the wheel turns it, the screen and the pads show its numbering, push
// sets it (said; every pad's job and light changes at once), Back leaves it
// as it was. Back again leaves the page.
//
// Setup itself is move-projects' (Shift + Step 2, a PADS row opening this
// list beside RESPONSE and PRESSURE); until it lands, Shift + Step 2 opens
// this page, and its MIDI CONFIG row opens move-midi's MIDI Config (the
// other stand-in for Setup), so both are in reach.
#include "app/pad_setting.h"
#include "app/screens/common.h"
#include "ui/pad_numbering.h"

namespace app::screens {
namespace {

std::string row(const std::string& name, const std::string& value) { // the name left, the value flush right
    size_t room = 16;
    return name + std::string(name.size() + value.size() < room ? room - name.size() - value.size() : 1, ' ') + value;
}

class SetupPads final : public Screen {
public:
    using Screen::Screen;

    void enter() override { editing = false; cursor = 0; }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (editing) pending = pad_setting::turned(pending, use.delta);
            else cursor = std::clamp(cursor + use.delta, 0, 1);
            return true;
        case Job::ListPush:
            if (!editing && cursor == 1) { studio.push("midi-config"); return true; }
            if (!editing) { editing = true; pending = pad_layout(); }
            else { editing = false; pad_setting::apply(studio, pending); }
            return true;
        case Job::Back:
            if (editing) editing = false;
            else studio.close();
            return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        auto shown = editing ? pending : pad_layout();
        for (unsigned number = 0; number < pad_count; ++number)
            lights.pad(physical_pad(uint8_t(number), shown), ui::numbering_colour(number, shown));
        for (unsigned step = 0; step < step_count; ++step) lights.step(step, colour::off);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "PADS", "", "SETP");
        if (editing) {
            ui::pad_numbering(g, pending);
            ui::detail(g, pad_setting::detail_left(pending), pad_setting::detail_right(pending));
            return;
        }
        ui::list(g, ui::content(0, ui::height), {row(pad_setting::name, pad_setting::value(pad_layout())), "MIDI CONFIG >"}, cursor, 1);
    }

    std::string status() const override { return std::string(" layout=") + pad_layout_name(pad_layout()) + (editing ? "*" : ""); }

private:
    bool editing = false;
    int cursor = 0; // 0 LAYOUT, 1 MIDI CONFIG
    PadLayout pending = PadLayout::Rows;
};

RegisterScreen setup_pads("setup-pads", make<SetupPads>, {{bindings::Job::Setup}});

} // namespace
} // namespace app::screens
