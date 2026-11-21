// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/pad_layout.h"
#include <string>

// Setup > PADS > LAYOUT (docs/UI.md, "Pads"; Kerem, 2026-11-20): the pad
// layout, Move rows or two MPC banks, one for every track and project, kept
// in Move.settings ("pads 0" Rows, "pads 1" Banks); no shortcut. Push edits
// it: the wheel turns it while the screen and the pads show its numbering
// (ui/pad_numbering.h), push sets it, Back leaves it as it was.
//
// The row for Setup's PADS list (move-projects' Setup; until it lands,
// screens/setup_pads.cpp is that list, on Shift + Step 2).
namespace app {
class Studio;

namespace pad_setting {

constexpr const char* name = "LAYOUT";
constexpr const char* value(PadLayout layout) { return layout == PadLayout::Banks ? "BANKS" : "ROWS"; }
// The detail line while it is edited, left and right.
constexpr const char* detail_left(PadLayout layout) { return layout == PadLayout::Banks ? "MPC BANKS" : "MOVE ROWS"; }
constexpr const char* detail_right(PadLayout layout) { return layout == PadLayout::Banks ? "2 X 4X4" : "8 A ROW"; }
constexpr const char* said(PadLayout layout) { return layout == PadLayout::Banks ? "PADS: MPC BANKS" : "PADS: MOVE ROWS"; }
// The wheel: right Banks, left Rows.
constexpr PadLayout turned(PadLayout layout, int delta) { return delta > 0 ? PadLayout::Banks : delta < 0 ? PadLayout::Rows : layout; }
// Pushed: the layout set (every pad's job and light changes at once), said,
// and Move.settings saved with the next save. Under the studio's lock.
void apply(Studio& studio, PadLayout layout);

} // namespace pad_setting
} // namespace app
