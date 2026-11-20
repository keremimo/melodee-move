// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/kit_player.h"
#include "app/module_host.h"
#include "app/project_store.h"
#include "move/app.h"
#include "melodee/engine/auto_chop.h"
#include "melodee/engine/onsets.h"
#include "melodee/engine/sample_data.h"
#include "melodee/model/chop_slices.h"
#include "melodee/model/sampler.h"
#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Melodee on the Move, first scope (docs/UI.md): one kit track of 32 pads
// on Melodee's own sampler and Chop, in the Rail design.
//
//   Play  the last pad's sample; knobs 1-8: tune, level, pan, start, end,
//         attack, decay, cutoff. Touch a knob to see it large.
//   Load  (Menu) sound files (WAV, AIFF, FLAC, Ogg, MP3) onto the last pad,
//         from the Move's own storage: your samples, your recordings,
//         Ableton's Core Library, or anywhere in /data/UserData.
//   Chop  (Loop) the last pad's sample: Auto, Beats, Regions or Threshold;
//         the pads play the slices; the jogwheel or Left/Right picks one;
//         knobs: its start, its end, the type, the amount. Push the
//         jogwheel for To Pads: Poly, Mono or Thru, onto the first empty
//         pads. Back leaves without changing the pads.
//
// Track 2 (Row 2) is a synth: one of Schwung's sound generators, loaded
// from the Move's Schwung install (module_host.h). Its pads are notes in
// rows of fourths, C2 (note 48) at the bottom left; Up and Down move an octave;
// pressure is polyphonic aftertouch. Knobs 1-8 are the module's page,
// Left/Right its pages, the jogwheel its presets. Menu lists the modules.
// Push the jogwheel for the module's menu (its ui_hierarchy): turn to a
// row, push to edit a value with the jogwheel or to open a level or a list
// (a bank, a soundfont) to choose from; Back goes up, then closes it.
// Row 1 is the kit again.
//
// The kit is saved as a Melodee project (project_store.h) in
// `home`/Projects/Move.mldp three seconds after the last change, as Melodee
// autosaves on the MPC, and when the app quits; it is loaded again at start.
//
// Pads sound from audio_event(), in the cycle they were read. The screen,
// the MIDI thread and the service thread share the model under one lock;
// the audio thread never takes it (KitPlayer's queue).
namespace app {

class MelodeeApp : public move::App {
public:
    // `library`: the folder Load opens (the Move's own sample library by
    // default). `home`: Melodee's folder, its projects in Projects
    // (/data/UserData/Melodee on the Move by default).
    // `modules`: Schwung's modules folder (/data/UserData/schwung/modules
    // by default), its sound_generators the synths.
    explicit MelodeeApp(std::string library = {}, std::string home = {}, std::string modules = {});
    ~MelodeeApp() override; // saves what is not saved yet

    void render(const move::AudioBlock& input, move::AudioBlock& output) override; // audio thread
    void audio_opened(const move::AudioFormat& format) override;                    // audio thread
    void audio_event(const move::SurfaceEvent& event) override;                     // audio thread
    void surface(const move::SurfaceEvent& event) override;                         // MIDI thread
    void lights(move::Lights& lights, Clock::time_point now) override;              // MIDI thread
    void draw(juce::Graphics& g) override;                                          // screen thread
    void service() override;                                                        // service thread
    std::string status() const override;

    // For tests and tools: load a file onto a pad and wait for it; save the
    // project now; read it back now (what the first service() does).
    std::string load_now(const std::string& path, unsigned pad);
    std::string save_now();
    std::string restore_now();
    unsigned slice_count() const;
    // Load one of Schwung's modules onto the synth track and wait for it;
    // `saved` (a restore) sets it as it was. Empty on success.
    std::string load_module_now(const std::string& id, const SynthFile* saved = nullptr);
    std::string synth_value(const std::string& key) const; // as the screen has it
    const std::string& project_path() const { return project; }

private:
    enum class Page : uint8_t { Play, Load, Chop, ToPads };
    enum class Track : uint8_t { Kit, Synth };
    struct Sample {
        std::string name, path;
        std::unique_ptr<melodee::SampleData> data;
        melodee::Overview overview {};
        std::array<melodee::Hit, melodee::max_hits> hits {};
        unsigned hit_count = 0;
        melodee::BeatGrid beats;
        melodee::AutoRead autos;
    };
    struct Entry { std::string name; bool folder = false; };
    struct Knob { std::string name, value; float fraction = 0; };

    // All under `lock`.
    void list_folder();
    void select_pad(unsigned pad);
    void set_pad(unsigned pad, const melodee::PadSound& sound);
    void enter_chop();
    void leave_chop();
    void chop_again();
    void play_slices();
    void slices_to_pads();
    void turn_knob(unsigned knob, int delta, bool fine);
    Knob knob(unsigned knob) const;
    const Sample* sample_of(unsigned pad) const;
    void say(std::string text);
    std::string install(std::unique_ptr<Sample> sample, unsigned pad);
    void put_sample(std::unique_ptr<Sample> sample, uint16_t slot, const melodee::SampleChop& chop);
    void changed();
    void remember_chop();
    KitFile snapshot() const;               // the kit as it stands, to save outside the lock
    SynthFile synth_snapshot() const;
    std::string write(const KitFile& kit, const SynthFile& synth); // to `project`, the folders made first
    // The synth track.
    int synth_note(unsigned pad) const;     // the note a pad plays
    void set_track(Track to);
    void turn_synth_knob(unsigned knob, int delta, bool fine);
    Knob synth_knob(unsigned knob) const;
    void synth_page_by(int way);
    void preset_by(int way);
    void refresh_synth();                   // ask the module what its page's knobs and preset are now
    void take_answers();
    void synth_set(const std::string& key, const std::string& value); // remembers when, so an older answer is not taken
    void destroy_retired();
    // The module's menu.
    struct MenuRow {
        enum class Kind : uint8_t { Preset, Param, Level, Item } kind = Kind::Param;
        std::string key, label;
        int index = 0; // an Item's
    };
    std::vector<MenuRow> menu_rows() const;
    std::string menu_text(const MenuRow& row, bool editing) const;
    void open_menu();
    void enter_level(const std::string& id);
    void menu_turn(int delta, bool fine);
    void menu_push();
    void menu_back();

    void draw_play(juce::Graphics& g);
    void draw_load(juce::Graphics& g);
    void draw_chop(juce::Graphics& g);
    void draw_to_pads(juce::Graphics& g);
    void draw_touched(juce::Graphics& g);
    void draw_synth(juce::Graphics& g);
    void draw_modules(juce::Graphics& g);
    void draw_menu(juce::Graphics& g);

    mutable std::mutex lock;
    KitPlayer player;
    std::vector<std::unique_ptr<Sample>> samples; // slot = index (null where a file was missing); kept for the run
    std::vector<melodee::SampleChop> chops;       // each slot's chop, as Chop left it
    std::array<melodee::PadSound, pad_count> pads {};
    unsigned pad = 0;
    Page page = Page::Play;
    bool shift = false;
    // Load: the places it opens on (the Move's own sample folders and the
    // Core Library, or the one --samples names), then folders within one.
    struct Place { std::string name, path, tag; };
    std::vector<Place> places;
    int place = -1;            // -1: the list of places
    std::string library, folder;
    std::vector<Entry> entries;
    int cursor = 0;
    void open_place(int index);
    struct Job { std::string path; unsigned pad; };
    std::vector<Job> jobs;
    bool loading = false;
    // Chop
    melodee::ChopMode mode = melodee::ChopMode::Auto;
    std::array<int, unsigned(melodee::ChopMode::Count)> amounts {50, 16, 1, 0, 1}; // Threshold %, Regions, Beats division, -, Auto size
    melodee::Slices slices;
    unsigned selected = 0;
    int to_pads = 0;
    // A touched knob, and a message, each for a moment.
    int touched = -1;
    Clock::time_point touched_until {}, message_until {};
    std::string message;
    // Saving.
    std::string home, project;
    bool dirty = false, restored = false;
    Clock::time_point changed_at {}, retry_at {};
    unsigned saves = 0;

    // The synth track. `module` is what the screen and knobs show; the
    // audio thread plays the same one through `synth`. A module replaced
    // waits in `leaving` until the audio thread has let it go.
    Track track = Track::Kit;
    SynthPlayer synth;
    std::string modules_root;
    std::vector<ModuleInfo> catalog;
    int module_cursor = 0;
    std::unique_ptr<LoadedModule> module;
    std::vector<std::unique_ptr<LoadedModule>> leaving;
    std::string module_job;
    bool module_loading = false;
    unsigned synth_page = 0;
    int octave = 0;
    std::array<bool, pad_count> held {}; // pads down, for their lights
    SynthFile saved_synth;                // read at start, for the module it names
    bool menu_open = false, menu_editing = false;
    std::vector<std::string> menu_path;        // level ids, root first
    int menu_cursor = 0;
    std::vector<std::pair<int, std::string>> pick_items; // an items list, as the module gave it
    std::string pick_key;                      // the list being read, until it is
    bool pick_reading = false;
    std::map<std::string, uint64_t> set_at; // each key's last set (SynthPlayer's serial)
    unsigned slow_calls = 0;              // calls into a module that held the audio thread 1 ms or more
    uint32_t slowest_us = 0;

    // The audio thread's: where each pad's press went (a synth note, or -1:
    // the kit), so its release and pressure go there too.
    std::atomic<int> audio_track {0}, audio_octave {0};
    std::array<int, pad_count> pad_went {};

    std::atomic<float> volume {0.8f};
};

} // namespace app
