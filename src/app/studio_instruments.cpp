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
#include <cctype>
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
    ref.streamed = sample->streamed; // a long one read from storage as it plays (kit depth's read_sample)
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

namespace app {

void Studio::publish_pads() {
    fill_pad_map(pad_maps.back(), *song, selected, shifts[selected], full_level.load(std::memory_order_relaxed));
    pad_maps.publish();
}

void Studio::send_arp(unsigned t) {
    t %= melodee::track_count;
    for (const auto& [index, value] : arp_settings(song->tracks[t])) send({Type::Arp, uint8_t(t), 0, index, value});
    publish_pads();
    changed();
}

// A note track's pad, on the audio thread in the cycle it was read: its
// notes as Keys & Scale lays them (app/keys.h), straight to the engine, to
// the arp, or to Note Repeat; let go, the same keys go.
void Studio::note_pad(const move::Input& input, Went& went, int track, int16_t velocity, bool quiet) {
    pad_maps.take();
    const PadMap& map = pad_maps.front();
    auto all = [&](const Went& w, Type type, int16_t value) {
        for (unsigned k = 0; k < w.count; ++k) engine->send({type, uint8_t(w.track), 0, w.keys[k], value});
    };
    if (chord_held.count && (!map.chord_hold || map.track != chord_held.track)) { all(chord_held, Type::NoteOff, 0); chord_held = {}; }
    if (input.pressed) {
        went = {};
        if (map.track != track || map.route == PadMap::Route::None) { // nothing published for this track yet: the rows of fourths
            went.key = note_for(input.index, audio_octave.load(std::memory_order_relaxed));
            went.keys[0] = uint8_t(went.key);
            went.count = 1;
            went.route = PadMap::Route::Notes;
        } else {
            const auto& notes = map.pads[input.index % keys_pads];
            went.keys = notes.keys;
            went.count = notes.count; // 0: a Prog pad with no chord
            went.route = map.route;
        }
        went.track = track;
        // move-seq's PadStamp, for the MIDI thread (Seq Record, a step held + the pad, step entry, Capture), quiet
        // or not: the track, its keys (the first as `key`), how hard, where the track's pattern is; `sounded`
        // once the keys below are sent. A Prog pad with no chord stamps none and sounds nothing.
        auto& stamp = stamps[input.index % pad_count];
        const unsigned count = std::min<unsigned>(went.count, PadStamp::most);
        stamp.sounded.store(false, std::memory_order_relaxed);
        stamp.track.store(track, std::memory_order_relaxed);
        stamp.key.store(count ? int(went.keys[0]) : -1, std::memory_order_relaxed);
        for (unsigned k = 0; k < count; ++k) stamp.keys[k].store(went.keys[k], std::memory_order_relaxed);
        stamp.count.store(count, std::memory_order_relaxed);
        stamp.velocity.store(velocity, std::memory_order_relaxed);
        stamp.position.store(engine->position_of(unsigned(track)), std::memory_order_relaxed);
        stamp.ticks.store(engine->ticks(), std::memory_order_relaxed);
        if (!went.count || quiet) { went = {}; return; }
        switch (went.route) {
        case PadMap::Route::Arp:
            if (map.latch) { // a fresh latch: the keys it held go
                bool others = false;
                for (const auto& w : pad_went) others = others || (&w != &went && w.track == track && w.route == PadMap::Route::Arp && w.count);
                if (!others) { engine->send({Type::Arp, uint8_t(track), 0, 0, 0}); engine->send({Type::Arp, uint8_t(track), 0, 0, 1}); }
            }
            all(went, Type::ArpKey, velocity);
            break;
        case PadMap::Route::Repeat: all(went, Type::RepeatKey, velocity); break;
        default:
            if (chord_held.count) { all(chord_held, Type::NoteOff, 0); chord_held = {}; }
            all(went, Type::NoteOn, velocity);
            break;
        }
        stamp.sounded.store(true, std::memory_order_release);
        return;
    }
    switch (went.route) {
    case PadMap::Route::Arp:
        if (map.latch) all(went, Type::Pressure, 0); // a latched arp plays on, without the pad's pressure
        else all(went, Type::ArpKey, 0);
        break;
    case PadMap::Route::Repeat: all(went, Type::RepeatKey, 0); break;
    default:
        if (map.chord_hold && went.count > 1) chord_held = went; // sounds on until the next pad
        else all(went, Type::NoteOff, 0);
        break;
    }
    went = {};
}

} // namespace app

#include "melodee/model/progression.h"
#include "melodee/shell/progression_file.h"
#include <filesystem>

namespace app {

void Studio::set_key(unsigned root, unsigned scale) {
    song->root = uint8_t(root % 12);
    song->scale = uint8_t(scale % melodee::scales().size());
    publish_pads();
    changed();
}

void Studio::set_keys(unsigned t, KeysMode mode, bool fourths) {
    set_keys_mode(song->tracks[t % melodee::track_count], mode, fourths);
    publish_pads();
    changed();
}

void Studio::set_transpose(unsigned t, int semitones) {
    song->tracks[t % melodee::track_count].transpose = int8_t(std::clamp(semitones, -12, 12));
    publish_pads();
    changed();
}

void Studio::changed_playing(unsigned t) {
    send_arp(t); // publishes the pads and marks the change too
}

std::string Studio::progressions_folder() const { return (std::filesystem::path(home_path) / "Progressions").string(); }

std::vector<std::string> Studio::progressions() const {
    // Melodee's own folder is <card>/Melodee/Progressions; here the card is the folder Melodee's home is in.
    auto card = std::filesystem::path(home_path).parent_path().string();
    auto found = melodee::list_progressions(card);
    // A home not named Melodee (tests, --home): its own folder first.
    if (std::filesystem::path(home_path).filename() != "Melodee") {
        std::error_code error;
        std::vector<std::string> own;
        for (const auto& entry : std::filesystem::directory_iterator(progressions_folder(), error))
            if (entry.path().extension() == ".progression") own.push_back(entry.path().string());
        std::sort(own.begin(), own.end());
        found.insert(found.begin(), own.begin(), own.end());
    }
    return found;
}

std::string Studio::load_progression(unsigned t, const std::string& path) {
    t %= melodee::track_count;
    melodee::Progression prog;
    auto error = melodee::read_progression(path, prog);
    if (!error.empty()) return error;
    auto& track = song->tracks[t];
    track.prog = prog;
    track.prog_shift = 0;
    set_keys_mode(track, KeysMode::Prog, false);
    track.bank = 0;
    publish_pads();
    changed();
    return {};
}

std::string Studio::store_chord(unsigned t, unsigned index, const std::vector<int>& keys) {
    t %= melodee::track_count;
    auto& track = song->tracks[t];
    std::vector<int> held(keys.begin(), keys.end());
    if (!melodee::store_prog_chord(track.prog, index, held.data(), unsigned(held.size()))) return "NO KEYS HELD";
    // Into Melodee's own folder: the file it came from when that is there, else Mine N (a file from elsewhere is
    // never written: its changed copy goes into Melodee's folder).
    namespace fs = std::filesystem;
    std::error_code error;
    fs::create_directories(progressions_folder(), error);
    std::string file = track.prog.file;
    if (file.empty() || !fs::exists(fs::path(progressions_folder()) / (file + ".progression"), error)) {
        unsigned n = 1;
        while (fs::exists(fs::path(progressions_folder()) / ("Mine " + std::to_string(n) + ".progression"), error)) ++n;
        file = "Mine " + std::to_string(n);
        std::snprintf(track.prog.file, sizeof track.prog.file, "%s", file.c_str());
        std::snprintf(track.prog.name, sizeof track.prog.name, "%s", file.c_str());
    }
    set_keys_mode(track, KeysMode::Prog, false);
    publish_pads();
    changed();
    if (!melodee::write_progression((fs::path(progressions_folder()) / (file + ".progression")).string(), track.prog)) return "NOT SAVED";
    return {};
}

} // namespace app

#include "app/auto_sampler.h"

namespace app {

std::string Studio::keygroups_folder() const { return (std::filesystem::path(home_path) / "Samples" / "Keygroups").string(); }

std::string Studio::auto_sample(unsigned t, const melodee::AutoSampleSettings& settings, bool sample) {
    t %= melodee::track_count;
    auto* track = module_track(t);
    if (!track || !track->module) return "NO MODULE";
    if (track->module->source) return "NOT AN INSTRUMENT";
    if (sampler->running()) return "BUSY";
    // Its own instance, set as the track's: the module, its preset, each value.
    ModuleInfo info = track->module->info();
    SynthFile state;
    state.module = info.id;
    state.preset = track->module->preset_param.empty() ? -1 : track->module->preset;
    for (const auto& param : track->module->params)
        if (param.key != track->module->preset_param && !param.trigger()) state.values.emplace_back(param.key, param.text());
    AutoSampler::Job job;
    job.track = t;
    job.name = !track->module->preset_name.empty() ? track->module->preset_name : info.name;
    job.folder = keygroups_folder();
    job.sample = sample;
    sampler->start(job, settings, [info, state](std::string& error) { return module_source(info, state, error); });
    return {};
}

void Studio::auto_sample_tick() {
    if (sampler->phase_now() != AutoSampler::Phase::Done) return;
    const auto program = sampler->program();
    const unsigned from = sampler->track();
    sampler->reset();
    std::string error;
    auto read = read_instrument_sample(program, error); // outside the lock: it reads the program
    std::lock_guard<std::mutex> guard(lock);
    if (!read) {
        for (auto& c : error) c = char(std::toupper(static_cast<unsigned char>(c)));
        say(error);
        return;
    }
    unsigned to = 0;
    while (to < melodee::track_count && song->tracks[to].used()) ++to;
    if (to >= melodee::track_count) { say("NO EMPTY TRACK"); return; }
    if (!new_track(to, TrackKind::Keys)) { say("NO EMPTY TRACK"); return; }
    // The module track's patterns and mix, as Melodee's auto sampler gives the new track.
    auto& made = song->tracks[to];
    made.mix = song->tracks[from].mix;
    made.pattern = song->tracks[from].pattern;
    std::snprintf(made.name, sizeof made.name, "%s", read->name.c_str());
    bank->at[to] = bank->at[from];
    apply_track(to);
    if (auto failed = install_keys(std::move(read), to); !failed.empty()) { say(failed); return; }
    auto_sampled = int(to);
    say("ON TRACK " + std::to_string(to + 1));
}

} // namespace app

namespace app {

std::string Studio::autosample_command(const std::string& arguments) {
    AutoCommand command;
    std::string error;
    if (!parse_autosample(arguments, command, error)) return "melodee-autosample: " + error;
    if (auto failed = auto_sample(command.track, command.settings, !command.check); !failed.empty()) return "melodee-autosample: " + failed;
    return std::string(command.check ? "checking" : "sampling") + " track " + std::to_string(command.track + 1);
}

std::string Studio::instruments_status() const {
    std::string line;
    const auto& track = current();
    if (track.engine == EngineKind::Keygroup || track.engine == EngineKind::Plugin || track.engine == EngineKind::Midi) {
        line += " keys=" + std::string(keys_mode_name(keys_mode(track))) + (keys_fourths(track) ? "/4THS" : "") + " key=" + std::to_string(song->root) + "/"
                + std::to_string(song->scale) + " arp=" + std::to_string(int(track.arp)) + " chord=" + std::to_string(int(track.chord));
    }
    if (const auto* sample = keys_sample(selected); sample && sample->instrument)
        line += " zones=" + std::to_string(sample->data->multi->zones.size()) + " landed=" + std::to_string(sample->instrument->landed.load()) + "/"
                + std::to_string(sample->instrument->parts.size());
    if (sampler->phase_now() != AutoSampler::Phase::Idle)
        line += std::string(" auto=") + phase_name(sampler->phase_now()) + " " + std::to_string(sampler->done()) + "/" + std::to_string(sampler->total());
    return line;
}

} // namespace app
