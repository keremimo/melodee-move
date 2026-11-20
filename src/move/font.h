// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <string_view>

// A 5 x 7 pixel font for the 128 x 64 screen, drawn as whole pixels so it
// stays sharp through the screen's one bit (an outline font at 7 pixels
// breaks up). ASCII 32-126; each glyph advances 6 pixels times `scale`.
// Draws in the Graphics' current colour.
namespace move {

constexpr int glyph_width = 5, glyph_height = 7, glyph_advance = 6;

// Returns the x after the last glyph.
int draw_text(juce::Graphics& g, int x, int y, std::string_view text, int scale = 1);
int text_width(std::string_view text, int scale = 1);
// Centred between x and x + width.
void draw_text_centred(juce::Graphics& g, int x, int y, int width, std::string_view text, int scale = 1);

} // namespace move
