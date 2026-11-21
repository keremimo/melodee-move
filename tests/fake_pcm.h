// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// A stand-in for the UAC2 gadget's playback PCM (move/usb_audio.h), for the
// tests: as u_audio behaves, a buffer the writer fills, room coming back only
// as the computer records (take()). Everything written is kept, decoded by
// sample().
#include "move/usb_audio.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

struct FakePcm final : move::PcmOut {
    std::string path = "/dev/snd/pcmC1D0p";
    unsigned channels = move::usb_channels, width = 4, period = 56, buffer = 896;
    bool refuse = false, xrun = false, is_open = false;
    unsigned room = 0, opens = 0, prepares = 0, closes = 0;
    std::vector<uint8_t> written;

    std::string find() override { return path; }
    bool open(const std::string&, unsigned rate, Opened& opened) override {
        if (refuse || rate != 44100) return false;
        opened = {channels, width, period, buffer};
        room = buffer; is_open = true; ++opens;
        return true;
    }
    void close() override { is_open = false; ++closes; }
    int wait(int) override { return room || xrun ? 1 : 0; }
    long write(const uint8_t* bytes, unsigned frames) override {
        if (xrun) return -EPIPE;
        if (!room) return -EAGAIN;
        unsigned n = std::min(frames, room);
        written.insert(written.end(), bytes, bytes + size_t(n) * channels * width);
        room -= n;
        return long(n);
    }
    bool prepare() override { xrun = false; room = buffer; ++prepares; return true; }
    void take(unsigned frames) { room = std::min(buffer, room + frames); }

    unsigned frames() const { return unsigned(written.size() / (size_t(channels) * width)); }
    // Sample `channel` of written frame `frame`, -1..1.
    double sample(unsigned frame, unsigned channel) const {
        const uint8_t* at = written.data() + (size_t(frame) * channels + channel) * width;
        int32_t value = 0;
        for (unsigned b = 0; b < width; ++b) value |= int32_t(uint32_t(at[b]) << (8 * (4 - width + b)));
        return double(value) / 2147483648.0;
    }
    // The loudest of `channel` over frames [from, frames()).
    double peak(unsigned channel, unsigned from = 0) const {
        double most = 0;
        for (unsigned f = from; f < frames(); ++f) most = std::max(most, sample(f, channel) < 0 ? -sample(f, channel) : sample(f, channel));
        return most;
    }
};

// A moment on the writer's clock, `seconds` in.
inline move::UsbAudioOut::Clock::time_point usb_time(double seconds) {
    return move::UsbAudioOut::Clock::time_point {} + std::chrono::microseconds(int64_t(seconds * 1e6));
}
