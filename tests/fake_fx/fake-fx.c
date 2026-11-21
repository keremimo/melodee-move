/* SPDX-License-Identifier: AGPL-3.0-only
 *
 * A stand-in Schwung audio effect for the tests: Schwung's audio_fx v2 API
 * as a module built against its audio_fx_api_v2.h presents it, and just
 * enough of an effect to see the host's calls arrive. "gain" (0..2) scales
 * the block in place; get_param("blocks") counts the blocks processed and
 * get_param("frames") the frames of the last one.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct host_api_v1 host_api_v1_t; /* not looked into */

typedef struct {
    uint32_t api_version;
    void *(*create_instance)(const char *module_dir, const char *config_json);
    void (*destroy_instance)(void *instance);
    void (*process_block)(void *instance, int16_t *audio_inout, int frames);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
} audio_fx_api_v2_t;

typedef struct {
    float gain;
    long blocks;
    int frames;
} fake_fx_t;

static void *create(const char *dir, const char *config) {
    (void)dir; (void)config;
    fake_fx_t *fx = calloc(1, sizeof *fx);
    if (fx) fx->gain = 0.5f;
    return fx;
}
static void destroy(void *instance) { free(instance); }
static void process(void *instance, int16_t *audio, int frames) {
    fake_fx_t *fx = instance;
    for (int n = 0; n < 2 * frames; ++n) {
        float v = audio[n] * fx->gain;
        audio[n] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    fx->blocks++;
    fx->frames = frames;
}
static void set_param(void *instance, const char *key, const char *val) {
    fake_fx_t *fx = instance;
    if (!strcmp(key, "gain")) fx->gain = (float)atof(val);
}
static int get_param(void *instance, const char *key, char *buf, int len) {
    fake_fx_t *fx = instance;
    if (!strcmp(key, "gain")) return snprintf(buf, (size_t)len, "%g", fx->gain);
    if (!strcmp(key, "blocks")) return snprintf(buf, (size_t)len, "%ld", fx->blocks);
    if (!strcmp(key, "frames")) return snprintf(buf, (size_t)len, "%d", fx->frames);
    return -1;
}

static audio_fx_api_v2_t api = {2, create, destroy, process, set_param, get_param, NULL};

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    (void)host;
    return &api;
}
