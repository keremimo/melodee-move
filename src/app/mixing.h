// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/engine/effect_info.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/punch.h"
#include "melodee/model/project.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>

// Melodee's mix and effects on the Move (docs/UI.md, "Mixer and effects";
// docs/PARITY.md, mixer & effects): every change the screens make to a
// track's strip, a chain, a bus, the master and its compressor, Knob FX,
// Pad FX and Flex Beat, made in the project (so it is saved) and sent to the
// engine host as melodee::Commands, as melodee-sample's shell makes them
// (shell/context.cpp, knob_fx.cpp, pad_fx.cpp, mute_mode.cpp). The engine
// host does the sound: the 41 effects, the chains, buses, master,
// compressor and sidechain, Knob FX, the punches and Flex Beat.
//
// Every function but the audio thread's is called under the studio's lock.
namespace app {

class Studio;

namespace mixing {

// ---------------------------------------------------------------- chains
//
// Numbered as Melodee numbers them for commands: tracks 0-15, buses A-D
// 16-19, the master 20; a kit pad's own chain melodee::pad_chain(track, pad).
constexpr unsigned master = melodee::master_chain;
constexpr unsigned bus(unsigned b) { return melodee::bus_chain(b); }
constexpr bool is_pad_chain(unsigned chain) { return chain >= melodee::pad_chain_base; }
unsigned slots(const Studio& studio, unsigned chain);  // 6, a pad's 4
unsigned used(const Studio& studio, unsigned chain);   // slots in use, packed from the front
const melodee::EffectSlot& slot(const Studio& studio, unsigned chain, unsigned at);
// `kind` into slot `at` (None: removed, the rest close up); past the last
// used slot it goes into the next free one. Its initial values come with it.
void set_effect(Studio& studio, unsigned chain, unsigned at, melodee::EffectKind kind);
void set_param(Studio& studio, unsigned chain, unsigned at, unsigned param, int value);
void turn_param(Studio& studio, unsigned chain, unsigned at, unsigned param, int delta, bool fine);
void toggle_bypass(Studio& studio, unsigned chain, unsigned at);
void move_effect(Studio& studio, unsigned chain, unsigned at, int way); // -1 left, 1 right
// Copy and Paste: the slot onto a clipboard, and from it into slot `at` of
// any chain (a pad's chain takes no Schwung effect). False: nothing done.
void copy_effect(Studio& studio, unsigned chain, unsigned at);
bool paste_effect(Studio& studio, unsigned chain, unsigned at);
bool has_clipboard(const Studio& studio);
// A Compressor's or Gate's KEY (Melodee's FXKY): 0 itself, a track 1-16,
// a bus 17-20 (melodee::fx_key_*). A track never keys itself.
void set_key(Studio& studio, unsigned chain, unsigned at, unsigned key);
void turn_key(Studio& studio, unsigned chain, unsigned at, int delta);
std::string key_name(unsigned key); // "OFF", "TRACK 3", "BUS A"
// Every slot of a chain to the engine again (after a move, a paste).
void sync_chain(Studio& studio, unsigned chain);

// The effect as the Move shows it: four letters on the Device View's row
// ("ECHO", "REVB", "TAPE"), its name in capitals, its family.
std::string short_name(melodee::EffectKind kind);
std::string long_name(melodee::EffectKind kind);
// A parameter's value as the knob shows it ("1/8D", "40%", "-18"), and its
// place along the knob's bar.
std::string param_text(melodee::EffectKind kind, unsigned param, int value);
float param_fraction(melodee::EffectKind kind, unsigned param, int value);

// ---------------------------------------------------------------- strips
//
// Level 0..127 (100 unity), pan -50..50, a send and a return 0..100. The
// level, mute and solo are the Studio's (set_level, set_mute, set_solo).
void set_pan(Studio& studio, unsigned track, int pan);
void set_send(Studio& studio, unsigned track, unsigned bus, int value);
void set_return(Studio& studio, unsigned bus, int value);
// A kit pad's own send into each bus (Melodee's PSND), its mute (Mute held
// + pad, as the Move's Drum Rack pads), and every mute let go (Shift +
// Mute, Melodee's Unmute everything): the tracks' and the pads'.
void set_pad_send(Studio& studio, unsigned track, unsigned pad, unsigned bus, int value);
void toggle_pad_mute(Studio& studio, unsigned track, unsigned pad);
void unmute_all(Studio& studio);
float level_db(int level); // 0..127 as Melodee shows it, -inf at 0
std::string level_text(int level); // "-1.4DB", "0.0DB", "-INF"
std::string pan_text(int pan);     // "C", "L12", "R40"

// ---------------------------------------------------------------- the master compressor
//
// Melodee's MasterComp, as Command::Compressor numbers it: 0 on, 1 Color,
// 2 attack, 3 release, 4 amount, 5 In Boost, 6 Sidechain, 7 the key track,
// 8 the key pad (-1 the whole kit).
enum Comp : unsigned { CompOn, CompColor, CompAttack, CompRelease, CompAmount, CompBoost, CompSidechain, CompKey, CompKeyPad };
void set_compressor(Studio& studio, unsigned what, int value);
int compressor(const Studio& studio, unsigned what);

// ---------------------------------------------------------------- Knob FX
//
// The MPC's 28 and Melodee's 15 (melodee::knob_fx_entry); K1-K3 kept in two
// settings, A and B, the morph between them 0..127; on the tracks and kit
// pads switched on.
void knob_fx_load(Studio& studio, unsigned entry);
void knob_fx_param(Studio& studio, unsigned param, int value); // 0-5, or melodee::knob_morph
void knob_fx_turn(Studio& studio, unsigned param, int delta, bool fine);
void knob_fx_edit(Studio& studio, unsigned setting); // 0 A, 1 B: heard at it
void knob_fx_on(Studio& studio, bool on);
void knob_fx_bypass(Studio& studio);
void knob_fx_track(Studio& studio, unsigned track);          // on or off for that track
void knob_fx_pad(Studio& studio, unsigned track, unsigned pad); // on or off for that kit pad

// ---------------------------------------------------------------- Pad FX and Flex Beat
//
// The pads as punches (Pad FX) or Flex Beat's sixteen, read by the audio
// thread in the cycle the pad was read: what each pad does there is set
// here, from the MIDI thread, as atomics (Performance), and the audio
// thread turns a press into the engine's PunchOn / FlexBeat at once.
enum class PadsAre : int { Sound, Punches, Flex };
struct Performance {
    std::atomic<int> pads {int(PadsAre::Sound)};
    std::atomic<int> target {255};        // 255 the master, else the track
    std::atomic<int> timing {2};          // Command::PunchOn's pattern: snap | tail << 4 | 64 (at once)
    std::atomic<int> depth {100};         // a press's depth, before its pressure
    std::atomic<bool> latch {false};      // a punch stays on when let go, until pressed again
    std::array<std::atomic<int16_t>, 32> punch {}; // each pad's PunchKind, -1 none
    std::atomic<uint32_t> sounding {0};   // a bit a pad whose punch sounds (the screen's and the lights')
    std::atomic<int> flex_pad {0};        // Flex Beat's pad last pressed, 0 Empty
    std::atomic<bool> released {false};   // every punch let go (the MIDI thread's): the slots are free
    // The audio thread's own: the slot (1-4) each pad's punch sounds in, and
    // the pad in each slot, with when it came, so a fifth takes the oldest's.
    std::array<int8_t, 32> slot_of {};
    std::array<int8_t, 4> slot_pad {{-1, -1, -1, -1}};
    std::array<uint32_t, 4> slot_age {};
    uint32_t presses = 0;
    Performance() { for (auto& p : punch) p.store(-1); slot_of.fill(0); }
};

// What the mix keeps beside the project: the effect copied, and the pads'
// jobs for the audio thread.
struct Desk {
    melodee::EffectSlot clipboard {};
    bool clipped = false;
    Performance perform;
};

void pads_are(Studio& studio, PadsAre what);
unsigned punch_banks();                          // 2: 47 punches, 32 a bank
void punch_bank(Studio& studio, int way);        // +1 / -1: the other bank
unsigned punch_bank(const Studio& studio);
int punch_on(unsigned bank, unsigned pad);       // its PunchKind, -1 none
void set_punch_target(Studio& studio, bool track);
void set_punch_snap(Studio& studio, int snap);   // 0 1/4 .. 3 1/32
void set_punch_tail(Studio& studio, int tail);   // 0 cut, 1 fade, 2 ring
void set_punch_depth(Studio& studio, int depth); // 0..100
void toggle_punch_quantize(Studio& studio);
void toggle_punch_latch(Studio& studio);
void release_punches(Studio& studio);            // every punch lets go
void flex(Studio& studio, unsigned what, int value); // Command::FlexBeat: 0 the pad, 1 mode, 2 mix, 3 quantize

// Audio thread: a pad's press, release or pressure while the pads are
// punches or Flex Beat's: the engine's command at once. True: taken.
bool audio_pad(Performance& performance, melodee::EngineHost& engine, unsigned pad, bool pressed, int value, bool pressure);

} // namespace mixing
} // namespace app
