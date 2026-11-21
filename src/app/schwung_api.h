// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>

// Schwung's module ABI, as its modules are built against it: the host table
// a module is given and the v2 plug-in table it gives back. Declared here,
// field for field, from Schwung's src/host/plugin_api_v1.h (MIT, Charles
// Vestal; THIRD_PARTY.md); the layout is the contract, so it is checked.
//
// Every entry point of a module (create, destroy, on_midi, set_param,
// get_param, render_block) belongs to the audio thread once the instance
// is live; before that, Schwung's own loader creates it and reads its
// parameters on another thread, and so does Melodee.
extern "C" {

typedef int (*move_mod_emit_value_fn)(void* ctx, const char* source_id, const char* target, const char* param, float signal,
                                      float depth, float offset, int bipolar, int enabled);
typedef void (*move_mod_clear_source_fn)(void* ctx, const char* source_id);

struct host_api_v1_t {
    uint32_t api_version;
    int sample_rate;
    int frames_per_block;
    uint8_t* mapped_memory; // the device page; modules read input audio at audio_in_offset
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char* msg);
    int (*midi_send_internal)(const uint8_t* msg, int len);
    int (*midi_send_external)(const uint8_t* msg, int len);
    int (*get_clock_status)(void);
    move_mod_emit_value_fn mod_emit_value;
    move_mod_clear_source_fn mod_clear_source;
    void* mod_host_ctx;
    float (*get_bpm)(void);
    int (*midi_inject_to_move)(const uint8_t* msg, int len);
    int (*slot_recv_channel)(void* instance);
    double (*get_beat_position)(void);
    void* reserved[8]; // must stay null
};
static_assert(offsetof(host_api_v1_t, reserved) == 120, "Schwung's host table layout");

struct plugin_api_v2_t {
    uint32_t api_version;
    void* (*create_instance)(const char* module_dir, const char* json_defaults);
    void (*destroy_instance)(void* instance);
    void (*on_midi)(void* instance, const uint8_t* msg, int len, int source);
    void (*set_param)(void* instance, const char* key, const char* val);
    int (*get_param)(void* instance, const char* key, char* buf, int buf_len);
    int (*get_error)(void* instance, char* buf, int buf_len);
    void (*render_block)(void* instance, int16_t* out_interleaved_lr, int frames);
};
typedef plugin_api_v2_t* (*move_plugin_init_v2_fn)(const host_api_v1_t* host);

// An audio effect (Schwung's src/host/audio_fx_api_v2.h, field for field):
// what its Signal Chain loads from modules/audio_fx/<id>/<id>.so. It works
// on a block in place, stereo interleaved 16-bit.
struct audio_fx_api_v2_t {
    uint32_t api_version;
    void* (*create_instance)(const char* module_dir, const char* config_json);
    void (*destroy_instance)(void* instance);
    void (*process_block)(void* instance, int16_t* audio_inout, int frames);
    void (*set_param)(void* instance, const char* key, const char* val);
    int (*get_param)(void* instance, const char* key, char* buf, int buf_len);
    void (*on_midi)(void* instance, const uint8_t* msg, int len, int source); // may be null
};
typedef audio_fx_api_v2_t* (*move_audio_fx_init_v2_fn)(const host_api_v1_t* host);
static_assert(offsetof(audio_fx_api_v2_t, on_midi) == 48, "Schwung's audio effect table layout");

} // extern "C"

namespace schwung {
constexpr uint32_t host_api_version = 1, plugin_api_version_2 = 2;
constexpr const char* init_v2_symbol = "move_plugin_init_v2";
constexpr uint32_t audio_fx_api_version_2 = 2;
constexpr const char* audio_fx_init_v2_symbol = "move_audio_fx_init_v2";
constexpr int midi_source_internal = 0, midi_source_external = 2;
constexpr int clock_stopped = 1, clock_running = 2;
} // namespace schwung
