/* SPDX-License-Identifier: AGPL-3.0-only
 *
 * A stand-in Schwung sound generator for the tests: Schwung's v2 plug-in
 * API as a module built against Schwung's plugin_api_v1.h presents it, and
 * just enough of a synth to see the host's calls arrive. A square (or a
 * flat "sine" of half its height) while a note is held, its height the
 * level, the velocity and the pressure; ten presets, each setting the
 * level; get_param("note") the note held, "tempo" what the host's
 * schwung_move_info() says, "bpm" what get_bpm() says. set_param("stall",
 * milliseconds) holds the caller up that long. "table" is chosen from
 * "table_list", an items list. Its page (page.js) sets "searched" to what
 * is typed into it.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint32_t api_version;
    int sample_rate;
    int frames_per_block;
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
    int (*get_clock_status)(void);
    void *mod_emit_value;
    void *mod_clear_source;
    void *mod_host_ctx;
    float (*get_bpm)(void);
    int (*midi_inject_to_move)(const uint8_t *msg, int len);
    int (*slot_recv_channel)(void *instance);
    double (*get_beat_position)(void);
    void *reserved[8];
} host_api_v1_t;

typedef struct {
    uint32_t api_version;
    void *(*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;

typedef struct {
    float level, attack;
    int wave, voices, preset, table;
    int note, velocity, pressure;
    int phase;
    char searched[64];
} fake_t;

static const host_api_v1_t *host;

static void *create(const char *module_dir, const char *json_defaults) {
    (void)module_dir;
    (void)json_defaults;
    fake_t *fake = calloc(1, sizeof *fake);
    if (!fake) return NULL;
    fake->level = 0.5f;
    fake->attack = 0.1f;
    fake->wave = 1;
    fake->voices = 4;
    fake->note = -1;
    if (host && host->log) host->log("fake: created");
    return fake;
}

static void destroy(void *instance) { free(instance); }

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    fake_t *fake = instance;
    if (len < 3) return;
    uint8_t kind = msg[0] & 0xf0;
    if (kind == 0x90 && msg[2]) { fake->note = msg[1]; fake->velocity = msg[2]; fake->pressure = 0; }
    else if ((kind == 0x80 || kind == 0x90) && msg[1] == fake->note) fake->note = -1;
    else if (kind == 0xa0 && msg[1] == fake->note) fake->pressure = msg[2];
}

static void set_param(void *instance, const char *key, const char *val) {
    fake_t *fake = instance;
    if (!strcmp(key, "level")) fake->level = (float)atof(val);
    else if (!strcmp(key, "attack")) fake->attack = (float)atof(val);
    else if (!strcmp(key, "wave")) fake->wave = atoi(val);
    else if (!strcmp(key, "voices")) fake->voices = atoi(val);
    else if (!strcmp(key, "preset")) { fake->preset = atoi(val); fake->level = (float)fake->preset / 10.0f; }
    else if (!strcmp(key, "table")) fake->table = atoi(val);
    else if (!strcmp(key, "searched")) snprintf(fake->searched, sizeof fake->searched, "%s", val);
    else if (!strcmp(key, "stall")) usleep((useconds_t)atoi(val) * 1000); /* a module slow to set: milliseconds */
}

typedef int (*move_info_fn)(void *out, size_t cap);

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    fake_t *fake = instance;
    if (!strcmp(key, "level")) return snprintf(buf, (size_t)buf_len, "%g", fake->level);
    if (!strcmp(key, "attack")) return snprintf(buf, (size_t)buf_len, "%g", fake->attack);
    if (!strcmp(key, "wave")) return snprintf(buf, (size_t)buf_len, "%d", fake->wave);
    if (!strcmp(key, "voices")) return snprintf(buf, (size_t)buf_len, "%d", fake->voices);
    if (!strcmp(key, "preset")) return snprintf(buf, (size_t)buf_len, "%d", fake->preset);
    if (!strcmp(key, "table")) return snprintf(buf, (size_t)buf_len, "%d", fake->table);
    if (!strcmp(key, "searched")) return snprintf(buf, (size_t)buf_len, "%s", fake->searched);
    if (!strcmp(key, "table_list")) return snprintf(buf, (size_t)buf_len, "[{\"index\":0,\"label\":\"Sine\"},{\"index\":1,\"label\":\"Saw\"},{\"index\":2,\"label\":\"Organ\"}]");
    if (!strcmp(key, "preset_count")) return snprintf(buf, (size_t)buf_len, "10");
    if (!strcmp(key, "preset_name")) return snprintf(buf, (size_t)buf_len, "Fake %d", fake->preset);
    if (!strcmp(key, "note")) return snprintf(buf, (size_t)buf_len, "%d", fake->note);
    if (!strcmp(key, "pressure")) return snprintf(buf, (size_t)buf_len, "%d", fake->pressure);
    if (!strcmp(key, "bpm")) return snprintf(buf, (size_t)buf_len, "%g", host && host->get_bpm ? host->get_bpm() : -1.0f);
    if (!strcmp(key, "tempo")) {
        /* As Schwung's move_info_read(): the host's own answer, found in the process. */
        move_info_fn info = (move_info_fn)dlsym(RTLD_DEFAULT, "schwung_move_info");
        uint8_t out[256];
        if (!info || info(out, sizeof out) != 1) return snprintf(buf, (size_t)buf_len, "none");
        float tempo;
        memcpy(&tempo, out + 24, sizeof tempo); /* after size, version, changes and the eight bytes of flags and two of signature */
        return snprintf(buf, (size_t)buf_len, "%g", tempo);
    }
    return -1; /* chain_params, ui_hierarchy: module.json has them */
}

static int get_error(void *instance, char *buf, int buf_len) {
    (void)instance;
    (void)buf;
    (void)buf_len;
    return 0;
}

static void render(void *instance, int16_t *out, int frames) {
    fake_t *fake = instance;
    for (int n = 0; n < frames; ++n) {
        int16_t value = 0;
        if (fake->note >= 0) {
            float height = fake->level * (float)fake->velocity / 127.0f * (1.0f + (float)fake->pressure / 127.0f) * 12000.0f;
            if (fake->wave == 0) height /= 2;
            value = (int16_t)(((fake->phase++ / 50) % 2) ? height : -height);
        }
        out[2 * n] = out[2 * n + 1] = value;
    }
}

static plugin_api_v2_t api = {2, create, destroy, on_midi, set_param, get_param, get_error, render};

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *given) {
    host = given;
    return &api;
}
