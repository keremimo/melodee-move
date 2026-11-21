// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/audio.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <sys/types.h>

namespace move {

namespace schwung_tts { struct Shared; }

// Speech through Schwung's screen reader: its sidecar `schwung-tts` (eSpeak
// NG, the user's voice, speed and volume from Schwung's settings), reached
// through the segment its shim uses (move/schwung_tts.h). A standalone tool
// has the Move without the shim, so this plays the shim's part: it starts
// the sidecar as a child, says text, and mixes what comes back into the
// Move's output. It speaks only while the user's screen reader is on, as
// Schwung saved it.
//
// start() and say() belong to one thread (the service thread); mix() to the
// audio thread, where it makes no system call and takes no lock.
class Speech {
public:
    Speech() = default;
    Speech(const Speech&) = delete;
    Speech& operator=(const Speech&) = delete;
    ~Speech(); // stops the sidecar

    // Starts Schwung's sidecar (`sidecar`, by default the Move's), if there
    // is one: false where there is not, and nothing is made. Env
    // MELODEE_TTS_SIDECAR and MELODEE_TTS_SEGMENT name others (the tests').
    bool start();
    bool start(const std::string& sidecar, const std::string& segment);
    void stop();
    // The sidecar has said what the user's settings are (polled here).
    bool ready();
    // The user's screen reader is on.
    bool enabled() { return ready() && on; }
    // Says `text`, in place of whatever is being said. False: not now.
    bool say(const std::string& text);
    // The speech so far, mixed into `output`.
    void mix(AudioBlock& output);
    // How many utterances were asked for and how much audio was mixed, for
    // the status line and the tests.
    uint32_t said() const { return speak_count.load(std::memory_order_relaxed); }
    // "none" (no sidecar), "waiting" (not heard from yet), "on", "off": the
    // service thread's view, for the status line.
    const char* state() const { return !shared ? "none" : !is_ready ? "waiting" : on ? "on" : "off"; }
    uint64_t mixed() const { return mixed_frames.load(std::memory_order_relaxed); }

private:
    schwung_tts::Shared* shared = nullptr;
    std::string segment_name;
    pid_t child = -1;
    // service thread
    uint32_t seen_state = 0;
    bool is_ready = false, on = false;
    // shared: the request count, which the audio thread compares with the
    // sidecar's ack (audio of an utterance not acted on yet is the old one)
    std::atomic<uint32_t> speak_count {0};
    std::atomic<bool> attached {false};
    // audio thread
    uint32_t seen_ack = 0;
    std::atomic<uint64_t> mixed_frames {0};
};

} // namespace move
