// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <string>
#include <vector>

// Song (Shift + Step 13, docs/UI.md "Song") as the 128 x 64 screen shows
// it, from plain data, so the mockups (mockups_song.cpp) and the screen
// (app/screens/song.cpp) draw the same thing. In the subject: a row of
// markers, the loop brace under it, and four lanes, the tracks of the bank
// in view (the track buttons, the pads' rows), time left to right; a clip is
// a box with its pattern's name, the one under the cursor filled; the
// cursor a line down the lanes, the playhead a dotted one.
namespace ui {

struct SongViewClip {
    int lane = 0;            // 0-3, top first
    int bar = 0, bars = 1;
    std::string name;        // the pattern's (A1 .. H16)
    bool muted = false;      // drawn dotted
};
struct SongViewMarker { int bar = 0; std::string name; };
struct SongView {
    int first = 0, zoom = 16;          // the first bar in view, and how many bars across
    int cursor = 0, lane = 0;          // the cursor's bar, the selected lane (0-3)
    std::vector<SongViewClip> clips;
    std::vector<SongViewMarker> markers;
    int loop_from = 0, loop_to = 0;    // bars [from, to); equal: no loop
    std::vector<int> tempo_points;     // bars with a tempo point (ticks on the brace's row)
    float playhead = -1;               // bars from the start while the song plays; < 0 none
    bool lane_used[4] {true, true, true, true}; // an empty track's lane is a dotted line
    bool recording = false;            // the record dot on the marker row
};
constexpr int song_lanes = 4;
// Where bar `bar` falls in x, as song_view() draws it.
int song_x(const SongView& view, int bar);
void song_view(juce::Graphics& g, const SongView& view);

// A frozen track's mark (docs/UI.md, "Print and freeze"): a snowflake, 7 x 7,
// in the current colour, its top left at x, y.
void snowflake(juce::Graphics& g, int x, int y);

// A progress bar: an outline `area`, filled to `fraction`.
void progress(juce::Graphics& g, juce::Rectangle<int> area, float fraction);

} // namespace ui
