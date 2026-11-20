// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/latency.h"
#include "melodee/engine/looper.h"
#include "melodee/model/project.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// The Looper on the Move (docs/UI.md, "Looper"; melodee-sample's
// docs/LOOPER.md). Melodee's own Looper (engine/looper.h) runs in the engine
// host: four tempo-synced loops played into tracks L1-L4, each with a strip
// and a chain. This is the Move's shell around it, as melodee-sample's
// shell/looper_jobs.cpp and looper_files.cpp are the MPC's:
//
// - asks (Command::Looper) with the project's values kept in step, so the
//   project file has them (LOOP, LPMX);
// - the loop tracks' strips (L1-L4 are tracks 16-19 to the engine host);
// - a loop's row of pads: the audio thread sends record, play / stop and
//   undo in the cycle the pad was read (Studio::audio_event()); the MIDI
//   thread picks the loop and times a clear held;
// - the loops kept with the project as "<project> Loops/Loop N.wav" (24-bit
//   stereo at the loop's own rate), Commit to pad, `melodee-looper`.
//
// Threads: everything but service() and flush() under the studio's lock.
// service() and flush() run on the service thread outside it: they hold a
// loop (the audio thread lets go of it once nothing writes it), copy it,
// let go, and only then write or read files.
namespace app {

class Studio;

class LoopDesk {
public:
    using Clock = std::chrono::steady_clock;
    using Do = melodee::looper::Do;
    using View = melodee::Looper::View;
    static constexpr unsigned loops = melodee::loop_tracks;
    static constexpr auto clear_time = std::chrono::milliseconds(600); // a clear pad held this long clears
    // A loop's row of pads (docs/UI.md, "Looper"): L1 the top row, its four left pads these jobs.
    enum PadJob : unsigned { RecordPad, PlayPad, UndoPad, ClearPad, PadJobs };
    // The Move's own round trip, output to input, when nothing else is set:
    // the runtime's one block (the simulated Move measures 128 + its cable).
    static constexpr int default_latency = 128;

    explicit LoopDesk(Studio& studio);

    // ---------------------------------------------------------- under the lock
    bool on() const;
    void set_on(bool on);            // off: every loop cleared, its memory handed back
    unsigned memory_made() const;    // buffers made, of 8 (two a loop)
    bool memory_ready() const { return memory_made() >= 2 * loops; }
    unsigned holding() const;        // loops with audio
    View view(unsigned loop) const;
    // One ask of the Looper, its project values kept; false: refused, the screen told why.
    bool ask(Do what, unsigned loop, int value = 0, uint32_t position = 0);
    unsigned selected = 0;           // the loop the knobs, the wheel and Rec are on
    void select(unsigned loop) { selected = loop % loops; }

    // The settings (knobs, the remote). A loop's length only before a take,
    // its source only between takes; quantize and after take are all four's.
    void step_length(unsigned loop, int delta);
    void step_source(unsigned loop, int delta);
    void set_feedback(unsigned loop, int percent);
    void set_quantize(int quantize); // looper::Quantize
    void set_then(int then);         // looper::Then
    // The loop tracks' strips.
    const melodee::Mix& mix(unsigned loop) const;
    void set_level(unsigned loop, int level); // 0..127, 100 unity
    void set_pan(unsigned loop, int pan);     // -50..50
    void set_send(unsigned loop, int send);   // 0..100
    void set_bus(unsigned loop, int bus);     // 0..3
    void set_mute(unsigned loop, bool on);
    void set_solo(unsigned loop, bool on);
    // Names as the screen shows them.
    static std::string source_name(uint8_t source); // "INPUT", "INPUT L", "TRACK 2", "L1", "MIX"
    static std::string length_name(unsigned bars);  // "FREE", "1 BAR", "4 BARS"
    // The input's round trip the loops write behind their play head (device frames).
    int latency() const { return input_latency; }
    void set_latency(int frames);
    // Measures it: clicks out, listened for at the input (a cable from the
    // Move's output into its line in), four of them, the shortest kept as
    // the latency. Again while measuring: stops.
    void measure();
    bool measuring_now() const { return measuring.load(std::memory_order_relaxed); }
    unsigned measured() const { return measure_got; }

    // A pad of a loop's row, on the MIDI thread: the audio thread sent its
    // record, play / stop or undo already; here the loop is picked, a clear
    // timed, and a Looper that is off says so.
    void pad(unsigned loop, unsigned job, bool down, Clock::time_point now);
    int clearing() const { return clear_loop; }                // the loop whose clear pad is held, -1 none
    float clear_progress(Clock::time_point now) const;         // 0..1 of clear_time
    // Often (lights()): a clear held long enough; what the audio thread
    // refused, said once; a loop's audio changed, the project saved for it.
    void tick(Clock::time_point now);

    // Commit to pad: the loop onto the selected kit's next empty pad, as
    // `<home>/Samples/Looper/L<n> Loop <m>.wav`; `note`: one note at bar 1 of
    // the kit's pattern as long as the loop, and the loop stopped. Done on
    // the service thread; false: refused, said why.
    bool commit(unsigned loop, bool note);
    int commit_pad() const; // where a commit lands: the selected kit's next empty pad; -1 none

    // `melodee-looper ARGS` (docs/LOOPER.md's remote): the answer, a status line or an error.
    std::string remote(const std::string& arguments);
    std::string status() const; // the status line's fields

    // Saving: each loop's length, tempo and layers into the project before
    // its bytes are made, and its file marked to be written if it changed.
    void keep_values();

    // ---------------------------------------------------------- audio thread
    // The Looper is on top: the left 16 pads are the loops' rows. Shift: a
    // play / stop pad acts at once. The kit bank and octave the right 16 play.
    std::atomic<bool> pads_shown {false}, pads_shift {false};
    std::atomic<bool> pads_free {true}; // nothing but Shift held: a loop's pad does its job (Mute, Copy, Delete or a step held: none)
    std::atomic<int> pads_bank {0}; // the kit's bank of 16 the right 16 play (the last pad's)
    // The input delay's measurement (measure()): render() runs the meter every block.
    move::LoopbackMeter meter;
    std::atomic<bool> measuring {false};

    // ---------------------------------------------------------- service thread
    void service();                            // memory and fits, loop files read and written, commits
    void flush(std::chrono::milliseconds wait); // the loop files written now (quitting), waiting at most `wait` a loop

private:
    struct Seen {
        bool empty = true;
        uint32_t length = 0;
        uint8_t layers = 0, undo = 0;
        uint16_t tempo = 0;
        bool fitting = false;
        bool operator!=(const Seen& o) const {
            return empty != o.empty || length != o.length || layers != o.layers || undo != o.undo || tempo != o.tempo || fitting != o.fitting;
        }
    };
    struct CommitJob { unsigned loop = 0, track = 0, pad = 0; bool note = false; double ticks = 0; };
    struct FileJob { std::string dir; uint8_t write = 0, remove = 0; std::array<uint64_t, loops> edits {}; };

    void observe(unsigned loop, const View& view); // a change of its audio counted, the project saved for it
    melodee::Looper& looper();
    const melodee::Looper& looper() const;
    std::string loop_dir() const;
    bool hold_wait(unsigned loop, std::chrono::milliseconds wait);
    void write_files(FileJob job, std::chrono::milliseconds wait);
    void read_files();
    void run_commit(const CommitJob& job);

    Studio& studio;
    int clear_loop = -1;
    Clock::time_point clear_at {};
    std::array<unsigned, loops> errors_seen {};
    std::array<Seen, loops> seen {};
    std::array<uint64_t, loops> edits {}, written {};
    std::array<bool, loops> adopting {};     // a saved loop on its way in: its file is the one there
    std::array<bool, loops> was_stopped {};
    int input_latency = default_latency;
    unsigned measure_seen = 0, measure_missed = 0, measure_got = 0;
    int measure_best = -1;
    // The service thread's work, handed over under the lock.
    std::vector<CommitJob> commits;
    FileJob files;
    bool files_due = false;
    uint8_t load_mask = 0;                   // saved loops to read back (the first service pass)
    bool loaded = false;
};

} // namespace app
