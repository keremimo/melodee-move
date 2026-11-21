// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/canvas_page.h"
#include "app/module_host.h"
#include "app/pad_layout.h"
#include "app/project_store.h"
#include "move/app.h"
#include "move/queues.h"
#include "melodee/engine/auto_chop.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/onsets.h"
#include "melodee/engine/sample_data.h"
#include "melodee/model/project.h"
#include "melodee/shell/sample_cache.h"
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
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

constexpr unsigned pad_count = 32; // the Move's pads: on a kit, two of its eight banks (Studio::pad_bank())

// What a track is, as the Move shows it: Melodee's engines, a PLUGIN track
// split by what it plays (one of Schwung's modules, or another plug-in).
enum class TrackKind : uint8_t { Empty, Kit, Synth, Keys, Midi, Audio, Plugin, Count };
const char* kind_name(TrackKind kind); // "empty", "kit", "synth", ...
const char* kind_tag(TrackKind kind);  // the rail's: "----", "KIT", "SYN", "KEYS", "MIDI", "AUD", "PLUG"

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
};
// Reads a sound file and finds its hits, beats and what Auto reads. Slow
// and allocating: never on the audio thread, never under the lock. As
// Melodee's SampleLibrary: refused ("Memory full") when what it would take
// leaves less than 256 MB free (shell/sample_memory.h); a long WAV read in
// place; a long MP3, FLAC, Ogg or AIFF decoded once into `cache` (Melodee/Cache,
// shell/sample_cache.h) and read in place from there.
std::unique_ptr<Sample> read_sample(const std::string& path, std::string& error, const melodee::SampleCache* cache = nullptr);

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
    // Typing for Melodee's own use (Save Sample's name): `done` with the text unless cancelled.
    void ask_text(std::string title, std::string initial, std::function<void(const std::string&)> done);
    std::function<void(const std::string&)> typing_done;
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

private:
    void forward();                        // audio thread: the ring into the host
    void apply_track(unsigned track);      // the engine's track made whole again from the project's (TrackLoad)
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
    std::string write(std::vector<uint8_t> bytes, std::string settings_text);
    SynthFile module_state(unsigned track) const; // a Schwung track's module as it is now: values, preset, its pages' state
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
    // A kit's: the lower bank in view, 16 Pitches on, its pad and each pitch's semitones.
    std::atomic<int> audio_bank {0}, audio_levels_pad {0};
    std::atomic<bool> audio_pitches {false}, audio_slices {false};
    std::array<std::atomic<int>, 16> audio_semitones {};
    std::atomic<bool> pads_typing {false};
    // The MIDI thread's word to the audio thread: a pad pressed now has
    // another job than its sound (Shift, Copy, Delete, Mute or a step held;
    // bindings::pad_sounds()), so it stays quiet.
    std::atomic<bool> pads_quiet {false};
    struct Went { int track = -1, key = -1; };
    std::array<Went, pad_count> pad_went {};
    // Last, so it goes first: it holds the samples and runners above.
    std::unique_ptr<melodee::EngineHost> engine;
};

} // namespace app
