// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/canvas_page.h"
#include "app/fx_host.h"
#include "app/mixing.h"
#include "app/midi.h"
#include "app/loop_desk.h"
#include "app/history.h"
#include "app/projects.h"
#include "ui/track_style.h"
#include <functional>
#include "app/keys.h"
#include "app/module_host.h"
#include "app/schwung_pages.h"
#include "app/user_presets.h"
#include "app/pad_layout.h"
#include "app/project_store.h"
#include "move/app.h"
#include "move/queues.h"
#include "melodee/engine/auto_chop.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/onsets.h"
#include "melodee/engine/root_find.h"
#include "melodee/engine/sample_data.h"
#include "melodee/model/project.h"
#include "melodee/shell/sample_cache.h"
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
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
namespace melodee { struct AutoSampleSettings; } // melodee/shell/auto_sample.h

namespace app {

constexpr unsigned pad_count = 32; // the Move's pads: on a kit, two of its eight banks (Studio::pad_bank())

// What a track is, as the Move shows it: Melodee's engines, a PLUGIN track
// split by what it plays (one of Schwung's modules, or another plug-in).
enum class TrackKind : uint8_t { Empty, Kit, Synth, Keys, Midi, Audio, Plugin, Count };
const char* kind_name(TrackKind kind); // "empty", "kit", "synth", ...
const char* kind_tag(TrackKind kind);  // the rail's: "----", "KIT", "SYN", "KEYS", "MIDI", "AUD", "PLUG"

struct Instrument; // app/instruments.h

// A sample in memory, with what Chop needs found in it. A long one (over
// ten seconds, up to six hours), or one memory has no room for, is not read
// into memory: it is read from storage as it plays (`streamed`, Melodee's
// engine/stream_pager.h), its waveform read from the file once.
struct Sample {
    std::string name, path;
    bool streamed = false;
    std::unique_ptr<melodee::SampleData> data;
    melodee::Overview overview {};
    std::array<melodee::Hit, melodee::max_hits> hits {};
    unsigned hit_count = 0;
    melodee::BeatGrid beats;
    melodee::AutoRead autos;
    std::shared_ptr<Instrument> instrument; // a multisampled instrument's (app/instruments.h): its zones and files
    melodee::SampleRoot root;               // the note it holds (engine/root_find.h), found as it is read
};
// Reads a sound file and finds its hits, beats and what Auto reads. Slow
// and allocating: never on the audio thread, never under the lock. As
// Melodee's SampleLibrary: refused ("Memory full") when what it would take
// leaves less than 256 MB free (shell/sample_memory.h); a long WAV read in
// place; a long MP3, FLAC, Ogg or AIFF decoded once into `cache` (Melodee/Cache,
// shell/sample_cache.h) and read in place from there.
std::unique_ptr<Sample> read_sample(const std::string& path, std::string& error, const melodee::SampleCache* cache = nullptr);
// The same for frames already in memory (a take, Audio Recall), kept as `path`.
std::unique_ptr<Sample> make_sample(const std::string& path, std::unique_ptr<melodee::SampleData> data);

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
    std::unique_ptr<SchwungPages> schwung_pages;           // its pages by Schwung's own code (schwung_pages.h)
    std::string user_preset;        // the user preset loaded or saved last (user_presets.h); empty: none
    bool user_preset_dirty = false; // a value set since
    SynthFile saved;            // as the project file has it: kept while the module is missing
    std::string job;            // a module to load (the service thread's)
    bool job_restores = false;  // set as `saved` has it (a project opened, a track copied)
    bool loading = false;
    std::map<std::string, uint64_t> set_at; // each key's last set (SynthPlayer's serial)
    std::vector<std::string> edited;        // the keys set since the last undo step closed (history.h)
    ~ModuleTrack() { player.stop(); } // a source's thread ends before the modules it plays go
};

class Screen;
class Sequencer;

// The Move's Setup (Shift + Step 2) as Melodee's Settings: the same for every
// project, kept in Move.settings with the project to open at start.
struct Settings {
    uint8_t response = 1;    // pad response: 0 soft, 1 medium, 2 hard, 3 full level
    bool pressure = true;    // pad pressure reaches the modules
    uint8_t pad_lights = 0;  // 0 the tracks' colours, 1 white, 2 off
    uint8_t pad_fade = 1;    // 150, 250, 400, 600 ms: how long a pad's light takes to fade down
    uint8_t brightness = 3;  // the lights: 0 low, 1 mid, 2 high, 3 max (the Move's four)
    bool light_show = false;
    uint8_t show_style = 0;  // Pulse, Sweep, Chase, Ripple, Levels
    bool usage_log = false;  // what the hands do, a file each start (Usage/)
    std::string start;       // the project opened at start; empty: the one open last
    bool operator==(const Settings& o) const {
        return response == o.response && pressure == o.pressure && pad_lights == o.pad_lights && pad_fade == o.pad_fade &&
               brightness == o.brightness && light_show == o.light_show && show_style == o.show_style &&
               usage_log == o.usage_log && start == o.start;
    }
};
constexpr unsigned pad_fade_ms[4] {150, 250, 400, 600};
constexpr const char* show_styles[5] {"PULSE", "SWEEP", "CHASE", "RIPPLE", "LEVELS"};
class SongDesk;
class Sampling;
class Landings;    // app/instruments.h
class AutoSampler; // app/auto_sampler.h

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
    // The physical pad's note: rows of fourths (Rows) or 1-32 up the
    // semitones (Banks), C2 at the bottom left at octave 0.
    int note_for(unsigned pad, int octave) const;

    // A kit track's pads: 128 in eight banks of 16 (A-H, melodee::sampler_pads),
    // the Move's 32 two of them, Track::bank the lower (Plus and Minus move it
    // 16 pads, docs/MOVE_CONTROLS.md). With 16 Pitches on (Melodee's 16 Levels,
    // Track::levels) the left 16 are bank Track::bank laid out as the MPC's
    // (bottom left first, rows of four) and the right 16 the last pad up the
    // project's key. pad() is the kit pad edited (0-127).
    unsigned pad() const { return song->tracks[selected].last_pad % melodee::sampler_pads; }
    void select_pad(unsigned pad); // a kit pad, 0-127
    int kit_pad_at(unsigned move_pad) const;   // the kit pad under Move pad 0-31; -1 a pitch of 16 Pitches
    int move_pad_of(unsigned kit_pad) const;   // the Move pad showing a kit pad; -1 out of view
    unsigned pad_bank() const { return song->tracks[selected].bank; }
    void set_pad_bank(int bank);               // the lower bank in view, 0..6 (0..7 with 16 Pitches)
    // The first kit pad in view: the bank's first, moved four pads at a time by
    // Shift + Plus / Minus (the Move's Control Live: four pads). The bank is
    // kept with the kit (Track::bank); the four pads past it only for the run.
    unsigned pad_first() const;
    void set_pad_first(int first);
    unsigned pads_in_view() const { return pitches() ? 16u : pad_count; }
    bool pitches() const;                      // 16 Pitches on the selected kit
    void set_pitches(bool on);
    void set_pitches_octave(int octave);       // melodee's kit Levels octave, kit_level_octave as it is
    int pitch_semitones(unsigned pitch) const; // pitch 0-15 (bottom left first): its semitones from the pad's own
    void refresh_pads();                       // the audio thread's copy of the above, after anything moved them
    // While Chop is open the engine's kit pads 0-31 are its slices: the pads
    // play them by their number in the pad layout, whatever bank is in view.
    void pads_play_slices(bool on) { audio_slices.store(on, std::memory_order_relaxed); }
    // Copy the sound of one pad onto another (its sample and values, chain,
    // mute, zone trims). Clear: the pad's notes in the selected pattern, or
    // with none its sound, or an empty pad's gap closed (the pads after it
    // down one, notes too: melodee's shell/pad_gap.h).
    bool copy_pad(unsigned track, unsigned from, unsigned to);
    enum class Cleared { Nothing, Notes, Emptied, GapClosed };
    Cleared clear_pad(unsigned track, unsigned pad);
    bool delete_pad_notes(unsigned track, unsigned pad); // in the playing pattern; false: none there
    // Which kit pads sound now, from the engine's taps of the selected track
    // (the newest voices); for the pads' flare.
    std::array<bool, melodee::sampler_pads> sounding() const;
    const Sample* sample_of(unsigned track, unsigned pad) const;
    const Sample* sample_of_pad() const { return sample_of(selected, pad()); }
    void set_pad(unsigned track, unsigned pad, const melodee::PadSound& sound); // the project's and the engine's
    void sound_pad(unsigned track, unsigned pad, const melodee::PadSound& sound); // the engine's only (Chop's slices)
    void sound_pads(unsigned track);                                             // the engine's back to the project's
    melodee::SampleChop* chop_of(unsigned slot);
    const Sample* sample_in(unsigned slot) const { return slot < samples.size() ? samples[slot].get() : nullptr; }
    void set_kit_param(unsigned track, int param, int value); // a kit-wide value (melodee::sampler::Param)
    // The layer the next sample loaded onto a pad goes on (LAYERS, kit_pages.cpp): 0 the pad's own.
    struct LayerTarget { int track = -1, pad = -1, layer = 0; };
    LayerTarget layer_target;
    // `keep`: the pad keeps its values, only its sample and region new (Trim Sample).
    std::string install(std::unique_ptr<Sample> sample, unsigned track, unsigned pad, bool keep = false); // empty on success
    // A sample into the project's first free slot, offered to the engine: the slot, or -1 and why.
    int add_sample(std::unique_ptr<Sample> sample, std::string& error);
    const melodee::SampleData* sample_of_slot(unsigned slot) const;
    // A keygroup track's sound (docs/UI.md, "Instruments"): an instrument or
    // one sample on its pad 1, its files landing on a thread of their own.
    std::string install_keys(std::unique_ptr<Sample> sample, unsigned track); // empty on success
    const Sample* keys_sample(unsigned track) const { return sample_of(track, 0); }
    struct Job { std::string path; unsigned track, pad; bool keep = false; };
    std::vector<Job> jobs; // files to load (the service thread's)
    // A pad's region (or a layer's) written as a WAV, on the service thread
    // (Sample Options): Trim Sample (Samples/Edits/<NAME>-Trimmed.wav, then the
    // pad onto all of it) and Save Sample (Samples/<name>.wav, the pad as it was).
    struct FileJob { bool trim = false; unsigned track = 0, pad = 0; int layer = 0; std::string path; };
    std::vector<FileJob> file_jobs;
    std::string samples_folder() const; // Melodee's Samples, under its home
    // Kits and banks from files, onto the selected kit (Melodee's
    // shell/project_kits.h): an MPC drum program (.xpm, .xty) whole or a bank
    // of it into a bank (`from_bank` -1: the whole kit), a bank file (.mldb)
    // into a bank; Save Bank writes Samples/Banks/<name>.mldb. Their files
    // are read by the service thread (`slots_wanted`). Empty on success.
    std::string load_program(const melodee::Project& program, int from_bank, unsigned into_bank);
    std::string load_bank_file(const std::string& path, unsigned bank);
    std::string save_bank_file(unsigned bank, const std::string& name);
    int slot_for(const melodee::SampleRef& ref); // this project's slot for a file, a new one queued to be read; -1 none free
    std::vector<unsigned> slots_wanted;
    std::string program_path; // the MPC drum program the LOAD KIT sheet loads (load_kit.cpp)
    // Typing for Melodee's own use (Save Sample's name): ask_name() below with
    // the usual room; `done` with the text unless cancelled.
    void ask_text(std::string title, std::string initial, std::function<void(const std::string&)> done);
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
    CanvasPage::Host fx_host(uint8_t entry); // a Schwung effect's (studio_fx.cpp)
    static constexpr int typing_schwung_pages = -2; // typing_canvas: the text is Schwung's pages' (typing_pages)
    // A module whose pages Schwung's code has (schwung_pages.h): a track's
    // synth, or a Schwung effect (its plug-in entry).
    struct PagesTarget {
        unsigned track = 0;
        uint8_t fx_entry = melodee::no_plugin;
        bool operator==(const PagesTarget& o) const { return track == o.track && fx_entry == o.fx_entry; }
    };
    PagesTarget pages_target;                   // whose pages screens/module_pages.cpp shows
    PagesTarget typing_pages;                   // whose keyboard is open
    std::optional<PagesTarget> swap_from_pages;   // a Swap Module asked from its pages
    std::optional<PagesTarget> remove_from_pages; // a Remove Module asked from its pages
    SchwungPages* pages_of(PagesTarget target);
    std::unique_ptr<SchwungPages> start_pages(CanvasPage::Host host, const LoadedModule& module, const std::string& dir,
                                              const std::vector<std::pair<std::string, std::string>>& kept, PagesTarget target);
    std::string pages_trailing(PagesTarget target, bool instrument);
    void pages_action(PagesTarget target, const std::string& action);
    void remove_module(PagesTarget target); // Remove Module: the synth's module (the track stays, empty) or the effect
    // My Presets (user_presets.h), on a synth's module: one loaded (its state
    // set, every value asked again), the current one saved over, or saved
    // under a new name (a number on where it is taken).
    void load_user_preset(PagesTarget target, const UserPreset& preset);
    void save_user_preset_as(PagesTarget target, const std::string& name, bool overwrite);
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
    // The pads held, bit n pad n (physical), as the runtime saw them when
    // the lights were last asked for (MelodeeApp::lights()). For the lights.
    uint32_t pads_held = 0;
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
    // The Volume knob turned (under `lock`): the main volume kept in
    // Move.settings three seconds on, the project left as it is (no UNSAVED).
    void volume_changed();
    bool shift = false;

    // The Looper (app/loop_desk.h, docs/UI.md "Looper"): four loops on
    // tracks L1-L4. loop_pads(): the MIDI thread's word to the audio thread
    // that a Looper screen is on top (its left 16 pads the loops' rows).
    LoopDesk& loops() { return *loop_desk; }
    const LoopDesk& loops() const { return *loop_desk; }
    void loop_pads(bool shown, bool free); // free: no modifier but Shift held
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
    // Files changed from outside (the web page's uploads, renames, deletes, folders): the browser lists its folder
    // again, the projects their folder (`melodee-files-changed`). Under `lock`.
    unsigned file_changes = 0;
    void files_changed();
    std::string project_message;                 // what the last job said ("" fine)
    Settings settings;
    void settings_changed();                     // Setup changed a setting: kept, and the audio thread told
    // Move.settings' lines this file does not read (another area's: MIDI
    // Config, the pad layout), by their first word (up to a space or '='),
    // the whole line kept and written back unless settings_text() already
    // wrote that word. An area may set its own here too.
    std::map<std::string, std::string> settings_more;
    // Called on the service thread, outside `lock`, before the open project
    // is saved and another swapped in, the audio thread still running: what
    // an area still has to write into the old one (the Looper's playing loops).
    std::vector<std::function<void()>> closing;
    // Called under `lock` before every save's bytes are made: what an area
    // keeps in the project first (the Looper's values).
    std::vector<std::function<void()>> saving;
    // Called under `lock` once another project is in (opened or new), its path given: an area's reset (the Looper's loops).
    std::vector<std::function<void(const std::string& path)>> opened;
    // Opening a project: the Loading sheet's counts.
    bool opening = false;
    std::string opening_name, opening_file;
    unsigned opening_samples = 0, opening_samples_done = 0, opening_synths = 0, opening_synths_done = 0;
    std::string project_name() const;            // the open project's, from its file

    // ---------------------------------------------------------- service thread
    void service();
    // The run is ending, the audio thread still running: the loops' files
    // and the project written now (the runtime's App::stopping()).
    void stopping();
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
    // MIDI and sync (midi.h): MIDI Config, each track's MIDI OUT, the port.
    Midi midi;
    // Sampling (src/app/sampling.h): Melodee's Sample Record, Input Config and Audio Recall.
    Sampling& sampling() { return *sampler_shell; }
    const Sampling& sampling() const { return *sampler_shell; }
    // MIDI thread: the pads play track `track` (-1: the selected one, as ever), whatever the
    // screen gives them to do: a take of Melodee's own sound plays the source from the pads.
    void pads_play(int track);
    // MIDI thread: the output quiet (the mic heard through the speaker would howl).
    void quiet_speaker(bool quiet) { speaker_quiet.store(quiet, std::memory_order_relaxed); }
    // MIDI thread: whether a pad pressed now stays quiet, its job another
    // than its sound (bindings::pad_sounds()); the audio thread reads it.
    void quiet_pads(bool quiet) { pads_quiet.store(quiet, std::memory_order_relaxed); }
    // The selected track's pads as Keys & Scale lays them out (app/keys.h),
    // to the audio thread: whenever the model may have changed (each lights
    // pass, a track selected). Under `lock`.
    void publish_pads();
    // Shift + Step 10, the Move's Full Velocity: every pad at 127.
    bool full_velocity() const { return full_level.load(std::memory_order_relaxed); }
    void set_full_velocity(bool on) { full_level.store(on, std::memory_order_relaxed); publish_pads(); }
    // Shift + Plus / Minus: a note track's pads a note of the key up or down
    // (Control Live's scale step); the Move's, not kept in the project.
    int key_shift(unsigned track) const { return shifts[track % melodee::track_count]; }
    // A note track's playing (docs/UI.md, "Instruments"): Keys & Scale (the
    // project's key and scale, the track's mode, rows and transpose), CHORD,
    // a progression (Prog) and a chord stored into it. Under `lock`; each
    // sends what the engine needs. A module track's knobs are the module's:
    // Melodee's FILTER, ECHO and SPACE are not on the Move (a project's are
    // let go as it opens, drop_module_macros()).
    void set_key(unsigned root, unsigned scale);
    void set_keys(unsigned track, KeysMode mode, bool fourths);
    void set_transpose(unsigned track, int semitones);
    void changed_playing(unsigned track); // a CHORD or arp field set on the track: the pads and the engine follow
    std::string load_progression(unsigned track, const std::string& path); // empty on success
    std::vector<std::string> progressions() const;                        // every .progression, Melodee's own first
    // The keys held (lowest first, as many as were held) as chord `index` of
    // the track's progression, saved: into the file it came from when that
    // is Melodee's own, else as a new "Mine N". Empty on success.
    std::string store_chord(unsigned track, unsigned index, const std::vector<int>& keys);
    std::string progressions_folder() const; // <home>/Progressions: Melodee's own
    // The auto sampler (app/auto_sampler.h) on a module track: the check,
    // then (`sample`) the program, which goes on the first empty track as a
    // keygroup track with the module track's patterns and mix. Empty on
    // success (started).
    std::string auto_sample(unsigned track, const melodee::AutoSampleSettings& settings, bool sample);
    AutoSampler& auto_sampler() { return *sampler; }
    std::string keygroups_folder() const; // <home>/Samples/Keygroups, as Melodee keeps them
    int auto_sampled = -1;                // the track the last program went on
    // `melodee-autosample` as Melodee takes it (MELODEE.md, Print and freeze):
    // "TRACK [LOW-HIGH] [step N] [layers N] [hold S] [tail S] [pre|post]
    // [loop] [raw|peak] [check]". What it started, or why not.
    std::string autosample_command(const std::string& arguments);
    std::string instruments_status() const; // " keys=... auto=..." for the status line
    // The arp of a note track (Melodee's Stage ARP, the Move's Repeat and Arp),
    // its settings taken from the track and sent to the engine.
    void send_arp(unsigned track);
    void set_key_shift(unsigned track, int shift) { shifts[track % melodee::track_count] = int8_t(std::clamp(shift, -24, 24)); publish_pads(); }

    // ---------------------------------------------------------- the mix and effects, under `lock`
    // What the mix keeps beside the project (mixing.h: the effect copied,
    // the pads as punches or Flex Beat's), and Schwung's audio effects in
    // the chains (fx_host.h), each on an entry of the project's plug-ins.
    mixing::Desk desk;
    std::vector<ModuleInfo> fx_catalog; // modules/audio_fx
    void rescan_fx();
    // Schwung's effect `id` into slot `at` of chain `chain` (past the last
    // used one: the next free), loaded on the service thread; it passes the
    // sound through until then. False: no room, or no such effect.
    bool load_fx(unsigned chain, unsigned at, const std::string& id);
    // A copy of entry `entry`'s effect, its own instance set as it is.
    bool copy_fx(uint8_t entry, unsigned chain, unsigned at);
    void fx_released(uint8_t entry); // its slot let it go: handed back, then destroyed
    FxModule* fx_module(uint8_t entry);
    void fx_set(FxModule& fx, const std::string& key, const std::string& value);
    // The sequencer (sequencer.h): the Move's step sequencing, Loop Mode,
    // Session, Seq Record and Capture on Melodee's patterns. Under `lock`.
    Sequencer& sequencer() { return *seq; }
    const Sequencer& sequencer() const { return *seq; }
    // What the audio thread did with a pad's last press (audio_event()),
    // read by the MIDI thread when the same press reaches it: the track and
    // key it went to (also when the press stayed quiet), how hard, where the
    // track's pattern was, and whether it sounded. Seq Record writes the
    // note at the tick it was heard; a step held + the pad adds that key.
    // A pad that plays a chord (a note track's chord or Keys & Scale pad)
    // stamps each of its keys, `key` the first.
    struct PadStamp {
        static constexpr unsigned most = 8; // melodee::notes_per_step
        std::atomic<int> track {-1}, key {-1}, velocity {0}, position {-1};
        std::atomic<uint32_t> ticks {0};
        std::atomic<bool> sounded {false};
        std::array<std::atomic<int>, most> keys {}; // keys[0..count)
        std::atomic<unsigned> count {0};
    };
    const PadStamp& stamp(unsigned pad) const { return stamps[pad % pad_count]; }
    // MIDI thread: the track the pads play repeats its keys (1, Note Repeat)
    // or plays them through its arp (2; 3 latched, the let-go not sent), 0
    // neither; the audio thread sends the pad's key there instead of a note.
    // Sequencer::route_pads() sets it from the track.
    void pads_repeat(int how) { audio_repeat.store(how, std::memory_order_relaxed); }
    // The time signature as the modules read it (set_module_transport()).
    void signature_to_modules(unsigned beats, unsigned unit) {
        audio_beats.store(beats, std::memory_order_relaxed);
        audio_unit.store(unit, std::memory_order_relaxed);
    }

    // Song, its exports, print and freeze (song_desk.h, exports.h, prints.h):
    // made with the studio, its threads stopped first as it goes. (`desk`
    // is the mix's, mixing.h.)
    SongDesk& song_desk() { return *arranger; }
    const SongDesk& song_desk() const { return *arranger; }
    // Each Schwung module (a track's, an effect's) as its PLUG entry keeps it (under `lock`).
    PluginStates plugin_states() const;
    SynthFile module_state(unsigned track) const; // a Schwung track's module as it is now: values, preset, its pages' state

private:
    struct Parked { enum Result { Yes, NoAudio, Failed } result = Failed; uint32_t serial = 0; };
    Parked park_engine(); // service thread
    Live live();                           // the project as the history compares it
    void apply_step(const Step& step, bool forward); // what an undo or redo changed, told to the engine and the modules
    void forward();                        // audio thread: the ring into the host
    void apply_track(unsigned track);      // the engine's track made whole again from the project's (TrackLoad)
    void sync_keyed_roots();               // a keygroup of one sample plays from its note (KeyedRoot)
    std::array<int16_t, melodee::track_count> keyed_sent {};
    unsigned free_slot() const;
    void setup_modules();                  // a ModuleTrack for each Schwung track of the project, offered
    ModuleTrack& make_module_track(unsigned track);
    void drop_module_track(unsigned track);
    void take_back();                      // what the audio thread handed back, freed
    void run_file_jobs();                  // Trim Sample, Save Sample: regions written as WAVs
    void hold_streams();                   // the long samples' chunks where pads start, held in memory (shell/stream_holds.h)
    melodee::SampleCache cache;            // long MP3, FLAC, Ogg and AIFF as WAVs (home/Cache)
    bool cache_tidied = false;
    void take_answers();
    void destroy_retired();
    void setup_fx(const PluginStates& states); // the project's Schwung effects, to load
    void service_fx();                          // the service thread: each one asked for, loaded and offered
    std::unique_ptr<FxModule> take_back_fx(melodee::PluginRunner* runner); // the effect whose runner it is, if it was let go
    void fx_states(PluginStates& states) const;
    uint8_t reserve_fx_entry(const ModuleInfo& info);
    void release_chain_fx(const melodee::Chain& chain); // its Schwung effects let go (a track deleted)
    void copy_chain_fx(melodee::Chain& chain);          // each its own instance (a track copied)
    std::string write(std::vector<uint8_t> bytes, std::string settings_text);
    std::string write_settings(const std::string& text); // Move.settings alone
    std::vector<uint8_t> project_bytes() const;
    std::vector<uint8_t> bytes_to_save(); // the `saving` hooks, then project_bytes()
    std::string settings_text() const;
    void read_settings();
    void run_project_job(const ProjectJob& job); // service thread
    unsigned read_missing_samples();             // service thread: the project's samples not in memory yet; how many are missing
    // Under `lock`: a ModuleTrack for each Schwung track of the project (a deep-frozen one's module left unloaded).
    void setup_modules_from(const PluginStates& states, const melodee::PrintTable& prints);
    void free_unused_slots();                    // under `lock`
    std::vector<ProjectJob> project_jobs;
    std::vector<std::unique_ptr<Sample>> samples_leaving; // another project's, until the audio thread lets them go
    std::vector<std::unique_ptr<Sample>> samples_printing; // let go by the audio thread, kept while a print reads them
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
    bool settings_dirty = false; // Move.settings alone to write (volume_changed()); a project save writes it too
    Clock::time_point settings_at {};

    // The audio thread's: the track the pads play, how, and where each
    // pad's press went (its track and key), so its release and pressure
    // go there too.
    std::atomic<int> audio_track {0}, audio_kind {0}, audio_octave {0};
    // A kit's: the lower bank in view, 16 Pitches on, its pad and each pitch's semitones.
    std::atomic<int> audio_first {0}, audio_levels_pad {0}; // the first kit pad in view
    std::array<uint8_t, melodee::track_count> pad_nudge {}; // Shift + Plus / Minus: 0, 4, 8 or 12 pads past the bank's first
    // Shift + Step 10, the Move's Full Velocity: every pad at 127 (full_velocity(); the
    // kit's sends and a note track's pads read it on the audio thread).
    std::atomic<bool> full_level {false};
    std::atomic<bool> audio_pitches {false}, audio_slices {false};
    std::array<std::atomic<int>, 16> audio_semitones {};
    std::atomic<bool> pads_typing {false};
    // The MIDI thread's word to the audio thread: a pad pressed now has
    // another job than its sound (Shift, Copy, Delete, Mute or a step held;
    // bindings::pad_sounds()), so it stays quiet.
    std::atomic<bool> pads_quiet {false};
    std::atomic<int> audio_repeat {0};
    std::atomic<unsigned> audio_beats {4}, audio_unit {4}; // the time signature, for the modules' transport
    std::array<PadStamp, pad_count> stamps {};
    std::unique_ptr<Sequencer> seq;
    // Sampling's: the pads on a track other than the selected one (-1 none), and the speaker quiet.
    std::atomic<int> pads_track {-1}, pads_kind {0}, pads_octave {0};
    std::atomic<bool> speaker_quiet {false};
    std::unique_ptr<Sampling> sampler_shell;
    struct Went {
        int track = -1, key = -1, how = 0; // a kit's: how 1 Note Repeat, 2-3 the arp (audio_repeat)
        PadMap::Route route = PadMap::Route::None; // a note track's: its notes, the arp's or Note Repeat's keys
        uint8_t count = 0;
        std::array<uint8_t, most_keys> keys {};
    };
    move::TripleBuffer<PadMap> pad_maps; // the MIDI thread's to write, the audio thread's to read
    // (Full Velocity is `full_level`, above, as the kit's sends read it.)
    std::array<int8_t, melodee::track_count> shifts {};
    // Audio thread: a note track's pad. `quiet`: another job has the pads
    // (bindings::pad_sounds()): its notes are worked out, not sent.
    // `velocity`: Full Velocity's 127, else Setup's pad response (as a kit's pads have it).
    void note_pad(const move::Input& input, Went& went, int track, int16_t velocity, bool quiet);
    Went chord_held;                     // CHORD's hold: the chord sounding on (audio thread)
    std::atomic<uint8_t> pad_response {1}; // Settings::response, for the audio thread's velocity
    std::atomic<bool> pad_pressure {true};
    // Parking (rebuild()): the service thread asks, the audio thread parks
    // the engine behind the commands before and says which park it was.
    std::atomic<bool> park_asked {false};
    std::atomic<uint32_t> park_serial {0};
    bool rebuild_wanted = false; // under `lock`: the engine rebuilt from the project, parked
    std::array<Went, pad_count> pad_went {};
    // Schwung's effects by plug-in entry, and those handed back by the
    // audio thread waiting to be destroyed. Before `engine`, which holds
    // their runners and goes first.
    std::array<std::unique_ptr<FxModule>, melodee::max_plugins> fx {};
    std::vector<std::unique_ptr<FxModule>> fx_leaving;
    std::unique_ptr<LoopDesk> loop_desk; // made with the engine host, after the project is read
    int loop_latency_setting = LoopDesk::default_latency; // Move.settings' loop_latency, for the Looper made after
    std::unique_ptr<SongDesk> arranger; // song_desk()
    // Last, so it goes first: it holds the samples and runners above.
    std::unique_ptr<melodee::EngineHost> engine;
    // After it, so they end before it: the instruments' files landing, the
    // auto sampler (its own module's instance).
    std::unique_ptr<Landings> landings;
    std::unique_ptr<AutoSampler> sampler;
    void auto_sample_tick(); // service thread: a program made goes onto its track
};

} // namespace app
