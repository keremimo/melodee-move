// SPDX-License-Identifier: AGPL-3.0-only
// The question before a loss (Melodee's one sheet, MELODEE.md › Across every
// page): what is asked, what goes, whether undo brings it back. A push says
// yes, or the pad it asks about pressed again (the Move's Delete + pad);
// Back, or any other press, says no. A touch (the wheel's, a knob's) is no
// press: it leaves the question up.
#include "app/screens/common.h"
#include "ui/system_draw.h"

namespace app::screens {
namespace {

class Question final : public Screen {
public:
    using Screen::Screen;

    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        const auto& q = studio.asked;
        // A touch is no press: a hand off the wheel or a knob and back on leaves it asked.
        if (use.control == bindings::Control::WheelTouch || use.control == bindings::Control::KnobTouch ||
            use.control == bindings::Control::VolumeTouch)
            return true;
        bool pad_again = use.control == bindings::Control::Pad && q.pad >= 0 && use.index == q.pad;
        if (job == Job::ListPush || pad_again) {
            auto yes = q.yes;
            studio.close();
            if (yes) yes();
            return true;
        }
        if (job == Job::ListTurn || job == Job::ListTurnFine || job == Job::TurnKnob || job == Job::TurnKnobFine)
            return true; // nothing to turn
        if (use.control == bindings::Control::Pad && q.pad < 0) return false; // the pads play on
        studio.close(); // Back, or anything else: no
        return true;
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        const auto& q = studio.asked;
        if (auto* play = studio.screen(home_screen(studio.kind())); play && play != this && q.pad < 0) play->lights(lights);
        if (q.pad >= 0) { // the pad it is about, red; the rest dark
            for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, int(at) == q.pad ? colour::red : colour::off);
        }
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        const auto& q = studio.asked;
        ui::system::Question shown;
        shown.rail_top = q.rail_top;
        shown.rail_big = q.rail_big;
        shown.rail_bottom = q.rail_bottom;
        shown.asked = q.asked;
        shown.lines = {q.line1, q.line2};
        shown.undoable = q.undoable;
        shown.yes = q.yes_key;
        ui::system::question(g, shown);
    }

    std::string status() const override { return studio.top() == this ? " asking=1" : ""; }
};

RegisterScreen registered("question", make<Question>);

} // namespace
} // namespace app::screens
