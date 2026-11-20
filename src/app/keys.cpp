// SPDX-License-Identifier: AGPL-3.0-only
#include "app/keys.h"
#include "app/pad_layout.h"
#include "move/controls.h"
#include "move/lights.h"
#include "melodee/model/chords.h"
#include "melodee/model/progression.h"
#include "melodee/model/scales.h"
#include <algorithm>

namespace app {

using Layout = melodee::PadLayout; // Melodee's pad layouts (app::PadLayout is the pads' physical order)
using melodee::Track;

namespace {

int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

bool note_track(const Track& track) {
    return track.engine == melodee::EngineKind::Keygroup || track.engine == melodee::EngineKind::Plugin || track.engine == melodee::EngineKind::Midi;
}

// The key's notes as degrees: degree(i) the i-th note of the key above the
// pads' base (negative below it), as Melodee's pad_note() counts a scale.
struct Key {
    std::array<uint8_t, 12> degrees {};
    unsigned count = 1;
    int base = 0;   // the bottom-left pad's root: the key's root in the track's octave, moved by its transpose
    uint16_t mask = 1;
    int degree(int i) const { return base + 12 * floor_div(i, int(count)) + degrees[size_t(((i % int(count)) + int(count)) % int(count))]; }
    // Of a note: whether the key has it, and its root.
    bool has(int note) const { return (mask >> unsigned(((note - base) % 12 + 12) % 12)) & 1; }
    bool root(int note) const { return ((note - base) % 12 + 12) % 12 == 0; }
    // The note's place among the key's degrees (it has it).
    int index_of(int note) const {
        int octave = floor_div(note - base, 12), within = (note - base) - 12 * octave;
        for (unsigned d = 0; d < count; ++d) if (degrees[d] == within) return octave * int(count) + int(d);
        return 0;
    }
};

Key key_of(const melodee::Project& project, const Track& track) {
    Key key;
    key.count = melodee::scale_degrees(project.scale, key.degrees);
    if (!key.count) { key.count = 1; key.degrees[0] = 0; }
    key.mask = melodee::scales()[project.scale % melodee::scales().size()].mask;
    key.base = 12 * (track.octave + 1) + project.root + track.transpose;
    return key;
}

// A chord stacked from the key above degree `index` (model/chords.h's types,
// voicings and inversions); Chromatic's out-of-key notes stack the major
// scale above themselves, as Melodee's chords do on a chromatic scale.
unsigned chord_on(const Key& key, int index, const Track& track, std::array<int, most_keys>& out, int chromatic_root = -1) {
    static const std::array<std::array<int, 4>, 6> stacks {{{0, 2, 4, -1}, {0, 2, 4, 6}, {0, 1, 4, -1}, {0, 3, 4, -1}, {0, 4, 7, -1}, {0, 2, 4, 8}}};
    static const int major[7] {0, 2, 4, 5, 7, 9, 11};
    auto degree = [&](int step) {
        if (chromatic_root >= 0) return chromatic_root + 12 * (step / 7) + major[step % 7];
        return key.degree(index + step);
    };
    const auto& stack = stacks[track.chord_type % 6];
    unsigned n = 0;
    for (int step : stack) if (step >= 0) out[n++] = degree(step);
    if (track.chord_type % 6 == 4) out[2] = out[0] + 12; // power: root, fifth, octave
    switch (track.chord_voicing % 4) {
    case 1: if (n > 2) out[1] += 12; break;
    case 2: if (n > 2) out[n - 2] -= 12; break;
    case 3: for (unsigned k = 1; k < n; k += 2) out[k] += 12; break;
    default: break;
    }
    auto sort = [&] { for (unsigned i = 1; i < n; ++i) for (unsigned j = i; j > 0 && out[j - 1] > out[j]; --j) std::swap(out[j - 1], out[j]); };
    sort();
    for (unsigned k = 0; k < std::min<unsigned>(track.chord_invert % 4, n - 1); ++k) { out[0] += 12; sort(); }
    return n;
}

void put(PadNotes& pad, const std::array<int, most_keys>& keys, unsigned n) {
    pad.count = 0;
    for (unsigned k = 0; k < n && pad.count < most_keys; ++k)
        if (keys[k] >= 0 && keys[k] <= 127) pad.keys[pad.count++] = uint8_t(keys[k]);
}

} // namespace

const char* keys_mode_name(KeysMode mode) {
    static const char* names[] {"IN-KEY", "CHROMATIC", "PROG", "DRUMS"};
    return names[unsigned(mode) % unsigned(KeysMode::Count)];
}

KeysMode keys_mode(const Track& track) {
    switch (track.layout) {
    case Layout::Chromatic: return KeysMode::Chromatic;
    case Layout::Prog: return KeysMode::Prog;
    case Layout::Drums: return KeysMode::Drums;
    default: return KeysMode::InKey; // Scale, Chords (In-Key in 4ths), Melodee's 16 Levels layout
    }
}

bool keys_fourths(const Track& track) { return track.layout == Layout::Chords || track.layout == Layout::Chromatic; }

void set_keys_mode(Track& track, KeysMode mode, bool fourths) {
    switch (mode) {
    case KeysMode::Chromatic: track.layout = Layout::Chromatic; break;
    case KeysMode::Prog: track.layout = Layout::Prog; break;
    case KeysMode::Drums: track.layout = Layout::Drums; break;
    default: track.layout = fourths ? Layout::Chords : Layout::Scale; break;
    }
    if (mode == KeysMode::Prog || mode == KeysMode::Drums) track.bank = uint8_t(std::min<unsigned>(track.bank, keys_bank_count(track) - 1));
}

unsigned keys_bank_count(const Track& track) {
    switch (keys_mode(track)) {
    case KeysMode::Prog: return melodee::prog_chords > 32 ? (melodee::prog_chords - 32) / 16 + 1 : 1; // 48 chords: 1-32, 17-48
    case KeysMode::Drums: return (127 - melodee::drum_first_key - 31) / 16 + 1;                    // to 127: four banks
    default: return 1;
    }
}

PadNotes pad_notes(const melodee::Project& project, const Track& track, unsigned pad, int shift) {
    PadNotes out;
    if (pad >= keys_pads) return out;
    const int column = int(move::pad_column(pad)), row = int(move::pad_row(pad));
    std::array<int, most_keys> keys {};
    switch (keys_mode(track)) {
    case KeysMode::Prog: {
        // "Pad n": numbered in the layout the pads are in (app/pad_layout.h).
        unsigned index = 16 * std::min<unsigned>(track.bank, keys_bank_count(track) - 1) + logical_pad(uint8_t(pad), pad_layout());
        if (index >= melodee::prog_chords) return out;
        std::array<int, melodee::notes_per_step> prog {};
        unsigned n = melodee::prog_keys(track, index, prog);
        for (unsigned k = 0; k < n && k < most_keys; ++k) keys[k] = prog[k] - 12; // the Move's octave 0 (C2 bottom left) plays the file's notes
        put(out, keys, std::min<unsigned>(n, most_keys));
        out.root = n && track.prog.chords[index].root;
        out.in_key = n > 0;
        return out;
    }
    case KeysMode::Drums: {
        unsigned at = 16 * std::min<unsigned>(track.bank, keys_bank_count(track) - 1) + logical_pad(uint8_t(pad), pad_layout());
        int key = track.engine == melodee::EngineKind::Midi ? int(track.midi.drum_note[at % melodee::sampler_pads]) : int(melodee::drum_first_key + at);
        if (key > 127) return out;
        keys[0] = key;
        put(out, keys, 1);
        out.in_key = true;
        return out;
    }
    case KeysMode::Chromatic: {
        Key key = key_of(project, track);
        int note = key.base + column + 5 * row + shift;
        out.root = key.root(note);
        out.in_key = key.has(note);
        unsigned n = 1;
        keys[0] = note;
        if (track.chord) n = out.in_key ? chord_on(key, key.index_of(note), track, keys) : chord_on(key, 0, track, keys, note);
        put(out, keys, n);
        return out;
    }
    default: {
        Key key = key_of(project, track);
        int rows = 0;
        if (keys_fourths(track)) for (unsigned d = 0; d < key.count; ++d) rows += key.degrees[d] < 5; // the key's notes within a fourth
        else rows = int(key.count);                                                                 // an octave a row
        int index = column + std::max(rows, 1) * row + shift;
        keys[0] = key.degree(index);
        unsigned n = track.chord ? chord_on(key, index, track, keys) : 1;
        out.root = key.root(keys[0]);
        out.in_key = true;
        put(out, keys, n);
        return out;
    }
    }
}

uint8_t pad_light(const PadNotes& notes, const Track& track, uint8_t colour, uint8_t dim, bool held, bool last) {
    if (held) return move::colour::white;
    if (!notes.count) return move::colour::off;
    switch (keys_mode(track)) {
    case KeysMode::Prog: return notes.root ? colour : dim;
    case KeysMode::Drums: return last ? colour : dim;
    default: return notes.root ? colour : notes.in_key ? move::colour::light_grey : move::colour::off;
    }
}

std::array<std::pair<uint16_t, int16_t>, 5> arp_settings(const Track& track) {
    return {{{1, int16_t(track.arp_mode)}, {2, int16_t(track.arp_rate)}, {3, int16_t(track.arp_octaves)}, {4, int16_t(track.arp_gate)},
             {0, int16_t(track.arp)}}};
}

void fill_pad_map(PadMap& map, const melodee::Project& project, unsigned t, int shift, bool full_velocity) {
    const auto& track = project.tracks[t % melodee::track_count];
    map.track = int8_t(t % melodee::track_count);
    map.full_velocity = full_velocity;
    map.latch = track.arp_latch;
    map.chord_hold = track.chord && track.chord_hold;
    if (!note_track(track)) {
        map.route = PadMap::Route::None;
        return;
    }
    map.route = track.repeat ? PadMap::Route::Repeat : track.arp ? PadMap::Route::Arp : PadMap::Route::Notes;
    for (unsigned pad = 0; pad < keys_pads; ++pad) map.pads[pad] = pad_notes(project, track, pad, shift);
}

} // namespace app
