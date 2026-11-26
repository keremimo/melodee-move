// SPDX-License-Identifier: AGPL-3.0-only
// The sequencer (docs/UI.md, "Sequencer"; docs/PARITY.md, Sequencing): its
// pattern edits on their own (src/app/seq_edit.*), then each of its keys
// through the app as a hand uses them (docs/MOVE_CONTROLS.md), against the
// engine host playing what they wrote.
#include "app/melodee_app.h"
#include "app/screen.h"
#include "app/seq_edit.h"
#include "app/sequencer.h"
#include "melodee/model/scene_links.h"
#include "move/audio.h"
#include "move/controls.h"
#include "move/tools.h"
#include "ui/track_style.h"
#include "melodee/engine/arp.h"
#include "melodee/engine/engine_info.h"
#include "melodee/model/generators.h"
#include "melodee/model/pad_params.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

using namespace move;
namespace seq = app::seq;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)
#define CHECK_SAID(condition, said) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition ": " << said << "\n"; } } while (0)

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

melodee::Event note(uint32_t tick, uint8_t key, uint8_t velocity = 100, uint32_t length = 240) {
    melodee::Event e;
    e.tick = tick;
    e.key = key;
    e.velocity = velocity;
    e.length = length;
    return e;
}

// ---------------------------------------------------------------- the edits alone

void test_edits() {
    melodee::Pattern p; // a bar of sixteenths
    CHECK(seq::page_steps(p) == 16);
    // A step tapped: the lane's note on it; again, off.
    CHECK(seq::toggle_step(p, 3, 36, {36}, 100));
    CHECK(p.events.size() == 1 && p.events[0].tick == 720 && p.events[0].key == 36 && p.events[0].length == 240);
    CHECK(!seq::toggle_step(p, 3, 36, {36}, 100) && p.events.empty());
    // A kit's steps are one lane: another pad's note on the step leaves it be.
    p.add(note(720, 38));
    CHECK(seq::toggle_step(p, 3, 36, {36}, 90) && p.events.size() == 2);
    CHECK(!seq::toggle_step(p, 3, 36, {36}, 90) && p.events.size() == 1 && p.events[0].key == 38);
    // A note track's step holds a chord: a tap clears it all, another lays the keys held.
    CHECK(!seq::toggle_step(p, 3, -1, {60}, 100) && p.events.empty());
    CHECK(seq::toggle_step(p, 5, -1, {60, 64, 67}, 100) && p.events.size() == 3);
    // Step held + Pad: a key in or out of the chord, at the step's length.
    seq::add_length(p, seq::step_span(p, 5), -1, 240);
    CHECK(!seq::toggle_key(p, 5, 64, 100) && p.events.size() == 2);
    CHECK(seq::toggle_key(p, 5, 62, 100) && p.events.size() == 3 && p.events[1].key == 62 && p.events[1].length == 480);
    p.events.clear();
    // A held step's turns (11.1-11.4).
    seq::toggle_step(p, 2, 36, {36}, 100);
    auto span = seq::step_span(p, 2);
    seq::add_velocity(p, span, 36, 30);
    CHECK(p.events[0].velocity == 127);
    seq::add_velocity(p, span, 36, -200);
    CHECK(p.events[0].velocity == 1);
    seq::add_length(p, span, 36, 20);
    CHECK(p.events[0].length == 260);
    seq::length_to(p, span, 36, seq::step_tick(p, 6) + 240);
    CHECK(p.events[0].length == 7 * 240 - 480);
    seq::transpose(p, span, 36, 1, true);
    CHECK(p.events[0].pitch == 1 && p.events[0].key == 36); // a kit pad's pitch, not another pad
    seq::nudge(p, span, 36, 50);
    CHECK(p.events[0].tick == 530);
    // Nudged less than half a step, it is still the step's.
    auto held = seq::held(p, seq::step_span(p, 2), 36, seq::step_tick(p, 2));
    CHECK(held.any && held.offset == 50 && held.count == 1);
    seq::nudge(p, seq::step_span(p, 2), 36, 100); // 150 ticks on: the next step's now
    CHECK(!seq::any_in(p, seq::step_span(p, 2), 36) && seq::any_in(p, seq::step_span(p, 3), 36));
    // A note track transposes the key.
    melodee::Pattern line;
    line.add(note(0, 60));
    seq::transpose(line, seq::step_span(line, 0), -1, 12, false);
    CHECK(line.events[0].key == 72);
    // Melodee's TRIG on a step.
    melodee::Pattern t;
    t.add(note(480, 36));
    auto two = seq::step_span(t, 2);
    seq::set_trig(t, two, 36, seq::Trig::Chance, -25);
    seq::set_trig(t, two, 36, seq::Trig::Condition, 1);
    seq::set_trig(t, two, 36, seq::Trig::Ratchet, 2);
    seq::set_trig(t, two, 36, seq::Trig::Ramp, -4);
    seq::set_trig(t, two, 36, seq::Trig::PitchRamp, 5);
    seq::set_trig(t, two, 36, seq::Trig::Accent, 1);
    seq::set_trig(t, two, 36, seq::Trig::Tie, 1);
    seq::set_trig(t, two, 36, seq::Trig::Tie, -1);
    const auto& e = t.events[0];
    CHECK(e.chance == 75 && e.condition == 1 && e.ratchet == 3 && e.ramp == -20 && e.pitch_ramp == 5);
    CHECK((e.flags & melodee::step_accent) && !(e.flags & melodee::step_tie));
    char text[16];
    auto h = seq::held(t, two, 36, 480);
    CHECK(std::string(seq::trig_value(h, seq::Trig::Condition, text, sizeof text)) == "1:2");
    CHECK(std::string(seq::trig_value(h, seq::Trig::Chance, text, sizeof text)) == "75%");
    // Copy a step and a bar; Delete + a bar; Delete + a pad.
    melodee::Pattern c;
    c.length = 2 * 3840;
    c.add(note(0, 36));
    c.add(note(960, 36));
    c.add(note(960, 38));
    seq::copy_span(c, seq::bar_span(0, 3840), seq::bar_span(1, 3840), -1);
    CHECK(c.events.size() == 6 && seq::notes_in(c, seq::bar_span(1, 3840)).size() == 3);
    seq::clear_span(c, seq::bar_span(1, 3840), 38);
    CHECK(c.events.size() == 5);
    CHECK(seq::clear_lane(c, 36) && c.events.size() == 1 && !seq::clear_lane(c, 36));
    // Every step of a bar (Loop Mode's step held + pad), and off again.
    melodee::Pattern b;
    CHECK(seq::toggle_key_every_step(b, seq::bar_span(0, 3840), 42, 90) && b.events.size() == 16);
    CHECK(!seq::toggle_key_every_step(b, seq::bar_span(0, 3840), 42, 90) && b.events.empty());
    // Quantize (Shift + Step 16): Workflow's amount of the way, the lane alone or all.
    melodee::Pattern qz;
    qz.add(note(300, 36));
    qz.add(note(300, 38));
    seq::quantize(qz, 36, 100);
    CHECK(qz.events[0].tick == 240 && qz.events[1].tick == 300);
    seq::quantize(qz, -1, 50);
    CHECK(qz.events[1].tick == 270);
    // Locks (14.2.4): held for the step's longest note, read back, one a lane a step.
    melodee::Pattern l;
    l.add(note(480, 36, 100, 600));
    unsigned cutoff = melodee::motion_pad_param(melodee::PadParam::FilterCutoff);
    seq::lock(l, seq::step_span(l, 2), 36, cutoff, 0, 40);
    int value = 0;
    CHECK(seq::lock_of(l, seq::step_span(l, 2), cutoff, 0, value) && value == 40);
    CHECK(l.motion.size() == 1 && l.motion[0].hold == 600 && l.motion[0].tick == 480);
    seq::lock(l, seq::step_span(l, 2), 36, cutoff, 0, 41);
    CHECK(l.motion.size() == 1 && seq::locks_in(l, seq::step_span(l, 2)) == 1);
    CHECK(!seq::lock_of(l, seq::step_span(l, 3), cutoff, 0, value));
    // Seq Record's place: wrapped, and onto Q (the last step's later half onto the top).
    melodee::Pattern r;
    CHECK(seq::record_tick(r, 500, false) == 500 && seq::record_tick(r, 500, true) == 480);
    CHECK(seq::record_tick(r, 3800, true) == 0 && seq::record_tick(r, 3840 + 100, false) == 100);
    // Lengths in bars; a triplet Q pages twelve.
    melodee::Pattern n;
    seq::set_length(n, 3 * 3840, 128 * 3840);
    CHECK(seq::bars_of(n, 3840) == 3 && !n.free);
    seq::set_length(n, 999 * 3840, 128 * 3840);
    CHECK(seq::bars_of(n, 3840) == 128);
    n.division = melodee::Division::SixteenthTriplet;
    CHECK(seq::page_steps(n) == 12);
}

// ---------------------------------------------------------------- the keys through the app

struct Hands {
    app::MelodeeApp& app;
    AudioBlock in {}, out {};
    void press(Button b) { app.surface(make_event(0xb0, button_cc(b), 127)); }
    void release(Button b) { app.surface(make_event(0xb0, button_cc(b), 0)); }
    void button(Button b) { press(b); release(b); }
    void step_down(unsigned s) { app.surface(make_event(0x90, step_note(s), 127)); }
    void step_up(unsigned s) { app.surface(make_event(0x80, step_note(s), 0)); }
    void step(unsigned s) { step_down(s); step_up(s); }
    void pad_down(unsigned p, uint8_t velocity = 100) {
        auto event = make_event(0x90, pad_note(p), velocity);
        app.audio_event(event); // as the device host has it: the audio thread first
        app.surface(event);
    }
    void pad_up(unsigned p) {
        auto event = make_event(0x80, pad_note(p), 0);
        app.audio_event(event);
        app.surface(event);
    }
    void pad(unsigned p, uint8_t velocity = 100) { pad_down(p, velocity); pad_up(p); }
    void turn(uint8_t cc, int detents) { app.surface(make_event(0xb0, cc, uint8_t(detents < 0 ? 128 + detents : detents))); }
    void wheel(int detents) { turn(main_knob_cc, detents); }
    void knob(unsigned k, int detents) { turn(uint8_t(knob_first_cc + k), detents); }
    void volume(int detents) { turn(master_knob_cc, detents); }
    void touch(unsigned k, bool on) { app.surface(make_event(on ? 0x90 : 0x80, uint8_t(k), on ? 127 : 0)); }
    template <class Do> void with(Button b, Do&& what) { press(b); what(); release(b); }
    void render(int blocks) { for (int n = 0; n < blocks; ++n) { out.fill(0); app.render(in, out); } }
    void lights() { move::Lights l; app.lights(l, std::chrono::steady_clock::now()); }
    std::string status() { lights(); return app.status(); }
};

melodee::Pattern& current(app::MelodeeApp& melodee) { return melodee.studio().sequencer().pattern(); }

// The lights as the app leaves them: each note light's value (pads, steps), by note.
std::array<int, 128> lit_notes(app::MelodeeApp& melodee) {
    move::Lights l;
    melodee.lights(l, std::chrono::steady_clock::now());
    std::array<int, 128> notes;
    notes.fill(-1);
    l.update([&](const uint8_t* m) { if ((m[0] & 0xf0) == 0x90) notes[m[1]] = m[2]; return true; }, 1000);
    return notes;
}

void test_steps(app::MelodeeApp& melodee) {
    Hands h {melodee};
    auto& p = current(melodee);
    // Step (Play): A01's note on the step as it is pressed, kept as it is let go; a tap again clears it.
    h.step_down(3);
    CHECK(p.events.size() == 1 && p.events[0].tick == 720 && p.events[0].key == melodee::sampler_first_note);
    CHECK(has(h.status(), "job=ToggleStep"));
    h.step_up(3);
    CHECK(p.events.size() == 1);
    {   // Steps (9.5): white with a note, the track's colour dim where empty.
        auto notes = lit_notes(melodee);
        CHECK(notes[step_note(3)] == colour::white && notes[step_note(2)] == ui::track_colour(0).dim);
    }
    h.step(3);
    CHECK(p.events.empty());
    // The steps follow the last pad: A02's lane.
    h.with(Button::Shift, [&] { h.pad(1); });
    h.step(0);
    CHECK(p.events.size() == 1 && p.events[0].key == melodee::sampler_first_note + 1);
    // Step held + Volume knob: the velocity; + wheel: the length; + Plus: a semitone; long Plus an octave.
    h.step_down(0);
    h.volume(-20);
    CHECK(p.events[0].velocity == 80 && has(h.status(), "job=StepVelocity") && has(h.status(), "held_step=0"));
    h.wheel(5);
    CHECK(p.events[0].length == 240 + 5 * 24 && has(h.status(), "job=StepLength"));
    h.button(Button::Up);
    CHECK(p.events[0].pitch == 1 && has(h.status(), "job=TransposeStep"));
    h.button(Button::Down);
    CHECK(p.events[0].pitch == 0);
    // Left / Right nudge a tenth (24 ticks of 240); Shift: a 96th of a quarter (10 ticks).
    h.button(Button::Right);
    CHECK(p.events[0].tick == 24 && has(h.status(), "job=NudgeStep"));
    h.with(Button::Shift, [&] { h.button(Button::Left); });
    CHECK(p.events[0].tick == 14 && has(h.status(), "job=NudgeStepFine"));
    // Step held + a later step: the length up to it.
    h.step(3);
    CHECK(p.events[0].length == 4 * 240 - 14 && has(h.status(), "job=StepLengthTo"));
    // Step held + Pad: that pad's note added on the step (quiet: the pad's job is the step).
    h.pad(4);
    CHECK(p.events.size() == 2 && has(h.status(), "job=NoteOnStep"));
    h.pad(4);
    CHECK(p.events.size() == 1);
    // Step held + a knob: a lock of that knob's lane on the step (kit-play's CUTOFF, knob 8).
    h.knob(7, -10);
    unsigned cutoff = melodee::motion_pad_param(melodee::PadParam::FilterCutoff);
    int value = 0;
    CHECK_SAID(seq::lock_of(p, seq::step_span(p, 0), cutoff, 1, value) && value == 90 && has(h.status(), "job=StepMotion"),
               value << " motion=" << p.motion.size() << " " << h.status());
    // Step held + push: Melodee's TRIG on the knobs; knob 1 the chance, 3 the ratchet.
    h.button(Button::MainPush);
    CHECK(melodee.studio().sequencer().trig_page && has(h.status(), "job=StepTrig"));
    h.knob(0, -10);
    h.knob(2, 1);
    CHECK_SAID(p.events[0].chance == 90 && p.events[0].ratchet == 2, int(p.events[0].chance) << " " << int(p.events[0].ratchet) << " " << p.events.size() << " " << h.status());
    h.step_up(0);
    CHECK(!melodee.studio().sequencer().trig_page && has(h.status(), "held_step=-1"));
    // Delete + Knob touch: the lane's motion cleared; Mute + touch: off and on again.
    h.press(Button::Mute);
    h.touch(7, true);
    h.touch(7, false);
    h.release(Button::Mute);
    CHECK(melodee.studio().sequencer().lane_off(0, 0, cutoff, 1) && has(h.status(), "job=ToggleMotion"));
    h.with(Button::Mute, [&] { h.touch(7, true); h.touch(7, false); });
    CHECK(!melodee.studio().sequencer().lane_off(0, 0, cutoff, 1));
    h.with(Button::Delete, [&] { h.touch(7, true); h.touch(7, false); });
    CHECK(p.motion.empty() && has(h.status(), "job=DeleteMotion"));
    // Copy held + a step, then another: the step pasted there.
    h.with(Button::Copy, [&] { h.step(0); h.step(8); });
    CHECK(p.events.size() == 2 && p.events[1].tick == 8 * 240 + 14 && has(h.status(), "job=CopyStep"));
    // Shift + Step 16 held + a pad: that pad's notes quantized; Shift + Step 16 alone: all.
    h.with(Button::Shift, [&] { h.step_down(15); h.pad(1); h.step_up(15); });
    CHECK(p.events[0].tick == 0 && p.events[1].tick == 8 * 240 && has(h.status(), "job=QuantizePad"));
    p.events[0].tick = 50;
    h.with(Button::Shift, [&] { h.step(15); });
    CHECK(p.events[0].tick == 0 && has(h.status(), "job=Quantize"));
    // Delete + Pad: its notes go; again, with none, the pad is emptied.
    h.with(Button::Delete, [&] { h.pad(1); });
    CHECK(p.events.empty() && has(h.status(), "job=ClearPad"));
}

// Step locks (a tester, 2026-11-24): an empty step held gets its note as it is pressed, so a knob turned locks it,
// and its let-go keeps both; held past a tap, a step keeps its notes. 16 Pitches: a step writes the last pitch
// played, a step held + a pitch pad puts that pitch beside it (again, off); Shift + Plus moves the kit's 16 a bank.
void test_locks_and_pitches(app::MelodeeApp& melodee) {
    Hands h {melodee};
    auto& p = current(melodee);
    auto& studio = melodee.studio();
    CHECK(p.events.empty());
    h.step_down(2);
    CHECK(p.events.size() == 1);
    h.knob(7, -10);
    h.step_up(2);
    CHECK_SAID(p.events.size() == 1 && seq::locks_in(p, seq::step_span(p, 2)) == 1, p.events.size() << " " << h.status());
    h.step_down(2);
    h.knob(7, -5);
    h.step_up(2);
    CHECK(p.events.size() == 1);
    h.step(2);
    CHECK(p.events.empty() && has(h.status(), "job=ClearStep"));
    p.motion.clear();
    // 16 Pitches (Shift + Step 8): pad 5 (Rows: the right 4x4's second) a pitch of the last pad.
    h.with(Button::Shift, [&] { h.step(7); });
    CHECK(studio.pitches());
    h.pad(5);
    int key = studio.stamp(5).key.load();
    int up = int(int8_t(key >> 8));
    CHECK_SAID(key >= 0 && up != 0, key);
    h.step(4);
    CHECK_SAID(p.events.size() == 1 && p.events[0].key == (key & 0xff) && p.events[0].pitch == up, p.events.size() << " " << h.status());
    h.step_down(4);
    h.pad(6);
    int higher = int(int8_t(studio.stamp(6).key.load() >> 8));
    CHECK(higher != up && p.events.size() == 2 && std::any_of(p.events.begin(), p.events.end(), [&](const melodee::Event& e) { return e.pitch == higher; }));
    h.pad(6);
    CHECK(p.events.size() == 1 && p.events[0].pitch == up);
    h.step_up(4);
    CHECK(p.events.size() == 1);
    // A kit pad (the left 16) and a step: the pad itself, no pitch.
    h.pad(0);
    h.step(6);
    CHECK(p.events.size() == 2 && p.events[1].pitch == 0);
    unsigned first = studio.pad_first();
    h.with(Button::Shift, [&] { h.button(Button::Up); });
    CHECK_SAID(studio.pad_first() == first + 16, studio.pad_first());
    h.with(Button::Shift, [&] { h.button(Button::Down); });
    CHECK(studio.pad_first() == first);
    h.with(Button::Shift, [&] { h.step(7); });
    CHECK(!studio.pitches());
    h.with(Button::Delete, [&] { h.pad(0); h.pad(1); }); // A01's and A02's notes
    CHECK(p.events.empty());
    h.with(Button::Shift, [&] { h.pad(1); }); // A02 the last pad again
}

void test_bars_and_patterns(app::MelodeeApp& melodee) {
    Hands h {melodee};
    auto& seqr = melodee.studio().sequencer();
    auto& p = current(melodee);
    p = melodee::Pattern {};
    // Loop held + wheel: the length in bars; Shift: in sixteenths.
    h.press(Button::Loop);
    h.wheel(3);
    h.release(Button::Loop);
    CHECK(seq::bars_of(p, 3840) == 4 && has(h.status(), "job=LoopLength") && has(h.status(), "seq_bars=4"));
    h.with(Button::Shift, [&] { h.with(Button::Loop, [&] { h.wheel(-1); }); });
    CHECK(p.length == 4 * 3840 - 240 && has(h.status(), "job=LoopLengthFine"));
    // Left / Right: the pattern's bars, a page of sixteen steps each.
    h.button(Button::Right);
    CHECK(seqr.page() == 1 && has(h.status(), "job=Bar"));
    h.step(2);
    CHECK(p.events.size() == 1 && p.events[0].tick == 3840 + 480);
    h.with(Button::Shift, [&] { h.button(Button::Left); });
    CHECK(seqr.page() == 0 && has(h.status(), "job=Bar16"));
    // A step past the end: the pattern grows to that bar (Melodee's spare steps).
    seq::set_length(p, 3840, 128 * 3840);
    h.button(Button::Right);
    h.step(0);
    CHECK(seq::bars_of(p, 3840) == 2);
    h.button(Button::Left);
    // Shift + Step 15: twice as long, the notes twice.
    size_t before = p.events.size();
    h.with(Button::Shift, [&] { h.step(14); });
    CHECK(seq::bars_of(p, 3840) == 4 && p.events.size() == 2 * before && has(h.status(), "job=DoubleLoop"));
    // Copy: the pattern into the next empty one, which plays (12.3).
    h.button(Button::Copy);
    CHECK(seqr.number(0) == 1 && seqr.pattern(0, 1).events.size() == 2 * before && has(h.status(), "job=DuplicatePattern"));
    // Delete: the pattern playing cleared (12.4).
    h.button(Button::Delete);
    CHECK(seqr.pattern(0, 1).events.empty() && seqr.pattern(0, 0).events.size() == 2 * before && has(h.status(), "job=DeleteCurrentPattern"));
    // Shift + Step 14: the next empty pattern (1.4.0): pattern 2 is empty again, so it stays there.
    h.with(Button::Shift, [&] { h.step(13); });
    CHECK(has(h.status(), "job=NewPattern") && seqr.pattern(0, seqr.number(0)).empty());
    seqr.launch(0, 0);
}

void test_record(app::MelodeeApp& melodee) {
    Hands h {melodee};
    auto& seqr = melodee.studio().sequencer();
    seqr.launch(0, 3);
    auto& p = seqr.pattern(0, 3);
    p = melodee::Pattern {}; // empty and free
    seqr.workflow.count_in = true;
    // Rec, stopped: a bar's count-in, Rec flashing, then the take.
    h.button(Button::Rec);
    h.render(4);
    CHECK(has(h.status(), "rec=1 count_in=1") && has(h.status(), "playing=1"));
    h.render(700); // a bar at 120 BPM: 3840 ticks, 0.52 ms each, 2.9 ms a block
    CHECK(has(h.status(), "count_in=0"));
    // A free pattern grows as the take goes: the bar playing and the next.
    CHECK(p.length >= 2 * 3840);
    h.render(60);
    h.pad(2, 90);
    CHECK_SAID(p.events.size() == 1, h.status());
    if (!p.events.empty()) {
        CHECK(p.events[0].key == melodee::sampler_first_note + 2 && p.events[0].velocity == 90);
        CHECK(p.events[0].tick % 240 == 0); // Rec Quantize: on the pattern's Q
    }
    h.render(800);
    h.lights();
    CHECK(p.length >= 3 * 3840);
    // Rec again: the take ends at the end of the bar playing; playback goes on.
    h.button(Button::Rec);
    h.render(2);
    CHECK(has(h.status(), "rec=0") && has(h.status(), "playing=1") && !p.free && p.length % 3840 == 0);
    h.button(Button::Play);
    h.render(2);
    // Rec while playing records at once, no count-in, and overdubs.
    h.button(Button::Play);
    h.render(20);
    h.button(Button::Rec);
    h.render(2);
    CHECK(has(h.status(), "rec=1 count_in=0"));
    h.render(30);
    h.pad(5);
    CHECK(p.events.size() == 2);
    // Another track ends the take, playback going on (14.1); Capture starts afresh (14.3).
    h.button(Button::Row2);
    h.render(2);
    CHECK(has(h.status(), "rec=0") && has(h.status(), "playing=1") && has(h.status(), "capture=0"));
    h.button(Button::Row1);
    h.button(Button::Rec);
    h.render(2);
    // Stop ends the take with the transport (14.1).
    h.button(Button::Play);
    h.render(2);
    CHECK(has(h.status(), "rec=0") && has(h.status(), "playing=0"));
    seqr.launch(0, 0);
}

void test_capture(app::MelodeeApp& melodee) {
    Hands h {melodee};
    auto& seqr = melodee.studio().sequencer();
    seqr.clear_capture();
    // Played stopped: Capture keeps it in the next empty pattern, offers three tempos, starts the transport.
    h.status();
    for (int n = 0; n < 4; ++n) { // a phrase of a second and a half, in real time (Capture's clock is the wall's)
        h.pad(n % 2);
        if (n < 3) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    CHECK(seqr.can_capture() && has(h.status(), "capture=4"));
    unsigned before = seqr.number(0);
    h.button(Button::Capture);
    h.render(2);
    CHECK(has(h.status(), "job=Recall") && has(h.status(), "playing=1"));
    CHECK(seqr.number(0) != before && !seqr.pattern(0, seqr.number(0)).events.empty());
    CHECK_SAID(!seqr.guesses.empty() && has(h.status(), "page=capture"), seqr.guesses.size() << " " << h.status());
    CHECK(!seqr.can_capture());
    h.button(Button::Back);
    // Shift + Capture clears what it would keep.
    h.pad(3);
    CHECK(seqr.can_capture());
    h.with(Button::Shift, [&] { h.button(Button::Capture); });
    CHECK(!seqr.can_capture() && has(h.status(), "job=ClearRecall"));
    h.button(Button::Play);
    h.render(2);
    seqr.launch(0, 0);
}

void test_fill_and_repeat(app::MelodeeApp& melodee) {
    Hands h {melodee};
    auto& seqr = melodee.studio().sequencer();
    // Loop held while playing: Fill until it is let go.
    h.button(Button::Play);
    h.render(4);
    h.press(Button::Loop);
    h.lights();
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    h.lights(); // the hold comes due
    h.render(2);
    CHECK(seqr.filling() && melodee.studio().host().filling());
    h.release(Button::Loop);
    h.render(2);
    CHECK(!seqr.filling() && !melodee.studio().host().filling());
    h.button(Button::Play);
    h.render(2);
    // Note Repeat on the selected track: the pads' keys go to the repeat.
    seqr.set_repeat(0, true, 0, 4);
    CHECK(melodee.studio().current().repeat);
    seqr.set_repeat(0, false, 0, 4);
    CHECK(!melodee.studio().current().repeat);
    // The arp's styles (1-5: Melodee's ArpModes), on the same menu.
    seqr.set_repeat(0, true, 4, 6);
    const auto& t = melodee.studio().current();
    CHECK(t.arp && !t.repeat && t.arp_mode == uint8_t(melodee::ArpMode::Random) && t.arp_rate == 6);
    seqr.set_repeat(0, false, 4, 6);
    CHECK(!t.arp);
}

void test_pages(app::MelodeeApp& melodee) {
    // Melodee's own sequencer jobs, as the SEQ pages call them.
    Hands h {melodee};
    auto& seqr = melodee.studio().sequencer();
    auto& project = melodee.studio().project();
    seqr.launch(0, 10);
    auto& p = seqr.pattern(0, 10);
    p = melodee::Pattern {};
    seq::set_length(p, 2 * 3840, 128 * 3840);
    p.add(note(0, 36));
    p.add(note(3840 + 960, 36));
    // Half Seq: the first half; Half and Double Speed: the notes further apart, or closer.
    seqr.half_seq();
    CHECK(p.length == 3840 && seq::bars_of(p, 3840) == 1);
    p.add(note(480, 37));
    seqr.half_speed(true);
    CHECK(p.events[1].tick == 960);
    seqr.half_speed(false);
    CHECK(p.events[1].tick == 480);
    // Q and the time signature.
    seqr.set_division(1);
    CHECK(p.division == melodee::Division::SixteenthTriplet && seqr.page_steps() == 12);
    seqr.set_division(-1);
    unsigned signature = project.signature;
    seqr.set_signature(-1);
    CHECK(project.signature == signature - 1 && project.bar_ticks() == 3 * 960);
    seqr.set_signature(1);
    // The track's speed: x1 to x2, its patterns sent again.
    seqr.set_speed(2);
    CHECK(project.tracks[0].speed == melodee::speed_count - 1);
    seqr.set_speed(-2);
    CHECK(project.tracks[0].speed == melodee::speed_normal);
    // BPM per pattern: this pattern number's own tempo, on when BPM per pattern is.
    unsigned base = project.tempo;
    seqr.set_pattern_tempo(-20);
    CHECK(project.pattern_tempo[10] == base - 200);
    seqr.set_seq_tempo(true);
    CHECK(project.seq_tempo && project.tempo_of(10) == base - 200);
    seqr.set_seq_tempo(false);
    seqr.set_pattern_tempo(-200);
    CHECK(project.pattern_tempo[10] == 0);
    // Variations: a busier copy in the next empty pattern, playing.
    int varied = seqr.vary(unsigned(melodee::Variation::Reversed), 0.5);
    CHECK(varied == 11 && seqr.number(0) == 11 && !seqr.pattern(0, 11).events.empty());
    seqr.launch(0, 10);
    // Feel on the others; a groove taken from another pattern.
    p.feel.strength = 50;
    seqr.feel_all();
    CHECK(seqr.pattern(0, 11).feel.strength == 50);
    seqr.set_groove_from(1);
    CHECK(p.feel.groove_from != 0);
    seqr.set_groove_from(-5);
    CHECK(p.feel.groove_from == 0);
    // Follow actions on the column.
    p.follow.action = uint8_t(melodee::FollowAction::Next);
    seqr.pattern(1, 10).add(note(0, 60));
    seqr.follow_column();
    CHECK(seqr.pattern(1, 10).follow.action == uint8_t(melodee::FollowAction::Next));
    // Euclid on the last pad's lane, its own length.
    melodee.studio().select_pad(2);
    seqr.lay_euclid(3, 8, 0);
    CHECK(seq::notes_in(p, {0, p.length}, 38).size() == 3 && p.lane_length(38) == 8 * 240); // looping on its own
    seqr.set_lane_steps(-2);
    CHECK(p.lane_length(38) == 6 * 240);
    seqr.set_lane_steps(100);
    CHECK(!p.own_lane(38));
    // Macros: one knob over lanes; MOD: a modulator to the engine.
    auto lanes = seqr.lanes_of(0);
    CHECK(lanes.size() == 9);
    auto& macro = project.macros[0];
    macro.targets[0] = {0, uint8_t(lanes[1].param), uint8_t(lanes[1].pad), 0, 127}; // the last pad's level
    seqr.turn_macro(0, 64);
    CHECK(macro.value == 64 && melodee.studio().current().pads[2].level == 64);
    project.modulators[0].kind = melodee::ModKind::Lfo;
    project.modulators[0].targets[0] = {0, uint8_t(melodee::motion_level), 0, 50, 0, 127};
    seqr.send_modulator(0);
    h.render(4);
    // Step entry: pads written at the caret, which moves on when they are let go; rest and erase.
    seqr.entry = true;
    seqr.caret = 4;
    h.pad(3);
    CHECK(seq::any_in(p, seq::step_span(p, 4), 39) && seqr.caret == 5);
    seqr.entry_rest();
    CHECK(seqr.caret == 6);
    seqr.entry_erase();
    CHECK(seqr.caret == 5);
    seqr.caret = 5;
    seqr.entry_erase();
    CHECK(seqr.caret == 4 && !seq::any_in(p, seq::step_span(p, 4), 39));
    seqr.entry = false;
    // The playhead by Q, stopped: Play continues from it.
    seqr.move_playhead(4);
    CHECK(seqr.play_from_shown() == 960);
    h.button(Button::Play);
    h.render(2);
    CHECK(melodee.studio().host().position_of(0) >= 960);
    h.button(Button::Play);
    h.render(2);
    CHECK(seqr.play_from_shown() == 0);
    seqr.launch(0, 0);
}

void test_screens(app::MelodeeApp& melodee) {
    Hands h {melodee};
    auto& seqr = melodee.studio().sequencer();
    melodee.studio().select_pad(0); // the steps on A01's lane
    seqr.launch(0, 20);
    auto& p = seqr.pattern(0, 20);
    p = melodee::Pattern {};
    seq::set_length(p, 4 * 3840, 128 * 3840);
    for (unsigned bar = 0; bar < 4; ++bar) p.add(note(bar * 3840, 36));
    // A held step: long Plus an octave, long Right a whole step.
    h.step_down(0);
    h.press(Button::Up);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    h.lights();
    h.release(Button::Up);
    CHECK(p.events[0].pitch == 12 && has(h.status(), "job=TransposeStepOctave"));
    h.press(Button::Right);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    h.lights();
    h.release(Button::Right);
    CHECK(p.events[0].tick == 240 && has(h.status(), "job=NudgeStepWhole"));
    h.step_up(0);
    p.events[0].tick = 0;
    p.events[0].pitch = 0;
    // Loop: Loop Mode, the bars on the steps.
    h.button(Button::Loop);
    CHECK(has(h.status(), "page=loop") && has(h.status(), "job=LoopMode"));
    {
        auto notes = lit_notes(melodee);
        CHECK(notes[step_note(0)] > 0 && notes[step_note(3)] > 0 && notes[step_note(4)] <= 0); // four bars; the fifth dark
    }
    h.step(2);
    CHECK(has(h.status(), "loop_bar=3") && has(h.status(), "job=SelectBar") && seqr.page() == 2);
    // A bar held: its notes' velocity, length, transpose, nudge; a pad on every step.
    h.step_down(1);
    h.volume(-10);
    CHECK(p.events[1].velocity == 90 && has(h.status(), "job=BarVelocity"));
    h.wheel(1);
    CHECK(p.events[1].length == 264 && has(h.status(), "job=BarLength"));
    h.button(Button::Up);
    CHECK(p.events[1].pitch == 1 && has(h.status(), "job=TransposeBar"));
    h.button(Button::Right);
    CHECK(p.events[1].tick == 3840 + 24 && has(h.status(), "job=NudgeBar"));
    h.pad(5);
    CHECK(seq::notes_in(p, seq::bar_span(1, 3840), 41).size() == 16 && has(h.status(), "job=NoteOnBar"));
    // A bar held + another: the loop from one to the other (from bar 2: an editor's loop).
    h.step(2);
    h.step_up(1);
    CHECK(seqr.loop_from(0, 20) == (1u | 3u << 16) && has(h.status(), "job=LoopRange"));
    // A double press: a one-bar loop, the first bar's: the pattern one bar long.
    h.step(0);
    h.step(0); // its release taps too: bar 1 selected
    CHECK(seqr.loop_from(0, 20) == 0 && p.length == 3840);
    seq::set_length(p, 4 * 3840, 128 * 3840);
    // Copy + a bar, then another: copied; Delete + a bar: cleared.
    h.with(Button::Copy, [&] { h.step(1); h.step(3); });
    CHECK(seq::notes_in(p, seq::bar_span(3, 3840), 41).size() == 16 && has(h.status(), "job=CopyBar"));
    h.with(Button::Delete, [&] { h.step(3); });
    CHECK(seq::notes_in(p, seq::bar_span(3, 3840)).empty() && has(h.status(), "job=ClearBar"));
    // Loop held + the wheel: the length here too (shorter as on the Play screen), and Loop Mode stays.
    h.with(Button::Loop, [&] { h.wheel(-1); });
    CHECK(p.length == 3 * 3840 && has(h.status(), "job=LoopLength") && has(h.status(), "page=loop"));
    h.with(Button::Loop, [&] { h.wheel(1); });
    CHECK(p.length == 4 * 3840 && has(h.status(), "page=loop"));
    // Left / Right the bar; Loop again, out.
    h.button(Button::Right);
    CHECK(has(h.status(), "loop_bar=2") && has(h.status(), "job=Bar"));
    h.button(Button::Loop);
    CHECK(has(h.status(), "page=kit-play"));
    // Note/Session: the Patterns grid; a pad launches its track's pattern.
    h.button(Button::Menu);
    CHECK(has(h.status(), "page=session") && has(h.status(), "job=ToggleSession"));
    h.pad(24 + 2); // the top row (track 1), the third column: pattern 3 of the eight from 17
    CHECK(seqr.number(0) == 18 && has(h.status(), "job=LaunchPattern"));
    // Left / Right eight more patterns; Plus / Minus the bank of tracks.
    h.button(Button::Left);
    CHECK(has(h.status(), "session_first=9") && has(h.status(), "job=PatternPage"));
    h.button(Button::Down);
    CHECK(has(h.status(), "track=5") && has(h.status(), "page=session") && has(h.status(), "job=TrackBank"));
    h.button(Button::Up);
    CHECK(has(h.status(), "track=1"));
    // Copy + a pad, then another: the pattern copied there; Delete + a pad: cleared at once.
    seqr.pattern(0, 8) = p;
    h.with(Button::Copy, [&] { h.pad(24 + 0); h.pad(24 + 1); });
    CHECK(seqr.pattern(0, 9).events.size() == p.events.size() && has(h.status(), "job=CopyPattern"));
    h.with(Button::Delete, [&] { h.pad(24 + 1); });
    CHECK(seqr.pattern(0, 9).empty() && has(h.status(), "job=DeletePattern"));
    // Hold a pad and tap others of its row: a chain.
    h.pad_down(24 + 0);
    h.pad(24 + 3);
    h.pad(24 + 4);
    h.pad_up(24 + 0);
    const auto& track = melodee.studio().project().tracks[0];
    CHECK(track.chain_count == 3 && track.chain[0] == 8 && track.chain[1] == 11 && track.chain[2] == 12 && has(h.status(), "job=ChainPattern"));
    // The wheel: the playhead by Q; its push, the column as a sequence.
    h.wheel(2);
    CHECK(seqr.play_from_shown() == 480 && has(h.status(), "job=Playhead"));
    seqr.take_play_from();
    h.button(Button::MainPush);
    CHECK(has(h.status(), "page=scene") && has(h.status(), "job=LaunchScene"));
    h.knob(0, -200);
    h.knob(1, 1);
    h.knob(1, 1); // a ready name, a detent each: Verse
    CHECK(std::string(melodee.studio().project().scenes[seqr.number(0)].name) == "Verse");
    h.knob(2, 1);
    h.knob(2, 1); // its colour: the second track's, a detent each
    CHECK(melodee.studio().project().scenes[seqr.number(0)].colour == 2);
    h.pad(3); // another column: a chain of sequences
    CHECK(has(h.status(), "scene_chain=2"));
    h.button(Button::MainPush);
    CHECK(has(h.status(), "page=session") && melodee.studio().project().tracks[0].chain_count == 2);
    // Note/Session again: Play. Held: a peek until it is let go.
    h.button(Button::Menu);
    CHECK(has(h.status(), "page=kit-play"));
    h.press(Button::Menu);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    h.lights();
    CHECK(has(h.status(), "page=session") && has(h.status(), "job=PeekSession"));
    h.release(Button::Menu);
    CHECK(has(h.status(), "page=kit-play"));
    // Shift + Step 3: Workflow; the wheel to Count-In, push, turn: off.
    h.with(Button::Shift, [&] { h.step(2); });
    CHECK(has(h.status(), "page=workflow") && has(h.status(), "job=Workflow"));
    h.wheel(1);
    h.wheel(1);
    h.button(Button::MainPush);
    h.wheel(-1);
    h.button(Button::MainPush);
    CHECK(!seqr.workflow.count_in && has(h.status(), "workflow_row=2 workflow_edit=0"));
    h.button(Button::Back);
    // Shift + Step 11: Repeat on, its rate on the wheel; again, off.
    h.with(Button::Shift, [&] { h.step(10); });
    CHECK(has(h.status(), "page=repeat") && melodee.studio().current().repeat && has(h.status(), "job=Repeat"));
    unsigned rate = melodee.studio().current().repeat_rate;
    h.wheel(1);
    CHECK(melodee.studio().current().repeat_rate == rate + 1);
    h.with(Button::Shift, [&] { h.step(10); });
    CHECK(!melodee.studio().current().repeat && has(h.status(), "page=kit-play"));
    // Shift + Loop (a tap): Melodee's SEQ pages; Shift + the wheel the page.
    h.with(Button::Shift, [&] { h.button(Button::Loop); });
    CHECK(has(h.status(), "page=seq-pages") && has(h.status(), "job=SeqPages") && has(h.status(), "seq_page_name=SEQ"));
    h.knob(0, -2); // the length: two bars
    CHECK(seq::bars_of(seqr.pattern(), 3840) == 2);
    h.button(Button::MainPush);
    CHECK(has(h.status(), "page=seq-jobs"));
    h.button(Button::MainPush); // Half Seq
    CHECK(seq::bars_of(seqr.pattern(), 3840) == 1 && has(h.status(), "page=seq-pages"));
    h.with(Button::Shift, [&] { h.wheel(1); h.wheel(1); });
    CHECK(has(h.status(), "seq_page_name=ROLL") && has(h.status(), "job=Page"));
    h.wheel(1);
    h.knob(1, -50); // the focused note's velocity
    CHECK(seqr.pattern().events[size_t(std::min<size_t>(1, seqr.pattern().events.size() - 1))].velocity < 100);
    h.button(Button::Back);
    CHECK(has(h.status(), "page=kit-play"));
    seqr.launch(0, 0);
}

extern "C" int schwung_move_info(void* out, size_t capacity);

void test_more(app::MelodeeApp& melodee) {
    Hands h {melodee};
    auto& seqr = melodee.studio().sequencer();
    melodee.studio().select_pad(0);
    seqr.launch(0, 30);
    auto& p = seqr.pattern(0, 30);
    p = melodee::Pattern {};
    seq::set_length(p, 2 * 3840, 128 * 3840);
    // Tap tempo on Tempo (Shift + Step 5): the steps tapped half a second apart, 120 BPM; Left halves, Right doubles.
    h.with(Button::Shift, [&] { h.step(4); });
    CHECK(has(h.status(), "page=tempo"));
    for (int n = 0; n < 3; ++n) { h.step(n); if (n < 2) std::this_thread::sleep_for(std::chrono::milliseconds(500)); }
    unsigned tapped = melodee.studio().tempo();
    CHECK_SAID(tapped >= 1100 && tapped <= 1250, tapped);
    h.button(Button::Left);
    CHECK(melodee.studio().tempo() == tapped / 2);
    h.button(Button::Right);
    CHECK(melodee.studio().tempo() == tapped / 2 * 2);
    h.button(Button::Back);
    melodee.studio().set_tempo(1200);
    // The metronome on Rec only (Workflow): on, it clicks only while recording.
    seqr.workflow.click_rec_only = true;
    melodee.studio().set_click(true, 70);
    CHECK(has(h.status(), "click_heard=0"));
    // The modules hear the transport (get_clock_status, schwung_move_info).
    h.button(Button::Play);
    h.render(40);
    uint8_t info[272] {};
    CHECK(schwung_move_info(info, sizeof info) == 1 && info[13] == 1);
    h.button(Button::Rec);
    h.render(2);
    CHECK(has(h.status(), "rec=1") && has(h.status(), "click_heard=1"));
    // A knob turned while recording: written as Motion where the playhead is (14.2).
    h.render(30);
    h.knob(7, -5);
    unsigned cutoff = melodee::motion_pad_param(melodee::PadParam::FilterCutoff);
    CHECK(p.lane_points(cutoff, 0) >= 1);
    // Note Repeat while recording: the repeats written as they play.
    seqr.set_repeat(0, true, 0, 4);
    size_t before = p.events.size();
    h.pad_down(0);
    h.render(400);
    h.pad_up(0);
    h.render(2);
    h.lights();
    CHECK_SAID(p.events.size() >= before + 3, p.events.size() - before);
    seqr.set_repeat(0, false, 0, 4);
    h.button(Button::Rec);
    h.render(2);
    CHECK(has(h.status(), "rec=0") && has(h.status(), "click_heard=0"));
    h.button(Button::Play);
    h.render(2);
    CHECK(schwung_move_info(info, sizeof info) == 1 && info[13] == 0);
    seqr.workflow.click_rec_only = false;
    melodee.studio().set_click(false, 70);
    // Roll's jobs: a key's notes marked, a step inserted, the velocities ramped.
    p = melodee::Pattern {};
    p.add(note(0, 36, 100));
    p.add(note(480, 36, 100));
    p.add(note(960, 38, 100));
    seqr.sync(0, 30);
    h.with(Button::Shift, [&] { h.button(Button::Loop); });
    h.with(Button::Shift, [&] { for (int n = 0; n < 9; ++n) h.wheel(-1); h.wheel(1); h.wheel(1); }); // the pages open where they were left: back to ROLL
    CHECK(has(h.status(), "seq_page_name=ROLL roll_focus=0"));
    auto roll_job = [&](int row) {
        h.button(Button::MainPush);
        for (int n = 0; n < 20; ++n) h.wheel(-1);
        for (int n = 0; n < row; ++n) h.wheel(1);
        h.button(Button::MainPush);
    };
    roll_job(12); // MARK THE KEY: A01's two
    CHECK(has(h.status(), "marks=2"));
    roll_job(14); // RAMP DOWN over them
    CHECK(p.events[0].velocity == 100 && p.events[1].velocity == 50);
    roll_job(15); // INSERT STEP at the focus's: everything a step later
    CHECK(p.events[0].tick == 240 && p.events[2].tick == 1200);
    h.button(Button::Delete); // the note in focus erased
    CHECK(p.events.size() == 2);
    h.button(Button::Back);
    // A chord pad (a note track's chord or Keys & Scale pad, which instruments' audio path stamps): every key recorded.
    {
        p = melodee::Pattern {};
        seqr.sync(0, 30);
        h.button(Button::Play);
        h.render(20);
        h.button(Button::Rec);
        h.render(2);
        auto& stamp = const_cast<app::Studio::PadStamp&>(melodee.studio().stamp(7));
        const int chord[3] {60, 64, 67};
        stamp.track.store(0);
        stamp.key.store(chord[0]);
        for (unsigned k = 0; k < 3; ++k) stamp.keys[k].store(chord[k]);
        stamp.count.store(3);
        stamp.velocity.store(90);
        stamp.position.store(480);
        stamp.ticks.store(melodee.studio().host().ticks());
        stamp.sounded.store(true);
        melodee.surface(make_event(0x90, pad_note(7), 90)); // the MIDI thread's side alone: the stamp stands as set
        CHECK(seq::notes_in(p, seq::step_span(p, 2)).size() == 3);
        melodee.surface(make_event(0x80, pad_note(7), 0));
        h.button(Button::Rec);
        h.button(Button::Play);
        h.render(2);
    }
    // To Pads PATTERN (Chop's sheet): the slices' notes into the playing pattern when it holds nothing, else the first
    // that holds nothing, which then plays.
    {
        melodee::Pattern made;
        made.length = melodee.studio().project().bar_ticks();
        for (unsigned k = 0; k < 3; ++k) {
            melodee::Event e;
            e.tick = k * made.length / 4;
            e.length = 120;
            e.key = uint8_t(40 + k);
            made.add(e);
        }
        unsigned playing = seqr.number(0);
        int target = seqr.slices_target();
        const auto& now = seqr.pattern(0, playing);
        bool blank = now.empty() && now.motion.empty() && now.punches.empty() && now.euclids.empty();
        CHECK(target >= 0 && (unsigned(target) == playing) == blank);
        seqr.slices_to_pattern(unsigned(target), made);
        CHECK(seqr.number(0) == unsigned(target));
        const auto& q = seqr.pattern(0, unsigned(target));
        CHECK(q.events.size() == 3 && !q.free && q.length == made.length && q.events[2].tick == made.length / 2);
        CHECK(seqr.slices_target() != target); // it holds notes now
        seqr.clear_pattern(0, unsigned(target));
    }
    seqr.launch(0, 0);
}

void test_app() {
    std::string home = "build/test-home-sequencer";
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home + "/Projects");
    app::MelodeeApp melodee("build/test-samples", home, "build/test-no-modules");
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    CHECK(melodee.restore_now().empty());
    CHECK(has(melodee.status(), "track=1 kind=kit"));
    test_steps(melodee);
    test_locks_and_pitches(melodee);
    test_bars_and_patterns(melodee);
    test_record(melodee);
    test_capture(melodee);
    test_fill_and_repeat(melodee);
    test_pages(melodee);
    test_screens(melodee);
    test_more(melodee);
    // The Workflow settings and loops come back with the Move's settings.
    melodee.studio().sequencer().workflow.quantize = 60;
    melodee.studio().sequencer().workflow.count_in = false;
    CHECK(melodee.save_now().empty());
    app::MelodeeApp again("build/test-samples", home, "build/test-no-modules");
    CHECK(again.studio().sequencer().workflow.quantize == 60 && !again.studio().sequencer().workflow.count_in);
}

// Sequence links (melodee-sample's, model/scene_links.h): a pattern launched by hand is kept in the sequence in hand;
// a sequence launches what it keeps (off where it keeps off), one that keeps nothing its column; the playing
// pattern's pad again turns its track off from the next bar, kept so; a launch ends off; all saved (SEQL).
void test_links() {
    std::string home = "build/test-home-links";
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home + "/Projects");
    {
        app::MelodeeApp melodee("build/test-samples", home, "build/test-no-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        Hands h {melodee};
        auto& seqr = melodee.studio().sequencer();
        auto& project = melodee.studio().project();
        for (auto [t, n] : {std::pair<unsigned, unsigned>{0, 0}, {0, 1}, {1, 0}, {1, 2}}) {
            auto& p = seqr.pattern(t, n);
            p.length = seqr.bar_ticks();
            p.free = false;
            melodee::Event e;
            e.key = uint8_t(t ? 60 : melodee::sampler_first_note);
            e.length = 240;
            p.events.push_back(e);
            seqr.sync(t, n);
        }
        h.button(Button::Menu);
        CHECK(has(h.status(), "page=session"));
        h.pad(24 + 1); // track 1's pattern 2, kept in the sequence in hand (1); track 2 keeps what it is on
        CHECK(project.scene == 0 && project.tracks[0].in_scene[0] == 2 && project.tracks[1].in_scene[0] == 1);
        // Sequence 3 keeps track 1 off and track 2's pattern 3.
        project.tracks[0].in_scene[2] = melodee::scene_off;
        project.tracks[1].in_scene[2] = 3;
        seqr.launch_scene(2);
        CHECK(project.scene == 2 && project.tracks[0].off && project.tracks[1].pattern == 2);
        // Sequence 2 keeps nothing: its column (track 1 has notes there), track 2 plays on.
        seqr.launch_scene(1);
        CHECK(project.scene == 1 && !project.tracks[0].off && project.tracks[0].pattern == 1 && project.tracks[1].pattern == 2);
        // Its sheet (the wheel's push): it keeps nothing; Capture keeps what every track is on.
        h.button(Button::MainPush);
        CHECK(has(h.status(), "page=scene") && has(h.status(), "scene_kept=0"));
        h.button(Button::Capture);
        CHECK(has(h.status(), "scene_kept=1") && project.tracks[0].in_scene[1] == 2 && project.tracks[1].in_scene[1] == 3);
        h.button(Button::Back);
        // Playing: the playing pattern's pad again turns track 1 off, as it is let go, kept off in sequence 2.
        h.button(Button::Play);
        h.render(8);
        h.pad(24 + 1);
        CHECK(project.tracks[0].off && project.tracks[0].in_scene[1] == melodee::scene_off);
        h.render(800); // past the bar
        CHECK((seqr.stopped_tracks() & 1) && !(seqr.stopped_tracks() & 2));
        h.pad(24 + 0); // a launch ends off, and is kept
        CHECK(!project.tracks[0].off && project.tracks[0].in_scene[1] == 1);
        h.button(Button::Play);
        CHECK(melodee.save_now().empty());
    }
    app::MelodeeApp again("build/test-samples", home, "build/test-no-modules");
    const auto& project = again.studio().project();
    CHECK(project.scene == 1 && project.tracks[1].in_scene[2] == 3 && project.tracks[0].in_scene[2] == melodee::scene_off);
}

} // namespace

int main() {
    test_edits();
    test_app();
    test_links();
    std::cout << "sequencer-tests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
