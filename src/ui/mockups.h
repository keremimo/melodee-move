// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>

// The Rail design's screens for the first scope (docs/UI.md): drawn with
// the UI kit at the screen's real 128 x 64 one bit, as PNGs four times
// size, and one sheet of them all (rail.png), the design's reference.
namespace ui {

int write_mockups(const std::string& directory);
// The screens and lights docs/MOVE_CONTROLS.md moves or adds: controls.png
// and controls/ (mockups_controls.cpp).
bool write_controls_mockups(const std::string& directory);

} // namespace ui
