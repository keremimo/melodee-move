// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/bindings.h"
#include "app/seq_edit.h"
#include "move/app.h"
#include "move/lights.h"
#include "melodee/model/project.h"
#include "melodee/shell/capture_ring.h"
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <bitset>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Melodee's sequencer on the Move (docs/UI.md, "Sequencer"; docs/PARITY.md,
// Sequencing): the Move's step sequencing, Loop Mode, Session, Seq Record
// and Capture (its manual, 11-14 and 17) on Melodee's patterns, which the
// engine host plays (melodee-sample's engine/engine_host.h). The keys come
// as jobs of the bindings table (bindings.h); the screens of their own
// (Loop, Session, Workflow, Repeat, Capture, Melodee's SEQ pages) are files
// in src/app/screens/seq_*.cpp and share this.
//
// Threads: everything here is the MIDI and screen threads', under the
// studio's lock, as the screens are. The audio thread only stamps each pad
// it plays (Studio::stamp()), so a note is written at the tick it was heard,
// and plays what the patterns sent it (PatternData copies, freed when
// handed back). Nothing here runs on the audio thread.
namespace app {

class Studio;
class Screen;

class Sequencer {
public:
    using Clock = std::chrono::steady_clock;
    explicit Sequencer(Studio& studio);
    ~Sequencer();

    // ------------------------------------------------------------ the keys
    // A job the screens left (bindings.h): steps, bars, patterns, Rec,
    // Capture, Shift + Steps 14-16, locks. False: not the sequencer's.
    bool job(bindings::Job job, const bindings::Use& use);
    // After a job was done: a knob turned while Seq Record runs is written
    // as Motion (the value it left).
    void after(bindings::Job job, const bindings::Use& use);
    // Every input as it comes (after the audio thread had the pads): pads
    // played into Seq Record and Capture, what is held let go.
    void input(const move::SurfaceEvent& event);
    // Every lights() call: Seq Record's free length growing, the engine's
    // repeats written, the click for Rec only, a launched pattern's loop.
    void tick(Clock::time_point now);
    // The steps, Loop and the arrows on a Play screen.
    void lights(move::Lights& lights, Clock::time_point now);
    // Rec and Capture, on every screen.
    void transport_lights(move::Lights& lights, Clock::time_point now);
    // A held step's card, its TRIG knobs, a lock just turned: drawn over the
    // Play screen. False: nothing to draw.
    bool draw(juce::Graphics& g);
    std::string status() const;

    // ------------------------------------------------------------ shared with the screens
    melodee::Pattern& pattern(unsigned track, unsigned number);
    melodee::Pattern& pattern(); // the selected track's, the one it plays
    unsigned number(unsigned track) const; // the pattern a track plays (or will)
    // A pattern changed: its copy to the audio thread (as played: its feel
    // and its track's speed laid on), and saved.
    void sync(unsigned track, unsigned number);
    void sync_track(unsigned track); // every pattern of a track (its speed changed)
    // Launch (17.1.2): at the next bar while playing, at once when stopped;
    // an empty, free one as long as the one playing. Ends a chain.
    void launch(unsigned track, unsigned number);
    void chain(unsigned track, const std::vector<uint8_t>& numbers); // Melodee's CHAN: each once, round again
    void launch_scene(unsigned number);  // pattern n of every track with notes there, and the scene's tempo
    int next_empty(unsigned track, unsigned after) const; // -1: none
    bool copy_pattern(unsigned track, unsigned from, unsigned to_track, unsigned to);
    void clear_pattern(unsigned track, unsigned number);
    uint32_t bar_ticks() const;
    int lane_key() const; // the steps' lane: a kit's last pad's key, -1 on a note track
    bool kit() const;

    // The page of steps in view (Left / Right), the steps a page.
    unsigned page() const;
    unsigned page_steps() const;
    void set_page(int page);

    // Loop Mode's loop (12.1): bars [from, to) of the selected pattern; from
    // above the first bar it plays as an editor's loop (EditLoop).
    unsigned loop_from(unsigned track, unsigned number) const;
    void set_loop(unsigned from_bar, unsigned to_bar);

    // Workflow (Shift + Step 3, 13): kept with the Move's settings.
    struct Workflow {
        unsigned quantize = 100;  // Shift + Step 16's amount, percent
        bool count_in = true;     // a bar of clicks before a take from stopped (14.1.1)
        bool rec_quantize = true; // notes land on the pattern's Q (Melodee's Rec Quantize)
        unsigned max_bars = 0;    // a free take's longest: 0 Melodee's 128 bars
        bool click_rec_only = false; // the metronome clicks only while recording
    } workflow;
    std::string settings_text() const;           // lines for Move.settings
    bool setting_line(const std::string& line);  // one read back; false: not ours

    // Seq Record (14.1).
    bool recording() const { return rec; }
    bool counting_in() const;
    void record(bool on);
    // Note Repeat (11.6): the selected track's, its style (0 Repeat, 1-5
    // the arp Up, Down, Up-down, Random, Order) and rate (engine/arp.h).
    void set_repeat(unsigned track, bool on, unsigned style, unsigned rate); // style 0 Repeat, 1-5 the arp's modes
    // The audio thread's routing of the pads (Studio::pads_repeat) from the
    // selected track's repeat, arp and arp latch: call after changing them.
    void route_pads();
    // Capture (14.3), Melodee's Recall.
    bool capture();                       // false: nothing played to keep
    void clear_capture();
    bool can_capture() const;
    struct TempoGuess { unsigned tenths = 0, bars = 1; };
    std::vector<TempoGuess> guesses;      // a capture played stopped: the wheel picks one (capture screen)
    void take_guess(unsigned which);
    // Melodee's lanes: a knob's (the screen on top's, or a kit page's by its
    // name) as a Motion lane; false where it has none.
    struct Lane { unsigned param = 0, pad = 0; int low = 0, high = 127; std::string name; };
    bool knob_lane(unsigned knob, Lane& lane) const;
    void set_lane(unsigned track, unsigned param, unsigned pad, int value); // as a pot would (macros)
    bool lane_off(unsigned track, unsigned number, unsigned param, unsigned pad) const;
    void toggle_lane(unsigned track, unsigned number, unsigned param, unsigned pad);
    // Tap tempo (Tempo screen): a tap now.
    void tap(Clock::time_point now);
    // Fill (Melodee's): FILL conditions play while on.
    void fill(bool on);
    bool filling() const { return fill_on; }

    // Melodee's SEQ pages (screens/seq_pages.cpp): what their knobs and pushes do.
    void half_seq();
    void half_speed(bool slower); // Half Speed (slower) or Double Speed: the notes twice as far apart, or half
    int vary(unsigned variation, double amount); // into the next empty pattern, launched; -1 none made
    void set_division(int delta);   // the pattern's Q (Workflow's Step Grid)
    void set_signature(int delta);  // the project's time signature (Workflow's)
    void set_speed(int delta);      // the track's speed, x1/2 .. x2
    void set_pattern_tempo(int delta); // this pattern number's tempo (BPM per pattern), tenths; below 30 BPM none
    void set_seq_tempo(bool on);
    void set_scene_tempo(unsigned number, int delta);
    void feel_all();                // the pattern's feel on the track's other patterns with notes (Time Correct's ALL)
    void set_groove_from(int delta);
    void follow_column();           // the follow action on every track's pattern of this number with notes
    void lay_euclid(unsigned hits, unsigned steps, unsigned rotate);
    void set_lane_steps(int delta); // the last pad's lane, steps of Q (0: the pattern's)
    void send_modulator(unsigned n);
    void turn_macro(unsigned n, int delta);
    std::vector<Lane> lanes_of(unsigned track) const; // what MOD and macros can move on a track
    // Step entry (Melodee's Step Edit PLAY, on the ENTRY page): the caret, a
    // step of Q; pads write there and it moves on when they are let go.
    unsigned caret = 0;
    bool entry = false;             // the ENTRY page is shown: pads write at the caret
    uint8_t entry_velocity = 0;     // 0: as hit
    unsigned entry_length = 1;      // steps
    uint8_t entry_chance = 100;
    void entry_rest();              // on without writing
    void entry_erase();             // back a step, taking it out
    // To Pads PATTERN (Melodee's Chop, as Live's Slice to New MIDI Track):
    // the pattern it goes into, the selected track's when it holds nothing,
    // else the first that holds nothing (-1: none); then `made`
    // (melodee::slice_pattern: a note a slice where it falls) written there,
    // its own settings kept, and played. A Seq Record take ends first.
    int slices_target() const;
    void slices_to_pattern(unsigned target, const melodee::Pattern& made);
    // The playhead by Q (Session's wheel, Melodee's Shift + wheel): playing,
    // it jumps; stopped, Play continues from it.
    void move_playhead(int steps);
    uint32_t take_play_from();      // Studio::play(): where Play starts (0 the top)
    uint32_t play_from_shown() const { return play_from; }

    // What a step held shows: the step (from the pattern's top), its span.
    int held_step() const;
    bool trig_page = false; // a held step's knobs: Melodee's TRIG, not the locks
    bool peek_session = false; // Note/Session held: Session shows until it is let go (5)

private:
    Studio& studio;
    void write_note(int key, int velocity, int position, uint32_t ticks, bool early);
    void release_note(int key, uint32_t ticks);
    void start_take();
    void end_take();
    void grow_take();
    void lights_steps(move::Lights& lights, Clock::time_point now);
    void lock_turn(unsigned knob, int delta);
    void tap_tempo_job(bool halve, bool doubled);
    std::vector<uint8_t> step_keys() const;
    unsigned step_of(unsigned index) const; // a step button as a step of the pattern
    bool on_tempo() const;
    melodee::Pattern copy_for(unsigned track, unsigned number) const;
    std::string screens_pad_name() const; // the last pad and its sample, as the card names it

    std::array<int, melodee::track_count> pages {};
    bool follow = true; // the steps follow the playhead's page while playing
    bool was_playing = false;
    // Steps held, as pressed (the first is the one held).
    std::vector<unsigned> steps_down;
    bool copy_down = false;
    int copy_from = -1, copy_to = -1; // Copy + steps: the steps copied (a range), -1 none
    int knob_shown = -1;
    Clock::time_point knob_until {};
    // Seq Record.
    bool rec = false, rec_waiting = false; // waiting: the count-in, or Play to come
    bool take_free = false;                // the take grows a free pattern
    unsigned take_track = 0, take_number = 0;
    bool any_written = false;
    struct HeldNote { int key = -1; unsigned track = 0, number = 0; uint32_t tick = 0, since = 0; };
    std::array<HeldNote, 32> held_notes {};
    std::array<int, move::pad_count> pad_keys {}; // what each pad played (its first key), -1 none
    std::array<std::vector<int>, move::pad_count> pad_chords; // and all its keys (a chord pad's)
    std::array<int, move::pad_count> pad_tracks {};
    std::vector<uint8_t> keys_down; // a note track's keys held, for a step's chord
    bool click_now = false;
    // Capture.
    std::unique_ptr<melodee::CaptureRing> ring;
    uint32_t capture_since = 0;
    uint16_t run = 0;
    Clock::time_point started = Clock::now();
    uint32_t ms_now() const;
    // Loop Mode's start bar, a pattern each (above 0: an editor's loop).
    std::map<unsigned, unsigned> loop_starts; // track * 128 + pattern
    std::array<int, melodee::track_count> loop_sent {};
    // Lanes off (Mute + a knob's touch): left out of what the audio thread plays.
    std::map<uint64_t, bool> lanes_off;
    // Tap tempo.
    std::vector<Clock::time_point> taps;
    bool fill_on = false;
    unsigned last_track = 0;
    uint32_t play_from = 0;
    Clock::time_point length_until {}, captured_until {}; // Loop held + wheel's length, a capture's notes: shown a moment
    int captured = -1;
    bool entry_written = false; // step entry: a chord written at the caret, moved on as the pads are let go
};

} // namespace app
