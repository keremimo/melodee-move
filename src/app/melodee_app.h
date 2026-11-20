// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/kit_player.h"
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
    explicit MelodeeApp(std::string library = {}, std::string home = {});
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
    const std::string& project_path() const { return project; }

private:
    enum class Page : uint8_t { Play, Load, Chop, ToPads };
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
    std::string write(const KitFile& kit);  // to `project`, the folders made first

    void draw_play(juce::Graphics& g);
    void draw_load(juce::Graphics& g);
    void draw_chop(juce::Graphics& g);
    void draw_to_pads(juce::Graphics& g);
    void draw_touched(juce::Graphics& g);

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

    std::atomic<float> volume {0.8f};
};

} // namespace app
