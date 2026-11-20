// SPDX-License-Identifier: AGPL-3.0-only
#include "move/lights.h"
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
}

void Lights::pad(unsigned index, uint8_t palette_index) {
    if (index < pad_count) note_want[pad_note(index)] = palette_index & 0x7f;
}

void Lights::step(unsigned index, uint8_t palette_index) {
    if (index < step_count) note_want[step_note(index)] = palette_index & 0x7f;
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
