// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/screens/common.h"
#include "melodee/model/scales.h"
#include <chrono>
#include <cmath>

// What a kit's screens share (docs/UI.md, "Kits"): the pads' lights, the
// pads in view, a pad's light colour.
namespace app::screens {

// A pad's colour as it lights: its own (Stage's COLOUR, an MPC kit's) or the
// track's; `bright` while it sounds or is chosen, else dim (Kerem,
// 2026-11-20: dim, with a flare as it sounds).
inline uint8_t pad_colour(const Studio& studio, const melodee::PadSound& sound, bool bright) {
    if (!sound.colour) {
        auto colour = ui::track_colour(studio.track());
        return bright ? colour.bright : colour.dim;
    }
    uint32_t rgb = melodee::full_colour(sound.colour);
    uint8_t r = uint8_t(rgb >> 16), g = uint8_t(rgb >> 8), b = uint8_t(rgb);
    if (!bright) { r = uint8_t(r * 3 / 10); g = uint8_t(g * 3 / 10); b = uint8_t(b * 3 / 10); }
    return move::nearest_colour({r, g, b});
}

// Blinking, for a pad waiting for something (the copy's source, the pad a
// sample loads onto): on half of each half second.
inline bool blink_on() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() / 250 % 2 == 0;
}

// The kit's pads (Kerem's rule for kit pad lights, docs/MOVE_CONTROLS.md):
// a pad with a sample dim in its colour and bright while it sounds, the last
// pad white, a muted pad dark grey (the Move's dim white), an empty one off;
// `blinking` (a kit pad, or -1) blinks white. With 16 Pitches the right 16
// are the last pad's pitches: the key's roots in the track's colour, its
// other notes light grey.
inline void kit_pads(const Studio& studio, move::Lights& lights, int blinking = -1) {
    using namespace move;
    const auto& track = studio.current();
    auto sounding = studio.sounding();
    for (unsigned at = 0; at < pad_count; ++at) {
        int kit = studio.kit_pad_at(at);
        if (kit < 0) { // a pitch
            unsigned pitch = pad_row(at) * 4 + pad_column(at) - 4;
            bool root = melodee::level_scale_step(studio.project(), pitch) % 12 == 0;
            lights.pad(at, root ? ui::track_colour(studio.track()).bright : colour::light_grey);
            continue;
        }
        const auto& sound = track.pads[unsigned(kit)];
        uint8_t shown = colour::off;
        if (kit == blinking) shown = blink_on() ? colour::white : sound.used() ? pad_colour(studio, sound, false) : colour::off;
        else if (unsigned(kit) == studio.pad()) shown = sound.used() ? colour::white : colour::dark_grey;
        else if (!sound.used()) shown = colour::off;
        else if (track.pad_muted(unsigned(kit))) shown = colour::dark_grey;
        else shown = pad_colour(studio, sound, sounding[unsigned(kit)]);
        lights.pad(at, shown);
    }
}

// The frames round `at` (the project's frames) across the subject, 32 times
// closer than the whole sample, the place a line down the middle: the Move's
// Sample Start zoom, for Shift + a touched START, END or LOOP POINT. Only a
// sample in memory has the frames to draw.
inline bool close_up(juce::Graphics& g, const Sample& sample, uint32_t at) {
    const auto& data = *sample.data;
    if (data.left.empty()) return false;
    auto area = ui::content(0, ui::subject_height);
    uint32_t frames = std::max<uint32_t>(1, data.model_frames());
    uint32_t span = std::max<uint32_t>(uint32_t(area.getWidth()), frames / 32);
    int64_t from = int64_t(at) - span / 2;
    std::vector<float> peaks(size_t(area.getWidth()), 0.0f);
    for (int column = 0; column < area.getWidth(); ++column) {
        int64_t a = from + int64_t(span) * column / area.getWidth(), b = from + int64_t(span) * (column + 1) / area.getWidth();
        float peak = 0;
        for (int64_t f = std::max<int64_t>(a, 0); f < b && f < int64_t(frames); f += std::max<int64_t>(1, (b - a) / 16)) {
            size_t own = data.own(uint32_t(f));
            if (own < data.left.size()) peak = std::max(peak, std::abs(data.left[own]));
            if (own < data.right.size()) peak = std::max(peak, std::abs(data.right[own]));
        }
        peaks[size_t(column)] = peak;
    }
    ui::waveform(g, area, peaks);
    g.fillRect(area.getX() + area.getWidth() / 2, area.getY(), 1, area.getHeight());
    return true;
}

// The pads in view as the rail and messages name them: "C01-D16".
inline std::string banks_in_view(const Studio& studio) {
    unsigned bank = studio.pad_bank();
    std::string text = pad_label(bank * 16);
    return text + "-" + pad_label(bank * 16 + (studio.pitches() ? 15 : 31));
}

} // namespace app::screens
