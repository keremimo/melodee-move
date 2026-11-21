// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/engine/sample_data.h"
#include "melodee/model/input_config.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Melodee's Sample Record, Input Config and Audio Recall on the Move: what
// melodee-sample's shell does around the engine host's TakeRecorder and
// RecallRing (shell/sample_record.cpp, shell/sample_capture.cpp), for the
// Move's one input.
//
// The Move has one stereo input, and its jack decides what it carries: the
// microphone, or the line in once a cable is in (the XMOS says so with CC
// 114, and where the sound goes with CC 115: the speaker or the line out).
// So the sources are the input (MIC or LINE, LINE L, LINE R, as the jack has
// it), Melodee's own mix (RESAMPLE) and a track of the project (TRACK n),
// and a hardware source follows the jack as a cable goes in or out.
//
// Threads: everything here is the MIDI and screen threads', under the
// studio's lock, but service(), which is the service thread's and takes the
// lock itself. The audio thread never sees this object: it records into the
// engine host's recorder and ring, and the commands reach it as any other
// (Studio::send). Files are written on the service thread only: a take as it
// is recorded, drained from the recorder into "TAKE n.wav.part", made whole
// once it ends; Audio Recall's copy of the ring.
namespace app {

class Studio;

class Sampling {
public:
    using Clock = std::chrono::steady_clock;
    explicit Sampling(Studio& studio);
    ~Sampling();
    Sampling(const Sampling&) = delete;
    Sampling& operator=(const Sampling&) = delete;

    // ------------------------------------------------- under the studio's lock
    melodee::InputConfig input;   // Input Config: kept in Move.settings, as Melodee keeps it with its settings
    melodee::SamplingPrefs prefs; // Settings' SAMPLING rows: Audio Recall's cut, a Free take's own end
    bool preview = true;          // the browser plays each file as the wheel lands on it

    // The jack (CC 114 and 115), as the XMOS reports it; unknown until it does.
    void sense_input(uint8_t value);
    void sense_output(uint8_t value);
    bool sensed() const { return input_known; }
    bool line_in() const { return line; }
    bool speaker() const { return to_speaker; }

    // The sources, in the order the SOURCE row turns through them: the
    // input as the jack has it, RESAMPLE, then each track with a sound of
    // its own (not the kit recorded onto, an empty or a MIDI track).
    struct Source { melodee::InputSource source; unsigned track = 0; };
    std::vector<Source> sources() const;
    int source_index() const; // the one set, in sources(); -1 when it is not there now
    void set_source(const Source& source);
    std::string source_tag() const;  // the rail's, four characters: MIC, LINE, LN L, LN R, IN, RSMP, TRK3
    std::string source_name() const; // a row's: MIC, LINE L, RESAMPLE, TRACK 3
    // Steps of a row's value (Input Config's knobs and the wheel).
    void step_source(int delta);
    void step_gain(int delta);
    void step_monitor(int delta);
    void step_threshold(int delta);
    void step_length(int delta);
    void changed(); // the input as set: to the engine, saved
    std::string gain_text() const;      // "+6 DB"
    std::string monitor_text() const;   // "AUTO"
    std::string threshold_text() const; // "AT ONCE", "-24 DB"
    std::string length_text() const;    // "FREE", "SEQ", "4 BARS"

    // A take, as Melodee's Record: a pad records onto itself (after the
    // threshold, or where the pattern comes round with SEQ while playing);
    // the pad again stops it (while it waits: starts it at once); another
    // pad carries the take on onto that pad from there (each pad its part
    // of the one file); a pad held 0.4 s ends it as it is let go.
    enum class Phase : uint8_t { Idle, Armed, Cued, Recording, Saving };
    Phase phase() const;
    bool taking() const { auto p = phase(); return p == Phase::Armed || p == Phase::Cued || p == Phase::Recording; }
    void press_pad(unsigned pad);
    void release_pad(unsigned pad, Clock::duration held);
    void stop();   // the take ends here (kept); one armed is let go
    void cancel(); // one armed is let go; one recording ends, kept
    int take_pad() const { return pads.empty() ? -1 : int(pads.back()); } // the pad recording now
    const std::vector<uint8_t>& take_pads() const { return pads; }
    const std::vector<uint32_t>& take_cuts() const { return cuts; } // where each pad's part starts (44.1 kHz frames)
    uint32_t take_frames() const;  // so far, 44.1 kHz frames
    uint32_t take_length() const;  // its length when it has one (bars, SEQ), else 0
    uint8_t take_peak(unsigned column) const; // the take's picture, 128 columns
    float level_left() const;
    float level_right() const;
    // Whether the pads play Melodee's own sound while a take of it waits or runs
    // (a track as the source: that track; Resample: the kit), not a pad to record onto.
    bool pads_play_source() const;
    // The last take kept: its name, length and the pads it went onto (for the screen).
    std::string last_name;
    uint32_t last_frames = 0;
    std::vector<uint8_t> last_pads;
    melodee::Overview last_overview {};
    std::vector<uint32_t> last_cuts;

    // Audio Recall (Shift + Sampling): the ring's last 25 s onto the next
    // empty pad of the selected kit, cut as the SAMPLING rows say. False: said why.
    bool recall();
    int recall_pad = -1;                  // where it goes, as the screen shows it
    std::vector<float> recall_peaks;      // what it kept, for the screen
    bool recall_waiting = false;          // the service thread has not landed it yet

    // Every lights() call, with whether the Sampling screen is in view: Monitor
    // (Auto hears the input only then), the speaker kept quiet while the mic is
    // heard through it, the pads routed to the source, a take armed and let go.
    void tick(bool in_view);

    // Settings in Move.settings ("word value"); unknown words are left alone.
    bool setting(const std::string& word, unsigned value);
    std::string settings_text() const;
    std::string status() const; // " rec=... source=..."

    // ------------------------------------------------- the browser's (sampling_files.cpp)
    // Starred files and the last 16 loaded, newest first, kept as Starred.txt
    // and Recent.txt in Melodee's folder, a path a line (Melodee's
    // shell/sample_lists.h); a file that has gone is left out.
    std::vector<std::string> starred, recent;
    bool star(const std::string& path); // starred or not; true: starred now
    bool is_starred(const std::string& path) const;
    void loaded(const std::string& path); // into RECENT
    // The preview: `path` plays on `track` (a kit) once it is read; "" stops it.
    void preview_file(const std::string& path, unsigned track);
    std::string previewing;          // the file playing, once read
    uint32_t preview_frames = 0;     // its length (44.1 kHz frames)
    Clock::time_point preview_at {}; // when it began
    // Deleting from storage (Delete on the browser): empty when `path` may
    // go, else why not. Kerem: in Melodee's folder and the Move's User
    // Library, never the Core Library, a place's top or what the project plays.
    std::string may_delete(const std::string& path) const;
    void delete_path(const std::string& path);
    unsigned deletes = 0; // done so far (the browser lists its folder again)
    // Trim Sample (the pad's region into Edits/<NAME>-Trimmed.wav, the pad onto
    // all of it), Save Sample (its region as Samples/<name>.wav, the pad as it
    // is), Extract (a slice into Edits/<NAME>-Slice n.wav, onto the next
    // empty pad): written on the service thread.
    void trim(unsigned track, unsigned pad);
    void save(unsigned track, unsigned pad, const std::string& name);
    void extract(unsigned track, unsigned pad, uint32_t start, uint32_t end, unsigned slice);
    // An MPC expansion (.xpn, a zip) installed into Samples/Expansions/<its
    // title>, a file a pass; a file already there at its size is kept.
    void install(const std::string& xpn);
    std::string installing;              // its name while it installs
    unsigned install_done = 0, install_total = 0;
    std::string installed;               // the folder it went into, once done
    unsigned installs = 0;               // done so far
    // The audio thread let go of a sample (EngineHost::take_retired): a preview's is freed.
    void retired(const melodee::SampleData* sample);

    // ------------------------------------------------- an audio track's takes (audio_takes.cpp)
    // Melodee's audio track (guide/audio-tracks.md) on the Move: Rec on its
    // INPUT arms it (again: let go; while recording: ends, kept); the next
    // pass of its pattern from the top is recorded (Play starts it when
    // stopped), drained into "<project> Audio/Take NNN.wav" as it comes; that
    // pattern plays it from then on. Its source is the input as the jack has
    // it; its monitor heard while armed or recording. Takes are held in
    // memory once read (a pattern's length at most), and each track's pack
    // made again when a take's CLIP or WARP changes.
    std::vector<Source> audio_sources() const;
    void step_audio_source(unsigned track, int delta);
    bool audio_armed(unsigned track) const { return audio_arm == int(track); }
    int audio_recording() const;     // the track recording now, -1 none
    void arm_audio(unsigned track);  // Rec: arms, lets go, or ends the take
    void audio_changed(unsigned track); // its takes or their settings changed: its pack again
    void restore_audio();             // every audio track's pack, once the project is read
    // Another project opened (Studio's `opened` hooks, under the lock): the
    // last one's takes let go (the engine let their packs go as it reloaded),
    // the new one's read from its own folder. A take still being written
    // lands first.
    void project_opened();
    struct TakeData {
        std::vector<float> left, right;
        unsigned rate = 44100;
        std::vector<uint8_t> peaks; // the loudest of each 441 frames (44.1 kHz), as Melodee's take peaks
    };
    const TakeData* take_data(unsigned take) const;
    std::vector<uint8_t> recording_peaks; // the take being recorded, for the screen
    uint32_t recording_frames = 0;

    // ------------------------------------------------- service thread
    void service();

private:
    struct Job;
    bool arm(unsigned pad);
    void send_source();
    unsigned next_number(const std::string& folder, const char* stem) const;
    std::string folder() const;
    void land_take(Job& job);
    void land_recall(Job& job);
    void service_files(); // sampling_files.cpp
    void read_lists();
    struct Edit;
    std::vector<std::unique_ptr<Edit>> edits;
    std::string preview_wanted;
    unsigned preview_track = 0;
    bool preview_asked = false;
    std::vector<std::unique_ptr<melodee::SampleData>> previews; // offered to the engine's preview slot, until handed back
    std::vector<std::string> deleting, to_install;
    bool lists_dirty = false;
    // An audio track's take.
    void service_audio(); // audio_takes.cpp
    void send_pack(unsigned track);
    int audio_arm = -1;
    struct AudioJob;
    std::unique_ptr<AudioJob> audio_job;
    std::map<unsigned, std::unique_ptr<TakeData>> takes;   // by take number, kept for the run (packs point into them)
    std::vector<unsigned> takes_wanted, packs_wanted;       // to read; tracks whose pack waits for them
    int audio_quiet_sent = -1;
    bool audio_restored = false, folders_made = false;

    Studio& studio;
    bool input_known = false, line = false, to_speaker = true;
    melodee::InputSource line_choice = melodee::InputSource::Rear; // LINE, LINE L or LINE R, for when a cable is back
    // The take: its track and pads, its cuts, its file.
    unsigned track = 0;
    std::vector<uint8_t> pads;
    std::vector<uint32_t> cuts;
    bool armed_here = false, saving = false, stopping = false;
    Clock::time_point armed_at {};
    bool monitor_sent = false, monitor_known = false, quiet_sent = false;
    int redirect_sent = -2;
    std::unique_ptr<Job> job; // the take's file (the service thread's while it records)
    std::vector<std::unique_ptr<Job>> recalls;
};

} // namespace app
