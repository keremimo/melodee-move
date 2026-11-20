// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/model/pattern.h"
#include <cstdint>
#include <vector>

// The Move's step sequencing (its manual, 11 and 12) on Melodee's patterns
// (melodee-sample's model/pattern.h, notes at any tick, 96 a quarter): what
// a step, a bar and a held step's turns do to a pattern's notes. Plain
// functions over a pattern, for the sequencer (sequencer.h) and its tests;
// no Studio, no thread.
//
// A step is a stretch of the pattern's Q. A note belongs to the step its
// tick is nearest, so a note nudged by less than half a step stays on it, as
// the Move shows a nudged note on its step. `key` picks a kit's lane (the
// pad's key, 36 up); -1 every note (a note track's steps hold chords).
namespace app::seq {

struct Span {
    uint32_t from = 0, to = 0; // ticks [from, to)
    bool holds(uint32_t tick) const { return tick >= from && tick < to; }
};

// Steps of Q a page shows: sixteen, twelve for a triplet Q (the Move shows a
// triplet grid twelve to a page, Workflow's Step Grid, 13.2).
unsigned page_steps(const melodee::Pattern& p);
// Step `step` (counted from the pattern's top) as the ticks whose notes it holds.
Span step_span(const melodee::Pattern& p, unsigned step);
uint32_t step_tick(const melodee::Pattern& p, unsigned step); // where a note on it is written
// A bar of `bar_ticks` (the time signature's).
Span bar_span(unsigned bar, uint32_t bar_ticks);

// The notes in `span` (on lane `key`, or all), as indexes into p.events.
std::vector<size_t> notes_in(const melodee::Pattern& p, Span span, int key = -1);
bool any_in(const melodee::Pattern& p, Span span, int key = -1);

// Step (Play): a step with notes (on the lane) is cleared; an empty one gets
// `keys`, each at `velocity`, a step long. True when notes went on.
bool toggle_step(melodee::Pattern& p, unsigned step, int lane, const std::vector<uint8_t>& keys, uint8_t velocity);
// Step held + Pad: that key added on the step, or taken off it when it is there.
bool toggle_key(melodee::Pattern& p, unsigned step, uint8_t key, uint8_t velocity);
// Step held + Pad (Loop): the key on every step of the span, or off them all when every step has it.
bool toggle_key_every_step(melodee::Pattern& p, Span span, uint8_t key, uint8_t velocity);

// A held step's or bar's turns (11.1-11.5), on the notes in `span` (on the lane):
void add_velocity(melodee::Pattern& p, Span span, int lane, int delta);                 // the Volume knob
void add_length(melodee::Pattern& p, Span span, int lane, int ticks);                    // the wheel: a tenth of a step a detent
void length_to(melodee::Pattern& p, Span span, int lane, uint32_t end);                  // step held + a later step: up to its end
void transpose(melodee::Pattern& p, Span span, int lane, int semitones, bool kit);       // Plus / Minus (a kit: the pad's pitch)
void nudge(melodee::Pattern& p, Span span, int lane, int ticks);                         // Left / Right
// The notes' values as the screen shows them: the first's velocity, length (in steps), how far off the step.
struct Held { bool any = false; uint8_t velocity = 0; uint32_t length = 0; int offset = 0; unsigned count = 0;
              uint8_t chance = 100, condition = 0, ratchet = 1, flags = 0; int8_t ramp = 0, pitch_ramp = 0; std::vector<uint8_t> keys; };
Held held(const melodee::Pattern& p, Span span, int lane, uint32_t at); // `at`: the step's own tick

// Melodee's TRIG on a held step's notes: what each knob sets.
enum class Trig : uint8_t { Chance, Condition, Ratchet, Ramp, PitchRamp, Accent, Slide, Tie, Count };
void set_trig(melodee::Pattern& p, Span span, int lane, Trig which, int delta);
const char* trig_value(const Held& held, Trig which, char* text, size_t size); // "75%", "1:2", "ON"

// Copy (12.3, 11.8): the notes of `from` laid on `to` (its own notes there replaced), as far apart as the spans.
void copy_span(melodee::Pattern& p, Span from, Span to, int lane);
void clear_span(melodee::Pattern& p, Span span, int lane); // Delete + a bar (12.4), the lane's notes in it
// Delete + Pad: every note of the lane (true when there were any).
bool clear_lane(melodee::Pattern& p, uint8_t key);
// Shift + Step 16 (11.7, Workflow's amount): each note `percent` of the way to its step; a lane alone, or all.
void quantize(melodee::Pattern& p, int lane, unsigned percent);

// Parameter locks (14.2.4): a lane (an engine parameter, a kit pad's value,
// the mix's) held at `value` on the step's notes, for as long as its longest
// note (a step at least); and read back (false: none there).
void lock(melodee::Pattern& p, Span span, int lane, unsigned param, unsigned pad, int value);
bool lock_of(const melodee::Pattern& p, Span span, unsigned param, unsigned pad, int& value);
unsigned locks_in(const melodee::Pattern& p, Span span); // locks starting on the step, of any lane

// Seq Record: where a hit heard at `position` (ticks into the pattern, as
// played) lands, with Rec Quantize on the pattern's Q (the last step's
// later half onto the top).
uint32_t record_tick(const melodee::Pattern& p, int position, bool quantize);

// Loop Mode: the pattern's length in bars of `bar_ticks`, and set (whole
// bars, or sixteenths with `fine`); notes past the end stay (spare bars).
unsigned bars_of(const melodee::Pattern& p, uint32_t bar_ticks);
void set_length(melodee::Pattern& p, uint32_t ticks, uint32_t most);

} // namespace app::seq
