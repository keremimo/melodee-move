// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/model/project.h"
#include <array>
#include <cstdint>

// A note track's 32 pads as the Move's Keys & Scale lays them out (its
// manual, 9.1; docs/UI.md, "Instruments"), with Melodee's own layouts:
//
// - In-Key, in octaves: a row an octave, a column the next note of the key
//   (a scale of more than eight notes cut at the eighth, of fewer going on
//   into the next octave on the same row);
// - In-Key, in 4ths: right the next note of the key, up a row a fourth (as
//   many notes of the key as lie within a fourth);
// - Chromatic: right a semitone, up a fourth;
// - Prog: a chord of the track's progression a pad (model/progression.h),
//   16 a bank, two banks on the 32 pads;
// - Drums: notes from 36 as a kit's pads (model/scales.h drum_key()), 16 a
//   bank.
//
// The key and scale are the project's; the mode, the rows and the transpose
// the track's, kept in Melodee's own fields so the MPC opens them: In-Key in
// octaves is Melodee's Scale layout, In-Key in 4ths its Chords layout (on
// the MPC the same notes, the chord's lit), Chromatic, Prog and Drums its
// own. A pad's chord (CHORD on, model/chords.h) and a progression's chords
// are Melodee's, built on the pad's place in the key.
//
// Pure functions of the model: the MIDI thread works the pads out with them
// and hands the audio thread a PadMap (studio.h), so a pad still sounds in
// the cycle it was read.
namespace app {

enum class KeysMode : uint8_t { InKey, Chromatic, Prog, Drums, Count };
const char* keys_mode_name(KeysMode mode); // "IN-KEY", "CHROMATIC", "PROG", "DRUMS"
KeysMode keys_mode(const melodee::Track& track);
bool keys_fourths(const melodee::Track& track); // In-Key in 4ths (Chromatic always is)
void set_keys_mode(melodee::Track& track, KeysMode mode, bool fourths);

constexpr unsigned keys_pads = 32;
constexpr unsigned most_keys = 8; // a chord's notes (Melodee's notes_per_step)

// What one pad plays and how it lights: its notes (none: a Prog pad with no
// chord), whether it is the key's root, in the key at all.
struct PadNotes {
    uint8_t count = 0;
    std::array<uint8_t, most_keys> keys {};
    bool root = false, in_key = false;
};
// `shift`: Shift + Plus / Minus, the bottom-left pad that many notes of the
// key up (In-Key; Chromatic a semitone each); not kept in the project.
PadNotes pad_notes(const melodee::Project& project, const melodee::Track& track, unsigned pad, int shift = 0);
// The key's notes held as a chord would be stored: lowest first.
// The bank of 16 a Prog's or Drums' pads start at (Plus / Minus, Shift + Plus / Minus).
unsigned keys_bank_count(const melodee::Track& track);

// The light a note track's pad shows, as the Move lights its notes: the
// key's roots in the track's colour, its other notes light grey, the rest
// dark; Prog: a chord dim, the file's root chord bright; Drums: dim, the
// last hit bright. `held`: white.
uint8_t pad_light(const PadNotes& notes, const melodee::Track& track, uint8_t track_colour, uint8_t track_dim, bool held, bool last);

// The audio thread's copy of the selected track's pads: worked out on the
// MIDI thread under the studio's lock, published whole (move::TripleBuffer).
struct PadMap {
    enum class Route : uint8_t { None, Notes, Arp, Repeat };
    int8_t track = -1;
    Route route = Route::None;
    bool full_velocity = false; // Shift + Step 10: every pad at 127
    bool latch = false;         // the arp's hold
    bool chord_hold = false;    // CHORD's hold: a chord sounds on until the next pad
    std::array<PadNotes, keys_pads> pads {};
};
void fill_pad_map(PadMap& map, const melodee::Project& project, unsigned track, int shift, bool full_velocity);

// The arp's settings as Melodee's engine takes them (Command::Arp: index 0
// on, 1 mode, 2 rate, 3 octaves, 4 gate), in the order to send them: the
// settings first, on last.
std::array<std::pair<uint16_t, int16_t>, 5> arp_settings(const melodee::Track& track);

} // namespace app
