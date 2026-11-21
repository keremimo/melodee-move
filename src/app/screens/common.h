// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/pad_layout.h"
#include "app/screen.h"
#include "move/display.h"
#include "move/font.h"
#include "ui/track_style.h"
#include "ui/widgets.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// What the screens share: names, numbers and the playhead as they show them.
namespace app::screens {

// A pad's number in the layout the user chose (pad_layout.h), from the
// physical pad a job carries; and a light set by that number.
inline unsigned pad_number(int physical) { return logical_pad(uint8_t(physical), pad_layout()); }
inline void light_pad(move::Lights& lights, unsigned number, uint8_t colour) { lights.pad(physical_pad(uint8_t(number), pad_layout()), colour); }

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

// A place close up (docs/UI.md, "A touched knob"): the loudest of each of
// `width` columns over `span` frames from `from` (as the project counts
// them, 44.1 kHz; `from` may lie before the start), read from frames at
// `rate` Hz; -1 where there are none. A long stretch is read every so many
// frames on a grid fixed to the sample, so the picture keeps still as the
// place moves under it.
inline std::vector<float> close_peaks(const std::vector<float>& left, const std::vector<float>& right, unsigned rate, int64_t from,
                                      uint32_t span, int width) {
    std::vector<float> peaks(size_t(std::max(width, 0)), -1.0f);
    auto own = [&](int64_t frame) { return frame * int64_t(rate) / 44100; };
    int64_t total = int64_t(left.size());
    int64_t stride = std::max<int64_t>(1, own(span) / std::max(width, 1) / 64);
    for (int x = 0; x < width; ++x) {
        int64_t a = own(from + int64_t(span) * x / width), b = std::max(a + 1, own(from + int64_t(span) * (x + 1) / width));
        if (b <= 0 || a >= total) continue;
        float most = 0;
        for (int64_t f = (std::max<int64_t>(a, 0) + stride - 1) / stride * stride; f < b && f < total; f += stride) {
            most = std::max(most, std::fabs(left[size_t(f)]));
            if (size_t(f) < right.size()) most = std::max(most, std::fabs(right[size_t(f)]));
        }
        peaks[size_t(x)] = most;
    }
    return peaks;
}
// The same from a sample: its frames, or, for a long one played from
// storage (none in memory), its overview, coarser.
inline std::vector<float> close_peaks(const Sample& sample, int64_t from, uint32_t span, int width) {
    const auto& data = *sample.data;
    if (!data.left.empty()) return close_peaks(data.left, data.right, data.rate, from, span, width);
    std::vector<float> peaks(size_t(std::max(width, 0)), -1.0f);
    int64_t frames = std::max<int64_t>(1, data.model_frames()), size = int64_t(sample.overview.size());
    for (int x = 0; x < width; ++x) {
        int64_t a = from + int64_t(span) * x / width, b = from + int64_t(span) * (x + 1) / width;
        if (b <= 0 || a >= frames) continue;
        int64_t first = std::max<int64_t>(a, 0) * size / frames, last = std::max(first + 1, std::min(b, frames) * size / frames);
        uint8_t most = 0;
        for (int64_t c = first; c < last && c < size; ++c) most = std::max(most, sample.overview[size_t(c)]);
        peaks[size_t(x)] = most / 255.0f;
    }
    return peaks;
}
// A place close up in `area` (ui::close_up): `span` frames with `at` down
// the middle, what plays (`kept_from` to `kept_to`) lit and its ends marked
// where in view. `peaks(from, span, width)` reads the frames.
template <class Peaks>
void draw_span(juce::Graphics& g, juce::Rectangle<int> area, uint32_t span, uint32_t at, uint32_t kept_from, uint32_t kept_to, Peaks peaks) {
    int width = area.getWidth();
    span = std::max<uint32_t>(span, 1);
    int64_t from = int64_t(at) - int64_t(span / 2);
    auto column = [&](uint32_t frame) { return (int64_t(frame) - from) * width / int64_t(span); };
    std::vector<int> marks;
    for (uint32_t end : {kept_from, kept_to})
        if (end != at && column(end) >= 0 && column(end) < width) marks.push_back(int(column(end)));
    int lit_from = int(std::clamp<int64_t>(column(kept_from), -1, width)), lit_to = int(std::clamp<int64_t>(column(kept_to) - 1, -1, width));
    ui::close_up(g, area, peaks(from, span, width), lit_from, lit_to, marks);
}
// A place in `frames` (START, END, a loop point) close up, `zoom` times
// nearer than the whole. Says how near ("X8").
template <class Peaks>
std::string draw_place(juce::Graphics& g, juce::Rectangle<int> area, uint32_t frames, uint32_t at, uint32_t kept_from, uint32_t kept_to,
                       unsigned zoom, Peaks peaks) {
    uint32_t span = std::max<uint32_t>(uint32_t(area.getWidth()), frames / std::max(zoom, 1u));
    draw_span(g, area, span, at, kept_from, kept_to, peaks);
    return "X" + std::to_string(std::max(1u, frames / span));
}
inline std::string draw_place(juce::Graphics& g, juce::Rectangle<int> area, const Sample& sample, uint32_t at, uint32_t kept_from,
                              uint32_t kept_to, unsigned zoom) {
    uint32_t frames = sample.data->model_frames();
    if (!frames) return {}; // an instrument's: no frames of its own
    return draw_place(g, area, frames, at, kept_from, kept_to, zoom,
                      [&](int64_t from, uint32_t span, int width) { return close_peaks(sample, from, span, width); });
}
// How near a touched place shows: 8 times the whole, Shift held 32 (as a
// turn moves 1/256 of it, Shift 1/8192).
inline unsigned place_zoom(bool fine) { return fine ? 32 : 8; }

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
// The Device View's items along the top of the subject (docs/ui/controls.png):
// the track, its sound, its two effects; the one the wheel is on inverted.
inline void items(juce::Graphics& g, const std::array<std::string, 4>& names, int selected) {
    int cell = ui::content_width / 4;
    for (int n = 0; n < 4; ++n) {
        int x = ui::content_x + n * cell;
        g.setColour(juce::Colours::white);
        if (n == selected) g.fillRect(x, 0, cell - 1, ui::header_height);
        else g.fillRect(x, ui::header_height - 1, cell - 1, 1);
        g.setColour(n == selected ? juce::Colours::black : juce::Colours::white);
        move::draw_text_centred(g, x, 1, cell - 1, names[size_t(n)].substr(0, 4));
    }
    g.setColour(juce::Colours::white);
}
inline void steps_playhead(const Studio& studio, move::Lights& lights) {
    int step = playing_step(studio);
    for (unsigned n = 0; n < move::step_count; ++n) lights.step(n, int(n) == step ? move::colour::white : move::colour::off);
}

// A note track's pads: every C in the track's colour, the other naturals
// dim, a held pad white.
inline void note_pads(const Studio& studio, move::Lights& lights, const std::array<bool, pad_count>& held, bool sounding) {
    auto colour = studio.colour_of(studio.track()).bright;
    for (unsigned at = 0; at < pad_count; ++at) {
        int note = studio.note_for(at, studio.octave(studio.track()));
        lights.pad(at, held[at] ? move::colour::white : !sounding ? move::colour::off : note % 12 == 0 ? colour : natural(note) ? move::colour::dark_grey : move::colour::off);
    }
}

} // namespace app::screens
