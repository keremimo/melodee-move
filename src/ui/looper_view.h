// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ui/widgets.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// How the Looper looks on the Move (docs/UI.md, "Looper"): its four loops
// L1-L4 as four rows, L1 on top, as the track buttons and the pad rows sit
// beside them, and their four strips on MIX. The mockups
// (mockups_looper.cpp) and the Looper's screen (app/screens/looper.cpp)
// both draw with these, so the screen is the mockup.
namespace ui::looper {

constexpr unsigned loops = 4;

// Each loop's colour on the lights (move/lights.h palette): mint, yellow,
// purple, sky, kept clear of the record red and the overdub orange.
struct LoopColour { uint8_t bright, dim; };
constexpr std::array<LoopColour, loops> loop_colours {{{12, 87}, {7, 77}, {20, 107}, {47, 93}}};
constexpr LoopColour loop_colour(unsigned loop) { return loop_colours[loop % loops]; }

// One loop's row: its tag (L1-L4), its state as a word, its waveform with
// the play head, its length.
struct Row {
    std::string state;          // "PLAY", "REC", "DUB", "ARM", "STOP", "FIT"; empty for an empty loop
    bool writing = false;       // recording or overdubbing: the word inverted
    std::vector<float> peaks;   // the loop's waveform across the strip, 0..1 a column
    float span = 1;             // how much of the strip is recorded yet (a set take running), 0..1
    float head = -1;            // the play head, 0..1 across the strip; < 0 none
    std::string length;         // "4B", "16B", "FREE"; "@98" for one taken at another tempo
    bool selected = false;      // the tag inverted
    bool muted = false;         // its waveform drawn every other column
};
// The four rows in `area` (the subject: 102 x 52), a row 13 pixels.
void rows(juce::Graphics& g, juce::Rectangle<int> area, const std::array<Row, loops>& rows);

// One loop track's strip on MIX.
struct Strip {
    float level = 0;  // 0..1 of the fader (unity at 100 of 127)
    float meter = 0;  // 0..1
    float pan = 0;    // -1..1
    bool mute = false, solo = false, selected = false;
};
// The four strips side by side in `area` (the subject), a column each.
void strips(juce::Graphics& g, juce::Rectangle<int> area, const std::array<Strip, loops>& strips);

// A question over the subject (the Looper off, a clear): up to three lines,
// and the detail line's PUSH YES / BACK NO.
void question(juce::Graphics& g, const std::string& first, const std::string& second = {}, const std::string& third = {});

} // namespace ui::looper
