// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// A WAV file written as it comes, for the exports and prints: 32-bit float
// (WAVE_FORMAT_IEEE_FLOAT, nothing clipped), or 24 or 16-bit integer PCM with
// TPDF dither of one step (two uniform randoms a sample, as Melodee dithers its
// integer mixdowns). The header's sizes are put in on close(). Off the audio
// thread only: it allocates and writes files.
namespace app {

class WavOut {
public:
    WavOut() = default;
    WavOut(const WavOut&) = delete;
    WavOut& operator=(const WavOut&) = delete;
    ~WavOut() { if (file) std::fclose(file); }

    // `data_at`: the frames start at that byte (a JUNK chunk before them), as
    // Melodee's prints keep them on a page for mapping; 0 right after the header.
    bool open(const std::string& path, unsigned rate, unsigned bits, unsigned channels, unsigned data_at = 0) {
        file = std::fopen(path.c_str(), "wb");
        if (!file) return false;
        this->bits = bits == 16 || bits == 24 ? bits : 32;
        this->channels = std::max(1u, channels);
        this->rate = rate;
        junk = data_at >= 44 + 8 ? data_at - 44 - 8 : 0;
        frames = 0;
        header();
        return !std::ferror(file);
    }
    // `count` frames, interleaved, `channels` floats each.
    bool add(const float* interleaved, size_t count) {
        if (!file) return false;
        size_t samples = count * channels;
        if (bits == 32) {
            ok &= std::fwrite(interleaved, sizeof(float), samples, file) == samples;
        } else {
            unsigned bytes = bits / 8;
            buffer.resize(samples * bytes);
            double scale = bits == 24 ? 8388607.0 : 32767.0, low = -scale - 1;
            for (size_t n = 0; n < samples; ++n) {
                double v = double(interleaved[n]) * scale + random() - random(); // TPDF: one step, triangular
                auto s = int32_t(std::lround(std::clamp(v, low, scale)));
                for (unsigned b = 0; b < bytes; ++b) buffer[n * bytes + b] = uint8_t(uint32_t(s) >> (8 * b));
            }
            ok &= std::fwrite(buffer.data(), 1, buffer.size(), file) == buffer.size();
        }
        frames += count;
        return ok;
    }
    // The sizes in place; false when anything failed on the way.
    bool close() {
        if (!file) return false;
        ok &= std::fflush(file) == 0;
        std::fseek(file, 0, SEEK_SET);
        header();
        ok &= std::fclose(file) == 0;
        file = nullptr;
        return ok;
    }
    uint64_t written() const { return frames; }
    uint64_t bytes() const { return 44 + (junk ? junk + 8 : 0) + frames * channels * (bits / 8); }

private:
    void put16(uint16_t v) { uint8_t b[2] {uint8_t(v), uint8_t(v >> 8)}; ok &= std::fwrite(b, 1, 2, file) == 2; }
    void put32(uint32_t v) { uint8_t b[4] {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; ok &= std::fwrite(b, 1, 4, file) == 4; }
    void header() {
        uint64_t data = frames * channels * (bits / 8);
        uint32_t data32 = uint32_t(std::min<uint64_t>(data, 0xffffffffull - 36));
        uint32_t extra = junk ? junk + 8 : 0;
        std::fwrite("RIFF", 1, 4, file);
        put32(36 + extra + data32);
        std::fwrite("WAVEfmt ", 1, 8, file);
        put32(16);
        put16(bits == 32 ? 3 : 1); // IEEE float, else PCM
        put16(uint16_t(channels));
        put32(rate);
        put32(rate * channels * (bits / 8));
        put16(uint16_t(channels * (bits / 8)));
        put16(uint16_t(bits));
        if (junk) {
            std::fwrite("JUNK", 1, 4, file);
            put32(junk);
            static const uint8_t zeros[256] {};
            for (unsigned left = junk; left;) { unsigned n = std::min(left, 256u); ok &= std::fwrite(zeros, 1, n, file) == n; left -= n; }
        }
        std::fwrite("data", 1, 4, file);
        put32(data32);
    }
    double random() { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return double(seed >> 11) / 9007199254740992.0; }
    std::FILE* file = nullptr;
    unsigned bits = 32, channels = 2, rate = 44100;
    unsigned junk = 0;
    uint64_t frames = 0, seed = 0x9e3779b97f4a7c15ull;
    bool ok = true;
    std::vector<uint8_t> buffer;
};

} // namespace app

namespace app {
// A WAV file of 32-bit float frames read whole (a print, as WavOut wrote it,
// or Melodee's on the MPC): the interleaved frames, its rate and channels.
// False when it is not one.
inline bool read_float_wav(const std::string& path, std::vector<float>& frames, unsigned& rate, unsigned& channels) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    auto u32 = [](const uint8_t* b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; };
    uint8_t head[12];
    bool ok = std::fread(head, 1, 12, f) == 12 && !std::memcmp(head, "RIFF", 4) && !std::memcmp(head + 8, "WAVE", 4);
    bool format = false;
    while (ok) {
        uint8_t chunk[8];
        if (std::fread(chunk, 1, 8, f) != 8) { ok = false; break; }
        uint32_t size = u32(chunk + 4);
        if (!std::memcmp(chunk, "fmt ", 4)) {
            uint8_t fmt[16];
            if (size < 16 || std::fread(fmt, 1, 16, f) != 16) { ok = false; break; }
            unsigned tag = fmt[0] | fmt[1] << 8, bits = fmt[14] | fmt[15] << 8;
            channels = fmt[2] | fmt[3] << 8;
            rate = u32(fmt + 4);
            format = (tag == 3 || tag == 0xfffe) && bits == 32 && channels;
            std::fseek(f, long(size - 16 + (size & 1)), SEEK_CUR);
        } else if (!std::memcmp(chunk, "data", 4)) {
            if (!format) { ok = false; break; }
            frames.resize(size / 4);
            ok = std::fread(frames.data(), 4, frames.size(), f) == frames.size();
            frames.resize(frames.size() / channels * channels);
            break;
        } else {
            std::fseek(f, long(size + (size & 1)), SEEK_CUR);
        }
    }
    std::fclose(f);
    return ok && format;
}
} // namespace app
