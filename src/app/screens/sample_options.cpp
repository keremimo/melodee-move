// SPDX-License-Identifier: AGPL-3.0-only
// Sample Options (docs/MOVE_CONTROLS.md): Shift + the wheel's push on a kit
// pad's sample, the Move's own door to slicing. Melodee's Chop is where the
// Move has Slice; Reverse, Trim, Normalize and Save Sample come with wave 2
// (kit depth). The wheel walks the list, its push opens the row, Back
// leaves.
#include "app/screens/common.h"

namespace app::screens {
namespace {

class SampleOptions final : public Screen {
public:
    using Screen::Screen;

    void enter() override { cursor = 0; }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: cursor = std::clamp(cursor + use.delta, 0, int(rows.size()) - 1); return true;
        case Job::ListPush:
            if (cursor == 0) studio.open("chop"); // in this one's place, over the Play screen
            else studio.say("NOT YET");
            return true;
        case Job::Back: studio.close(); return true;
        default: return false; // the pads and knobs: the kit's Play screen's
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "OPT", pad_label(studio.pad()), "SMP");
        ui::list(g, ui::content(0, ui::height), rows, cursor, 1);
    }

private:
    const std::vector<std::string> rows {"CHOP >", "REVERSE", "TRIM >", "NORMALIZE", "SAVE SAMPLE"};
    int cursor = 0;
};

RegisterScreen registered("sample-options", make<SampleOptions>);

} // namespace
} // namespace app::screens
