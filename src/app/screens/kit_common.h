// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/screens/common.h"
#include "app/pad_layout.h"
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
        if (kit < 0) { // a pitch: the right bank, MPC style
            unsigned pitch = logical_pad(uint8_t(at), PadLayout::Banks) - 16;
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

// The pads in view as the rail and messages name them: "C01-D16".
inline std::string banks_in_view(const Studio& studio) {
    unsigned first = studio.pad_first();
    return pad_label(first) + "-" + pad_label(first + studio.pads_in_view() - 1);
}

} // namespace app::screens
