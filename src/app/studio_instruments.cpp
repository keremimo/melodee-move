// SPDX-License-Identifier: AGPL-3.0-only
// The studio's keygroup tracks (docs/UI.md, "Instruments"): an instrument or
// one sample on the track's pad 1, as Melodee's KEYGROUP track plays it
// (model/keygroup_track.h): its notes MIDI notes, a sample of one note from
// that note (model/scales.h keyed_root()). Kept apart from studio.cpp, which
// the tracks, transport and saving share.
#include "app/instruments.h"
#include "app/studio.h"
#include "melodee/engine/engine_host.h"
#include "melodee/model/scales.h"
#include <algorithm>
#include <climits>
#include <cstring>

namespace app {

using melodee::Command;
using melodee::EngineKind;
using Type = melodee::Command::Type;

unsigned Studio::free_slot() const {
    unsigned slot = 0; // the first slot no file of the project names
    while (slot < melodee::sample_slots && (song->samples[slot].path[0] || (slot < samples.size() && samples[slot]))) ++slot;
    return slot;
}

std::string Studio::install_keys(std::unique_ptr<Sample> sample, unsigned t) {
    t %= melodee::track_count;
    auto& track = song->tracks[t];
    if (track.engine != EngineKind::Keygroup) return "NOT A KEYGROUP";
    unsigned slot = free_slot();
    if (slot >= melodee::sample_slots) return "MEMORY FULL";
    if (!engine->offer_sample(slot, sample->data.get())) return "BUSY, TRY AGAIN";
    auto& ref = song->samples.edit(slot);
    auto copy = [](char* to, size_t size, const std::string& from) {
        std::memset(to, 0, size);
        std::memcpy(to, from.data(), std::min(from.size(), size - 1));
    };
    copy(ref.path, sizeof ref.path, sample->path);
    copy(ref.name, sizeof ref.name, sample->name);
    ref.frames = sample->data->model_frames();
    const bool instrument = sample->instrument != nullptr;
    if (instrument) {
        // What the zones cover, as Melodee keeps it for 16 Levels' Zones.
        const auto& multi = *sample->data->multi;
        ref.zones = uint16_t(std::min<size_t>(multi.zones.size(), 0xffff));
        std::array<uint64_t, 2> keys {};
        bool one_key = true;
        for (const auto& z : multi.zones) {
            if (z.on_release) continue;
            one_key = one_key && z.low_key == z.high_key;
            for (unsigned k = z.low_key; k <= z.high_key && k < 128; ++k) keys[k / 64] |= uint64_t(1) << (k % 64);
        }
        ref.keys = keys;
        ref.one_key_zones = one_key && melodee::zone_key_count(ref) >= 2;
        ref.root = melodee::root_none;
    } else {
        ref.root = sample->root.found() ? int8_t(sample->root.pitch_class) : melodee::root_none;
    }
    // The pad: held while held, poly (chords), as Melodee loads a keygroup's
    // sound; an instrument's own envelope and velocity on it.
    melodee::PadSound sound;
    sound.sample = uint16_t(slot);
    sound.play = melodee::PadPlay::Gate;
    sound.mono = false;
    sound.decay = 100;
    sound.sustain = 100;
    if (instrument) {
        const auto& read = sample->instrument->read;
        const auto& e = read.envelope;
        if (e.attack) sound.attack = uint8_t(*e.attack > 0 ? melodee::sfz_attack_step(*e.attack) : 0);
        if (e.decay) sound.decay = uint8_t(melodee::sfz_decay_step(*e.decay));
        if (e.sustain) sound.sustain = uint8_t(melodee::sfz_percent_step(*e.sustain));
        if (e.release) sound.release = uint8_t(melodee::sfz_release_step(*e.release));
        sound.velocity_sense = uint8_t(std::clamp(read.velocity, 0.0, 100.0));
    }
    if (slot >= samples.size()) samples.resize(slot + 1);
    samples[slot] = std::move(sample);
    track.last_pad = 0;
    set_pad(t, 0, sound);
    if (samples[slot]->instrument) landings->start(*samples[slot]);
    keyed_sent[t] = INT16_MIN;
    say("LOADED " + std::string(ref.name));
    return {};
}

void Studio::sync_keyed_roots() {
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        const auto& track = song->tracks[t];
        if (track.engine != EngineKind::Keygroup) continue;
        const auto root = int16_t(melodee::keyed_root(*song, track));
        if (root == keyed_sent[t]) continue;
        if (send({Type::KeyedRoot, uint8_t(t), 0, 0, root})) keyed_sent[t] = root;
    }
}

} // namespace app
