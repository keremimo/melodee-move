// SPDX-License-Identifier: AGPL-3.0-only
#include "app/kit_player.h"
#include "melodee/engine/dsp.h"
#include <algorithm>

namespace app {

KitPlayer::KitPlayer() {
    melodee::dsp::device_rate.store(44100.0f);
    engine = std::make_unique<melodee::SamplerEngine>();
    table = std::make_unique<melodee::SamplerEngine::Table>();
    table->fill(nullptr);
    engine->set_samples(table.get());
    engine->set_pager(nullptr); // samples are in memory: no streaming
}

bool KitPlayer::set_sample(uint16_t slot, const melodee::SampleData* data) {
    Command command;
    command.kind = Command::Kind::Sample;
    command.slot = slot;
    command.data = data;
    return commands.push(command);
}

bool KitPlayer::set_pad(unsigned pad, const melodee::PadSound& sound) {
    if (pad >= pad_count) return false;
    Command command;
    command.kind = Command::Kind::Pad;
    command.pad = pad;
    command.sound = sound;
    return commands.push(command);
}

void KitPlayer::apply_commands() {
    Command command;
    while (commands.pop(command)) {
        if (command.kind == Command::Kind::Sample) {
            if (command.slot < melodee::sample_slots) {
                if ((*table)[command.slot]) engine->sample_gone(command.slot);
                (*table)[command.slot] = command.data;
            }
        } else {
            applied[command.pad] = command.sound;
            engine->set_pad(command.pad, command.sound);
        }
    }
}

void KitPlayer::opened(unsigned new_rate) {
    if (new_rate == rate) return;
    rate = new_rate;
    // The engine works its coefficients out at a note's start and when a
    // sound is set: a new rate takes silence and every pad again.
    melodee::dsp::device_rate.store(float(new_rate));
    engine->silence();
    for (unsigned pad = 0; pad < pad_count; ++pad) engine->set_pad(pad, applied[pad]);
}

void KitPlayer::hit(unsigned pad, int velocity) {
    if (pad >= pad_count) return;
    apply_commands(); // a sound set this cycle plays at once
    if (velocity > 0) engine->note_on(int(melodee::sampler_first_note + pad), velocity);
    else engine->note_off(int(melodee::sampler_first_note + pad));
}

void KitPlayer::release(unsigned pad) {
    if (pad < pad_count) engine->note_off(int(melodee::sampler_first_note + pad));
}

void KitPlayer::render(move::AudioBlock& output, float gain) {
    apply_commands();
    left.fill(0.0f);
    right.fill(0.0f);
    engine->render_stereo(left.data(), right.data(), move::audio_frames);
    for (unsigned n = 0; n < move::audio_frames; ++n) {
        output[2 * n] = move::to_s32(std::clamp(left[n] * gain, -1.0f, 1.0f));
        output[2 * n + 1] = move::to_s32(std::clamp(right[n] * gain, -1.0f, 1.0f));
    }
}

} // namespace app
