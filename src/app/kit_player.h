// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/audio.h"
#include "move/queues.h"
#include "melodee/engine/sampler.h"
#include <array>
#include <memory>

// The audio side of one kit track: Melodee's own SamplerEngine
// (melodee-sample, src/melodee/engine/sampler.h), compiled unchanged. The
// Move's 32 pads are its pads 1-32 (A01-B16), notes 36-67.
//
// The engine lives on the audio thread. Everything else reaches it through
// a queue the audio thread empties at the start of each block: a sample put
// in a slot, a pad's sound. Pad hits come straight from App::audio_event(),
// in the cycle they were read.
namespace app {

constexpr unsigned pad_count = 32;

class KitPlayer {
public:
    KitPlayer(); // builds the engine: it allocates, so never on the audio thread

    // Any thread but the audio thread, one at a time (the app's lock).
    // False when the queue is full.
    bool set_sample(uint16_t slot, const melodee::SampleData* data);
    bool set_pad(unsigned pad, const melodee::PadSound& sound);

    // Audio thread.
    void opened(unsigned rate);
    void hit(unsigned pad, int velocity);
    void release(unsigned pad);
    void render(move::AudioBlock& output, float gain);
    bool sounding() const { return engine->sounding(); }

private:
    struct Command {
        enum class Kind : uint8_t { Sample, Pad } kind = Kind::Pad;
        uint16_t slot = 0;
        const melodee::SampleData* data = nullptr;
        unsigned pad = 0;
        melodee::PadSound sound;
    };
    void apply_commands();

    move::SpscRing<Command, 256> commands;
    std::unique_ptr<melodee::SamplerEngine> engine;
    std::unique_ptr<melodee::SamplerEngine::Table> table;
    std::array<melodee::PadSound, pad_count> applied {}; // to set again after a rate change
    std::array<float, move::audio_frames> left {}, right {};
    unsigned rate = 0;
};

} // namespace app
