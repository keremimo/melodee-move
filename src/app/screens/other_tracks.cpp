// SPDX-License-Identifier: AGPL-3.0-only
// The Play screens of the tracks whose own screens come later (docs/PARITY.md,
// wave 2): a keygroup, a MIDI track, an audio track, another host's plug-in,
// and an empty track. Each plays: a keygroup's, a MIDI track's and a
// plug-in's pads are notes as a synth track's (Up / Down the octave); an
// audio track's pads do nothing. An empty track says how to make one.
#include "app/screens/common.h"
#include "melodee/engine/engine_info.h"

namespace app::screens {
namespace {

constexpr int lowest_octave = -3, highest_octave = 4;

class NoteTrack : public Screen {
public:
    using Screen::Screen;
    void enter() override { held.fill(false); }

    bool surface(const move::Input& input) override {
        using Kind = move::Input::Kind;
        using move::Button;
        if (input.kind == Kind::Pad) {
            if (input.index < pad_count) held[input.index] = input.pressed;
            return true;
        }
        if (input.kind == Kind::Button && input.pressed && (Button(input.index) == Button::Up || Button(input.index) == Button::Down)) {
            int octave = std::clamp(studio.octave(studio.track()) + (Button(input.index) == Button::Up ? 1 : -1), lowest_octave, highest_octave);
            studio.set_octave(studio.track(), octave);
            studio.say("OCTAVE " + note_name(studio.note_for(0, octave)));
            return true;
        }
        return input.kind == Kind::Knob;
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        note_pads(studio, lights, held, true);
        steps_playhead(studio, lights);
        int octave = studio.octave(studio.track());
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu, Button::Back}) lights.button(button, 0);
        lights.button(Button::Up, octave < highest_octave ? white_dim : 0);
        lights.button(Button::Down, octave > lowest_octave ? white_dim : 0);
    }

protected:
    std::string lowest() const { return note_name(studio.note_for(0, studio.octave(studio.track()))); }
    std::string name() const { return upper(studio.current().name).substr(0, 16); }
    std::array<bool, pad_count> held {};
};

class KeysPlay final : public NoteTrack {
public:
    using NoteTrack::NoteTrack;
    void draw(juce::Graphics& g) override {
        ui::rail(g, "KEYS", track_number(studio), lowest());
        move::draw_text(g, ui::content_x + 1, 1, name());
        const auto& pad = studio.current().pads[0];
        bool loaded = pad.used() && studio.project().samples[pad.sample].path[0];
        move::draw_text_centred(g, ui::content_x, 22, ui::content_width, loaded ? upper(studio.project().samples[pad.sample].name).substr(0, 16) : "NO INSTRUMENT");
        ui::detail(g, "KEYGROUP", bar_beat(studio));
    }
};

class MidiPlay final : public NoteTrack {
public:
    using NoteTrack::NoteTrack;
    void draw(juce::Graphics& g) override {
        int channel = studio.current().params[melodee::midi_track::Channel];
        ui::rail(g, "MIDI", track_number(studio), "CH" + std::to_string(channel));
        move::draw_text(g, ui::content_x + 2, 2, "CH " + std::to_string(channel), 3);
        move::draw_text(g, ui::content_x + 2, 30, name());
        move::draw_text(g, ui::content_x + 2, 40, "LOWEST " + lowest());
        ui::detail(g, "MIDI OUT: LATER", bar_beat(studio));
    }
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

class AudioPlay final : public Screen {
public:
    using Screen::Screen;
    bool surface(const move::Input& input) override { return input.kind == move::Input::Kind::Pad || input.kind == move::Input::Kind::Knob; }
    void lights(move::Lights& lights) override {
        for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, move::colour::off);
        steps_playhead(studio, lights);
        for (auto button : {move::Button::Left, move::Button::Right, move::Button::Up, move::Button::Down, move::Button::Loop, move::Button::Menu, move::Button::Back})
            lights.button(button, 0);
    }
    void draw(juce::Graphics& g) override {
        ui::rail(g, "AUD", track_number(studio), "IN");
        move::draw_text(g, ui::content_x + 1, 1, upper(studio.current().name).substr(0, 16));
        move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "AUDIO TRACK");
        ui::detail(g, "RECORD: LATER", bar_beat(studio));
    }
};

class EmptyPlay final : public Screen {
public:
    using Screen::Screen;
    bool surface(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Button && input.pressed && move::Button(input.index) == move::Button::MainPush) {
            studio.open("new-track");
            return true;
        }
        return input.kind == move::Input::Kind::Pad || input.kind == move::Input::Kind::Knob;
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

RegisterScreen keys("keys-play", make<KeysPlay>, {}, kind_bit(TrackKind::Keys));
RegisterScreen midi("midi-play", make<MidiPlay>, {}, kind_bit(TrackKind::Midi));
RegisterScreen plugin("plugin-play", make<PluginPlay>, {}, kind_bit(TrackKind::Plugin));
RegisterScreen audio("audio-play", make<AudioPlay>, {}, kind_bit(TrackKind::Audio));
RegisterScreen empty("empty-play", make<EmptyPlay>, {}, kind_bit(TrackKind::Empty));

} // namespace
} // namespace app::screens
