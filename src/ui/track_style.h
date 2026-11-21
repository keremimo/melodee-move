// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>

// How the sixteen tracks look on the Move (docs/UI.md, "Tracks"): each its
// own colour on the lights, bright where it is the selected one, dim where
// it is only there. Palette indices of the Move's fixed palette
// (move/lights.h).
namespace ui {

struct TrackColour { uint8_t bright, dim; };
constexpr std::array<TrackColour, 16> track_colours {{
    {15, 97},  {3, 75},   {18, 101}, {8, 79},   // azure, orange, violet, green
    {7, 77},   {21, 105}, {14, 89},  {1, 65},   // yellow, magenta, cyan, red
    {16, 95},  {23, 109}, {12, 87},  {20, 107}, // royal blue, pink, mint, purple
    {11, 85},  {25, 113}, {47, 93},  {4, 73},   // lime, rose, sky, tan
}};
constexpr TrackColour track_colour(unsigned track) { return track_colours[track % track_colours.size()]; }

// Four tracks to a bank: the four track buttons are the selected track's
// bank (tracks 1-4, 5-8, 9-12, 13-16).
constexpr unsigned tracks_a_bank = 4;
constexpr unsigned bank_of(unsigned track) { return track / tracks_a_bank; }

} // namespace ui
