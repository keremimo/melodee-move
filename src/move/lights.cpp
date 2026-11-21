// SPDX-License-Identifier: AGPL-3.0-only
#include "move/lights.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace move {
namespace {

// The documented palette entries (Cycling '74's rnbo.move.control, as listed
// in Schwung's constants.mjs; THIRD_PARTY.md). Duplicates are left out so a
// colour always maps to the first index that shows it.
struct PaletteEntry { uint8_t index; uint32_t rgb; };
constexpr PaletteEntry palette[] {
    {0, 0x000000}, {124, 0x1a1a1a}, {118, 0x595959}, {120, 0xffffff},
    {1, 0xff2424}, {2, 0xf23a0c}, {3, 0xff9900}, {4, 0xa68956}, {5, 0xedf95a}, {6, 0xc19d08},
    {7, 0xffff00}, {8, 0x56bf13}, {9, 0x2c8403}, {10, 0x246b24}, {11, 0x19ff30}, {12, 0x159573},
    {13, 0x176b50}, {14, 0x00ffff}, {15, 0x0074fc}, {16, 0x274fcc}, {17, 0x00448c}, {18, 0x644ad9},
    {19, 0x4d3fa0}, {20, 0x8700ff}, {21, 0xe657e3}, {22, 0x660099}, {23, 0xff0099}, {24, 0xa14c5f},
    {25, 0xff4dc4}, {26, 0xeb8be1}, {27, 0xa63421}, {28, 0x995628}, {29, 0x876700}, {30, 0x90821f},
    {31, 0x4a8700}, {32, 0x007f12}, {33, 0x1853b2}, {34, 0x624bad}, {35, 0x733a67},
    {43, 0xaeff99}, {44, 0x7cdd9f}, {45, 0x89b47d}, {46, 0x80f3ff}, {47, 0x7acefc}, {48, 0x68a1d3},
    {49, 0x858fc2}, {50, 0xbbaaf2},
    {65, 0x4d0b0b}, {66, 0x1a0404}, {67, 0x4d1204}, {73, 0x4d491f}, {74, 0x1a180a}, {75, 0x403302},
    {76, 0x1a1501}, {77, 0x4d4d00}, {78, 0x1a1a00}, {79, 0x1c4007}, {80, 0x0b1a03}, {81, 0x113301},
    {83, 0x113311}, {85, 0x0a4d0a}, {87, 0x073327}, {89, 0x104d39}, {90, 0x030d0a}, {93, 0x00234d},
    {95, 0x0c1940}, {97, 0x00254d}, {99, 0x231a4d}, {100, 0x0c091a}, {101, 0x251e4d}, {102, 0x0c0a1a},
    {104, 0x0d001a}, {105, 0x4d1d4c}, {106, 0x1a0a19}, {107, 0x33004d}, {108, 0x11001a}, {109, 0x4d002e},
    {110, 0x1a000f}, {111, 0x4d242d}, {112, 0x1a0c0f}, {113, 0x4d173b}, {114, 0x1a0814}, {115, 0x4d2d49},
    {116, 0x1a0f18}, {125, 0x0000ff}, {126, 0x00ff00}, {127, 0xff0000},
};

Rgb unpack(uint32_t rgb) { return {uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)}; }

// A light made dimmer goes down its own colour's rungs and no other: the
// palette's darker rows are colours 1-26 again, colour k's dim version at
// 63 + 2k and its dark one at 64 + 2k, about a third and a tenth as bright
// (Schwung's shared/constants.mjs lists every pair; ours lists most). Other
// palette colours bear no darker version of themselves and stay as they
// are; neighbouring entries of a like hue are other colours on the Move.
struct Rung { uint8_t index; float level; };
constexpr float dim_level = 0.35f, dark_level = 0.12f;
// The greys, as Schwung's white ramp: white, 122 (#CCCCCC), light grey,
// 123 (#404040), dark grey.
constexpr Rung greys[] {{120, 1.0f}, {122, 0.8f}, {118, 0.35f}, {123, 0.25f}, {124, 0.08f}};

// The index that shows `index` at `scale` of its brightness: of its own
// rungs from it down, the one nearest that (by ratio).
uint8_t dimmed_colour(uint8_t index, float scale) {
    Rung rungs[std::size(greys)] {};
    size_t count = 0;
    if (index >= 1 && index <= 26) {
        rungs[count++] = {index, 1.0f};
        rungs[count++] = {uint8_t(63 + 2 * index), dim_level};
        rungs[count++] = {uint8_t(64 + 2 * index), dark_level};
    } else if (index >= 65 && index <= 116 && index % 2) {
        rungs[count++] = {index, dim_level};
        rungs[count++] = {uint8_t(index + 1), dark_level};
    } else {
        for (const auto& grey : greys)
            if (count || grey.index == index) rungs[count++] = grey;
    }
    if (count < 2) return index; // a dark rung, or no darker version
    float target = rungs[0].level * scale;
    uint8_t best = index;
    float best_distance = std::numeric_limits<float>::max();
    for (size_t n = 0; n < count; ++n) {
        float distance = std::fabs(std::log(rungs[n].level / target));
        if (distance < best_distance) { best_distance = distance; best = rungs[n].index; }
    }
    return best;
}

} // namespace

uint8_t nearest_colour(Rgb rgb) {
    if (!rgb.r && !rgb.g && !rgb.b) return colour::off;
    uint8_t best = 0;
    long best_distance = std::numeric_limits<long>::max();
    for (const auto& entry : palette) {
        if (entry.index == 0) continue; // a colour asked for is never off
        Rgb candidate = unpack(entry.rgb);
        long dr = long(rgb.r) - candidate.r, dg = long(rgb.g) - candidate.g, db = long(rgb.b) - candidate.b;
        // Weighted for the eye: green differences show most, blue least.
        long distance = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (distance < best_distance) { best_distance = distance; best = entry.index; }
    }
    return best;
}

Rgb palette_rgb(uint8_t index) {
    for (const auto& entry : palette)
        if (entry.index == index) return unpack(entry.rgb);
    return {};
}

Lights::Lights() {
    note_want.fill(unset); note_sent.fill(unset);
    cc_want.fill(unset); cc_sent.fill(unset);
    for (const auto& info : buttons)
        if (info.led == LedKind::Rgb) cc_rgb[info.cc & 127] = true;
    // Play, Rec and Sampling keep their colours at every brightness, as on
    // the Move: green, red and dim white have no darker versions of
    // themselves, and a transport light in another colour reads as another state.
    for (Button kept : {Button::Play, Button::Rec, Button::Sample}) cc_kept[button_cc(kept)] = true;
    for (unsigned n = 0; n < 128; ++n) dimmed[n] = uint8_t(n);
}

uint8_t Lights::pad_wanted(unsigned index) const {
    if (index >= pad_count) return 0;
    uint8_t want = note_want[pad_note(index)];
    return want == unset ? 0 : want;
}

uint8_t Lights::step_wanted(unsigned index) const {
    if (index >= step_count) return 0;
    uint8_t want = note_want[step_note(index)];
    return want == unset ? 0 : want;
}

void Lights::set_brightness(uint8_t level) {
    level = std::min<uint8_t>(level, 3);
    if (level == brightness) return;
    brightness = level;
    // A quarter, a half and three quarters as the eye sees them: the LEDs'
    // light to the power 2.2.
    static const float scale[4] {0.047f, 0.218f, 0.531f, 1.0f};
    for (unsigned n = 0; n < 128; ++n) dimmed[n] = dimmed_colour(uint8_t(n), scale[level]);
    note_sent.fill(unset); // everything again, at the new level
    cc_sent.fill(unset);
}

void Lights::pad(unsigned index, uint8_t palette_index) {
    if (index < pad_count) note_want[pad_note(index)] = palette_index & 0x7f;
}

void Lights::step(unsigned index, uint8_t palette_index) {
    if (index < step_count) note_want[step_note(index)] = palette_index & 0x7f;
}

void Lights::step_icon(unsigned index, uint8_t brightness) {
    if (index < step_count) cc_want[step_icon_cc(index)] = brightness & 0x7f;
}

void Lights::button(Button button, uint8_t value) {
    if (button >= Button::Count || buttons[size_t(button)].led == LedKind::None) return;
    cc_want[button_cc(button)] = value & 0x7f;
}

void Lights::clear_all() {
    note_want.fill(0); cc_want.fill(0);
    note_sent.fill(unset); cc_sent.fill(unset);
}

bool Lights::pending() const {
    for (unsigned n = 0; n < 128; ++n)
        if ((note_want[n] != unset && note_want[n] != note_sent[n]) || (cc_want[n] != unset && cc_want[n] != cc_sent[n]))
            return true;
    return false;
}

} // namespace move
