// SPDX-License-Identifier: AGPL-3.0-only
// The Play screens of the tracks whose own screens come later (docs/PARITY.md,
// wave 2): another host's plug-in and an empty track (a keygroup's is
// screens/keys_play.cpp, a MIDI track's screens/midi_play.cpp, an audio
// track's sampling's screens/audio_track.cpp). A plug-in's pads are notes as
// Keys & Scale lays them out (Up / Down the octave). An empty track says how
// to make one.
#include "app/screens/note_pages.h"
#include "melodee/engine/engine_info.h"

namespace app::screens {
namespace {

constexpr int lowest_octave = -3, highest_octave = 4;

class NoteTrack : public Screen {
public:
    using Screen::Screen;
    void enter() override { held.fill(false); }

    void input(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Pad && input.index < pad_count) held[input.index] = input.pressed;
    }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::Octave: studio.say(octave_or_bank(studio, use.control == bindings::Control::Plus ? 1 : -1, lowest_octave, highest_octave)); return true;
        case Job::PlayNote: case Job::TurnKnob: case Job::TurnKnobFine: return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        key_pads(studio, lights, held); // as Keys & Scale lays them out (app/keys.h)
        steps_playhead(studio, lights);
        int octave = studio.octave(studio.track());
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu, Button::Back}) lights.button(button, 0);
        lights.button(Button::Up, octave < highest_octave ? white_dim : 0);
        lights.button(Button::Down, octave > lowest_octave ? white_dim : 0);
    }

protected:
    std::string lowest() const { return lowest_note(studio); }
    std::string name() const { return upper(studio.current().name).substr(0, 16); }
    std::array<bool, pad_count> held {};
};

class PluginPlay final : public NoteTrack {
public:
    using NoteTrack::NoteTrack;
    void draw(juce::Graphics& g) override {
        const auto& track = studio.current();
        const auto* ref = track.plugin < melodee::max_plugins ? &studio.project().plugins[track.plugin] : nullptr;
        ui::rail(g, "PLUG", track_number(studio), lowest());
        move::draw_text(g, ui::content_x + 1, 1, name());
        move::draw_text_centred(g, ui::content_x, 22, ui::content_width, ref && ref->used() ? upper(ref->name).substr(0, 16) : "NO PLUG-IN");
        ui::detail(g, "PLUG-IN", bar_beat(studio));
    }
};

class EmptyPlay final : public Screen {
public:
    using Screen::Screen;
    bool job(bindings::Job job, const bindings::Use&) override {
        if (job == bindings::Job::OpenBrowser) { studio.open("new-track"); return true; } // the browser's top: the track types
        return job == bindings::Job::TurnKnob || job == bindings::Job::TurnKnobFine;
    }
    void lights(move::Lights& lights) override {
        for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, move::colour::off);
        steps_playhead(studio, lights);
        for (auto button : {move::Button::Left, move::Button::Right, move::Button::Up, move::Button::Down, move::Button::Loop, move::Button::Menu, move::Button::Back})
            lights.button(button, 0);
    }
    void draw(juce::Graphics& g) override {
        ui::rail(g, "----", track_number(studio), "");
        move::draw_text_centred(g, ui::content_x, 18, ui::content_width, "EMPTY", 2);
        ui::detail(g, "PUSH: NEW TRACK");
    }
};

RegisterScreen plugin("plugin-play", make<PluginPlay>, {}, kind_bit(TrackKind::Plugin));
RegisterScreen empty("empty-play", make<EmptyPlay>, {}, kind_bit(TrackKind::Empty));

} // namespace
} // namespace app::screens
