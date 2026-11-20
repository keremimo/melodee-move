// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The sequencer's pieces of the Rail design (docs/UI.md, "Sequencer"): the
// same drawing for the mockups (mockups_sequencer.cpp) and the app's
// screens (src/app/screens/seq_*.cpp), so the screens are the mockups.
namespace ui::seq {

// A knob page as cells of text: two rows of four, each a name (up to three
// characters) over its value (up to four), the touched one inverted. An
// empty name: the knob does nothing here.
void value_cells(juce::Graphics& g, juce::Rectangle<int> area, const std::array<std::string, 8>& names,
                 const std::array<std::string, 8>& values, int touched = -1);
// A page's title along the top of the subject, its place on the right ("2/9").
void title(juce::Graphics& g, const std::string& left, const std::string& right = {});

// A pattern's bars as blocks, as Loop Mode shows them: up to sixteen (two
// rows of eight), the selected one inverted, those in the loop framed, a dot
// in each bar with notes, the bar playing marked under it.
struct Bars {
    unsigned count = 4;     // bars in the pattern (up to 16 shown: the page's)
    unsigned first = 0;     // the first bar of the page (0, 16, 32 ...)
    unsigned loop_from = 0, loop_to = 4; // the loop, bars [from, to)
    int selected = 0;       // the bar the steps show
    int playing = -1;       // the bar the playhead is in
    std::array<uint8_t, 16> notes {}; // notes in each bar of the page
};
void bar_blocks(juce::Graphics& g, juce::Rectangle<int> area, const Bars& bars);

// The Patterns grid as the pads sit (Session): four tracks a row, eight
// patterns a column; each cell its pattern number, framed with notes,
// filled while it plays, dashed while it waits for its bar, a dot where
// empty; the track's tag on the left.
enum class Cell : uint8_t { Empty, Notes, Playing, Queued, Stopping };
struct GridRow {
    std::string tag;               // the track's, as the rail names it ("KIT", "OBXD"); empty: no track
    std::array<Cell, 8> cells {};
};
void pattern_grid(juce::Graphics& g, juce::Rectangle<int> area, const std::array<GridRow, 4>& rows, unsigned first_pattern,
                  int selected_row, int selected_column);

// Roll: notes at their ticks over the bars in view, low keys at the bottom;
// the note in focus inverted (a box round it), marked notes underlined, the
// playhead a line.
struct RollNote { uint32_t tick = 0, length = 24; uint8_t key = 60; bool marked = false; };
void roll(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<RollNote>& notes, uint32_t from, uint32_t span,
          int low_key, int high_key, int focus, int playhead_tick, uint32_t bar_ticks);

// A rhythm on a row of steps (Euclid, a lane): a filled box a hit, an open
// one a rest, those past the lane's end dotted.
void step_row(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<bool>& hits, unsigned shown);

// One cycle of an LFO's shape (Melodee's seven: sine, triangle, saw up, saw
// down, square, random, smooth random).
void lfo_shape(juce::Graphics& g, juce::Rectangle<int> area, unsigned shape);

// A step held: what it holds (the pad's name or its notes), the velocity
// large, its length and nudge, and Melodee's marks on it (chance, condition,
// ratchet, accent, slide, tie, locks).
struct StepCard {
    std::string what;       // "A05 BREAK", "C3 E3 G3"
    unsigned velocity = 100;
    std::string length;     // "1.00" (steps)
    std::string nudge;      // "+0.10" (steps)
    std::vector<std::string> marks; // "ACC", "1:2", "R3", "75%", "LCK2"
    bool empty = false;     // nothing on it yet
};
void step_card(juce::Graphics& g, juce::Rectangle<int> area, const StepCard& card);

// The count-in's beat, large, with the bar's beats as dots under it.
void count_in(juce::Graphics& g, juce::Rectangle<int> area, unsigned beat, unsigned beats);

} // namespace ui::seq
