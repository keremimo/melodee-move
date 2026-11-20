// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/app.h"
#include "move/latency.h"
#include "move/queues.h"
#include <array>
#include <atomic>
#include <string>
#include <vector>

// The spike: proves each path of the Move runtime on the device before
// Melodee moves across.
//   Audio   32 pads play sines (C3 upward, a semitone a pad, pressure adds
//           the octave); Play runs a 16-step click at the tempo; the master
//           knob is the volume. The input level shows on the screen.
//           Pads sound from audio_event(), on the audio thread in the cycle
//           they were read, the shortest way the runtime offers.
//   Input   jog to the INPUTS page: every control names itself there.
//   Latency jog to LATENCY, push the jog: a quiet click every half second
//           goes out and is timed coming back in (a cable from the output to
//           the input, or the speaker into the microphone), beside the time
//           from reading a pad to the end of its render.
//   Lights  pads by row, held pads white, steps on and the playhead, Play,
//           the four row buttons, Shift and Back.
//   Screen  HOME, INPUTS, LATENCY, then Melodee's own pages crushed to
//           128 x 64 (--previews DIR): jog turns them, jog push swaps
//           letterbox and stretch, Shift shows the name.
class SpikeApp : public move::App {
public:
    explicit SpikeApp(std::string previews_directory = {});

    void render(const move::AudioBlock& input, move::AudioBlock& output) override; // audio thread
    void audio_opened(const move::AudioFormat& format) override;                    // audio thread
    void audio_event(const move::SurfaceEvent& event) override;                     // audio thread
    void surface(const move::SurfaceEvent& event) override;                         // MIDI thread
    void lights(move::Lights& lights, Clock::time_point now) override;              // MIDI thread
    void draw(juce::Graphics& g) override;                                          // screen thread
    void service() override;                                                        // service thread
    std::string status() const override;

private:
    static constexpr int home_page = 0, inputs_page = 1, latency_page = 2, first_preview_page = 3;
    void draw_home(juce::Graphics& g);
    void draw_inputs(juce::Graphics& g);
    void draw_latency(juce::Graphics& g);
    void draw_preview(juce::Graphics& g, int index);
    void log(const char* text);

    // MIDI thread to audio thread.
    std::array<std::atomic<float>, move::pad_count> pad_level {}, pad_pressure {};
    std::atomic<uint32_t> steps_on {0x1111};
    std::atomic<bool> playing {false};
    std::atomic<float> tempo {120}, volume {0.7f}, overtone {0.0f};
    // Audio thread to the others.
    std::atomic<int> playhead {-1};
    // MIDI thread to the screen.
    std::array<std::atomic<int>, move::knob_count> knob_values {};
    std::atomic<int> page {home_page};
    std::atomic<bool> stretch {true};
    struct LogLine { std::array<char, 24> text {}; };
    move::SpscRing<LogLine, 64> log_lines;

    // The round-trip meter: the MIDI thread turns it on, the audio thread runs it.
    std::atomic<bool> measuring {false};
    move::LoopbackMeter meter;

    // Audio thread only.
    struct Voice { double phase = 0; float level = 0, pressure = 0; };
    std::array<Voice, move::pad_count> voices {};
    unsigned rate = 44100;
    double step_frames = 0;
    int step = -1;
    bool was_playing = false;
    float click_level = 0, click_frequency = 1000;
    double click_phase = 0;

    // Screen thread only.
    std::array<std::string, 6> recent;
    float input_meter = 0;

    // Previews: the service thread loads a few each call, so quitting never
    // waits on them, into a vector reserved up front (it never moves), and
    // raises the count after each one is complete.
    struct Preview { juce::String name; juce::Image letterbox, stretched; };
    std::string previews_directory;
    std::vector<Preview> previews;
    std::atomic<int> preview_count {0};
    std::vector<juce::File> preview_files;
    size_t next_preview_file = 0;
    bool previews_listed = false;
};
