// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/pad_layout.h"
#include <juce_graphics/juce_graphics.h>
#include <cstdint>

// The pad layout shown while Setup > PADS > LAYOUT is edited (docs/UI.md,
// "Pads"; docs/ui/pads.png): on the screen, where each run of pads starts;
// on the pads, each one's number by colour.
namespace ui {

// The subject: the 32 pads as they lie, where each run starts with its
// number inverted (1, 9, 17, 25 in Rows; 1, 5, 9, 13 and 17, 21, 25, 29 in
// Banks), the other pads small squares, the two banks apart.
void pad_numbering(juce::Graphics& g, app::PadLayout layout);

// A pad's light by its number (0-31) in `layout`: pad 1 white (and 17 in
// Banks), Banks the left bank azure and the right violet, Rows each row of
// eight azure and violet in turn.
uint8_t numbering_colour(unsigned number, app::PadLayout layout);

} // namespace ui
