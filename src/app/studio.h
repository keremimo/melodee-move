// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/canvas_page.h"
#include "app/history.h"
#include "app/projects.h"
#include "ui/track_style.h"
#include <functional>
#include "app/module_host.h"
#include "app/project_store.h"
#include "move/app.h"
#include "move/queues.h"
#include "melodee/engine/auto_chop.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/onsets.h"
#include "melodee/engine/sample_data.h"
#include "melodee/model/project.h"
#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Melodee on the Move: everything the screens share. Melodee's own project
// (melodee-sample's model/project.h, sixteen tracks) and its engine host
// (engine/engine_host.h: the tracks' engines, their mix, the sequencer's
// clock, the metronome), the samples and Schwung modules the tracks play,
// the project file, and the way to the audio thread.
//
// Threads, as docs/UI.md and AGENTS.md want them:
// - The MIDI, screen and service threads share the model under `lock`.
//   Whatever changes the sound becomes a melodee::Command, pushed under the
//   lock onto one ring (send()), so there is one producer at a time.
// - The audio thread is the engine host's only feeder: at audio_event() and
//   at the start of render() it moves the ring's commands into the host's
//   own queue, so the host's single-producer queue has the audio thread as
//   its one producer. A pad is pushed straight onto it in audio_event(), in
//   the cycle it was read, and the render() that follows plays it from the
//   block's first frame: no thread, queue or block in between.
// - Samples and modules are offered to the host under the lock too, and
//   handed back to be freed off the audio thread.
namespace app {

constexpr unsigned pad_count = 32; // the Move's pads: kit pads 1-32 (A01-B16), notes 36-67

// What a track is, as the Move shows it: Melodee's engines, a PLUGIN track
// split by what it plays (one of Schwung's modules, or another plug-in).
enum class TrackKind : uint8_t { Empty, Kit, Synth, Keys, Midi, Audio, Plugin, Count };
const char* kind_name(TrackKind kind); // "empty", "kit", "synth", ...
const char* kind_tag(TrackKind kind);  // the rail's: "----", "KIT", "SYN", "KEYS", "MIDI", "AUD", "PLUG"

// A sample in memory, with what Chop needs found in it.
struct Sample {
    std::string name, path;
    std::unique_ptr<melodee::SampleData> data;
    melodee::Overview overview {};
    std::array<melodee::Hit, melodee::max_hits> hits {};
    unsigned hit_count = 0;
    melodee::BeatGrid beats;
    melodee::AutoRead autos;
};
// Reads a sound file and finds its hits, beats and what Auto reads. Slow
// and allocating: never on the audio thread, never under the lock.
std::unique_ptr<Sample> read_sample(const std::string& path, std::string& error);

// Melodee's PLUGIN track's plug-in, for one of Schwung's modules: the
// engine host calls it on the audio thread (engine/plugin_engine.h).
class SchwungRunner final : public melodee::PluginRunner {
public:
    explicit SchwungRunner(SynthPlayer& player) : player(player) {}
    void midi(uint8_t status, uint8_t data1, uint8_t data2) override { player.midi(status, data1, data2); }
    void silence() override { player.midi(0xb0, 64, 0); player.midi(0xb0, 123, 0); }
    void render(float* left, float* right, unsigned frames) override { player.render_add(left, right, frames); }

private:
    SynthPlayer& player;
};

// A track playing one of Schwung's modules: its player (whose runner the
// engine host holds as the track's plug-in), the module, and how its
// screens are left. The module replaced waits in `leaving` until the audio
// thread has let it go.
struct ModuleTrack {
    SynthPlayer player;
    SchwungRunner runner {player};
    uint8_t entry = melodee::no_plugin; // its entry of Project::plugins
    std::unique_ptr<LoadedModule> module;
    std::vector<std::unique_ptr<LoadedModule>> leaving;
    std::vector<std::unique_ptr<CanvasPage>> canvas_pages; // one a module->canvases, null where its script failed
    SynthFile saved;            // as the project file has it: kept while the module is missing
    std::string job;            // a module to load (the service thread's)
    bool job_restores = false;  // set as `saved` has it (a project opened, a track copied)
    bool loading = false;
    std::map<std::string, uint64_t> set_at; // each key's last set (SynthPlayer's serial)
    std::vector<std::string> edited;        // the keys set since the last undo step closed (history.h)
    // The screen's: the page, the menu, the module's own pages.
    unsigned page = 0;
    bool menu_open = false, menu_editing = false;
    std::vector<std::string> menu_path; // level ids, root first
    int menu_cursor = 0;
    std::vector<std::pair<int, std::string>> pick_items; // an items list, as the module gave it
    std::string pick_key;                                // the list being read, until it is
    bool pick_reading = false;
    int canvas_open = -1;        // a page opened from the menu, over it
    bool canvas_entered = false; // the page has the jogwheel
    ~ModuleTrack() { player.stop(); } // a source's thread ends before the modules it plays go
};

class Screen;

// The Move's Setup (Shift + Step 2) as Melodee's Settings: the same for every
// project, kept in Move.settings with the project to open at start.
struct Settings {
    uint8_t response = 1;    // pad response: 0 soft, 1 medium, 2 hard, 3 full level
    bool pressure = true;    // pad pressure reaches the modules
    uint8_t pad_lights = 0;  // 0 the tracks' colours, 1 white, 2 off
    uint8_t pad_fade = 1;    // 60, 100, 150, 250 ms: how fast a pad's light fades
    uint8_t brightness = 3;  // the lights: 0 low, 1 mid, 2 high, 3 max (the Move's four)
    bool light_show = false;
    uint8_t show_style = 0;  // Pulse, Sweep, Chase, Ripple, Levels
    bool full = true;        // Full, or Basic: the Move's own pages, tracks, modules, the mixer and Song
    bool usage_log = false;  // what the hands do, a file each start (Usage/)
    std::string start;       // the project opened at start; empty: the one open last
    bool operator==(const Settings& o) const {
        return response == o.response && pressure == o.pressure && pad_lights == o.pad_lights && pad_fade == o.pad_fade &&
               brightness == o.brightness && light_show == o.light_show && show_style == o.show_style && full == o.full &&
               usage_log == o.usage_log && start == o.start;
    }
};
constexpr unsigned pad_fade_ms[4] {60, 100, 150, 250};
constexpr const char* show_styles[5] {"PULSE", "SWEEP", "CHASE", "RIPPLE", "LEVELS"};

class Studio {
public:
    using Clock = std::chrono::steady_clock;
    // `library`: the folder Load opens (the Move's own sample folders by
    // default). `home`: Melodee's folder, its project in Projects/Move.mldp
    // (/data/UserData/Melodee on the Move). `modules`: Schwung's modules
    // (/data/UserData/schwung/modules). The project is read here, before
    // the audio thread starts; its samples and modules in restore().
    Studio(std::string library, std::string home, std::string modules);
    ~Studio();
    Studio(const Studio&) = delete;
    Studio& operator=(const Studio&) = delete;

    mutable std::mutex lock;

    // ---------------------------------------------------------- audio thread
    void render(const move::AudioBlock& input, move::AudioBlock& output);
    void audio_opened(const move::AudioFormat& format);
    void audio_event(const move::SurfaceEvent& event);

    // ---------------------------------------------------------- under `lock`
    melodee::Project& project() { return *song; }
    const melodee::Project& project() const { return *song; }
    melodee::PatternBank& patterns() { return *bank; }
    melodee::EngineHost& host() { return *engine; }
    const melodee::EngineHost& host() const { return *engine; }
    // A change of sound, to the audio thread (false: the ring was full).
    bool send(const melodee::Command& command);

    // The tracks.
    unsigned track() const { return selected; }
    melodee::Track& current() { return song->tracks[selected]; }
    const melodee::Track& current() const { return song->tracks[selected]; }
    TrackKind kind(unsigned track) const;
    TrackKind kind() const { return kind(selected); }
    void select(unsigned track);
    // A new track of `kind` on empty track `track`; false when it is not empty.
    bool new_track(unsigned track, TrackKind kind);
    void delete_track(unsigned track);
    bool copy_track(unsigned from, unsigned to); // `to` replaced
    // Melodee's Move: `from` goes to `to`, a track there trading places, mix
    // strip, module, colour and patterns with it. Nothing is lost.
    bool move_track(unsigned from, unsigned to);
    // A track's colour on the lights: its own (track options) or the
    // track's place's (ui/track_style.h).
    ui::TrackColour colour_of(unsigned track) const;
    ModuleTrack* module_track(unsigned track);
    ModuleTrack* module_track() { return module_track(selected); }
    int octave(unsigned track) const; // the bottom-left pad's octave on a note track (0: C2)
    void set_octave(unsigned track, int octave);
    int note_for(unsigned pad, int octave) const; // rows of fourths, C2 at the bottom left at octave 0

    // A kit track's pads.
    unsigned pad() const { return song->tracks[selected].last_pad % pad_count; }
    void select_pad(unsigned pad);
    const Sample* sample_of(unsigned track, unsigned pad) const;
    const Sample* sample_of_pad() const { return sample_of(selected, pad()); }
    void set_pad(unsigned track, unsigned pad, const melodee::PadSound& sound); // the project's and the engine's
    void sound_pad(unsigned track, unsigned pad, const melodee::PadSound& sound); // the engine's only (Chop's slices)
    void sound_pads(unsigned track);                                             // the engine's back to the project's
    melodee::SampleChop* chop_of(unsigned slot);
    std::string install(std::unique_ptr<Sample> sample, unsigned track, unsigned pad); // empty on success
    struct Job { std::string path; unsigned track, pad; };
    std::vector<Job> jobs; // files to load (the service thread's)
    bool loading = false;

    // Transport and tempo.
    bool playing() const { return engine->playing(); }
    void play();
    void stop();
    void restart(); // every playing pattern from its top again (the Move's Shift + Play); nothing while stopped
    unsigned tempo() const { return song->tempo; } // tenths of a BPM
    void set_tempo(unsigned tenths);
    void set_swing(unsigned percent);
    // A track's mix strip, as Move's track button combinations set it.
    void set_level(unsigned track, int level); // 0..127, 100 unity
    void set_mute(unsigned track, bool on);
    void set_solo(unsigned track, bool on);
    bool click() const { return click_on; }
    unsigned click_level() const { return click_volume; }
    void set_click(bool on, unsigned level);

    // A Schwung module's values (as the screen and the save see them).
    void synth_set(ModuleTrack& track, const std::string& key, const std::string& value);
    std::string module_get(ModuleTrack& track, const std::string& key); // now, as a page script asks
    CanvasPage::Host canvas_host(unsigned track);
    std::vector<ModuleInfo> catalog;
    std::string modules_root;
    void rescan_modules();
    // Typing, for a module's page that asked: the pads and steps a keyboard.
    bool typing = false;
    std::string typed, typing_title;
    int typing_canvas = -1, typing_track = -1;
    void finish_typing(bool cancelled);
    // The name sheet (the Move's typing): `done` gets the text unless it is
    // cancelled (Back held). Under `lock`.
    void ask_name(std::string title, std::string initial, size_t room, std::function<void(const std::string&)> done);
    std::function<void(const std::string&)> typing_done;
    size_t typing_room = 120;
    int typing_strip = 0; // the strip's character in the box (ui::system::name_sheet)

    // The question before a loss (Melodee's one sheet): what is asked, what
    // goes, whether undo brings it back. `yes` runs on a push (or the pad
    // asked about, pressed again); Back or anything else leaves it.
    struct Question {
        std::string rail_top, rail_big, rail_bottom, asked, line1, line2;
        bool undoable = false;
        std::string yes_key = "PUSH YES";
        int pad = -1; // a pad that says yes too (the Move's Delete + pad, again)
        std::function<void()> yes;
    };
    void ask(Question question);
    Question asked;
    bool quit = false; // Setup's Back to Move: the runtime ends, Ableton's app comes back
    unsigned options_track = 0; // whose options Shift + a track button opened
    // How the Move is doing, for Setup's About (MelodeeApp fills it from the runtime's session).
    struct Machine { float cpu_percent = 0; unsigned peak_us = 0; unsigned late = 0; std::string version; } machine;
    unsigned slow_calls = 0; // calls into a module that held the audio thread 1 ms or more
    uint32_t slowest_us = 0;

    // The screens (screen.h): the one on top has the controls; below it
    // the track's own Play screen. open() puts `id` over the Play screen,
    // push() over the one on top; close() goes back down one.
    Screen* top() const;
    Screen* screen(const std::string& id) const;
    void open(const std::string& id);
    void push(const std::string& id); // over the one on top (a sheet of it), which stays as it is
    void close();
    void home(); // back to the track's Play screen
    std::vector<std::unique_ptr<Screen>> screens; // every registered screen, made once
    std::vector<Screen*> stack;

    // Undo (history.h): every change to the project between two closes is
    // one step. The app closes one after each press of a control, after a
    // knob's or the wheel's turns (another control, or two seconds still),
    // and the service thread after a load; nothing has to say what changed.
    // A screen names the step it makes (shown on undo: "UNDO TUNE A05"); a
    // change that is no undo step (the last pad played, a mute, the octave)
    // says not_a_step(), and the next close takes it in without one. A take
    // that lasts holds its step open from hold_step() to release_step().
    void name_step(std::string what, int track = -1);
    void not_a_step();
    void hold_step(std::string what, int track = -1);
    void release_step();
    bool step_held() const { return holding_step; }
    // What changed since the last close, kept as a step (or taken in, with
    // `keep` false, or when not_a_step() was said); the app's and the
    // service thread's. `fallback`: the step's name when no screen gave one.
    bool close_step(const std::string& fallback = {}, bool keep = true);
    bool undo(); // the newest step taken back, the engine and modules told; false: none
    bool redo();
    History history;
    MoveExtras& extras() { return move_extras; }
    const MoveExtras& extras() const { return move_extras; }
    std::string last_step; // the step undone or redone last, for the status line
    unsigned step_us = 0, step_us_peak = 0; // the last close's time and the longest (MIDI thread)

    // Messages, saving.
    void say(std::string text);
    std::string message;
    Clock::time_point message_until {};
    void changed(); // the project changed: saved three seconds on
    bool dirty = false;
    unsigned saves = 0;
    std::atomic<float> volume {0.8f};
    bool shift = false;

    // ---------------------------------------------------------- projects (projects.h)
    // The Set Overview's jobs, done on the service thread in turn: under
    // `lock` a screen asks (ask_projects()), the service thread does it and
    // reads the folder again (`listing`).
    struct ProjectJob {
        enum class Kind : uint8_t { List, Open, New, SaveAs, Save, Rename, Duplicate, Delete, MoveTo, NewFolder, LoadKit, Copy, Colour } kind = Kind::List;
        std::string path, name;  // the project or folder; a name, a folder (MoveTo), the target (Copy)
        int pad = -1;            // where a new one lies
        uint8_t colour = 0;
        bool play = false;       // Open: and play it (the overview's Play, the Move's preview)
        int track = -1;          // Open, New: then this track, on Play (a track button opened it)
    };
    void ask_projects(ProjectJob job);
    std::string projects_root() const;           // <home>/Projects
    std::string listing_folder;                  // the folder in view, under the root ("" the top)
    projects::Folder listing;                    // as read last
    unsigned listings = 0;                       // how many times read, for screens waiting on one
    std::string project_message;                 // what the last job said ("" fine)
    Settings settings;
    void settings_changed();                     // Setup changed a setting: kept, and the audio thread told
    // Opening a project: the Loading sheet's counts.
    bool opening = false;
    std::string opening_name, opening_file;
    unsigned opening_samples = 0, opening_samples_done = 0, opening_synths = 0, opening_synths_done = 0;
    std::string project_name() const;            // the open project's, from its file

    // ---------------------------------------------------------- service thread
    void service();
    std::string restore();  // the project's samples and modules read (the first service() does it)
    std::string save_now(); // empty on success
    // A project opened (`fresh`: a new one at `file`), the one open saved
    // first; the sound fades out, the audio thread waits while the project
    // is built (park()), fades back in; then its samples and modules are read
    // with the Loading sheet up. An MPC project (.xpj) opens as a copy, not
    // saved until it is a .mldp beside it. Empty on success.
    std::string open_project(const std::string& file, bool fresh = false);
    // Saved now, the sample slots nothing can play again freed (Melodee's Save).
    std::string save_project(const std::string& as = {});
    // The first kit of the project at `file` onto the selected track, its
    // patterns left (Melodee's Load kit): one undo step.
    std::string load_kit(const std::string& file);
    std::string load_now(const std::string& path, unsigned pad); // onto the selected kit, waited for
    std::string load_module_now(unsigned track, const std::string& id, const SynthFile* saved = nullptr);
    const std::string& project_path() const { return path; }
    const std::string& home_folder() const { return home_path; }
    // The engine made whole again from the project (an undo outside the
    // tracks, a project opened): the audio thread parked, faded out and
    // silent, while it is rebuilt, then faded back in. Never under `lock`.
    std::string rebuild();
    unsigned rebuilds = 0;
    std::string library;
    std::string status() const;
    // MIDI thread: whether a pad pressed now stays quiet, its job another
    // than its sound (bindings::pad_sounds()); the audio thread reads it.
    void quiet_pads(bool quiet) { pads_quiet.store(quiet, std::memory_order_relaxed); }

private:
    struct Parked { enum Result { Yes, NoAudio, Failed } result = Failed; uint32_t serial = 0; };
    Parked park_engine(); // service thread
    Live live();                           // the project as the history compares it
    void apply_step(const Step& step, bool forward); // what an undo or redo changed, told to the engine and the modules
    void forward();                        // audio thread: the ring into the host
    void apply_track(unsigned track);      // the engine's track made whole again from the project's (TrackLoad)
    void setup_modules();                  // a ModuleTrack for each Schwung track of the project, offered
    ModuleTrack& make_module_track(unsigned track);
    void drop_module_track(unsigned track);
    void take_back();                      // what the audio thread handed back, freed
    void take_answers();
    void destroy_retired();
    std::string write(std::vector<uint8_t> bytes, std::string settings_text);
    std::vector<uint8_t> project_bytes() const;
    std::string settings_text() const;
    void read_settings();
    void run_project_job(const ProjectJob& job); // service thread
    unsigned read_missing_samples();             // service thread: the project's samples not in memory yet; how many are missing
    void setup_modules_from(const PluginStates& states); // under `lock`: a ModuleTrack for each Schwung track of the project
    void free_unused_slots();                    // under `lock`
    std::vector<ProjectJob> project_jobs;
    std::vector<std::unique_ptr<Sample>> samples_leaving; // another project's, until the audio thread lets them go
    uint8_t free_plugin_entry() const;

    std::unique_ptr<melodee::Project> song;
    std::unique_ptr<melodee::PatternBank> bank;
    move::SpscRing<melodee::Command, 1024> commands;
    std::vector<std::unique_ptr<Sample>> samples; // slot = index; null where nothing is read (yet)
    std::array<std::unique_ptr<ModuleTrack>, melodee::track_count> modules {};
    std::vector<std::unique_ptr<ModuleTrack>> modules_leaving; // their runners handed back first
    unsigned selected = 0;
    MoveExtras move_extras;
    std::string step_name;     // the open step's name (name_step())
    int step_track = -1;
    bool step_kept = true;     // false: the open step is no undo step (not_a_step())
    bool holding_step = false; // hold_step(): closes wait for release_step()
    bool click_on = false;
    unsigned click_volume = 70;
    std::string home_path, path, settings_path;
    std::string last_path;   // Move.settings' last= (read at start)
    unsigned last_track = 0; // and its track=
    bool restored = false;
    Clock::time_point changed_at {}, retry_at {};

    // The audio thread's: the track the pads play, how, and where each
    // pad's press went (its track and key), so its release and pressure
    // go there too.
    std::atomic<int> audio_track {0}, audio_kind {0}, audio_octave {0};
    std::atomic<bool> pads_typing {false};
    // The MIDI thread's word to the audio thread: a pad pressed now has
    // another job than its sound (Shift, Copy, Delete, Mute or a step held;
    // bindings::pad_sounds()), so it stays quiet.
    std::atomic<bool> pads_quiet {false};
    std::atomic<uint8_t> pad_response {1}; // Settings::response, for the audio thread's velocity
    std::atomic<bool> pad_pressure {true};
    // Parking (rebuild()): the service thread asks, the audio thread parks
    // the engine behind the commands before and says which park it was.
    std::atomic<bool> park_asked {false};
    std::atomic<uint32_t> park_serial {0};
    bool rebuild_wanted = false; // under `lock`: the engine rebuilt from the project, parked
    struct Went { int track = -1, key = -1; };
    std::array<Went, pad_count> pad_went {};
    // Last, so it goes first: it holds the samples and runners above.
    std::unique_ptr<melodee::EngineHost> engine;
};

} // namespace app
