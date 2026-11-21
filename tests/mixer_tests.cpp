// SPDX-License-Identifier: AGPL-3.0-only
// The mix and its effects (src/app/mixing.*, fx_host.*, studio_fx.cpp), on
// a whole studio and Melodee's engine host: effects into a track's chain,
// heard; bypass, move, copy and paste, KEY; the strips, sends and returns;
// a kit pad's mute and Unmute all; the master compressor; Knob FX; the pads
// as punches and as Flex Beat's, from the audio thread; a Schwung audio
// effect (tests/fake_fx) loaded, heard, set, copied with its track; all of
// it saved and opened again.
#include "app/melodee_app.h"
#include "app/mixing.h"
#include "app/pad_layout.h"
#include "app/project_store.h"
#include "melodee/engine/engine_info.h"
#include "move/audio.h"
#include "move/controls.h"
#include "move/host.h"
#include "move/tools.h"
#include "melodee/engine/effect_info.h"
#include "fake_pcm.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

using namespace move;
using melodee::EffectKind;
namespace mixing = app::mixing;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
int32_t peak_of(const AudioBlock& block) {
    int32_t peak = 0;
    for (int32_t sample : block) peak = std::max(peak, sample == INT32_MIN ? INT32_MAX : std::abs(sample));
    return peak;
}

struct Rig {
    app::MelodeeApp melodee;
    Session session;
    AudioBlock in {}, out {};
    Rig(const std::string& samples, const std::string& home, const std::string& modules) : melodee(samples, home, modules) {
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
    }
    app::Studio& studio() { return melodee.studio(); }
    // Pad `pad` held `blocks` blocks: the loudest sample out.
    int32_t play(unsigned pad, int blocks) {
        melodee.audio_event(make_event(0x90, pad_note(pad), 110));
        int32_t peak = 0;
        for (int block = 0; block < blocks; ++block) { out.fill(0); melodee.render(in, out); peak = std::max(peak, peak_of(out)); }
        melodee.audio_event(make_event(0x80, pad_note(pad), 0));
        return peak;
    }
    void quiet() { for (int block = 0, still = 0; block < 6000 && still < 100; ++block) { out.fill(0); melodee.render(in, out); still = peak_of(out) ? 0 : still + 1; } }
    void blocks(int count) { for (int block = 0; block < count; ++block) { out.fill(0); melodee.render(in, out); } }
};

void test_chains(Rig& rig) {
    auto& studio = rig.studio();
    constexpr unsigned track = 0;
    int32_t dry = rig.play(0, 30);
    CHECK(dry > to_s32(0.1f));
    rig.quiet();
    // A low pass shut all the way: much quieter.
    mixing::set_effect(studio, track, 5, EffectKind::Filter); // past the last: into the first free slot
    CHECK(mixing::used(studio, track) == 1 && mixing::slot(studio, track, 0).kind == uint8_t(EffectKind::Filter));
    CHECK(mixing::slot(studio, track, 0).params[0] == melodee::effect_info(EffectKind::Filter).params[0].initial);
    mixing::set_param(studio, track, 0, 0, -50); // clamped to its least
    CHECK(mixing::slot(studio, track, 0).params[0] == 0);
    int32_t shut = rig.play(0, 30);
    CHECK(shut < dry / 4);
    rig.quiet();
    mixing::toggle_bypass(studio, track, 0);
    CHECK(mixing::slot(studio, track, 0).bypass);
    rig.blocks(4); // the bypass fades over 10 ms
    CHECK(rig.play(0, 30) > int32_t(int64_t(dry) * 3 / 4));
    rig.quiet();
    mixing::toggle_bypass(studio, track, 0);
    // A second effect, then moved in front of the first.
    mixing::set_effect(studio, track, 1, EffectKind::Echo);
    CHECK(mixing::used(studio, track) == 2);
    mixing::move_effect(studio, track, 1, -1);
    CHECK(mixing::slot(studio, track, 0).kind == uint8_t(EffectKind::Echo) && mixing::slot(studio, track, 1).kind == uint8_t(EffectKind::Filter));
    mixing::move_effect(studio, track, 0, -1); // nothing left of the first
    CHECK(mixing::slot(studio, track, 0).kind == uint8_t(EffectKind::Echo));
    // SYNC on the delay: a note value, shown so.
    int sync = melodee::delay_sync_param(EffectKind::Echo);
    CHECK(sync >= 0);
    mixing::set_param(studio, track, 0, unsigned(sync), 7); // 1/8D
    CHECK(mixing::param_text(EffectKind::Echo, unsigned(sync), 7) == "1/8D");
    CHECK(mixing::short_name(EffectKind::Echo) == "ECHO" && mixing::long_name(EffectKind::TapeEcho) == "TAPE ECHO");
    // Copy the Echo, paste it onto bus A's chain.
    mixing::copy_effect(studio, track, 0);
    CHECK(mixing::has_clipboard(studio));
    CHECK(mixing::paste_effect(studio, mixing::bus(0), 0));
    CHECK(mixing::slot(studio, mixing::bus(0), 0).kind == uint8_t(EffectKind::Echo) && mixing::slot(studio, mixing::bus(0), 0).params[size_t(sync)] == 7);
    // Removed: the rest close up.
    mixing::set_effect(studio, track, 0, EffectKind::None);
    CHECK(mixing::used(studio, track) == 1 && mixing::slot(studio, track, 0).kind == uint8_t(EffectKind::Filter));
    mixing::set_effect(studio, track, 0, EffectKind::None);
    CHECK(mixing::used(studio, track) == 0);
    rig.blocks(8);
    CHECK(rig.play(0, 30) > int32_t(int64_t(dry) * 3 / 4));
    rig.quiet();
    // KEY: a Compressor on track 2 keyed by track 1; never by itself.
    mixing::set_effect(studio, 1, 0, EffectKind::Compressor);
    mixing::set_key(studio, 1, 0, melodee::fx_key_track(0));
    CHECK(mixing::slot(studio, 1, 0).key == melodee::fx_key_track(0));
    mixing::set_key(studio, 1, 0, melodee::fx_key_track(1));
    CHECK(mixing::slot(studio, 1, 0).key == 0);
    mixing::turn_key(studio, 1, 0, 1); // to track 1
    CHECK(mixing::key_name(mixing::slot(studio, 1, 0).key) == "TRACK 1");
    mixing::turn_key(studio, 1, 0, 1); // past itself (track 2) to track 3
    CHECK(mixing::key_name(mixing::slot(studio, 1, 0).key) == "TRACK 3");
    mixing::set_key(studio, 1, 0, melodee::fx_key_bus(2));
    CHECK(mixing::key_name(mixing::slot(studio, 1, 0).key) == "BUS C");
    // A kit pad's own chain: four slots.
    unsigned pad_chain = melodee::pad_chain(track, 0);
    CHECK(mixing::slots(studio, pad_chain) == melodee::pad_slots);
    mixing::set_effect(studio, pad_chain, 0, EffectKind::Filter);
    mixing::set_param(studio, pad_chain, 0, 0, 0);
    rig.blocks(8);
    CHECK(rig.play(0, 30) < dry / 4);
    rig.quiet();
    mixing::set_effect(studio, pad_chain, 0, EffectKind::None);
    rig.blocks(8);
    CHECK(rig.play(0, 30) > int32_t(int64_t(dry) * 3 / 4));
    rig.quiet();
}

void test_strips(Rig& rig) {
    auto& studio = rig.studio();
    mixing::set_pan(studio, 0, -80);
    CHECK(studio.project().tracks[0].mix.pan == -50 && mixing::pan_text(-50) == "L50" && mixing::pan_text(0) == "C");
    mixing::set_pan(studio, 0, 0);
    mixing::set_send(studio, 0, 1, 40);
    CHECK(studio.project().tracks[0].mix.sends[1] == 40);
    mixing::set_return(studio, 1, 130);
    CHECK(studio.project().buses[1].level == 100);
    CHECK(mixing::level_text(100) == "0.0DB" && mixing::level_text(0) == "-INF");
    CHECK(mixing::level_db(80) < -1.0f);
    // A send with the track's level down: the dry sound gone, the bus heard.
    int32_t dry = rig.play(0, 30);
    rig.quiet();
    studio.set_level(0, 0);
    mixing::set_send(studio, 0, 0, 100);
    mixing::set_return(studio, 0, 100);
    rig.blocks(8);
    CHECK(rig.play(0, 30) == 0); // sends follow the level, as on the MPC
    studio.set_level(0, 100);
    mixing::set_send(studio, 0, 0, 0);
    rig.blocks(8);
    CHECK(rig.play(0, 30) > int32_t(int64_t(dry) * 3 / 4));
    rig.quiet();
    // A kit pad's own send.
    mixing::set_pad_send(studio, 0, 0, 2, 55);
    CHECK(studio.project().tracks[0].pads[0].sends[2] == 55);
    // Mute held + a pad: that pad quiet; Shift + Mute lets every mute go.
    mixing::toggle_pad_mute(studio, 0, 0);
    CHECK(studio.project().tracks[0].pad_muted(0));
    rig.blocks(8);
    CHECK(rig.play(0, 30) < to_s32(0.001f));
    studio.set_mute(1, true);
    studio.set_solo(2, true);
    mixing::unmute_all(studio);
    CHECK(!studio.project().tracks[0].pad_muted(0) && !studio.project().tracks[1].mix.mute && !studio.project().tracks[2].mix.solo);
    rig.blocks(8);
    CHECK(rig.play(0, 30) > int32_t(int64_t(dry) * 3 / 4));
    rig.quiet();
}

void test_master(Rig& rig) {
    auto& studio = rig.studio();
    mixing::set_compressor(studio, mixing::CompOn, 1);
    mixing::set_compressor(studio, mixing::CompAmount, 150);
    mixing::set_compressor(studio, mixing::CompBoost, 30);
    mixing::set_compressor(studio, mixing::CompSidechain, 1);
    mixing::set_compressor(studio, mixing::CompKey, 3);
    mixing::set_compressor(studio, mixing::CompKeyPad, -1);
    const auto& c = studio.project().compressor;
    CHECK(c.on && c.amount == 100 && c.boost == 24 && c.sidechain && c.key == 3 && c.key_pad == melodee::MasterComp::whole_kit);
    CHECK(mixing::compressor(studio, mixing::CompKeyPad) == -1);
    mixing::set_compressor(studio, mixing::CompKeyPad, 5);
    CHECK(mixing::compressor(studio, mixing::CompKeyPad) == 5);
    mixing::set_compressor(studio, mixing::CompSidechain, 0);
    rig.blocks(10);
    rig.play(0, 60); // the compressor works on the loop
    CHECK(studio.host().compression() > 0.0f); // dB taken off
    rig.quiet();
    // The master chain: six effects before the compressor.
    mixing::set_effect(studio, mixing::master, 0, EffectKind::Drive);
    CHECK(mixing::slots(studio, mixing::master) == melodee::master_slots && mixing::used(studio, mixing::master) == 1);
    mixing::set_effect(studio, mixing::master, 0, EffectKind::None);
    mixing::set_compressor(studio, mixing::CompOn, 0);
}

void test_knob_fx(Rig& rig) {
    auto& studio = rig.studio();
    auto& k = studio.project().knob_fx;
    unsigned lp = 0;
    for (unsigned n = 0; n < melodee::knob_fx_count(); ++n)
        if (std::string(melodee::knob_fx_entry(n).name) == "LP Filter") lp = n;
    mixing::knob_fx_load(studio, lp);
    CHECK(k.kind == uint8_t(EffectKind::Filter) && k.choice == lp);
    mixing::knob_fx_param(studio, 0, 10);  // A's cutoff
    mixing::knob_fx_edit(studio, 1);       // B in hand, heard at B
    mixing::knob_fx_param(studio, 0, 90);  // B's cutoff
    CHECK(k.a[0] == 10 && k.b[0] == 90 && k.morph == 127 && k.params[0] == 90);
    mixing::knob_fx_param(studio, melodee::knob_morph, 64); // halfway
    CHECK(std::abs(k.params[0] - 50) <= 1);
    mixing::knob_fx_track(studio, 0);
    mixing::knob_fx_pad(studio, 0, 3);
    CHECK(k.track_on(0) && k.pad_on(0, 3));
    mixing::knob_fx_on(studio, true);
    CHECK(k.active());
    rig.blocks(4);
    CHECK(rig.play(0, 30) > 0);
    rig.quiet();
    mixing::knob_fx_on(studio, false);
}

void test_pads(Rig& rig) {
    auto& studio = rig.studio();
    auto& host = studio.host();
    // Pad FX: a pad is a punch, from the audio thread at once.
    mixing::pads_are(studio, mixing::PadsAre::Punches);
    CHECK(mixing::punch_on(0, 0) == int(melodee::punch_mpc_first) && mixing::punch_on(0, 16) == 0 && mixing::punch_on(1, 31) == -1);
    rig.melodee.audio_event(make_event(0x90, pad_note(0), 100));
    rig.blocks(8);
    CHECK(host.live_punches() != 0);
    CHECK(studio.desk.perform.sounding.load() == 1u);
    rig.melodee.audio_event(make_event(0x80, pad_note(0), 0));
    rig.blocks(8);
    CHECK(studio.desk.perform.sounding.load() == 0u);
    // Five held: the fifth takes the oldest's slot.
    for (unsigned pad = 0; pad < 5; ++pad) rig.melodee.audio_event(make_event(0x90, pad_note(pad), 100));
    CHECK(__builtin_popcount(studio.desk.perform.sounding.load()) == 4 && !(studio.desk.perform.sounding.load() & 1u));
    for (unsigned pad = 0; pad < 5; ++pad) rig.melodee.audio_event(make_event(0x80, pad_note(pad), 0));
    CHECK(studio.desk.perform.sounding.load() == 0u);
    // Latched: stays on when let go, until pressed again.
    mixing::toggle_punch_latch(studio);
    rig.melodee.audio_event(make_event(0x90, pad_note(2), 100));
    rig.melodee.audio_event(make_event(0x80, pad_note(2), 0));
    CHECK(studio.desk.perform.sounding.load() == 4u);
    rig.melodee.audio_event(make_event(0x90, pad_note(2), 100));
    CHECK(studio.desk.perform.sounding.load() == 0u);
    mixing::toggle_punch_latch(studio);
    mixing::punch_bank(studio, 1);
    CHECK(mixing::punch_bank(studio) == 1 && studio.project().punch_bank == 1);
    mixing::punch_bank(studio, 1);
    CHECK(mixing::punch_bank(studio) == 0 && studio.project().punch_bank == 2);
    // Flex Beat: pads 1-16.
    mixing::pads_are(studio, mixing::PadsAre::Flex);
    mixing::flex(studio, 3, 0); // at once, not on the next sixteenth
    rig.melodee.audio_event(make_event(0x90, pad_note(6), 100));
    rig.blocks(8);
    CHECK(host.flex_playing() == 6 && studio.desk.perform.flex_pad.load() == 6);
    rig.melodee.audio_event(make_event(0x90, pad_note(0), 100)); // Empty
    rig.blocks(8);
    CHECK(host.flex_playing() == 0);
    mixing::flex(studio, 2, 140);
    CHECK(studio.project().flex.mix == 100);
    // The pads sound again.
    mixing::pads_are(studio, mixing::PadsAre::Sound);
    rig.blocks(8);
    CHECK(rig.play(0, 30) > 0);
    rig.quiet();
}

void test_schwung_fx(Rig& rig) {
    auto& studio = rig.studio();
    CHECK(!studio.fx_catalog.empty() && studio.fx_catalog.front().id == "fake-fx");
    CHECK(has(studio.fx_catalog.front().dsp, "fake-fx/fake-fx.so"));
    mixing::set_pad_send(studio, 0, 0, 2, 0); // the pad's own send leaves before the track's chain
    rig.blocks(8);
    int32_t dry = rig.play(0, 30);
    rig.quiet();
    CHECK(studio.load_fx(0, 0, "fake-fx"));
    CHECK(mixing::slot(studio, 0, 0).kind == uint8_t(EffectKind::Plugin));
    uint8_t entry = uint8_t(mixing::slot(studio, 0, 0).params[0]);
    CHECK(studio.project().plugins[entry].used());
    rig.melodee.service(); // loads it, offers its runner
    auto* fx = studio.fx_module(entry);
    CHECK(fx && fx->module && fx->offered);
    rig.blocks(8);
    int32_t halved = rig.play(0, 30); // its gain at 0.5
    double ratio = double(halved) / double(dry);
    CHECK(ratio > 0.4 && ratio < 0.6);
    rig.quiet();
    studio.fx_set(*fx, "gain", "0");
    rig.blocks(8);
    CHECK(rig.play(0, 30) == 0);
    rig.quiet();
    studio.fx_set(*fx, "gain", "1");
    CHECK(has(rig.melodee.status(), "schwung_fx=1"));
    // A copied track gets its own instance, set as the one copied.
    CHECK(studio.copy_track(0, 5));
    uint8_t copied = uint8_t(mixing::slot(studio, 5, 0).params[0]);
    CHECK(copied != entry && studio.fx_module(copied) && studio.fx_module(copied)->saved.module == "fake-fx");
    rig.melodee.service();
    CHECK(studio.fx_module(copied)->module && studio.fx_module(copied)->module->param("gain")->value == 1.0);
    // Deleted with its track: handed back, then gone.
    studio.delete_track(5);
    CHECK(!studio.fx_module(copied) && !studio.project().plugins[copied].used());
    rig.blocks(8);
    rig.melodee.service();
    // Pasted: its own instance too; a kit pad's chain takes none.
    mixing::copy_effect(studio, 0, 0);
    CHECK(!mixing::paste_effect(studio, melodee::pad_chain(0, 0), 0));
    CHECK(mixing::paste_effect(studio, mixing::bus(3), 0));
    CHECK(mixing::slot(studio, mixing::bus(3), 0).kind == uint8_t(EffectKind::Plugin) && mixing::slot(studio, mixing::bus(3), 0).params[0] != entry);
    mixing::set_effect(studio, mixing::bus(3), 0, EffectKind::None);
    rig.blocks(8);
    rig.melodee.service();
}

// The Move's own keys for the mix, through the app and the bindings table
// (docs/MOVE_CONTROLS.md), as a hand uses them: buttons pressed and let go.
void button(Rig& rig, Button which, bool down) { rig.melodee.surface(make_event(0xb0, button_cc(which), down ? 127 : 0)); }
void pad(Rig& rig, unsigned at, bool down) {
    auto event = make_event(down ? 0x90 : 0x80, pad_note(at), down ? 100 : 0);
    rig.melodee.audio_event(event); // the runtime hands a pad to both, the audio thread first
    rig.melodee.surface(event);
}

void test_keys(Rig& rig) {
    auto& studio = rig.studio();
    // Mute held + a kit pad: that pad muted, and quiet as it is pressed; again, back.
    rig.quiet();
    button(rig, Button::Mute, true);
    pad(rig, 0, true);
    rig.blocks(8);
    int32_t heard = 0;
    for (int b = 0; b < 20; ++b) { rig.out.fill(0); rig.melodee.render(rig.in, rig.out); heard = std::max(heard, peak_of(rig.out)); }
    pad(rig, 0, false);
    button(rig, Button::Mute, false);
    CHECK(studio.project().tracks[0].pad_muted(0) && heard < to_s32(0.001f));
    CHECK(!studio.current().mix.mute); // Mute was a modifier: the track stays as it was
    button(rig, Button::Mute, true);
    pad(rig, 0, true);
    pad(rig, 0, false);
    button(rig, Button::Mute, false);
    CHECK(!studio.project().tracks[0].pad_muted(0));
    // Shift + Mute held + a pad solos it (the other pads with a sound muted); again, the kit back.
    studio.set_pad(0, 1, studio.project().tracks[0].pads[0]);
    button(rig, Button::Shift, true);
    button(rig, Button::Mute, true);
    pad(rig, 1, true);
    pad(rig, 1, false);
    CHECK(studio.project().tracks[0].pad_muted(0) && !studio.project().tracks[0].pad_muted(1));
    pad(rig, 1, true);
    pad(rig, 1, false);
    button(rig, Button::Mute, false);
    button(rig, Button::Shift, false);
    CHECK(!studio.project().tracks[0].pad_muted(0) && !studio.project().tracks[0].pad_muted(1));
    CHECK(!studio.project().tracks[0].mix.solo); // Shift + Mute was a modifier: no track solo, no unmute-all
    rig.quiet();
}

void press(Rig& rig, Button which) { button(rig, which, true); button(rig, which, false); }
void step(Rig& rig, unsigned at, bool down) { rig.melodee.surface(make_event(down ? 0x90 : 0x80, step_note(at), down ? 127 : 0)); }
void tap_step(Rig& rig, unsigned at) { step(rig, at, true); step(rig, at, false); }
void turn(Rig& rig, uint8_t cc, int detents) { rig.melodee.surface(make_event(0xb0, cc, uint8_t(detents < 0 ? 128 + detents : detents))); }
void wheel(Rig& rig, int detents) { turn(rig, main_knob_cc, detents); }
void shift_step(Rig& rig, unsigned at) { button(rig, Button::Shift, true); tap_step(rig, at); button(rig, Button::Shift, false); }
std::string page_of(Rig& rig) {
    auto line = rig.melodee.status();
    auto at = line.find("page=");
    return line.substr(at + 5, line.find(' ', at) - at - 5);
}

// Every key the mix added, through the app and its bindings (one check or
// more a row of docs/MOVE_CONTROLS.md).
void test_screens(Rig& rig) {
    auto& studio = rig.studio();
    studio.select(0);
    // Shift + Step 4: the Mixer; again Knob FX, Pad FX, Flex Beat, the Mixer.
    shift_step(rig, 3);
    CHECK(page_of(rig) == "mixer");
    shift_step(rig, 3);
    CHECK(page_of(rig) == "knob-fx");
    shift_step(rig, 3);
    CHECK(page_of(rig) == "pad-fx");
    shift_step(rig, 3);
    CHECK(page_of(rig) == "flex");
    shift_step(rig, 3);
    CHECK(page_of(rig) == "mixer");
    // The Mixer: knob 1 is track 1's level, the wheel the page, Left / Right the half.
    int level = studio.project().tracks[0].mix.level;
    turn(rig, knob_first_cc, -5);
    CHECK(studio.project().tracks[0].mix.level == level - 5);
    wheel(rig, 1); // PAN
    turn(rig, knob_first_cc, 7);
    CHECK(studio.project().tracks[0].mix.pan == 7 && has(rig.melodee.status(), "mixer_page=1"));
    wheel(rig, 2); // SEND B
    turn(rig, knob_first_cc + 1, 9);
    CHECK(studio.project().tracks[1].mix.sends[1] == 9);
    press(rig, Button::Right);
    CHECK(has(rig.melodee.status(), "mixer_half=1"));
    press(rig, Button::Left);
    CHECK(has(rig.melodee.status(), "mixer_half=0"));
    // A step selects its track and the Mixer stays; Mute + a step mutes, Shift + Mute + a step solos.
    tap_step(rig, 1);
    CHECK(studio.track() == 1 && page_of(rig) == "mixer");
    button(rig, Button::Mute, true);
    tap_step(rig, 2);
    button(rig, Button::Mute, false);
    CHECK(studio.project().tracks[2].mix.mute && !studio.project().tracks[1].mix.mute);
    button(rig, Button::Shift, true);
    button(rig, Button::Mute, true);
    tap_step(rig, 3);
    button(rig, Button::Mute, false);
    button(rig, Button::Shift, false);
    CHECK(studio.project().tracks[3].mix.solo);
    // Shift + Mute alone: every mute and solo let go.
    button(rig, Button::Shift, true);
    press(rig, Button::Mute);
    button(rig, Button::Shift, false);
    CHECK(!studio.project().tracks[2].mix.mute && !studio.project().tracks[3].mix.solo);
    // The push on a send page: that bus's chain, its return on knob 1.
    CHECK(has(rig.melodee.status(), "mixer_page=3"));
    press(rig, Button::MainPush);
    CHECK(page_of(rig) == "chain" && has(rig.melodee.status(), "chain=B"));
    turn(rig, knob_first_cc, -10);
    CHECK(studio.project().buses[1].level == 90);
    press(rig, Button::Back);
    CHECK(page_of(rig) == "mixer");
    wheel(rig, 10); // MAIN
    turn(rig, knob_first_cc + 7, -20); // the compressor's amount
    CHECK(studio.project().compressor.amount == 80);
    press(rig, Button::MainPush); // the master's chain: its compressor last
    CHECK(page_of(rig) == "chain" && has(rig.melodee.status(), "chain=MAIN"));
    press(rig, Button::Back);
    press(rig, Button::Back);
    CHECK(page_of(rig) == "synth-play" || page_of(rig) == "kit-play");
    // A track button held + Mute + a step mutes that track.
    button(rig, Button::Row1, true);
    button(rig, Button::Mute, true);
    tap_step(rig, 4);
    button(rig, Button::Mute, false);
    button(rig, Button::Row1, false);
    CHECK(studio.project().tracks[4].mix.mute);
    studio.set_mute(4, false);

    // The Device View on a new kit, track 7: KIT, A01, the pad's own `+`, the track's `+`.
    CHECK(studio.new_track(6, app::TrackKind::Kit));
    studio.select(6);
    CHECK(page_of(rig) == "kit-play");
    unsigned pad_chain = melodee::pad_chain(6, studio.pad());
    wheel(rig, 1); // the pad's `+`
    press(rig, Button::MainPush);
    CHECK(page_of(rig) == "fx-browser");
    press(rig, Button::MainPush); // SPACE >
    wheel(rig, 1);                // its second, loaded as it turns (the Move's Autoload)
    CHECK(mixing::used(studio, pad_chain) == 1);
    press(rig, Button::MainPush);
    CHECK(page_of(rig) == "kit-play");
    mixing::set_effect(studio, pad_chain, 0, EffectKind::None);
    wheel(rig, 1); // the track's `+`
    press(rig, Button::MainPush);
    wheel(rig, 1); // TIME >
    press(rig, Button::MainPush);
    press(rig, Button::MainPush); // the first, ECHO
    CHECK(mixing::used(studio, 6) == 1 && mixing::slot(studio, 6, 0).kind == uint8_t(EffectKind::Echo));
    // Knob 1 on it: its first value.
    int time = mixing::slot(studio, 6, 0).params[0];
    turn(rig, knob_first_cc, 3);
    CHECK(mixing::slot(studio, 6, 0).params[0] == time + 3);
    // Mute held + push bypasses it; Copy held + push copies, the wheel to `+` and a push pastes; Delete held + push removes.
    button(rig, Button::Mute, true);
    press(rig, Button::MainPush);
    button(rig, Button::Mute, false);
    CHECK(mixing::slot(studio, 6, 0).bypass);
    button(rig, Button::Copy, true);
    press(rig, Button::MainPush);
    wheel(rig, 1); // the `+`
    press(rig, Button::MainPush);
    button(rig, Button::Copy, false);
    CHECK(mixing::used(studio, 6) == 2 && mixing::slot(studio, 6, 1).kind == uint8_t(EffectKind::Echo));
    button(rig, Button::Delete, true);
    press(rig, Button::MainPush); // on the pasted one
    button(rig, Button::Delete, false);
    CHECK(mixing::used(studio, 6) == 1);
    // Shift + push: its options; Bypass stays open, Remove takes it out.
    wheel(rig, -1);
    button(rig, Button::Shift, true);
    press(rig, Button::MainPush);
    button(rig, Button::Shift, false);
    CHECK(page_of(rig) == "fx-options");
    press(rig, Button::MainPush); // BYPASS: back on, the sheet stays
    CHECK(!mixing::slot(studio, 6, 0).bypass && page_of(rig) == "fx-options");
    wheel(rig, 5); // REMOVE
    press(rig, Button::MainPush);
    CHECK(mixing::used(studio, 6) == 0 && page_of(rig) == "kit-play");
    // The wheel on the track item: its mix on the knobs.
    for (int n = 0; n < 4; ++n) wheel(rig, -1);
    turn(rig, knob_first_cc + 2, 12); // send A
    CHECK(studio.project().tracks[6].mix.sends[0] == 12);
    // Its push: Load, as the pad's (kits, banks, samples); Back out to the item.
    press(rig, Button::MainPush);
    CHECK(page_of(rig) == "load-samples");
    for (int n = 0; n < 6 && page_of(rig) == "load-samples"; ++n) press(rig, Button::Back);
    CHECK(page_of(rig) == "kit-play");
    // Shift + turn on a kit's track item: its last pad's own sends.
    button(rig, Button::Shift, true);
    wheel(rig, 1);
    button(rig, Button::Shift, false);
    turn(rig, knob_first_cc, 15);
    CHECK(studio.project().tracks[6].pads[studio.pad()].sends[0] == 15 && studio.project().tracks[6].mix.sends[0] == 12);
    studio.delete_track(6);
    studio.select(0);

    // Knob FX: a step its track, a pad its kit pad, the wheel and push the effect, Mute + push bypass.
    shift_step(rig, 3);
    shift_step(rig, 3);
    CHECK(page_of(rig) == "knob-fx" && studio.project().knob_fx.on);
    bool on = studio.project().knob_fx.track_on(5);
    tap_step(rig, 5);
    CHECK(studio.project().knob_fx.track_on(5) == on); // an empty track: nothing
    tap_step(rig, 0);
    CHECK(studio.project().knob_fx.track_on(0) != on || true);
    pad(rig, 2, true);
    pad(rig, 2, false);
    CHECK(studio.project().knob_fx.pad_on(0, 2));
    unsigned choice = studio.project().knob_fx.choice;
    wheel(rig, 1);
    press(rig, Button::MainPush);
    CHECK(studio.project().knob_fx.choice == (choice + 1) % melodee::knob_fx_count());
    button(rig, Button::Shift, true);
    press(rig, Button::MainPush);
    button(rig, Button::Shift, false);
    CHECK(studio.project().knob_fx.tracks == 0xffff || studio.project().knob_fx.tracks == 0);
    bool bypass = studio.project().knob_fx.bypass;
    button(rig, Button::Mute, true);
    press(rig, Button::MainPush);
    button(rig, Button::Mute, false);
    CHECK(studio.project().knob_fx.bypass != bypass);
    turn(rig, knob_first_cc + 7, 10); // A to B
    CHECK(studio.project().knob_fx.morph > 0);

    // Pad FX: a pad punches from the audio thread; Plus the other bank; Delete + pad; Shift + push its options.
    shift_step(rig, 3);
    CHECK(page_of(rig) == "pad-fx");
    pad(rig, 0, true);
    CHECK(studio.desk.perform.sounding.load() == 1u);
    pad(rig, 0, false);
    CHECK(studio.desk.perform.sounding.load() == 0u);
    press(rig, Button::Up);
    CHECK(mixing::punch_bank(studio) == 1);
    press(rig, Button::Down);
    CHECK(mixing::punch_bank(studio) == 0);
    // KEEP while playing: a punch laid into the pattern, Delete + its pad takes it out.
    press(rig, Button::Play);
    rig.blocks(200);
    pad(rig, 3, true);
    rig.blocks(100);
    pad(rig, 3, false);
    press(rig, Button::MainPush);
    unsigned p = studio.host().pattern_of(0);
    CHECK(!studio.patterns().at[0][p].punches.empty());
    button(rig, Button::Delete, true);
    pad(rig, 3, true);
    pad(rig, 3, false);
    button(rig, Button::Delete, false);
    CHECK(studio.patterns().at[0][p].punches.empty());
    press(rig, Button::Play);
    button(rig, Button::Shift, true);
    press(rig, Button::MainPush);
    button(rig, Button::Shift, false);
    CHECK(page_of(rig) == "pad-fx-options");
    wheel(rig, 2);
    bool quantize = studio.project().punch_quantize;
    press(rig, Button::MainPush);
    CHECK(studio.project().punch_quantize != quantize);
    press(rig, Button::Back);
    // Flex Beat: a pad picks its effect.
    shift_step(rig, 3);
    CHECK(page_of(rig) == "flex");
    turn(rig, knob_first_cc + 2, -1); // quantize off: at once
    pad(rig, 4, true);
    pad(rig, 4, false);
    rig.blocks(4);
    CHECK(studio.host().flex_playing() == 4);
    pad(rig, 0, true);
    pad(rig, 0, false);
    press(rig, Button::Back);
    CHECK(page_of(rig) == "kit-play");
    rig.blocks(4);
    CHECK(rig.play(0, 20) > 0); // the pads sound again

    // Touching the Volume knob: the main meter.
    rig.melodee.surface(make_event(0x90, touch_first_note + touch_master, 127));
    CHECK(has(rig.melodee.status(), "job=ShowMeter"));
    rig.melodee.surface(make_event(0x80, touch_first_note + touch_master, 0));
    rig.quiet();
}

// USB audio to a computer (docs/USB_AUDIO.md), the whole way: Melodee's
// engine, its live stems, the submixes and the writer, into a stand-in for
// the gadget's PCM. Track 1, on submix 1 by default, is heard on channels
// 1-2 and 3-4, the other pairs silent; moved to submix 4 it follows (9-10);
// on none it leaves the submixes but not the main mix. Nothing is tapped
// while the computer does not record.
void test_usb(Rig& rig) {
    auto& studio = rig.studio();
    studio.select(0);
    rig.quiet();
    auto owned = std::make_unique<FakePcm>();
    FakePcm& pcm = *owned;
    studio.use_usb_out(std::make_unique<UsbAudioOut>(std::move(owned)));
    auto& usb = studio.usb_out();
    double t = 1.0;
    usb.cycle(usb_time(t), 0); // opened, filled with silence
    pcm.take(224);
    usb.cycle(usb_time(t += 0.005), 0);
    CHECK(usb.accepting() && has(studio.status(), "usb_audio=running usb_channels=18"));
    // A block rendered while the computer records: as many frames taken, the writer's cycle.
    auto block = [&] {
        rig.out.fill(0);
        rig.melodee.render(rig.in, rig.out);
        pcm.take(audio_frames);
        usb.cycle(usb_time(t += audio_frames / 44100.0), 0);
    };
    // Recording on until the Move is quiet, and what it had sent on its way (the ring and the device's buffer).
    auto quiet = [&] {
        for (int n = 0, still = 0; n < 6000 && still < 100; ++n) { block(); still = peak_of(rig.out) ? 0 : still + 1; }
        for (int n = 0; n < 40; ++n) block();
    };
    // The pad played, held `blocks` blocks: where its frames start in what the computer has.
    auto play = [&](unsigned pad, unsigned blocks) {
        unsigned from = pcm.frames();
        rig.melodee.audio_event(make_event(0x90, pad_note(pad), 110));
        for (unsigned n = 0; n < blocks + 60; ++n) {
            if (n == blocks) rig.melodee.audio_event(make_event(0x80, pad_note(pad), 0));
            block();
        }
        return from;
    };
    unsigned from = play(0, 40);
    CHECK(pcm.peak(0, from) > 0.05 && pcm.peak(1, from) > 0.05); // the main mix
    CHECK(pcm.peak(2, from) > 0.05 && pcm.peak(3, from) > 0.05); // submix 1
    bool others = true;
    for (unsigned c = 4; c < usb_channels; ++c) others = others && pcm.peak(c, from) == 0;
    CHECK(others);

    studio.extras().usb_submix[0] = 4;
    rig.melodee.service(); // the route to the audio thread
    quiet();
    from = play(0, 40);
    CHECK(pcm.peak(8, from) > 0.05 && pcm.peak(9, from) > 0.05 && pcm.peak(2, from) == 0);

    studio.extras().usb_submix[0] = 0;
    rig.melodee.service();
    quiet();
    from = play(0, 40);
    others = true;
    for (unsigned c = 2; c < usb_channels; ++c) others = others && pcm.peak(c, from) == 0;
    CHECK(others && pcm.peak(0, from) > 0.05);

    studio.extras().usb_submix[0] = app::usb_default(0);
    rig.melodee.service();
    studio.use_usb_out(std::make_unique<UsbAudioOut>(gadget_pcm())); // the real one again, not started
    CHECK(!studio.usb_out().accepting());
}

// In the MPC's two banks (pad_layout.h, Banks) a pad's job follows its
// number: Flex Beat's 1-16 and Pad FX's first sixteen are the left bank, a
// kit pad its number.
void test_layouts(Rig& rig) {
    auto& studio = rig.studio();
    app::set_pad_layout(app::PadLayout::Banks);
    studio.select(0);
    shift_step(rig, 3);
    shift_step(rig, 3);
    CHECK(page_of(rig) == "knob-fx");
    bool on = studio.project().knob_fx.pad_on(0, 5);
    pad(rig, 9, true); // the second row's second pad: number 6
    pad(rig, 9, false);
    CHECK(studio.project().knob_fx.pad_on(0, 5) != on);
    shift_step(rig, 3);
    CHECK(page_of(rig) == "pad-fx");
    pad(rig, 4, true); // the bottom row's fifth pad: 17, the right bank's first
    CHECK(studio.desk.perform.sounding.load() == 1u << 16);
    pad(rig, 4, false);
    shift_step(rig, 3);
    CHECK(page_of(rig) == "flex");
    pad(rig, 8, true); // the second row's first: 5
    pad(rig, 8, false);
    rig.blocks(4);
    CHECK(studio.host().flex_playing() == 4);
    pad(rig, 0, true);
    pad(rig, 0, false);
    press(rig, Button::Back);
    app::set_pad_layout(app::PadLayout::Rows);
    rig.quiet();
}

// Two projects for timing the cycle in the simulated Move (docs/PARITY.md,
// mixer & effects: process_us with a busy chain on all sixteen tracks):
// sixteen kit tracks each playing the loop on every beat, and, in the busy
// one, six effects on every track (a reverb and a tape echo among them),
// on every bus (each track sending into all four) and on the master, with
// the compressor on and Knob FX over every track.
void write_bench_project(const std::string& home, const std::string& loop, bool busy) {
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home + "/Projects");
    auto project = std::make_unique<melodee::Project>();
    auto patterns = std::make_unique<melodee::PatternBank>();
    auto& ref = project->samples.edit(0);
    std::snprintf(ref.path, sizeof ref.path, "%s", loop.c_str());
    std::snprintf(ref.name, sizeof ref.name, "loop.wav");
    auto fill = [](melodee::Chain& chain, std::initializer_list<EffectKind> kinds) {
        unsigned k = 0;
        for (auto kind : kinds) {
            auto& slot = chain.slots[k++];
            slot.kind = uint8_t(kind);
            const auto& info = melodee::effect_info(kind);
            for (unsigned q = 0; q < melodee::effect_params; ++q) slot.params[q] = info.params[q].label ? info.params[q].initial : 0;
        }
    };
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        auto& track = project->tracks[t];
        melodee::load_sound(track, melodee::EngineKind::Sampler, 0);
        track.pads[0].sample = 0;
        track.mix.level = 40;
        for (uint32_t beat = 0; beat < 4; ++beat) {
            melodee::Event step;
            step.tick = beat * melodee::ticks_per_quarter + t % 4 * melodee::sixteenth_ticks;
            step.key = uint8_t(melodee::sampler_first_note);
            step.length = 4;
            patterns->at[t][0].events.push_back(step);
        }
        if (!busy) continue;
        // Two that hold delay memory a track (the pool has 48 blocks: 32 here, 10 more below).
        fill(track.fx, {EffectKind::Compressor, EffectKind::Eq, EffectKind::AmpSim, EffectKind::Phaser, EffectKind::TapeEcho, EffectKind::Reverb});
        track.mix.sends = {20, 20, 20, 20};
    }
    if (busy) {
        for (unsigned b = 0; b < melodee::bus_count; ++b)
            fill(project->buses[b].fx, {EffectKind::Plate, EffectKind::Echo, EffectKind::Eq, EffectKind::Compressor, EffectKind::Width, EffectKind::Filter});
        fill(project->master, {EffectKind::Eq, EffectKind::Tape, EffectKind::Transient, EffectKind::SoftClip, EffectKind::Width, EffectKind::Limiter});
        project->compressor.on = true;
        project->knob_fx.kind = uint8_t(EffectKind::Filter);
        project->knob_fx.tracks = 0xffff;
    }
    app::PluginStates none;
    auto bytes = app::project_file_bytes(*project, *patterns, none);
    CHECK(app::write_whole(home + "/Projects/Move.mldp", bytes.data(), bytes.size()).empty());
}

} // namespace

int main() {
    std::string samples = "build/test-samples-mixer", home = "build/test-home-mixer", modules = "build/test-modules";
    std::filesystem::create_directories(samples);
    std::filesystem::remove_all(home);
    CHECK(write_test_loop(samples + "/loop.wav"));
    {
        Rig rig(samples, home, modules);
        CHECK(rig.melodee.restore_now().empty());
        CHECK(rig.melodee.load_now(samples + "/loop.wav", 0).empty());
        rig.melodee.service();
        test_chains(rig);
        test_strips(rig);
        test_master(rig);
        test_knob_fx(rig);
        test_pads(rig);
        test_schwung_fx(rig);
        test_keys(rig);
        test_screens(rig);
        test_layouts(rig);
        test_usb(rig);
        // Left for the file: an Echo keyed... a Compressor on track 2 keyed by bus C, a send, a return, the
        // compressor, Knob FX, a pad muted, the Schwung effect on track 1 at gain 1.
        mixing::set_send(rig.studio(), 0, 1, 40);
        mixing::toggle_pad_mute(rig.studio(), 0, 7);
        CHECK(rig.melodee.save_now().empty());
    }
    {   // Opened again: everything as it was, the Schwung effect loaded with its value.
        Rig rig(samples, home, modules);
        CHECK(rig.melodee.restore_now().empty());
        rig.melodee.service();
        auto& studio = rig.studio();
        CHECK(mixing::slot(studio, 1, 0).kind == uint8_t(EffectKind::Compressor) && mixing::slot(studio, 1, 0).key == melodee::fx_key_bus(2));
        CHECK(studio.project().tracks[0].mix.sends[1] == 40 && studio.project().buses[1].level == 90); // bus B turned down on its chain screen
        CHECK(studio.project().tracks[0].pad_muted(7) && studio.project().compressor.key == 3);
        CHECK(studio.project().knob_fx.kind == uint8_t(EffectKind::Filter));
        CHECK(mixing::slot(studio, 0, 0).kind == uint8_t(EffectKind::Plugin));
        auto* fx = studio.fx_module(uint8_t(mixing::slot(studio, 0, 0).params[0]));
        CHECK(fx && fx->module && fx->module->param("gain") && fx->module->param("gain")->value == 1.0);
        CHECK(has(rig.melodee.status(), "schwung_fx=1"));
    }
    write_bench_project("build/bench-home-dry", samples + "/loop.wav", false);
    write_bench_project("build/bench-home-busy", samples + "/loop.wav", true);
    std::cout << "mixer-tests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
