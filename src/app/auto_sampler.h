// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/module_host.h"
#include "app/project_store.h"
#include "melodee/shell/auto_sample.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Melodee's auto sampler on the Move (docs/UI.md, "Instruments"; MELODEE.md,
// Print and freeze, Auto sample): a module track's sound played a key at a
// time and kept as an MPC keygroup program (.xpm and its WAVs), which a new
// keygroup track then plays. Its own instance of the module plays on a
// thread of its own at the lowest priority, faster than real time, as
// Melodee's print host plays a plug-in; the track's own instance and the
// audio thread are never touched.
//
// The check runs first and reads nothing from the module: Melodee's own
// probes (engine/auto_probe.h) find drums, mono and glide, the accent and
// velocity layers, takes, whether it sustains (loops, engine/loop_find.h)
// and how long to hold each key; the release is fitted to how the sampled
// notes fell (engine/release_fit.h). The settings and findings are
// Melodee's AutoSampleSettings (shell/auto_sample.h), the program Melodee's
// .xpm (shell/xpm_write.h).
namespace app {

// What plays the notes: a module's instance, or (tests) anything else.
class NoteSource {
public:
    virtual ~NoteSource() = default;
    virtual void note(int key, int velocity) = 0; // velocity 0: let go
    virtual void quiet() = 0;                     // every note off
    virtual void render(float* left, float* right, unsigned frames) = 0;
};
// A Schwung module of its own, set as the track's (its preset and values).
std::unique_ptr<NoteSource> module_source(const ModuleInfo& info, const SynthFile& state, std::string& error);

class AutoSampler {
public:
    enum class Phase : uint8_t { Idle, Opening, Checking, Checked, Sampling, Writing, Done, Failed };
    using Opener = std::function<std::unique_ptr<NoteSource>(std::string& error)>;
    struct Job {
        unsigned track = 0;
        std::string name;    // the program's
        std::string folder;  // Keygroups/: the program goes in <folder>/<name>/
        bool sample = false; // false: the check alone
    };
    ~AutoSampler(); // the thread called off and waited for
    // Starts the check, then (job.sample) the sampling, on a thread of its
    // own. False while one runs.
    bool start(const Job& job, const melodee::AutoSampleSettings& settings, Opener open);
    void cancel(); // called off: what was made is deleted
    bool running() const { return phase_now() == Phase::Opening || phase_now() == Phase::Checking || phase_now() == Phase::Sampling || phase_now() == Phase::Writing; }

    Phase phase_now() const { return phase.load(); }
    unsigned done() const { return notes_done.load(); }
    unsigned total() const { return notes_total.load(); }
    // What the check heard, the error, and the program made: copies.
    melodee::AutoFindings findings() const;
    std::string error() const;
    std::string program() const; // the .xpm, once Done
    std::vector<std::string> heard() const; // the check's rows, as the sheet lists them
    unsigned track() const { return job_now.track; }
    void reset(); // back to Idle once its result was taken

private:
    void run(Job job, melodee::AutoSampleSettings settings, Opener open);
    void fail(const std::string& why);
    std::thread thread;
    std::atomic<Phase> phase {Phase::Idle};
    std::atomic<bool> cancelled {false};
    std::atomic<unsigned> notes_done {0}, notes_total {0};
    mutable std::mutex lock;
    melodee::AutoFindings found;
    std::string why, made;
    std::vector<std::string> rows;
    Job job_now;
};
const char* phase_name(AutoSampler::Phase phase); // "IDLE", "OPENING", "CHECKING", ...

// The check alone (the thread's; tests call it directly): what `source` is.
melodee::AutoFindings check_source(NoteSource& source, const melodee::AutoSampleSettings& settings, std::vector<std::string>* rows = nullptr,
                                   const std::atomic<bool>* cancelled = nullptr);

} // namespace app
