// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/kit_player.h"
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
//   Load  (Menu) WAV files from the Move's sample library onto the last pad.
//   Chop  (Loop) the last pad's sample: Auto, Beats, Regions or Threshold;
//         the pads play the slices; the jogwheel or Left/Right picks one;
//         knobs: its start, its end, the type, the amount. Push the
//         jogwheel for To Pads: Poly, Mono or Thru, onto the first empty
//         pads. Back leaves without changing the pads.
//
// Pads sound from audio_event(), in the cycle they were read. The screen,
// the MIDI thread and the service thread share the model under one lock;
// the audio thread never takes it (KitPlayer's queue).
namespace app {

class MelodeeApp : public move::App {
public:
    // `library`: the folder Load opens (the Move's own sample library by default).
    explicit MelodeeApp(std::string library = {});
    ~MelodeeApp() override;

    void render(const move::AudioBlock& input, move::AudioBlock& output) override; // audio thread
    void audio_opened(const move::AudioFormat& format) override;                    // audio thread
    void audio_event(const move::SurfaceEvent& event) override;                     // audio thread
    void surface(const move::SurfaceEvent& event) override;                         // MIDI thread
    void lights(move::Lights& lights, Clock::time_point now) override;              // MIDI thread
    void draw(juce::Graphics& g) override;                                          // screen thread
    void service() override;                                                        // service thread
    std::string status() const override;

    // For tests and tools: load a file onto a pad and wait for it.
    std::string load_now(const std::string& path, unsigned pad);
    unsigned slice_count() const;

private:
    enum class Page : uint8_t { Play, Load, Chop, ToPads };
    struct Sample {
        std::string name;
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

    void draw_play(juce::Graphics& g);
    void draw_load(juce::Graphics& g);
    void draw_chop(juce::Graphics& g);
    void draw_to_pads(juce::Graphics& g);
    void draw_touched(juce::Graphics& g);

    mutable std::mutex lock;
    KitPlayer player;
    std::vector<std::unique_ptr<Sample>> samples; // slot = index; kept for the run
    std::array<melodee::PadSound, pad_count> pads {};
    unsigned pad = 0;
    Page page = Page::Play;
    bool shift = false;
    // Load
    std::string library, folder;
    std::vector<Entry> entries;
    int cursor = 0;
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

    std::atomic<float> volume {0.8f};
};

} // namespace app
