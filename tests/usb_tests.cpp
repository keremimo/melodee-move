// SPDX-License-Identifier: AGPL-3.0-only
// USB audio to a computer (src/move/usb_audio.*, docs/USB_AUDIO.md): the
// writer against a stand-in for the gadget's PCM, driven a cycle at a time
// with time handed in. No thread, no device.
#include "move/usb_audio.h"
#include "app/usb_submix.h"
#include "fake_pcm.h"
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace move;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)

UsbAudioOut::Clock::time_point at(double seconds) { return usb_time(seconds); }

// One block of the main mix and the submixes, every channel its own level:
// mix 0.25 and -0.25, submix channel c (c + 1) / 32.
struct Block {
    std::vector<int32_t> mix = std::vector<int32_t>(2 * usb_block_frames);
    std::vector<float> submix = std::vector<float>(2 * usb_submixes * usb_block_frames);
    Block() {
        for (unsigned f = 0; f < usb_block_frames; ++f) { mix[2 * f] = 1 << 29; mix[2 * f + 1] = -(1 << 29); }
        for (unsigned c = 0; c < 2 * usb_submixes; ++c)
            for (unsigned f = 0; f < usb_block_frames; ++f) submix[c * usb_block_frames + f] = float(c + 1) / 32.0f;
    }
};

// As the Move's audio thread and the computer go together: a block pushed,
// as many frames recorded, a cycle of the writer, every 2.9 ms.
double play(UsbAudioOut& usb, FakePcm& pcm, double from, unsigned blocks, bool recording = true) {
    static const Block block;
    for (unsigned n = 0; n < blocks; ++n) {
        usb.push(block.mix.data(), block.submix.data(), usb_block_frames);
        if (recording) pcm.take(usb_block_frames);
        usb.cycle(at(from), 0);
        from += 128.0 / 44100.0;
    }
    return from;
}

void test_missing_and_waiting() {
    auto owned = std::make_unique<FakePcm>();
    FakePcm& pcm = *owned;
    pcm.path.clear();
    UsbAudioOut usb(std::move(owned));
    usb.cycle(at(1), 0);
    CHECK(usb.stats().state == UsbState::Missing);
    CHECK(usb.status() == "usb_audio=missing");
    CHECK(!usb.accepting());

    // The gadget appears: opened at the next check, then filled with silence;
    // still waiting, as nothing has been taken yet.
    pcm.path = "/dev/snd/pcmC1D0p";
    usb.cycle(at(1.1), 0);
    CHECK(usb.stats().state == UsbState::Missing); // Not yet: checks are 250 ms apart
    usb.cycle(at(1.3), 0);
    CHECK(pcm.opens == 1);
    CHECK(usb.stats().state == UsbState::Waiting);
    CHECK(pcm.frames() == pcm.buffer);
    bool silent = true;
    for (uint8_t b : pcm.written) silent = silent && b == 0;
    CHECK(silent);
    CHECK(!usb.accepting());
    auto stats = usb.stats();
    CHECK(stats.channels == 18 && stats.bits == 32 && stats.period == 56 && stats.buffer == 896);

    // A block pushed while nothing records goes nowhere.
    Block block;
    usb.push(block.mix.data(), block.submix.data(), usb_block_frames);
    CHECK(usb.stats().buffered == 0);
}

void test_running_layout() {
    auto owned = std::make_unique<FakePcm>();
    FakePcm& pcm = *owned;
    UsbAudioOut usb(std::move(owned));
    usb.cycle(at(1), 0); // Opened and filled
    pcm.take(224);
    usb.cycle(at(1.01), 0);
    CHECK(usb.stats().state == UsbState::Running);
    CHECK(usb.accepting());
    CHECK(usb.status().rfind("usb_audio=running usb_channels=18 usb_bits=32", 0) == 0);

    double t = play(usb, pcm, 1.02, 200);
    // Once the ring has primed, every channel carries its own level: the mix
    // on 1-2, submix channel c on 3 + c.
    unsigned last = pcm.frames() - 1;
    CHECK(std::fabs(pcm.sample(last, 0) - 0.25) < 1e-4);
    CHECK(std::fabs(pcm.sample(last, 1) + 0.25) < 1e-4);
    bool levels = true;
    for (unsigned c = 0; c < 2 * usb_submixes; ++c) levels = levels && std::fabs(pcm.sample(last, 2 + c) - (c + 1) / 32.0) < 1e-4;
    CHECK(levels);
    CHECK(usb.stats().underruns == 0);
    CHECK(usb.stats().buffered >= UsbAudioOut::prime_frames / 2);

    // The computer stops recording: 250 ms without room, noticed at the next
    // check (within 500 ms), then waiting, and blocks are no longer taken.
    t = play(usb, pcm, t, 180, false);
    CHECK(usb.stats().state == UsbState::Waiting);
    CHECK(!usb.accepting());
    // And starts again: running from the next frames it takes.
    t = play(usb, pcm, t, 4);
    CHECK(usb.stats().state == UsbState::Running);
    CHECK(usb.accepting());

    // Late writing empties the buffer: prepared and filled again.
    pcm.xrun = true;
    usb.cycle(at(t), 0);
    CHECK(pcm.prepares == 1);
    CHECK(usb.stats().xruns == 1);
    play(usb, pcm, t + 0.003, 4);
    CHECK(usb.stats().state == UsbState::Running);

    // Stopping writes silence first while the computer takes it.
    pcm.take(pcm.buffer);
    usb.stop();
    CHECK(!pcm.is_open);
    CHECK(usb.stats().state == UsbState::Stopped);
    unsigned end = pcm.frames() - 1;
    CHECK(pcm.sample(end, 0) == 0.0 && pcm.sample(end, 5) == 0.0);
}

// A gadget set up with other sample sizes or fewer channels still gets what fits.
void test_formats() {
    for (unsigned width : {3u, 2u}) {
        auto owned = std::make_unique<FakePcm>();
        FakePcm& pcm = *owned;
        pcm.width = width;
        UsbAudioOut usb(std::move(owned));
        usb.cycle(at(1), 0);
        pcm.take(224);
        usb.cycle(at(1.01), 0);
        play(usb, pcm, 1.02, 200);
        unsigned last = pcm.frames() - 1;
        double step = width == 2 ? 1.0 / 32768 : 1.0 / 8388608;
        CHECK(std::fabs(pcm.sample(last, 0) - 0.25) < 2 * step);
        CHECK(std::fabs(pcm.sample(last, 17) - 16 / 32.0) < 2 * step);
        CHECK(usb.stats().bits == width * 8);
    }
    auto owned = std::make_unique<FakePcm>();
    FakePcm& pcm = *owned;
    pcm.channels = 2;
    UsbAudioOut usb(std::move(owned));
    usb.cycle(at(1), 0);
    pcm.take(224);
    usb.cycle(at(1.01), 0);
    play(usb, pcm, 1.02, 200);
    unsigned last = pcm.frames() - 1;
    CHECK(std::fabs(pcm.sample(last, 0) - 0.25) < 1e-4 && std::fabs(pcm.sample(last, 1) + 0.25) < 1e-4);

    // A device that will not open is a failure, tried again at each check.
    auto refusing = std::make_unique<FakePcm>();
    FakePcm& refused = *refusing;
    refused.refuse = true;
    UsbAudioOut failed(std::move(refusing));
    failed.cycle(at(1), 0);
    CHECK(failed.stats().state == UsbState::Failed);
    refused.refuse = false;
    failed.cycle(at(1.3), 0);
    CHECK(failed.stats().state == UsbState::Waiting);
}

// The submixes from the taps (app/usb_submix.h): each stem into its pair,
// two on one pair added, none and out-of-range left out.
void test_submix_sums() {
    constexpr unsigned stems = 20, frames = 4;
    std::vector<float> taps(stems * 2 * frames, 0.0f), submix(2 * usb_submixes * frames, 9.0f);
    auto tap = [&](unsigned stem, float left, float right) {
        for (unsigned f = 0; f < frames; ++f) { taps[(stem * 2) * frames + f] = left; taps[(stem * 2 + 1) * frames + f] = right; }
    };
    tap(0, 0.1f, 0.2f);   // track 1
    tap(1, 0.3f, -0.1f);  // track 2, on the same pair
    tap(5, 0.5f, 0.5f);   // track 6, on none
    tap(17, 0.25f, 0.0f); // bus B, on pair 8
    tap(9, 0.7f, 0.7f);   // track 10, routed past the eighth pair: left out
    std::array<uint8_t, stems> route {};
    route[0] = 3; route[1] = 3; route[5] = 0; route[17] = 8; route[9] = 9;
    app::sum_submixes(taps.data(), route.data(), stems, frames, submix.data());
    auto at = [&](unsigned pair, unsigned side) { return submix[(pair * 2 + side) * frames + 2]; };
    CHECK(std::fabs(at(2, 0) - 0.4f) < 1e-6f && std::fabs(at(2, 1) - 0.1f) < 1e-6f);
    CHECK(at(7, 0) == 0.25f && at(7, 1) == 0.0f);
    bool rest_silent = true;
    for (unsigned pair : {0u, 1u, 3u, 4u, 5u, 6u}) rest_silent = rest_silent && at(pair, 0) == 0.0f && at(pair, 1) == 0.0f;
    CHECK(rest_silent);
    // The audio thread's copy of the routes, atomics, sums the same.
    std::array<std::atomic<uint8_t>, stems> atomic_route {};
    for (unsigned n = 0; n < stems; ++n) atomic_route[n].store(route[n]);
    std::vector<float> again(submix.size(), 1.0f);
    app::sum_submixes(taps.data(), atomic_route.data(), stems, frames, again.data());
    CHECK(again == submix);
}

// The real device's finder: nothing to find where /proc/asound has no gadget.
void test_gadget_pcm() {
    auto pcm = gadget_pcm();
    std::string found = pcm->find();
    CHECK(found.empty() || found.rfind("/dev/snd/pcmC", 0) == 0);
    PcmOut::Opened opened;
    CHECK(!pcm->open("/dev/snd/no-such-pcm", 44100, opened));
}

} // namespace

int main() {
    test_missing_and_waiting();
    test_running_layout();
    test_formats();
    test_submix_sums();
    test_gadget_pcm();
    std::cout << "usb-tests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
