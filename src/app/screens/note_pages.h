// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/screens/common.h"
#include "melodee/engine/arp.h"
#include "melodee/engine/macros.h"
#include "melodee/model/chords.h"
#include "melodee/model/midi_track.h"

// The pages every note track's Play screen has (docs/UI.md, "Instruments"):
// MACRO on a module track (Melodee's FILTER, ECHO and SPACE, the mod wheel,
// pitch bend), and ARP and CHORD on every note track (a keygroup, a synth,
// a MIDI track). Each is eight knobs; the knobs write the track's own
// fields and the studio sends what the engine needs. Shared by the Play
// screens (keys_play.cpp, synth_play.cpp; a MIDI track's screen may take
// them too).
namespace app::screens {

enum class NotePage : uint8_t { Macro, Arp, Chord };
inline const char* note_page_title(NotePage page) {
    static const char* titles[] {"MACRO", "ARP", "CHORD"};
    return titles[unsigned(page) % 3];
}

// Knob n of a page: its cell name, its value as shown, its bar.
inline Knob note_page_knob(const Studio& studio, unsigned t, NotePage page, unsigned n) {
    const auto& track = studio.project().tracks[t % melodee::track_count];
    auto on_off = [](bool on) { return std::string(on ? "ON" : "OFF"); };
    switch (page) {
    case NotePage::Macro: {
        if (n < melodee::macro_slots) {
            const auto kind = melodee::macro_kind(track.params[melodee::plugin_track::Kind1 + n], n);
            const auto& info = melodee::macro_param(kind);
            int value = track.params[melodee::plugin_track::Macro1 + n];
            std::string shown = std::to_string(value);
            if (kind == melodee::MacroKind::Filter) shown = value < 0 ? "LP " + std::to_string(100 + value) : value > 0 ? "HP " + std::to_string(value) : "OFF";
            return {info.label, shown, float(value - info.min) / float(std::max(1, info.max - info.min))};
        }
        if (n == 3) return {"MOD", std::to_string(track.wheel), track.wheel / 127.0f};
        if (n == 4) return {"BEND", (track.bend > 0 ? "+" : "") + std::to_string(track.bend), (track.bend + 64) / 127.0f};
        return {};
    }
    case NotePage::Arp:
        switch (n) {
        case 0: return {"ON", on_off(track.arp), track.arp ? 1.0f : 0.0f};
        case 1: return {"MODE", upper(melodee::arp_mode_name(melodee::ArpMode(track.arp_mode))), track.arp_mode / 4.0f};
        case 2: return {"RATE", melodee::arp_rate_name(track.arp_rate), track.arp_rate / float(melodee::arp_rates - 1)};
        case 3: return {"OCT", std::to_string(track.arp_octaves), (track.arp_octaves - 1) / 3.0f};
        case 4: return {"GATE", std::to_string(track.arp_gate) + "%", track.arp_gate / 100.0f};
        case 5: return {"HOLD", on_off(track.arp_latch), track.arp_latch ? 1.0f : 0.0f};
        default: return {};
        }
    case NotePage::Chord:
        switch (n) {
        case 0: return {"ON", on_off(track.chord), track.chord ? 1.0f : 0.0f};
        case 1: return {"TYPE", upper(melodee::chord_type_name(track.chord_type)), track.chord_type / 5.0f};
        case 2: return {"VOICE", upper(melodee::chord_voicing_name(track.chord_voicing)), track.chord_voicing / 3.0f};
        case 3: return {"INV", upper(melodee::chord_invert_name(track.chord_invert)), track.chord_invert / 3.0f};
        case 4: return {"HOLD", on_off(track.chord_hold), track.chord_hold ? 1.0f : 0.0f};
        default: return {};
        }
    }
    return {};
}

// A knob turned: `delta` detents (an on-off or a list a step a detent; a
// level a unit, Shift a unit too for those already that fine).
inline void note_page_turn(Studio& studio, unsigned t, NotePage page, unsigned n, int delta) {
    if (!delta) return;
    t %= melodee::track_count;
    auto& track = studio.project().tracks[t];
    auto step = [&](auto& field, int low, int high) { field = std::decay_t<decltype(field)>(std::clamp(int(field) + delta, low, high)); };
    auto flip = [&](bool& field) { field = delta > 0; };
    switch (page) {
    case NotePage::Macro:
        if (n < melodee::macro_slots) studio.set_macro(t, n, track.params[melodee::plugin_track::Macro1 + n] + delta);
        else if (n == 3) studio.set_wheel(t, track.wheel + delta);
        else if (n == 4) studio.set_bend(t, track.bend + delta);
        return;
    case NotePage::Arp:
        switch (n) {
        case 0: flip(track.arp); break;
        case 1: step(track.arp_mode, 0, int(melodee::ArpMode::Count) - 1); break;
        case 2: step(track.arp_rate, 0, int(melodee::arp_rates) - 1); break;
        case 3: step(track.arp_octaves, 1, 4); break;
        case 4: step(track.arp_gate, 5, 100); break;
        case 5: flip(track.arp_latch); break;
        default: return;
        }
        studio.send_arp(t);
        return;
    case NotePage::Chord:
        switch (n) {
        case 0: flip(track.chord); break;
        case 1: step(track.chord_type, 0, 5); break;
        case 2: step(track.chord_voicing, 0, 3); break;
        case 3: step(track.chord_invert, 0, 3); break;
        case 4: flip(track.chord_hold); break;
        default: return;
        }
        studio.changed_playing(t);
        return;
    }
}

// The page drawn under the Device View's items: its title and knob cells.
inline void draw_note_page(juce::Graphics& g, const Studio& studio, unsigned t, NotePage page, const std::string& right) {
    move::draw_text(g, ui::content_x + 1, 11, note_page_title(page));
    if (!right.empty()) move::draw_text(g, ui::width - move::text_width(right), 11, right);
    std::array<std::string, 8> names;
    std::array<float, 8> values {};
    for (unsigned n = 0; n < 8; ++n) {
        auto knob = note_page_knob(studio, t, page, n);
        names[n] = knob.name;
        values[n] = knob.fraction;
    }
    ui::knob_cells(g, ui::content(20, ui::subject_height - 20), names, values);
}

// What the page shows on the right of its title and on the detail line.
inline std::string note_page_right(const Studio& studio, unsigned t, NotePage page) {
    const auto& track = studio.project().tracks[t % melodee::track_count];
    switch (page) {
    case NotePage::Arp: return track.arp ? "ON" : "OFF";
    case NotePage::Chord: {
        if (!track.chord) return "OFF";
        auto notes = pad_notes(studio.project(), track, 0, studio.key_shift(t));
        int keys[most_keys];
        for (unsigned k = 0; k < notes.count; ++k) keys[k] = notes.keys[k];
        auto name = melodee::name_chord(keys, notes.count);
        return upper(name.empty() ? "ON" : name).substr(0, 7);
    }
    default: return {};
    }
}

// Plus / Minus on a note track: the octave, as the Move's; on Drums, 16 pads
// (as a kit's banks). What it did, to say.
inline std::string octave_or_bank(Studio& studio, int way, int lowest_octave, int highest_octave) {
    auto& track = studio.current();
    if (keys_mode(track) == KeysMode::Drums) {
        track.bank = uint8_t(std::clamp(int(track.bank) + way, 0, int(keys_bank_count(track)) - 1));
        studio.publish_pads();
        studio.changed();
        auto first = pad_notes(studio.project(), track, 0);
        return "DRUMS FROM " + std::to_string(first.count ? first.keys[0] : 36);
    }
    studio.set_octave(studio.track(), std::clamp(studio.octave(studio.track()) + way, lowest_octave, highest_octave));
    auto first = pad_notes(studio.project(), track, 0, studio.key_shift(studio.track()));
    return "LOWEST " + (first.count ? note_name(first.keys[0]) : std::string("--"));
}

// A drum's name on Drums: a MIDI track's General MIDI one, else its note.
inline std::string drum_name(int key) {
    const char* name = melodee::gm_drum_name(unsigned(key));
    return std::to_string(key) + " " + upper(name ? name : note_name(key));
}

// The rows of fourths' lowest note, or a drum's, for the rail.
inline std::string lowest_note(const Studio& studio) {
    auto notes = pad_notes(studio.project(), studio.current(), 0, studio.key_shift(studio.track()));
    return notes.count ? note_name(notes.keys[0]) : "--";
}

// A note track's pads lit as Keys & Scale has them (app/keys.h), held ones white.
inline void key_pads(const Studio& studio, move::Lights& lights, const std::array<bool, pad_count>& held, int last_pad = -1) {
    auto colour = ui::track_colour(studio.track());
    const auto& track = studio.current();
    for (unsigned at = 0; at < pad_count; ++at)
        lights.pad(at, pad_light(pad_notes(studio.project(), track, at, studio.key_shift(studio.track())), track, colour.bright, colour.dim, held[at], int(at) == last_pad));
}

} // namespace app::screens
