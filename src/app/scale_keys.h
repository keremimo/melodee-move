// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "melodee/model/project.h"
#include "melodee/model/scales.h"
#include <array>

// MIDI Config's SCALE KEYS (Midi::scale_keys), melodee-sample's Scale on
// white keys (its shell/scale_keys.h): on a track whose pads play the
// project's scale (In-Key: the Scale and Chords layouts), a keyboard's white
// keys play the scale's notes in order and its black keys play nothing.
// C4 (60) plays the root nearest it (C to F# above, G to B below), so a
// seven-note scale has its root on every C; a scale of more or fewer notes
// goes on up the white keys, a note a key, with no key left out. The
// Chromatic scale plays every key as it is.
namespace melodee {

// Whether the white keys play the scale on `track` (with the setting on).
inline bool scale_keys_on(const Project& project, const Track& track) {
    if (track.layout != PadLayout::Scale && track.layout != PadLayout::Chords) return false;
    if (track.engine == EngineKind::None || track.engine == EngineKind::Sampler || track.engine == EngineKind::Audio) return false;
    std::array<uint8_t, 12> degrees {};
    return scale_degrees(project.scale, degrees) < 12;
}

// The note white key `key` plays in the project's key and scale; -1 for a black key or a note past 0..127.
inline int scale_key_note(const Project& project, int key) {
    static constexpr int white[12] {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6}; // a key's place in its octave's white keys
    if (key < 0 || key > 127 || white[key % 12] < 0) return -1;
    std::array<uint8_t, 12> degrees {};
    const int count = int(scale_degrees(project.scale, degrees));
    const int root = int(project.root % 12), nearest = root > 6 ? root - 12 : root;
    const int step = 7 * (key / 12 - 5) + white[key % 12]; // white keys from C4: C4 0, D4 1, B3 -1
    const int octave = step >= 0 ? step / count : -((count - 1 - step) / count);
    const int note = 60 + nearest + 12 * octave + degrees[size_t(step - octave * count)];
    return note >= 0 && note <= 127 ? note : -1;
}

} // namespace melodee
