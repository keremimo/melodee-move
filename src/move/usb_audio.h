// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/usb_limits.h"
#include "dsp/frame_ring.h" // sample-base's, through melodee-sample's base/ (MELODEE_INCLUDES)
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// The Move as a sound card to a computer on USB-C (docs/USB_AUDIO.md). Once
// tools/usb-audio.sh has added its UAC2 gadget, the computer records 18
// channels from the Move: 1-2 the main mix, 3-18 eight stereo submixes.
//
// The audio thread push()es each block. A thread of its own finds the
// gadget's ALSA device, waits for the computer to record and writes to it.
// The Move's clock (the XMOS) and the computer's USB clock drift apart by tens
// of parts per million; drift_trim() nudges a band-limited converter so the
// ring between them stays near prime_frames, as sample-base does on the MPC
// (its docs/HARDWARE.md, "USB-C audio output"). The 5.15 kernel's u_audio
// has no Playback Rate control to say the computer records: the device's
// buffer draining says it instead, and 250 ms without room says it stopped.
namespace move {

enum class UsbState : uint8_t { Stopped, Missing, Waiting, Running, Failed };
const char* usb_state_name(UsbState state);

// What UsbAudioOut writes to: the gadget's playback PCM (gadget_pcm()), or a
// stand-in in the tests. Called from the writer's thread only.
class PcmOut {
public:
    struct Opened {
        unsigned channels = 0, width = 0; // Bytes a sample: 2, 3 or 4
        unsigned period = 0, buffer = 0;  // Frames
    };
    virtual ~PcmOut() = default;
    // The device's path, or empty when there is none (the gadget is off).
    virtual std::string find() = 0;
    // Opens `path` at `rate` with the channels and format the device has, as
    // large a buffer as it allows: false, closed, when it cannot.
    virtual bool open(const std::string& path, unsigned rate, Opened& opened) = 0;
    virtual void close() = 0;
    // Waits up to `ms` for room: 1 room (or an xrun, which the next write
    // reports), 0 none, or -errno.
    virtual int wait(int ms) = 0;
    // Frames taken, -EAGAIN when full, -EPIPE after an xrun, or another -errno.
    virtual long write(const uint8_t* bytes, unsigned frames) = 0;
    // Ready to write again after an xrun.
    virtual bool prepare() = 0;
};
// The UAC2 gadget's playback device through ALSA's ioctls on /dev/snd: the
// Move has no libasound.
std::unique_ptr<PcmOut> gadget_pcm();

struct UsbStats {
    UsbState state = UsbState::Stopped;
    unsigned channels = 0, bits = 0, period = 0, buffer = 0;
    unsigned buffered = 0, overflows = 0, underruns = 0, xruns = 0, opens = 0;
    int trim_ppm = 0;
};

class UsbAudioOut {
public:
    static constexpr unsigned rate = 44100;
    static constexpr unsigned ring_frames = 8192, prime_frames = 1024;
    using Clock = std::chrono::steady_clock;

    explicit UsbAudioOut(std::unique_ptr<PcmOut> pcm);
    ~UsbAudioOut();
    UsbAudioOut(const UsbAudioOut&) = delete;
    UsbAudioOut& operator=(const UsbAudioOut&) = delete;

    // The writer's thread; stop() closes the device. start() again restarts.
    void start();
    void stop();

    // Audio thread: true while the computer records, so that a block's
    // submixes are worth making.
    bool accepting() const { return accepting_now.load(std::memory_order_acquire); }
    // Audio thread: `frames` (at most usb_block_frames) of the main mix,
    // interleaved stereo S32, and the sixteen submix channels, each its own
    // row of `frames` floats (`submix[c * frames + f]`, pairs L then R), or
    // null for silence. Drops the block while not accepting. Never blocks.
    void push(const int32_t* mix, const float* submix, unsigned frames);

    UsbStats stats() const;
    // The status line's words: `usb_audio=running usb_channels=18 ...`.
    std::string status() const;

    // The writer's one pass, from its thread or a test: at most one wait of
    // `wait_ms` on the device, then as many periods as it has room for.
    void cycle(Clock::time_point now, int wait_ms);

private:
    using Ring = FrameRing<ring_frames, usb_channels>;
    static constexpr unsigned chunk_frames = 32;
    void run();
    void set_state(UsbState state) { current.store(uint8_t(state), std::memory_order_release); }
    void close_device(UsbState then);
    void check(Clock::time_point now);
    bool fill_period();

    std::unique_ptr<PcmOut> pcm;
    std::unique_ptr<Ring> ring = std::make_unique<Ring>(); // 590 KB: on the heap
    std::unique_ptr<SincResampler<usb_channels>> resampler = std::make_unique<SincResampler<usb_channels>>();
    std::array<float, usb_block_frames * usb_channels> block {}; // Audio thread: push()'s staging
    std::thread thread;
    std::atomic<bool> running {false}, accepting_now {false};
    std::atomic<uint8_t> current {uint8_t(UsbState::Stopped)};
    std::atomic<unsigned> open_channels {0}, open_width {0}, open_period {0}, open_buffer {0}, xruns {0}, opens {0};
    std::atomic<int> trim_ppm {0};

    // The writer's own (its thread, or cycle() in a test).
    bool open = false, full_once = false;
    PcmOut::Opened opened;
    std::vector<float> staged;  // Converted frames not yet encoded, usb_channels each
    std::vector<uint8_t> raw;   // One period, encoded
    unsigned pending = 0, offset = 0; // Encoded frames in `raw` still to write, and where they start
    std::array<float, chunk_frames * usb_channels> chunk {};
    Clock::time_point next_check {}, last_room {};
    double fill_sum = 0;
    unsigned fill_count = 0;
};

} // namespace move
