// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/display.h"
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <string>
#include <string_view>
#include <vector>

// The pieces Melodee's Move screens are made of (docs/UI.md): drawn in whole
// pixels, white on black, for the 128 x 64 one-bit screen. Big and focused:
// one subject a screen, large values, the knobs' detail only when touched.
namespace ui {

constexpr int width = move::screen_width, height = move::screen_height;
constexpr int header_height = 9, strip_height = 5;

// The rail (docs/UI.md, "The rail"): a white column on the left that always
// says where you are. `top` and `bottom` are up to four characters; `big`
// is the one number that matters here, drawn large when it is one or two
// characters. The subject has the rest: content(), and under it detail().
constexpr int rail_width = 24, content_x = rail_width + 2, content_width = width - content_x;
constexpr int detail_y = 56, subject_height = detail_y - 4;
void rail(juce::Graphics& g, std::string_view top, std::string_view big, std::string_view bottom);
inline juce::Rectangle<int> content(int y = 0, int h = height) { return {content_x, y, content_width, h}; }
// The line under the subject: the left text, and the right one flush right.
void detail(juce::Graphics& g, std::string_view left, std::string_view right = {});
// A vertical line through a waveform: where playback is.
void playhead(juce::Graphics& g, juce::Rectangle<int> area, int column);
// A filled circle, for the record light.
void dot(juce::Graphics& g, int cx, int cy, int radius);

// The top line: inverted, what this screen is on the left, where on the right.
void header(juce::Graphics& g, std::string_view left, std::string_view right = {});

// A sample's waveform: one peak (0..1) a column, mirrored about the middle.
// Columns from `selected_from` to `selected_to` are shown inverted.
void waveform(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<float>& peaks,
              int selected_from = -1, int selected_to = -1);
// Cuts across a waveform: dotted lines, the selected one solid with tabs.
void cuts(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<int>& columns, int selected = -1);

// The eight knobs as a strip along the bottom, left to right as on the Move:
// each a bar of its value; the touched one drawn as a solid block.
void knob_strip(juce::Graphics& g, const std::array<float, 8>& values, int touched = -1);

// A touched knob, full width: its name, its value large, a bar.
void big_parameter(juce::Graphics& g, juce::Rectangle<int> area, std::string_view name, std::string_view value, float fraction);

// A list the jogwheel turns: rows of text at `scale`, the selected one
// inverted, kept in view.
void list(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<std::string>& rows, int selected, int scale);

// Two level bars, a threshold tick across both (none when it is below 0).
void meter(juce::Graphics& g, juce::Rectangle<int> area, float left, float right, float threshold);

// A sheet over the screen: a black box with a frame.
void sheet(juce::Graphics& g, juce::Rectangle<int> area);

// Eight small labelled cells, two rows of four, for a module's page.
void knob_cells(juce::Graphics& g, juce::Rectangle<int> area, const std::array<std::string, 8>& names,
                const std::array<float, 8>& values, int touched = -1);

// Eight cells, two rows of four, each a name and its value as text (a MIDI
// track's page: a channel, POLY, -12): for values a bar cannot say.
void knob_values(juce::Graphics& g, juce::Rectangle<int> area, const std::array<std::string, 8>& names,
                 const std::array<std::string, 8>& values, int touched = -1);

} // namespace ui
