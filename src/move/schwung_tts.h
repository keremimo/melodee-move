// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Schwung's screen reader speech, as its sidecar `schwung-tts` takes
// requests: the segment /schwung-tts, declared field for field from
// Schwung's src/host/tts_shm.h (MIT, Copyright (c) 2025-2026 Charles
// Vestal), the contract between its shim and the sidecar. The shim is not
// running while a standalone tool has the Move, so the tool plays its part:
// it makes the segment, starts the sidecar as its own child (which ends when
// its parent does), writes requests and drains the audio.
//
// The request block is a seqlock (req_seq odd while being written); the
// audio is an SPSC ring of int16 stereo samples counted by two monotonic
// counters; speak_ack and discard_until say which audio is the newest
// utterance's.
#include <cstdint>

namespace move::schwung_tts {

inline constexpr const char* segment = "/schwung-tts";
inline constexpr const char* sidecar = "/data/UserData/schwung/schwung-tts";
inline constexpr uint32_t magic = 0x31535454u; // "TTS1"
inline constexpr uint32_t version = 1;
inline constexpr unsigned text_length = 8192;
inline constexpr uint32_t ring_samples = 32768u; // int16, stereo interleaved
inline constexpr int engine_espeak = 0, engine_flite = 1;

struct Settings {
    int32_t enabled;
    int32_t volume; // 0..100
    int32_t engine;
    float speed;
    float pitch; // Hz
};

struct Shared {
    uint32_t magic;
    uint32_t version;
    // the speaker's (here: the tool's) requests
    volatile uint32_t req_seq;
    uint32_t speak_count;
    Settings want;
    char text[text_length];
    // the sidecar's
    volatile uint32_t sidecar_pid;
    volatile uint32_t state_seq;
    Settings state; // what it loaded at start: the user's settings
    volatile uint32_t discard_until;
    volatile uint32_t speak_ack;
    // the audio ring
    volatile uint32_t audio_write; // the sidecar's
    volatile uint32_t audio_read;  // the speaker's
    int16_t ring[ring_samples];
};

} // namespace move::schwung_tts
