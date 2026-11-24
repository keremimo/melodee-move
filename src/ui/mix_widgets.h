// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ui/widgets.h"
#include <array>
#include <string>
#include <vector>

// The mixer's and the effects' pieces (docs/UI.md, "Mixer and effects"),
// for the mockups (mockups_mixer.cpp) and the screens alike: the Device
// View's row of items when a track's chain makes it longer than four, the
// mixer's strips, a compressor's reduction, the strip's EQ and compressor
// pictures, and Pad FX's bar of punches.
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
struct Strip {
    std::string label;
    float value = 0, meter = 0;
    bool bipolar = false, muted = false, soloed = false, used = true;
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

// The mixer's EQ (Melodee's strip): nine bands, 63 Hz .. 16 kHz, each `db`
// -12..12, as a curve across `area`, a dot on each band and `touched` (the
// band a knob turned last) a ring; dotted while it is `on` no more, the
// bands kept.
void eq_curve(juce::Graphics& g, juce::Rectangle<int> area, const std::array<float, 9>& db, bool on, int touched);

// The strip's compressor: its curve in a square (in against out, the knee
// at `threshold` 0..1 of the way up, `ratio` past it) and what it took off
// of late, `taken` dB a column, hanging from the top to its right; OFF
// there while it is not `on` (switched off; amount 0 is on, a straight line).
void comp_picture(juce::Graphics& g, juce::Rectangle<int> area, float threshold, float ratio, bool on, const std::vector<float>& taken);

// A level in dB as the mixer shows it: "-4.2DB", "0.0DB", "-INF".
std::string db_text(float db);

} // namespace ui::mix
