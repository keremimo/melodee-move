// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/engine/bounce.h"
#include "melodee/model/print.h"
#include "melodee/model/project.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Song's exports (docs/UI.md, "Song"; Melodee's song export, MELODEE.md ›
// Sequencing, Song): a mixdown as 32-bit float, or 24 or 16-bit integer PCM
// TPDF dithered (the only files Melodee dithers); stems (the mix as Mix.wav
// and each track and bus as it joins the mix, all 32-bit float, the same
// frames, in the same pass); a MIDI file; an MPC 3 project; an Ableton Live
// Set (melodee/shell/live_export.h); the song into a new pattern. Under Melodee's own folder
// (/data/UserData/Melodee on the Move, as Melodee/ on the MPC's card):
// Mixdowns/<name>.wav, Stems/<name>/, MIDI/<name>.mid, MPC 3/<name>.xpj,
// Ableton/<name> Project/<name>.als; <name> is the project's, numbered so
// nothing is written over.
//
// A mixdown and stems play the song once through in real time, as Melodee
// does: the engine host renders the master into its Bounce ring (and the
// stems into a StemRing) from the song's first frame to its end and its
// tail (10 s at most), and this class's own thread drains them into the
// files as they fill. Play or Back stops it and drops the files. Nothing of
// it is on the audio thread but the engine host's copying into the rings.
namespace app {

class Studio;

class Exports {
public:
    enum class Kind : uint8_t { Mix32, Mix24, Mix16, Stems, Midi, Mpc3, Live, ToPattern, Count };
    static const char* kind_name(Kind kind); // "MIXDOWN 32 FLOAT", ...
    explicit Exports(Studio& studio);
    ~Exports();
    Exports(const Exports&) = delete;
    Exports& operator=(const Exports&) = delete;
    void stop(); // the thread ends (an export under way is dropped)

    // MIDI thread, under the studio's lock. Empty: started (or done at once:
    // a MIDI file, a new pattern); else why not.
    std::string start(Kind kind);
    void cancel(); // a mixdown or stems stopped: its files dropped; an MPC 3 or Live export's prints called off
    // MPC 3 and Live: synth tracks go as their prints, made first, a track at a time (the print host skips unchanged
    // ones), then the project is written. Often, under the lock (SongDesk::tick()).
    void tick();
    bool busy() const { return running.load(std::memory_order_acquire); }

    // What the screen shows. Any thread.
    struct Shown {
        bool busy = false, done = false;
        Kind kind = Kind::Mix32;
        std::string name, file;     // the export's name, and the file (or folder) relative to Melodee's
        float fraction = 0;         // of the song (and its tail)
        double seconds = 0, total = 0, tail = 0;
        uint64_t bytes = 0;
        std::string error, said;
    };
    Shown shown() const;
    void seen(); // the result screen let go: nothing to show
    std::string status() const;

    // Where each kind goes, under Melodee's folder.
    static std::string folder_of(Kind kind);
    // `folder`/`stem` + extension, numbered past what is there ("Move", "Move 2", ...).
    static std::string free_name(const std::string& folder, const std::string& stem, const std::string& extension);
    // A kind that writes a project of files on the export thread, its synth tracks printed first: MPC 3, Live.
    static bool writes_project(Kind kind) { return kind == Kind::Mpc3 || kind == Kind::Live; }

private:
    void run();
    void render_job();   // the export thread: a mixdown or stems, as the song plays
    void project_job();  // the export thread: the MPC 3 project or the Live Set
    Studio& studio;
    std::unique_ptr<melodee::StemRing> stems; // made on the first stems export
    mutable std::mutex lock;
    std::condition_variable wake;
    Shown now;                     // under lock
    bool stopping = false;         // under lock
    bool job_waiting = false;      // under lock
    std::atomic<bool> running {false}, cancelled {false};
    // The job, as the MIDI thread set it up.
    Kind kind = Kind::Mix32;
    std::string home, path;        // Melodee's folder, and the file (or the stems' folder)
    uint32_t stem_mask = 0;
    std::array<std::string, melodee::StemRing::stem_count> stem_names {};
    std::unique_ptr<melodee::Project> copy_project; // MPC 3, Live: copies, as they were when it started
    std::unique_ptr<melodee::PatternBank> copy_bank;
    std::unique_ptr<melodee::PrintTable> copy_prints;
    std::string copy_prints_dir, copy_audio_dir;
    uint32_t prints_pending = 0;   // synth tracks still to print
    bool prints_waiting = false;   // printing them first
    double song_seconds = 0;
    unsigned exported = 0;
    std::thread thread;
};

} // namespace app
