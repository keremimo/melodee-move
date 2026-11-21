// SPDX-License-Identifier: AGPL-3.0-only
// The new-track sheet (docs/UI.md "Tracks"): what an empty track becomes, in
// Melodee's order: a kit of 32 pads, a synth (one of Schwung's modules, Load
// opening next), a keygroup, a MIDI track, an audio track. The jogwheel
// picks, its push makes it, Back leaves the track empty.
#include "app/screens/common.h"

namespace app::screens {
namespace {

struct Choice { TrackKind kind; const char* row; };
constexpr Choice choices[] {
    {TrackKind::Kit, "KIT      32 PADS"},
    {TrackKind::Synth, "SYNTH    SCHWUNG"},
    {TrackKind::Keys, "KEYGROUP     SFZ"},
    {TrackKind::Midi, "MIDI         OUT"},
    {TrackKind::Audio, "AUDIO      INPUT"},
};
constexpr int choice_count = int(sizeof choices / sizeof choices[0]);

class NewTrack final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        if (studio.kind() != TrackKind::Empty) studio.close(); // only an empty track becomes something
    }

    bool surface(const move::Input& input) override {
        using Kind = move::Input::Kind;
        using move::Button;
        if (input.kind == Kind::MainKnob) { cursor = std::clamp(cursor + input.delta, 0, choice_count - 1); return true; }
        if (input.kind == Kind::Pad || input.kind == Kind::Knob) return true;
        if (input.kind != Kind::Button || !input.pressed) return false;
        switch (Button(input.index)) {
        case Button::MainPush: {
            unsigned track = studio.track();
            auto kind = choices[cursor].kind;
            if (!studio.new_track(track, kind)) return true;
            studio.select(track); // its own Play screen
            if (kind == TrackKind::Synth) studio.open("load-modules");
            else if (kind == TrackKind::Kit) studio.open("load-samples");
            return true;
        }
        case Button::Back: studio.close(); return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, colour::off);
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "NEW", track_number(studio), "TRK");
        std::vector<std::string> rows;
        for (const auto& choice : choices) rows.push_back(choice.row);
        ui::list(g, ui::content(0, 50), rows, cursor, 1);
        ui::detail(g, "PUSH: MAKE");
    }

private:
    int cursor = 0;
};

RegisterScreen registered("new-track", make<NewTrack>);

} // namespace
} // namespace app::screens
