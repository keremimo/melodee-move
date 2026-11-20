// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <functional>
#include <string>
#include <vector>

// The Rail design's screens (docs/UI.md): drawn with the UI kit at the
// screen's real 128 x 64 one bit, as PNGs four times size, and a sheet of
// each set, the design's reference: rail.png (the first scope), web.png
// (the web page's screens on the Move). A set's screens are in a file of its
// own (mockups.cpp, mockups_web.cpp), so screens for new areas go in new
// files without touching the others.
namespace ui {

int write_mockups(const std::string& directory);

namespace mockup {

using Draw = std::function<void(juce::Graphics&)>;
struct Screen { const char* name; const char* caption; Draw draw; };

// A sheet: a title, a line under it, the screens two to a row.
juce::Image sheet(const char* title, const char* subtitle, const std::vector<Screen>& screens);
juce::Image screen_image(const Draw& draw, int scale);
bool write_png(const juce::Image& image, const juce::File& file);

std::vector<Screen> rail_screens();
std::vector<Screen> web_screens();

} // namespace mockup
} // namespace ui
