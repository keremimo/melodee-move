// SPDX-License-Identifier: AGPL-3.0-only
// The new-track sheet's KIT, its second step (Melodee's Drum sources,
// guide/tracks.md): where the kit's first sound comes from. LIBRARY (the
// MPC's Card) makes the kit and opens Load for its first pad; EMPTY makes
// it with no samples; INPUT makes it and opens Sampling to record its first
// pad. The wheel picks, its push makes it, Back goes back to the types.
#include "app/screens/common.h"

namespace app::screens {
namespace {

struct Source { const char* row; const char* opens; const char* name; };
constexpr Source sources[] {{"LIBRARY >", "load-samples", "library"}, {"EMPTY", "", "empty"}, {"INPUT >", "sampling", "input"}};
constexpr int source_count = int(sizeof sources / sizeof sources[0]);

class KitSources final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        if (studio.kind() != TrackKind::Empty) studio.close();
    }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: cursor = std::clamp(cursor + use.delta, 0, source_count - 1); return true;
        case Job::ListPush: {
            unsigned track = studio.track();
            if (!studio.new_track(track, TrackKind::Kit)) return true;
            studio.select(track); // its own Play screen
            if (*sources[cursor].opens) studio.open(sources[cursor].opens);
            return true;
        }
        case Job::Back: studio.close(); return true; // the types again
        case Job::TurnKnob: case Job::TurnKnobFine: case Job::PlayPad: case Job::PlayNote: return true;
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
        ui::rail(g, "NEW", track_number(studio), "KIT");
        std::vector<std::string> rows;
        for (const auto& source : sources) rows.push_back(source.row);
        ui::list(g, ui::content(0, 50), rows, cursor, 1);
        ui::detail(g, "PUSH: MAKE");
    }
    std::string status() const override { return std::string(" kit_source=") + sources[cursor].name; }

private:
    int cursor = 0;
};

RegisterScreen registered("kit-sources", make<KitSources>);

} // namespace
} // namespace app::screens
