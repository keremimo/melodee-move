// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/screen.h"
#include "move/display.h"
#include "move/font.h"
#include "ui/track_style.h"
#include "ui/widgets.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

// What the screens share: names, numbers and the playhead as they show them.
namespace app::screens {

inline std::string upper(std::string text) {
    for (auto& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}
inline std::string pad_label(unsigned pad) {
    char text[8];
    std::snprintf(text, sizeof text, "%c%02u", 'A' + int(pad / 16), pad % 16 + 1);
    return text;
}
inline std::string seconds(uint32_t frames) {
    char text[16];
    std::snprintf(text, sizeof text, "%.2fS", frames / 44100.0);
    return text;
}
inline std::string two_digits(unsigned value) {
    char text[8];
    std::snprintf(text, sizeof text, "%02u", value % 100);
    return text;
}
inline std::vector<float> peaks_of(const melodee::Overview& overview) {
    std::vector<float> peaks(overview.size());
    for (size_t n = 0; n < overview.size(); ++n) peaks[n] = overview[n] / 255.0f;
    return peaks;
}
inline std::string note_name(int note) {
    static const char* names[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[note % 12] + std::to_string(note / 12 - 2); // Ableton's names: 60 is C3
}
inline bool natural(int note) {
    int in_octave = note % 12;
    return in_octave != 1 && in_octave != 3 && in_octave != 6 && in_octave != 8 && in_octave != 10;
}
inline std::string track_number(const Studio& studio) { return std::to_string(studio.track() + 1); }

// Where the song is while it plays, as "bar.beat" (from Play), else empty.
inline std::string bar_beat(const Studio& studio) {
    if (!studio.playing()) return {};
    unsigned ticks = studio.host().ticks(), bar = studio.project().bar_ticks(), beat = studio.project().beat_ticks();
    return std::to_string(ticks / std::max(bar, 1u) + 1) + "." + std::to_string(ticks % std::max(bar, 1u) / std::max(beat, 1u) + 1);
}
// The step the selected track's pattern is on (0-15), -1 while stopped.
inline int playing_step(const Studio& studio) {
    if (!studio.playing()) return -1;
    int position = studio.host().position_of(studio.track());
    if (position < 0) position = int(studio.host().ticks());
    return int(unsigned(position) / melodee::sixteenth_ticks % move::step_count);
}
inline void steps_playhead(const Studio& studio, move::Lights& lights) {
    int step = playing_step(studio);
    for (unsigned n = 0; n < move::step_count; ++n) lights.step(n, int(n) == step ? move::colour::white : move::colour::off);
}

// A note track's pads: every C in the track's colour, the other naturals
// dim, a held pad white.
inline void note_pads(const Studio& studio, move::Lights& lights, const std::array<bool, pad_count>& held, bool sounding) {
    auto colour = ui::track_colour(studio.track()).bright;
    for (unsigned at = 0; at < pad_count; ++at) {
        int note = studio.note_for(at, studio.octave(studio.track()));
        lights.pad(at, held[at] ? move::colour::white : !sounding ? move::colour::off : note % 12 == 0 ? colour : natural(note) ? move::colour::dark_grey : move::colour::off);
    }
}

} // namespace app::screens
