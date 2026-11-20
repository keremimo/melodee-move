// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/project_store.h"
#include "melodee/model/project.h"
#include <array>
#include <bitset>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <vector>

// Undo and redo for everything a project holds (Melodee's History,
// shell/history.h, on the Move): the last 32 steps. A step is what changed
// between two moments the app closes one: a press of a control, a knob's
// turns (closed by another control or two seconds still), a sample or a
// module loaded, a take. Nothing has to say what it changed. The history
// keeps a copy of the project as the last step left it (the baseline), and
// a step is the difference between that and the project now, part by part:
//
// - Melodee's Project as bytes, the parts that differ (every field but the
//   two slot tables, which keep their slots on the heap), each with what it
//   was and what it became;
// - the sample and chop slots that differ, slot by slot;
// - the patterns that differ, whole;
// - the Move's own part of the project (MoveExtras: the tracks' colours);
// - each Schwung module's state: the keys the screens set since the last
//   step, or the whole state where the module or its preset changed.
//
// A change that is no step (a mute, the last pad played, the octave: what
// Melodee leaves out of undo) is taken into the baseline without one
// (close() with keep false), so no undo touches it later.
//
// Plain data, on the MIDI or service thread under the studio's lock; never
// on the audio thread. The studio tells the engine what an undo changed.
namespace app {

// A Schwung module as a step sees it: which, its preset, its values.
struct ModuleState {
    bool present = false; // the track plays a module (or waits for one)
    bool settled = true;  // false while a module loads: not compared, the baseline kept
    SynthFile file;
};

struct Step {
    using Clock = std::chrono::steady_clock;
    std::string what;      // "TUNE A05"
    int track = -1;        // whose, for the list; -1 the project's
    Clock::time_point at {};
    struct Bytes { uint32_t at = 0; std::vector<uint8_t> before, after; };
    std::vector<Bytes> project, extras;
    struct Sample { uint16_t slot = 0; melodee::SampleRef before, after; };
    std::vector<Sample> samples;
    struct Chop { uint16_t slot = 0; melodee::SampleChop before, after; };
    std::vector<Chop> chops;
    struct PatternChange { uint8_t track = 0, pattern = 0; melodee::Pattern before, after; };
    std::vector<PatternChange> patterns;
    struct Key { std::string key, before, after; };
    struct Module {
        uint8_t track = 0;
        bool whole = false;            // the module or its preset changed: before / after are its whole state
        ModuleState before, after;
        std::vector<Key> keys;         // else the values set
    };
    std::vector<Module> modules;
    // What it touched, for the engine: tracks (their bytes or patterns), and
    // whether anything outside the tracks.
    std::bitset<melodee::track_count> tracks;
    bool globals = false;
    bool tempo_only = true; // of the globals, only the tempo and swing
    bool empty() const {
        return project.empty() && extras.empty() && samples.empty() && chops.empty() && patterns.empty() && modules.empty();
    }
};

// The state the history compares and restores: the studio's, live.
struct Live {
    melodee::Project& project;
    melodee::PatternBank& bank;
    MoveExtras& extras;
    std::array<ModuleState, melodee::track_count> modules {};
    // The keys each module track's screens set since the last close (the
    // only values a step compares: the rest move with the module's answers).
    std::array<std::vector<std::string>, melodee::track_count> edited {};
};

class History {
public:
    using Clock = Step::Clock;
    static constexpr size_t depth = 32;
    History();
    ~History();

    // Forget every step; `now` is the baseline (a project opened or new).
    void reset(const Live& now);
    // The difference between the baseline and `now` as a step named `what`
    // (kept, the redo steps forgotten), or, with `keep` false, taken into the
    // baseline without one. True when a step was kept.
    bool close(const Live& now, const std::string& what, int track, Clock::time_point at, bool keep = true);
    // The newest step taken back (or the last one undone done again) on
    // `now` and the baseline. The step is returned for the studio to tell
    // the engine and the modules; null when there is none. Close any open
    // step first.
    const Step* undo(Live& now);
    const Step* redo(Live& now);
    bool can_undo() const { return !done.empty(); }
    bool can_redo() const { return !undone.empty(); }
    size_t size() const { return done.size(); }
    size_t redos() const { return undone.size(); }
    const Step& kept(size_t n) const { return done[n]; }       // oldest first
    const Step& undone_step(size_t n) const { return undone[n]; } // the next redo last
    // A module that finished loading as a step left it (a project opened, a
    // track copied, an undo): what it reads back is the baseline, no step.
    void settle(unsigned track, const ModuleState& state) { if (track < base_modules.size()) base_modules[track] = state; }
    // Every slot a step could bring back (a save frees only the others).
    void slots_in_use(std::vector<bool>& used) const;
    // The baseline's bytes of the project, for the tests.
    size_t baseline_bytes() const { return base_bytes.size(); }

private:
    void diff(const Live& now, Step& step) const;
    void take(const Live& now); // the baseline made `now`, whole
    void apply(Live& now, const Step& step, bool forward);
    std::vector<uint8_t> base_bytes;        // the project's bytes, the slot tables' left zero
    std::unique_ptr<melodee::Project> base; // its slot tables
    std::unique_ptr<melodee::PatternBank> base_bank;
    MoveExtras base_extras;
    std::array<ModuleState, melodee::track_count> base_modules {};
    std::deque<Step> done, undone;
};

// A project's bytes the history compares: every field but the slot tables.
struct ProjectSpan { size_t from, to; };
std::vector<ProjectSpan> project_spans();
// The bytes a span list covers, of `project`.
bool span_holds(const std::vector<ProjectSpan>& spans, size_t at);
// Which track the project's byte `at` belongs to, or -1 outside the tracks.
int track_of_byte(size_t at);
// Whether byte `at` is the tempo's or the swing's.
bool tempo_byte(size_t at);

} // namespace app
