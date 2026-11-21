// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>

// The Move's 32 pads in the layout the user chose (docs/MOVE_CONTROLS.md,
// "Pads"; Kerem, 2026-11-20):
//
//   Rows   the Move's own: eight a row, the bottom row 1-8, then 9-16 above
//          it, up to 25-32.
//   Banks  two MPC 4x4 banks side by side: the left bank 1-16 from its
//          bottom left (1 2 3 4 / 5 6 7 8 / 9 10 11 12 / 13 14 15 16, bottom
//          to top), the right bank 17-32 the same way.
//
// A pad's job (a kit pad, a slice, a note, a key, its light) is chosen by
// its logical number; move::Input reports the physical one. The lookup is
// arithmetic: no lock, no allocation, safe in App::audio_event().
namespace app {

enum class PadLayout : uint8_t { Rows, Banks };  // Rows: the Move's eight a row; Banks: two MPC 4x4 banks side by side

// physical: 0-31 as move::Input reports a pad (bottom-left first, eight a row)
// logical:  0-31, the pad's number minus one in the layout
// Anything past 31 is handed back as it came.
constexpr uint8_t logical_pad(uint8_t physical, PadLayout layout) {
    if (layout == PadLayout::Rows || physical >= 32) return physical;
    uint8_t row = physical / 8, column = physical % 8;
    return column < 4 ? uint8_t(row * 4 + column) : uint8_t(16 + row * 4 + (column - 4));
}
constexpr uint8_t physical_pad(uint8_t logical, PadLayout layout) {  // for lights
    if (layout == PadLayout::Rows || logical >= 32) return logical;
    uint8_t bank = logical / 16, in_bank = logical % 16;
    return uint8_t((in_bank / 4) * 8 + bank * 4 + in_bank % 4);
}

PadLayout pad_layout();                 // the current choice, an atomic: safe on the audio thread
void set_pad_layout(PadLayout layout);  // the MIDI or service thread's: Setup, the settings file
const char* pad_layout_name(PadLayout layout); // "rows", "banks": the settings file's words

} // namespace app
