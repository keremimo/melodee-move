// SPDX-License-Identifier: AGPL-3.0-only
#include "app/mixing.h"
#include "app/studio.h"
#include "melodee/engine/engine_info.h"
#include "melodee/engine/pad_fx_info.h"
#include "melodee/model/sampler.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace app::mixing {
namespace {

using melodee::Command;
using melodee::EffectKind;
using Type = melodee::Command::Type;

std::string upper(std::string text) {
    for (auto& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

melodee::Chain& chain_of(Studio& studio, unsigned chain) { return studio.project().chain(chain); }
const melodee::Chain& chain_of(const Studio& studio, unsigned chain) { return studio.project().chain(chain); }

// A chain's command, as melodee-sample's shell sends it (Context::send_fx):
// a track's, a bus's or the master's by its number; a kit pad's as PadFx*.
void send_fx(Studio& studio, Type type, unsigned chain, unsigned at, unsigned param, int16_t value) {
    if (chain < melodee::pad_chain_base) {
        studio.send({type, uint8_t(chain), uint8_t(param), uint16_t(at), value});
        return;
    }
    unsigned n = chain - melodee::pad_chain_base;
    Type pad = type == Type::FxKind ? Type::PadFxKind : type == Type::FxParam ? Type::PadFxParam : Type::PadFxBypass;
    studio.send({pad, uint8_t(n / melodee::sampler_pads), uint8_t(n % melodee::sampler_pads), uint16_t(at | param << 8), value});
}

// A chain's track (-1 for a bus or the master), so a Schwung effect let go
// is handed back.
void let_go(Studio& studio, const melodee::EffectSlot& slot) {
    if (slot.kind == uint8_t(EffectKind::Plugin)) studio.fx_released(uint8_t(slot.params[0]));
}

const char* const short_names[] {
    "----", "ROOM", "REVB", "PLAT", "SHIM", "ECHO", "TPEC", "PING", "LOOP", "CHOR", "FLNG", "PHAS", "PCHO", "PPHS", "APAN",
    "TREM", "DRIV", "TAPE", "CRSH", "SPIT", "FUZZ", "EQ", "FILT", "FORM", "WIDE", "COMP", "GATE", "TRAN", "LIMT", "DIFF",
    "SDLY", "SPRG", "PUMP", "AMP", "TUBE", "CLIP", "ENSM", "MCHO", "WAH", "VNTG", "VINL", "TEMU", "PLUG",
};
static_assert(sizeof(short_names) / sizeof(short_names[0]) == size_t(EffectKind::Count), "a short name an effect");

} // namespace

// ---------------------------------------------------------------- chains

unsigned slots(const Studio& studio, unsigned chain) { return chain_of(studio, chain).size; }
unsigned used(const Studio& studio, unsigned chain) { return chain_of(studio, chain).count(); }
const melodee::EffectSlot& slot(const Studio& studio, unsigned chain, unsigned at) {
    return chain_of(studio, chain).slots[at % melodee::max_slots];
}

void sync_chain(Studio& studio, unsigned chain) {
    const auto& c = chain_of(studio, chain);
    for (unsigned k = 0; k < c.size; ++k) {
        const auto& s = c.slots[k];
        send_fx(studio, Type::FxKind, chain, k, 0, int16_t(s.kind));
        for (unsigned q = 0; q < melodee::effect_params; ++q) send_fx(studio, Type::FxParam, chain, k, q, s.params[q]);
        send_fx(studio, Type::FxBypass, chain, k, 0, int16_t(s.bypass));
        if (chain < melodee::chain_count) studio.send({Type::FxKey, uint8_t(chain), 0, uint16_t(k), int16_t(s.key)});
    }
}

void set_effect(Studio& studio, unsigned chain, unsigned at, EffectKind kind) {
    auto& c = chain_of(studio, chain);
    unsigned count = c.count();
    if (kind == EffectKind::None) { // removed: the rest close up
        if (at >= count) return;
        let_go(studio, c.slots[at]);
        for (unsigned k = at + 1; k < c.size; ++k) c.slots[k - 1] = c.slots[k];
        c.slots[c.size - 1] = melodee::EffectSlot {};
        sync_chain(studio, chain);
        studio.changed();
        return;
    }
    if (kind == EffectKind::Plugin) return; // a Schwung effect comes with its module (Studio::load_fx)
    at = std::min(at, count);
    if (at >= c.size) return;
    auto& s = c.slots[at];
    let_go(studio, s);
    s = melodee::EffectSlot {};
    s.kind = uint8_t(kind);
    const auto& info = melodee::effect_info(kind);
    for (unsigned q = 0; q < melodee::effect_params; ++q) s.params[q] = info.params[q].label ? info.params[q].initial : 0;
    send_fx(studio, Type::FxKind, chain, at, 0, int16_t(kind)); // the host takes the same initial values
    send_fx(studio, Type::FxBypass, chain, at, 0, 0);
    if (chain < melodee::chain_count) studio.send({Type::FxKey, uint8_t(chain), 0, uint16_t(at), 0});
    studio.changed();
}

void set_param(Studio& studio, unsigned chain, unsigned at, unsigned param, int value) {
    auto& c = chain_of(studio, chain);
    if (at >= c.count() || param >= melodee::effect_params) return;
    auto& s = c.slots[at];
    const auto& info = melodee::effect_info(EffectKind(s.kind));
    if (!info.params[param].label) return;
    s.params[param] = int16_t(std::clamp<int>(value, info.params[param].min, info.params[param].max));
    send_fx(studio, Type::FxParam, chain, at, param, s.params[param]);
    studio.changed();
}

void turn_param(Studio& studio, unsigned chain, unsigned at, unsigned param, int delta, bool fine) {
    const auto& c = chain_of(studio, chain);
    if (at >= c.count() || param >= melodee::effect_params) return;
    const auto& s = c.slots[at];
    const auto& p = melodee::effect_info(EffectKind(s.kind)).params[param];
    if (!p.label) return;
    int range = p.max - p.min;
    int step = fine || range <= 100 ? 1 : std::max(1, range / 100);
    set_param(studio, chain, at, param, s.params[param] + delta * step);
}

void toggle_bypass(Studio& studio, unsigned chain, unsigned at) {
    auto& c = chain_of(studio, chain);
    if (at >= c.count()) return;
    auto& s = c.slots[at];
    s.bypass = !s.bypass;
    send_fx(studio, Type::FxBypass, chain, at, 0, int16_t(s.bypass));
    studio.changed();
}

void move_effect(Studio& studio, unsigned chain, unsigned at, int way) {
    auto& c = chain_of(studio, chain);
    int to = int(at) + way;
    if (at >= c.count() || to < 0 || unsigned(to) >= c.count()) return;
    std::swap(c.slots[at], c.slots[size_t(to)]);
    sync_chain(studio, chain);
    studio.changed();
}

void copy_effect(Studio& studio, unsigned chain, unsigned at) {
    const auto& c = chain_of(studio, chain);
    if (at >= c.count()) return;
    studio.desk.clipboard = c.slots[at];
    studio.desk.clipped = true;
}

bool has_clipboard(const Studio& studio) { return studio.desk.clipped; }

bool paste_effect(Studio& studio, unsigned chain, unsigned at) {
    if (!studio.desk.clipped) return false;
    auto copied = studio.desk.clipboard;
    if (copied.kind == uint8_t(EffectKind::Plugin)) {
        if (is_pad_chain(chain)) return false; // a pad's chain takes none (Melodee's rule)
        return studio.copy_fx(uint8_t(copied.params[0]), chain, at); // its own instance, set as the one copied
    }
    auto& c = chain_of(studio, chain);
    at = std::min(at, c.count());
    if (at >= c.size) return false;
    let_go(studio, c.slots[at]);
    if (copied.key && (is_pad_chain(chain) || !melodee::fx_keyable(copied.kind) || (melodee::fx_key_is_track(copied.key) && copied.key - 1u == chain)))
        copied.key = 0;
    c.slots[at] = copied;
    send_fx(studio, Type::FxKind, chain, at, 0, int16_t(copied.kind));
    for (unsigned q = 0; q < melodee::effect_params; ++q) send_fx(studio, Type::FxParam, chain, at, q, copied.params[q]);
    send_fx(studio, Type::FxBypass, chain, at, 0, int16_t(copied.bypass));
    if (chain < melodee::chain_count) studio.send({Type::FxKey, uint8_t(chain), 0, uint16_t(at), int16_t(copied.key)});
    studio.changed();
    return true;
}

void set_key(Studio& studio, unsigned chain, unsigned at, unsigned key) {
    if (chain >= melodee::chain_count) return; // a pad's chain has no KEY
    auto& c = chain_of(studio, chain);
    if (at >= c.count() || !melodee::fx_keyable(c.slots[at].kind)) return;
    if (key >= melodee::fx_keys || (melodee::fx_key_is_track(key) && chain < melodee::track_count && key - 1 == chain)) key = 0;
    c.slots[at].key = uint8_t(key);
    studio.send({Type::FxKey, uint8_t(chain), 0, uint16_t(at), int16_t(key)});
    studio.changed();
}

void turn_key(Studio& studio, unsigned chain, unsigned at, int delta) {
    if (chain >= melodee::chain_count) return;
    const auto& c = chain_of(studio, chain);
    if (at >= c.count() || !melodee::fx_keyable(c.slots[at].kind)) return;
    int key = int(c.slots[at].key);
    do { // past itself, which it never keys
        key = std::clamp(key + (delta > 0 ? 1 : -1), 0, int(melodee::fx_keys) - 1);
    } while (key != 0 && melodee::fx_key_is_track(unsigned(key)) && chain < melodee::track_count && unsigned(key) - 1 == chain
             && key != int(melodee::fx_keys) - 1);
    set_key(studio, chain, at, unsigned(key));
}

std::string key_name(unsigned key) {
    if (melodee::fx_key_is_track(key)) return "TRACK " + std::to_string(key);
    if (melodee::fx_key_is_bus(key)) return std::string("BUS ") + char('A' + int(key - 17));
    return "OFF";
}

std::string short_name(EffectKind kind) { return short_names[unsigned(kind) % unsigned(EffectKind::Count)]; }
std::string long_name(EffectKind kind) { return upper(melodee::effect_info(kind).name); }

std::string param_text(EffectKind kind, unsigned param, int value) {
    const auto& p = melodee::effect_info(kind).params[param % melodee::effect_params];
    if (!p.label) return {};
    auto shown = melodee::show(p, value);
    return upper(shown.value + shown.unit);
}

float param_fraction(EffectKind kind, unsigned param, int value) {
    const auto& p = melodee::effect_info(kind).params[param % melodee::effect_params];
    return p.label ? melodee::gauge_of(p, value) : 0.0f;
}

// ---------------------------------------------------------------- strips

void set_pan(Studio& studio, unsigned t, int pan) {
    auto& mix = studio.project().tracks[t % melodee::track_count].mix;
    mix.pan = int8_t(std::clamp(pan, -50, 50));
    studio.send({Type::Pan, uint8_t(t % melodee::track_count), 0, 0, int16_t(mix.pan)});
    studio.changed();
}

void set_send(Studio& studio, unsigned t, unsigned b, int value) {
    auto& mix = studio.project().tracks[t % melodee::track_count].mix;
    mix.sends[b % melodee::bus_count] = uint8_t(std::clamp(value, 0, 100));
    studio.send({Type::Send, uint8_t(t % melodee::track_count), 0, uint16_t(b % melodee::bus_count), int16_t(mix.sends[b % melodee::bus_count])});
    studio.changed();
}

void set_return(Studio& studio, unsigned b, int value) {
    auto& bus_now = studio.project().buses[b % melodee::bus_count];
    bus_now.level = uint8_t(std::clamp(value, 0, 100));
    studio.send({Type::Return, 0, 0, uint16_t(b % melodee::bus_count), int16_t(bus_now.level)});
    studio.changed();
}

void set_pad_send(Studio& studio, unsigned t, unsigned pad, unsigned b, int value) {
    auto sound = studio.project().tracks[t % melodee::track_count].pads[pad % melodee::sampler_pads];
    sound.sends[b % melodee::bus_count] = uint8_t(std::clamp(value, 0, 100));
    studio.set_pad(t % melodee::track_count, pad % melodee::sampler_pads, sound);
}

void toggle_pad_mute(Studio& studio, unsigned t, unsigned pad) {
    t %= melodee::track_count;
    pad %= melodee::sampler_pads;
    auto& track = studio.project().tracks[t];
    bool on = !track.pad_muted(pad);
    track.set_pad_muted(pad, on);
    studio.send({Type::PadMute, uint8_t(t), 0, uint16_t(pad), int16_t(on)});
    studio.changed();
}

void solo_pad(Studio& studio, unsigned t, unsigned pad) {
    t %= melodee::track_count;
    pad %= melodee::sampler_pads;
    auto& track = studio.project().tracks[t];
    bool alone = !track.pad_muted(pad);
    for (unsigned n = 0; n < melodee::sampler_pads && alone; ++n)
        if (n != pad && track.pads[n].used() && !track.pad_muted(n)) alone = false;
    for (unsigned n = 0; n < melodee::sampler_pads; ++n) {
        if (!track.pads[n].used() && n != pad) continue;
        bool on = alone ? false : n != pad;
        if (track.pad_muted(n) == on) continue;
        track.set_pad_muted(n, on);
        studio.send({Type::PadMute, uint8_t(t), 0, uint16_t(n), int16_t(on)});
    }
    studio.changed();
}

void unmute_all(Studio& studio) {
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        auto& track = studio.project().tracks[t];
        if (track.mix.mute) studio.set_mute(t, false);
        if (track.mix.solo) studio.set_solo(t, false);
        if (track.any_pad_muted()) {
            track.pad_mutes = {};
            studio.send({Type::PadMute, uint8_t(t), 0, 0xffff, 0});
            studio.changed();
        }
    }
}

float level_db(int level) { return level <= 0 ? -100.0f : melodee::volume_db(level); }

std::string level_text(int level) {
    if (level <= 0) return "-INF";
    char text[16];
    float db = level_db(level);
    std::snprintf(text, sizeof text, db > 0.05f ? "+%.1fDB" : "%.1fDB", double(db));
    return text;
}

std::string pan_text(int pan) {
    if (pan == 0) return "C";
    return (pan < 0 ? "L" : "R") + std::to_string(std::abs(pan));
}

// ---------------------------------------------------------------- the master compressor

void set_compressor(Studio& studio, unsigned what, int value) {
    auto& c = studio.project().compressor;
    switch (what) {
    case CompOn: c.on = value != 0; value = c.on; break;
    case CompColor: c.color = value != 0; value = c.color; break;
    case CompAttack: c.attack = uint8_t(std::clamp(value, 0, 100)); value = c.attack; break;
    case CompRelease: c.release = uint8_t(std::clamp(value, 0, 100)); value = c.release; break;
    case CompAmount: c.amount = uint8_t(std::clamp(value, 0, 100)); value = c.amount; break;
    case CompBoost: c.boost = uint8_t(std::clamp(value, 0, 24)); value = c.boost; break;
    case CompSidechain: c.sidechain = value != 0; value = c.sidechain; break;
    case CompKey: c.key = uint8_t(std::clamp<int>(value, 0, melodee::track_count - 1)); value = c.key; break;
    case CompKeyPad:
        c.key_pad = value < 0 || value >= int(melodee::sampler_pads) ? melodee::MasterComp::whole_kit : uint8_t(value);
        value = c.key_pad == melodee::MasterComp::whole_kit ? -1 : c.key_pad;
        break;
    default: return;
    }
    studio.send({Type::Compressor, 0, 0, uint16_t(what), int16_t(value)});
    studio.changed();
}

int compressor(const Studio& studio, unsigned what) {
    const auto& c = studio.project().compressor;
    switch (what) {
    case CompOn: return c.on;
    case CompColor: return c.color;
    case CompAttack: return c.attack;
    case CompRelease: return c.release;
    case CompAmount: return c.amount;
    case CompBoost: return c.boost;
    case CompSidechain: return c.sidechain;
    case CompKey: return c.key;
    case CompKeyPad: return c.key_pad == melodee::MasterComp::whole_kit ? -1 : c.key_pad;
    default: return 0;
    }
}

// ---------------------------------------------------------------- Knob FX

namespace {
void send_knob_state(Studio& studio) {
    studio.send({Type::KnobFx, 0, 0, 2, int16_t(studio.project().knob_fx.active() ? 1 : 0)});
}
void send_knob_targets(Studio& studio, unsigned t) {
    const auto& k = studio.project().knob_fx;
    Command tracks {Type::KnobFx, 0, 0, 3};
    tracks.position = k.tracks;
    studio.send(tracks);
    for (unsigned quarter = 0; quarter < 4; ++quarter) {
        Command pads {Type::KnobFx, uint8_t(t % melodee::track_count), uint8_t(quarter), 4};
        pads.position = uint32_t(k.pads[t % melodee::track_count][quarter / 2] >> (quarter % 2 * 32));
        studio.send(pads);
    }
}
} // namespace

void knob_fx_load(Studio& studio, unsigned entry) {
    auto& k = studio.project().knob_fx;
    entry %= melodee::knob_fx_count();
    const auto& e = melodee::knob_fx_entry(entry);
    k.choice = uint8_t(entry);
    k.kind = uint8_t(e.kind);
    const auto& info = melodee::effect_info(e.kind);
    for (unsigned q = 0; q < melodee::effect_params; ++q) k.params[q] = info.params[q].label ? info.params[q].initial : 0;
    studio.send({Type::KnobFx, 0, 0, 0, int16_t(k.kind)});
    if (e.param >= 0 && unsigned(e.param) < melodee::effect_params) {
        k.params[size_t(e.param)] = e.value;
        studio.send({Type::KnobFx, 0, uint8_t(e.param), 1, e.value});
    }
    k.settle(); // the entry's values in both A and B, the morph at A
    send_knob_state(studio);
    studio.changed();
}

void knob_fx_param(Studio& studio, unsigned q, int value) {
    auto& k = studio.project().knob_fx;
    const auto& info = melodee::effect_info(EffectKind(k.kind));
    auto heard = [&](unsigned p) { // K1-K3's value p where the morph has it, to the engine
        if (!info.params[p].label) return;
        k.params[p] = k.morphed(p);
        studio.send({Type::KnobFx, 0, uint8_t(p), 1, k.params[p]});
    };
    if (q == melodee::knob_morph) {
        k.morph = uint8_t(std::clamp(value, 0, 127));
        for (unsigned p = 0; p < 3; ++p) heard(p);
        studio.changed();
        return;
    }
    if (q >= melodee::effect_params || !info.params[q].label) return;
    auto v = int16_t(std::clamp<int>(value, info.params[q].min, info.params[q].max));
    if (q < 3) { k.setting()[q] = v; heard(q); }
    else { k.params[q] = v; studio.send({Type::KnobFx, 0, uint8_t(q), 1, k.params[q]}); }
    studio.changed();
}

void knob_fx_turn(Studio& studio, unsigned q, int delta, bool fine) {
    const auto& k = studio.project().knob_fx;
    if (q == melodee::knob_morph) { knob_fx_param(studio, q, k.morph + delta * (fine ? 1 : 2)); return; }
    if (q >= melodee::effect_params) return;
    const auto& p = melodee::effect_info(EffectKind(k.kind)).params[q];
    if (!p.label) return;
    int range = p.max - p.min, step = fine || range <= 100 ? 1 : std::max(1, range / 100);
    int now = q < 3 ? k.setting()[q] : k.params[q];
    knob_fx_param(studio, q, now + delta * step);
}

void knob_fx_edit(Studio& studio, unsigned setting) {
    auto& k = studio.project().knob_fx;
    k.editing = uint8_t(setting ? 1 : 0);
    knob_fx_param(studio, melodee::knob_morph, k.editing ? 127 : 0); // heard at it, so the knobs change what is heard
}

void knob_fx_on(Studio& studio, bool on) {
    auto& k = studio.project().knob_fx;
    k.on = on;
    if (on && k.kind == 0) knob_fx_load(studio, k.choice);
    send_knob_state(studio);
    send_knob_targets(studio, studio.track());
}

void knob_fx_bypass(Studio& studio) {
    auto& k = studio.project().knob_fx;
    k.bypass = !k.bypass;
    send_knob_state(studio);
    studio.changed();
}

void knob_fx_track(Studio& studio, unsigned t) {
    auto& k = studio.project().knob_fx;
    k.set_track(t, !k.track_on(t));
    send_knob_targets(studio, studio.track());
    studio.changed();
}

void knob_fx_pad(Studio& studio, unsigned t, unsigned pad) {
    auto& k = studio.project().knob_fx;
    k.set_pad(t, pad, !k.pad_on(t, pad));
    send_knob_targets(studio, t);
    studio.changed();
}

bool knob_fx_all(Studio& studio) {
    auto& k = studio.project().knob_fx;
    bool all = k.tracks == 0xffff;
    k.tracks = all ? 0 : 0xffff;
    send_knob_targets(studio, studio.track());
    studio.changed();
    return !all;
}

// ---------------------------------------------------------------- Pad FX and Flex Beat

namespace {
void send_timing(Studio& studio) {
    const auto& p = studio.project();
    studio.desk.perform.timing.store(int(p.punch_snap % 4) | int(p.punch_tail % 3) << 4 | (p.punch_quantize ? 0 : 64), std::memory_order_relaxed);
    studio.desk.perform.depth.store(p.punch_depth, std::memory_order_relaxed);
    studio.desk.perform.target.store(p.punch_target ? int(studio.track()) : 255, std::memory_order_relaxed);
}
void lay_punches(Studio& studio) {
    unsigned bank = punch_bank(studio);
    for (unsigned pad = 0; pad < 32; ++pad) studio.desk.perform.punch[pad].store(int16_t(punch_on(bank, pad)), std::memory_order_relaxed);
}
} // namespace

void pads_are(Studio& studio, PadsAre what) {
    auto& perform = studio.desk.perform;
    if (what != PadsAre::Punches && PadsAre(perform.pads.load()) == PadsAre::Punches) release_punches(studio);
    send_timing(studio);
    lay_punches(studio);
    perform.pads.store(int(what), std::memory_order_relaxed);
}

unsigned punch_banks() { return 2; }
unsigned punch_bank(const Studio& studio) { return studio.project().punch_bank == 1 ? 1 : 0; }

void punch_bank(Studio& studio, int way) {
    if (!way) return;
    release_punches(studio);
    unsigned next = (punch_bank(studio) + 1) % punch_banks();
    studio.project().punch_bank = next == 1 ? 1 : 2; // Melodee's numbers: 2 the MPC's sixteen, 1 Melodee 2
    lay_punches(studio);
    studio.changed();
}

// The Move's 32 pads: bank 1 the MPC's sixteen (Melodee's bank 3, new
// projects' first), then Melodee 1; bank 2 Melodee 2 with Flex Beat's four.
int punch_on(unsigned bank, unsigned pad) {
    pad %= 32;
    if (bank % 2 == 0) return pad < 16 ? melodee::punch_on_pad(2, pad) : melodee::punch_on_pad(0, pad - 16);
    return pad < 16 ? melodee::punch_on_pad(1, pad) : -1;
}

void set_punch_target(Studio& studio, bool track) {
    release_punches(studio);
    studio.project().punch_target = track ? 1 : 0;
    send_timing(studio);
    studio.changed();
}
void set_punch_snap(Studio& studio, int snap) {
    studio.project().punch_snap = uint8_t(std::clamp(snap, 0, 3));
    send_timing(studio);
    studio.changed();
}
void set_punch_tail(Studio& studio, int tail) {
    studio.project().punch_tail = uint8_t(std::clamp(tail, 0, 2));
    send_timing(studio);
    studio.changed();
}
void set_punch_depth(Studio& studio, int depth) {
    studio.project().punch_depth = uint8_t(std::clamp(depth, 0, 100));
    send_timing(studio);
    studio.changed();
}
void toggle_punch_quantize(Studio& studio) {
    studio.project().punch_quantize = !studio.project().punch_quantize;
    send_timing(studio);
    studio.changed();
}
void toggle_punch_latch(Studio& studio) {
    auto& latch = studio.desk.perform.latch;
    bool on = !latch.load();
    latch.store(on);
    if (!on) release_punches(studio);
}

void release_punches(Studio& studio) {
    // Every slot lets go (Command::PunchOff, track 254); the audio thread
    // forgets its slots when it next sees no punch held.
    Command off {Type::PunchOff, 254};
    studio.send(off);
    studio.desk.perform.sounding.store(0, std::memory_order_relaxed);
    studio.desk.perform.flex_pad.store(0, std::memory_order_relaxed);
    studio.desk.perform.released.store(true, std::memory_order_release);
}

void flex(Studio& studio, unsigned what, int value) {
    auto& f = studio.project().flex;
    switch (what) {
    case 0: break; // the pad: the audio thread's, from the pads
    case 1: f.mode = uint8_t(value ? 1 : 0); value = f.mode; break;
    case 2: f.mix = uint8_t(std::clamp(value, 0, 100)); value = f.mix; break;
    case 3: f.quantize = value != 0; value = f.quantize; break;
    default: return;
    }
    studio.send({Type::FlexBeat, 0, 0, uint16_t(what), int16_t(value)});
    if (what) studio.changed();
}

bool audio_pad(Performance& p, melodee::EngineHost& engine, unsigned pad, bool pressed, int value, bool pressure) {
    auto mode = PadsAre(p.pads.load(std::memory_order_relaxed));
    if (mode == PadsAre::Sound || pad >= 32) return false;
    if (p.released.exchange(false, std::memory_order_acquire)) { // the MIDI thread let every punch go
        p.slot_of.fill(0);
        p.slot_pad = {{-1, -1, -1, -1}};
    }
    if (mode == PadsAre::Flex) {
        if (pressed && !pressure && pad < melodee::flex_pads) {
            engine.send({Type::FlexBeat, 0, 0, 0, int16_t(pad)});
            p.flex_pad.store(int(pad), std::memory_order_relaxed);
        }
        return true;
    }
    int kind = p.punch[pad].load(std::memory_order_relaxed);
    if (kind < 0) return true;
    int target = p.target.load(std::memory_order_relaxed);
    int depth = p.depth.load(std::memory_order_relaxed);
    auto off = [&](unsigned at_pad) {
        int s = p.slot_of[at_pad];
        if (!s) return;
        Command c {Type::PunchOff, uint8_t(target)};
        c.position = uint32_t(s);
        engine.send(c);
        p.slot_pad[size_t(s - 1)] = -1;
        p.slot_of[at_pad] = 0;
        p.sounding.fetch_and(~(1u << at_pad), std::memory_order_relaxed);
    };
    if (pressure) {
        if (int s = p.slot_of[pad]) {
            Command c {Type::PunchDepth, uint8_t(target), 0, 0, int16_t(value * depth / 127)};
            c.position = uint32_t(s);
            engine.send(c);
        }
        return true;
    }
    bool latch = p.latch.load(std::memory_order_relaxed);
    if (!pressed) {
        if (!latch) off(pad);
        return true;
    }
    if (p.slot_of[pad]) { // latched and pressed again: it lets go
        off(pad);
        return true;
    }
    // A free slot, else the oldest one's.
    int free_slot = -1;
    for (int s = 0; s < 4 && free_slot < 0; ++s) if (p.slot_pad[size_t(s)] < 0) free_slot = s;
    if (free_slot < 0) {
        free_slot = 0;
        for (int s = 1; s < 4; ++s) if (p.slot_age[size_t(s)] < p.slot_age[size_t(free_slot)]) free_slot = s;
        off(unsigned(p.slot_pad[size_t(free_slot)]));
    }
    Command on {Type::PunchOn, uint8_t(target), uint8_t(p.timing.load(std::memory_order_relaxed)), uint16_t(kind), int16_t(depth)};
    on.position = uint32_t(free_slot + 1);
    engine.send(on);
    p.slot_pad[size_t(free_slot)] = int8_t(pad);
    p.slot_age[size_t(free_slot)] = ++p.presses;
    p.slot_of[pad] = int8_t(free_slot + 1);
    p.sounding.fetch_or(1u << pad, std::memory_order_relaxed);
    return true;
}

} // namespace app::mixing
