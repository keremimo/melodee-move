// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

// Audio as an app sees it: the same blocks as on sample-base, 128 stereo
// frames of interleaved S32, so Melodee's engine renders here unchanged.
// JACK's float periods of whatever length are cut into these blocks
// (Reblocker).
namespace move {

constexpr unsigned audio_frames = 128, audio_channels = 2;
using AudioBlock = std::array<int32_t, audio_frames * audio_channels>;

struct AudioFormat {
    unsigned rate = 44100;
    unsigned period = 128;         // JACK's period, frames
    unsigned latency_frames = 0;   // Added by the Reblocker: 0 when the period is a multiple of 128, else 128
};

inline int32_t to_s32(float sample) {
    return int32_t(std::clamp(double(sample), -1.0, 1.0) * 2147483647.0);
}
inline float from_s32(int32_t sample) { return float(sample) * (1.0f / 2147483648.0f); }

// JACK periods in, the app's 128-frame blocks out. When the period is a
// multiple of 128 each block renders in place, adding no latency. Otherwise
// input gathers into blocks and the output runs one block (128 frames) late,
// so a block is always ready when JACK asks. Realtime-safe: process() and
// reset() never allocate.
class Reblocker {
public:
    static constexpr unsigned max_period = 8192;

    void reset(unsigned period) {
        in_fill = 0; ring_read = 0; ring_count = 0;
        latency = period % audio_frames == 0 ? 0 : audio_frames;
        for (unsigned n = 0; n < latency; ++n) push_frame(0, 0);
    }
    unsigned latency_frames() const { return latency; }
    uint64_t underruns() const { return short_frames; }

    // `render(const AudioBlock& in, AudioBlock& out)`; null inputs are silence.
    template <class Render>
    void process(const float* in_left, const float* in_right, float* out_left, float* out_right, unsigned frames, Render&& render) {
        if (frames > max_period) {
            std::fill(out_left, out_left + frames, 0.0f); std::fill(out_right, out_right + frames, 0.0f);
            short_frames += frames;
            return;
        }
        for (unsigned position = 0; position < frames;) {
            unsigned take = std::min(frames - position, audio_frames - in_fill);
            for (unsigned n = 0; n < take; ++n) {
                in_block[2 * (in_fill + n)] = in_left ? to_s32(in_left[position + n]) : 0;
                in_block[2 * (in_fill + n) + 1] = in_right ? to_s32(in_right[position + n]) : 0;
            }
            in_fill += take; position += take;
            if (in_fill == audio_frames) {
                out_block.fill(0);
                render(in_block, out_block);
                for (unsigned n = 0; n < audio_frames; ++n) push_frame(from_s32(out_block[2 * n]), from_s32(out_block[2 * n + 1]));
                in_fill = 0;
            }
        }
        for (unsigned n = 0; n < frames; ++n) {
            if (!ring_count) { out_left[n] = out_right[n] = 0; ++short_frames; continue; }
            out_left[n] = ring[2 * ring_read]; out_right[n] = ring[2 * ring_read + 1];
            ring_read = (ring_read + 1) % ring_frames; --ring_count;
        }
    }

private:
    static constexpr unsigned ring_frames = max_period + 2 * audio_frames;
    void push_frame(float left, float right) {
        if (ring_count == ring_frames) return;
        unsigned at = (ring_read + ring_count) % ring_frames;
        ring[2 * at] = left; ring[2 * at + 1] = right; ++ring_count;
    }

    AudioBlock in_block {}, out_block {};
    unsigned in_fill = 0, latency = 0;
    std::array<float, 2 * ring_frames> ring {};
    unsigned ring_read = 0, ring_count = 0;
    uint64_t short_frames = 0;
};

} // namespace move
