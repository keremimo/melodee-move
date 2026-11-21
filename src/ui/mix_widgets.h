// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ui/widgets.h"
#include <array>
#include <string>
#include <vector>

// The mixer's and the effects' pieces (docs/UI.md, "Mixer and effects"),
// for the mockups (mockups_mixer.cpp) and the screens alike: the Device
// View's row of items when a track's chain makes it longer than four, the
// mixer's strips, a compressor's reduction, and Pad FX's bar of punches.
namespace ui::mix {

// One item of the Device View's row: a name of up to four characters; `pad`
// marks a kit pad's own effect (drawn with a dotted underline), `off` a
// bypassed one (struck through).
struct Item {
    std::string name;
    bool pad = false, off = false;
};
// The row along the top of the subject, three at a time, kept around the
// selected one (inverted); a notch at either end where more lie that way.
void item_row(juce::Graphics& g, const std::vector<Item>& items, int selected);

// One strip of the mixer: its label under it (a track number, a bus),
// `value` 0..1 (a level or a send, a bar up from the floor) or, `bipolar`,
// -1..1 (a pan, a bar out from the middle), `meter` 0..1 what it plays now
// (a thin bar beside it). Muted: its bar hollow; soloed: an S over it;
// unused: a dot and nothing else.
//
// Text mode (`text` not empty: the USB page's submix, "1".."8"): the text in
// a chip at the top of the strip instead of a bar, cut out of it, the chip
// hollow when muted; "-" (nothing chosen) alone, without a chip; the meter a
// thin bar under it. `value` and `bipolar` are not used.
struct Strip {
    std::string label;
    float value = 0, meter = 0;
    bool bipolar = false, muted = false, soloed = false, used = true;
    std::string text;
};
// The strips side by side across `area`; the selected one's label inverted.
void strips(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<Strip>& strips, int selected);

// A compressor's work over a bar: `reduction` a column at a time, 0..1 of
// the most it shows, hanging from the top; the beats marked along the foot,
// `now` the column the transport is on (-1: stopped).
void reduction(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<float>& reduction, int beats, int now);

// Pad FX over the bar the pattern plays: a row a punch, from the sixteenth
// it began to where it ends; `kept` punches dotted (they loop with the
// pattern), live ones solid; `now` the playhead's sixteenth (-1: stopped).
struct Lane { int from = 0, to = 0; bool kept = false; };
void punch_lanes(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<Lane>& lanes, int now);

// A level in dB as the mixer shows it: "-4.2DB", "0.0DB", "-INF".
std::string db_text(float db);

} // namespace ui::mix
