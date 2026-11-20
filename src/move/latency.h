// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/audio.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>

// The round trip from the app's output to its input, measured with a click:
// a cable from the Move's output into its input, or the speaker into the
// microphone, closes the loop. What it reports is everything outside the
// app: the buffers between render() and the converters, both ways. Half of
// it is about how long a pad's sound waits after render().
//
// Audio thread: process() once a block. Any thread: the results.
namespace move {

class LoopbackMeter {
public:
    static constexpr float click_level = 0.2f;    // quiet: a speaker may be the path
    static constexpr unsigned click_frames = 8;

    void set_rate(unsigned rate) { this->rate = rate; }

    // Adds the click to `output` while measuring, and looks for it in `input`.
    void process(const AudioBlock& input, AudioBlock& output, bool measuring) {
        if (!measuring) { listening = false; frame += audio_frames; return; }
        uint64_t period = rate / 2, timeout = rate / 4;
        for (unsigned n = 0; n < audio_frames; ++n, ++frame) {
            float in = std::max(std::fabs(from_s32(input[2 * n])), std::fabs(from_s32(input[2 * n + 1])));
            if (listening) {
                if (in > threshold) {
                    found(frame - clicked_at);
                    listening = false;
                } else if (frame - clicked_at > timeout) {
                    misses.fetch_add(1, std::memory_order_relaxed);
                    listening = false;
                }
            } else {
                noise = std::max(in, noise * 0.9995f); // the floor before the click, decaying over ~50 ms
            }
            if (!listening && frame >= next_click) {
                clicked_at = frame;
                next_click = frame + period;
                threshold = std::max(0.02f, noise * 6.0f);
                listening = true;
            }
            if (listening && frame - clicked_at < click_frames) {
                int32_t click = to_s32(click_level);
                output[2 * n] = output[2 * n + 1] = click;
            }
        }
    }

    // Frames from writing the click into render()'s output to reading it back.
    int last() const { return last_frames.load(std::memory_order_relaxed); }
    int best() const { return best_frames.load(std::memory_order_relaxed); }
    unsigned count() const { return measured.load(std::memory_order_relaxed); }
    unsigned missed() const { return misses.load(std::memory_order_relaxed); }

private:
    void found(uint64_t frames) {
        int value = int(frames);
        last_frames.store(value, std::memory_order_relaxed);
        if (best_frames.load(std::memory_order_relaxed) < 0 || value < best_frames.load(std::memory_order_relaxed))
            best_frames.store(value, std::memory_order_relaxed);
        measured.fetch_add(1, std::memory_order_relaxed);
    }

    unsigned rate = 44100;
    uint64_t frame = 0, next_click = 0, clicked_at = 0;
    bool listening = false;
    float noise = 0, threshold = 0.02f;
    std::atomic<int> last_frames {-1}, best_frames {-1};
    std::atomic<unsigned> measured {0}, misses {0};
};

} // namespace move
