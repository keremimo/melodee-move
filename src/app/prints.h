// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/engine/print_player.h"
#include "melodee/model/print.h"
#include "melodee/model/project.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Print and freeze (docs/UI.md, "Print and freeze"; Melodee's MELODEE.md ›
// Print and freeze, done on the Move): a track's patterns printed to audio
// files and the track playing them instead of its engine and chain, which
// saves the CPU (and with a deep freeze the Schwung module's memory). Pads
// still play live; level, pan, sends and mute stay live.
//
// The print host: a thread of its own at SCHED_IDLE on the last core (it
// takes only the time nothing else wants, so the audio thread never waits for
// it), one pattern at a time, faster than real time: Melodee's own lean
// EngineHost holding print_project() (model/print.h), the track's samples
// (the live ones, read only, kept for the run) and, on a synth track, a
// second instance of its Schwung module set as the live one is, rendered by
// Melodee's render_print() (engine/print_render.h) into
// "<project> Prints/Track N A1.wav" beside the project, 32-bit float with its
// frames on a page, as Melodee writes them, so the MPC plays them too. A
// pattern whose record (PRNT) has the same hash and whose file is there is
// not printed again.
//
// A freeze: the prints read whole into memory (resident: the audio thread
// reads them as it reads a sample, no pager) and handed to the engine host
// (Command::Freeze), which switches on a bar line while playing. The
// project keeps the table (PRNT, FRZN), so a frozen track opens frozen.
namespace app {

class Studio;

class Printer {
public:
    explicit Printer(Studio& studio);
    ~Printer();
    Printer(const Printer&) = delete;
    Printer& operator=(const Printer&) = delete;
    void stop(); // the print host ends (a print under way is called off)

    // ---- MIDI thread (or the service thread), under the studio's lock
    melodee::PrintTable& table() { return prints; }
    const melodee::PrintTable& table() const { return prints; }
    bool frozen(unsigned track) const { return prints.freeze[track % melodee::track_count].frozen(); }
    bool deep(unsigned track) const { return prints.freeze[track % melodee::track_count].state == melodee::FreezeState::Deep; }
    bool thawing(unsigned track) const { return thaw_mask >> (track % melodee::track_count) & 1; }
    // Whether a track's kind can be printed: a kit, a synth (a Schwung module that takes notes).
    bool printable(unsigned track) const;
    // The patterns of `track` with notes, a bit each.
    std::array<uint64_t, 2> with_notes(unsigned track) const;

    struct Job {
        unsigned track = 0;
        std::vector<uint8_t> patterns;
        melodee::PrintTap tap = melodee::PrintTap::PostFx;
        melodee::PrintTail tail = melodee::PrintTail::Loop;
        bool freeze = true, deep = false;
    };
    std::string start(Job job); // empty: started; else why not
    void cancel();              // the prints under way called off: nothing changes
    bool busy() const { return running.load(std::memory_order_acquire) || job_waiting; }
    // Plays live again at once (a deep-frozen one thaws first: its module loads again, the prints playing meanwhile).
    std::string unfreeze(unsigned track);
    // A control would change what a frozen track's prints are made of: true when `track` is frozen, so the caller
    // asks "Unfreeze track N?" first.
    bool asks(unsigned track) const { return frozen(track); }

    struct Progress {
        bool busy = false, freeze = false;
        unsigned track = 0;
        int pattern = -1;            // printing now
        float fraction = 0, times_real = 0, seconds_left = 0;
        uint16_t count = 0, through = 0;
        std::array<uint64_t, 2> done {}, failed {};
        std::string error;           // the last job's, if it failed
        bool is_done(unsigned p) const { return done[p / 64 % 2] >> (p % 64) & 1; }
    };
    Progress progress() const;
    // The share of a block the track takes now, in percent (what freezing saves).
    unsigned cpu_percent(unsigned track) const;
    // Bytes the prints of `patterns` would take (a pass, two for Loop, 10 s more for Ring at most).
    uint64_t room(unsigned track, const std::vector<uint8_t>& patterns, melodee::PrintTail tail) const;

    // Results in, packs to the engine, packs handed back freed, a deep freeze's module let go once its print plays
    // and the engine sleeps, a thaw's end. Often (Studio::service(), under the lock).
    void tick();
    // A project read (its table set): each frozen track's prints read and sent (the service thread, without the lock).
    void opened();

    // melodee-print, melodee-freeze, melodee-unfreeze (MELODEE.md › Print and freeze), the words after the command's
    // name; the reply. Under the lock.
    std::string command(const std::string& name, const std::string& words);
    std::string status() const;

private:
    struct Result {
        melodee::PrintRecord record;
        bool made = false;
        std::string error;
        double seconds = 0, times_real = 0;
    };
    struct Ready {
        unsigned track = 0;
        uint32_t serial = 0;
        melodee::FreezePack* pack = nullptr;
        melodee::TrackFreeze freeze;
        std::string error;
    };
    struct Work; // a job as the print thread takes it (prints.cpp)
    void run();
    Result print_one(const Work& work, unsigned pattern);
    Ready freeze_pack(const Work& work, const melodee::PrintTable& known);
    void send_pack(Ready& ready);
    Studio& studio;
    melodee::PrintTable prints;
    uint32_t thaw_mask = 0, deep_pending = 0;
    std::array<std::chrono::steady_clock::time_point, melodee::track_count> deep_since {}, thaw_since {};
    uint32_t serial = 0;
    unsigned printed = 0, skipped = 0, froze = 0;
    std::vector<melodee::FreezePack*> sent; // packs the engine holds or held (freed as it hands them back)
    mutable std::mutex lock;
    std::condition_variable wake;
    std::unique_ptr<Work> waiting;     // under lock
    std::deque<Result> results;        // under lock
    std::deque<Ready> ready;           // under lock
    Progress shown;                    // under lock
    std::deque<std::string> lines;     // under lock: the latest results, for status
    bool stopping = false, job_waiting = false; // under lock (job_waiting read without it as a hint)
    std::atomic<bool> running {false}, cancelled {false};
    std::thread thread;
};

} // namespace app
