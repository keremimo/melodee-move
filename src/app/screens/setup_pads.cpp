// SPDX-License-Identifier: AGPL-3.0-only
// Setup > PADS > LAYOUT (docs/UI.md, "Pads"; docs/ui/pads.png): the pad
// layout being edited, Move rows or two MPC banks (app/pad_setting.h). The
// wheel turns it while the screen shows where each run of pads starts and
// the pads light their numbering; push sets it (said; every pad's job and
// light changes at once) and goes back to the list; Back goes back leaving
// it as it was.
//
// Setup and its PADS list are move-projects' (screens/setup.cpp, Shift +
// Step 2): the list's LAYOUT row pushes this screen by its id, `setup-pads`.
// It has no Trigger of its own, so Setup alone answers Shift + Step 2.
#include "app/pad_setting.h"
#include "app/screens/common.h"
#include "ui/pad_numbering.h"

namespace app::screens {
namespace {

class SetupPads final : public Screen {
public:
    using Screen::Screen;

    void enter() override { pending = pad_layout(); }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: pending = pad_setting::turned(pending, use.delta); return true;
        case Job::ListPush: pad_setting::apply(studio, pending); studio.close(); return true;
        case Job::Back: studio.close(); return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        for (unsigned number = 0; number < pad_count; ++number)
            lights.pad(physical_pad(uint8_t(number), pending), ui::numbering_colour(number, pending));
        for (unsigned step = 0; step < step_count; ++step) lights.step(step, colour::off);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "PADS", "", "SETP");
        ui::pad_numbering(g, pending);
        ui::detail(g, pad_setting::detail_left(pending), pad_setting::detail_right(pending));
    }

    // The layout in use, on the status line wherever the screens are.
    std::string status() const override { return std::string(" layout=") + pad_layout_name(pad_layout()); }

private:
    PadLayout pending = PadLayout::Rows;
};

RegisterScreen setup_pads("setup-pads", make<SetupPads>);

} // namespace
} // namespace app::screens
