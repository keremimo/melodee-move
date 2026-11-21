// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/canvas_page.h"
#include "app/keys.h"
#include "app/module_host.h"
#include "app/pad_layout.h"
#include "app/project_store.h"
#include "move/app.h"
#include "move/queues.h"
#include "melodee/engine/auto_chop.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/onsets.h"
#include "melodee/engine/root_find.h"
#include "melodee/shell/auto_sample.h"
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

struct Instrument; // app/instruments.h

// A sample in memory, with what Chop needs found in it.
struct Sample {
    std::string name, path;
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
    ModuleTrack* module_track(unsigned track);
    ModuleTrack* module_track() { return module_track(selected); }
    int octave(unsigned track) const; // the bottom-left pad's octave on a note track (0: C2)
    void set_octave(unsigned track, int octave);
    // The physical pad's note: rows of fourths (Rows) or 1-32 up the
    // semitones (Banks), C2 at the bottom left at octave 0.
    int note_for(unsigned pad, int octave) const;

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
    // A keygroup track's sound (docs/UI.md, "Instruments"): an instrument or
    // one sample on its pad 1, its files landing on a thread of their own.
    std::string install_keys(std::unique_ptr<Sample> sample, unsigned track); // empty on success
    const Sample* keys_sample(unsigned track) const { return sample_of(track, 0); }
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

    // Messages, saving.
    void say(std::string text);
    std::string message;
    Clock::time_point message_until {};
    void changed(); // the project changed: saved three seconds on
    bool dirty = false;
    unsigned saves = 0;
    std::atomic<float> volume {0.8f};
    bool shift = false;

    // ---------------------------------------------------------- service thread
    void service();
    std::string restore();  // the project's samples and modules read (the first service() does it)
    std::string save_now(); // empty on success
    std::string load_now(const std::string& path, unsigned pad); // onto the selected kit, waited for
    std::string load_module_now(unsigned track, const std::string& id, const SynthFile* saved = nullptr);
    const std::string& project_path() const { return path; }
    const std::string& home_folder() const { return home_path; }
    std::string library;
    std::string status() const;
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
    // A note track's sound and playing (docs/UI.md, "Instruments"): the
    // macro knobs of a module track (engine/macros.h: FILTER -100..100, ECHO
    // and SPACE 0..100), its mod wheel (0..127) and pitch bend (-64..63, not
    // kept), Keys & Scale (the project's key and scale, the track's mode,
    // rows and transpose), CHORD, a progression (Prog) and a chord stored
    // into it. Under `lock`; each sends what the engine needs.
    void set_macro(unsigned track, unsigned slot, int value);
    void set_wheel(unsigned track, int value);
    void set_bend(unsigned track, int value);
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

private:
    void forward();                        // audio thread: the ring into the host
    void apply_track(unsigned track);      // the engine's track made whole again from the project's (TrackLoad)
    void sync_keyed_roots();               // a keygroup of one sample plays from its note (KeyedRoot)
    std::array<int16_t, melodee::track_count> keyed_sent {};
    unsigned free_slot() const;
    void setup_modules();                  // a ModuleTrack for each Schwung track of the project, offered
    ModuleTrack& make_module_track(unsigned track);
    void drop_module_track(unsigned track);
    void take_back();                      // what the audio thread handed back, freed
    void take_answers();
    void destroy_retired();
    std::string write(std::vector<uint8_t> bytes, std::string settings_text);
    std::vector<uint8_t> project_bytes() const;
    std::string settings_text() const;
    uint8_t free_plugin_entry() const;

    std::unique_ptr<melodee::Project> song;
    std::unique_ptr<melodee::PatternBank> bank;
    move::SpscRing<melodee::Command, 1024> commands;
    std::vector<std::unique_ptr<Sample>> samples; // slot = index; null where nothing is read (yet)
    std::array<std::unique_ptr<ModuleTrack>, melodee::track_count> modules {};
    std::vector<std::unique_ptr<ModuleTrack>> modules_leaving; // their runners handed back first
    unsigned selected = 0;
    bool click_on = false;
    unsigned click_volume = 70;
    std::string home_path, path, settings_path;
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
    struct Went {
        int track = -1, key = -1;
        PadMap::Route route = PadMap::Route::None; // a note track's: its notes, the arp's or Note Repeat's keys
        uint8_t count = 0;
        std::array<uint8_t, most_keys> keys {};
    };
    move::TripleBuffer<PadMap> pad_maps; // the MIDI thread's to write, the audio thread's to read
    std::atomic<bool> full_level {false};
    std::array<int8_t, melodee::track_count> shifts {};
    void note_pad(const move::Input& input, Went& went, int track); // audio thread
    Went chord_held;                     // CHORD's hold: the chord sounding on (audio thread)
    std::array<Went, pad_count> pad_went {};
    // Last, so it goes first: it holds the samples and runners above.
    std::unique_ptr<melodee::EngineHost> engine;
    // After it, so they end before it: the instruments' files landing, the
    // auto sampler (its own module's instance).
    std::unique_ptr<Landings> landings;
    std::unique_ptr<AutoSampler> sampler;
    void auto_sample_tick(); // service thread: a program made goes onto its track
};

} // namespace app
