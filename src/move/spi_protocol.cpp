// SPDX-License-Identifier: AGPL-3.0-only
#include "move/spi_protocol.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace move::spi {
namespace {

// The data bytes a USB-MIDI code index carries; 0 for one this host ignores.
unsigned code_index_size(uint8_t code) {
    switch (code) {
    case 0x8: case 0x9: case 0xa: case 0xb: case 0xe: return 3;
    case 0xc: case 0xd: return 2;
    case 0xf: return 1;
    default: return 0; // 0-3 reserved and system common, 4-7 SysEx pieces
    }
}

} // namespace

unsigned read_midi_in(const uint8_t* page, std::array<MidiIn, midi_in_slots>& events, unsigned& skipped) {
    unsigned count = 0;
    skipped = 0;
    for (unsigned slot = 0; slot < midi_in_slots; ++slot) {
        const uint8_t* event = page + in_midi + slot * 8;
        if (event[0] == 0) break; // an empty slot ends the list
        uint8_t code = event[0] & 0x0f;
        unsigned size = code_index_size(code);
        if (!size || !(event[1] & 0x80)) { ++skipped; continue; }
        MidiIn& in = events[count++];
        in.cable = event[0] >> 4;
        in.size = uint8_t(size);
        in.bytes = {event[1], size > 1 ? event[2] : uint8_t(0), size > 2 ? event[3] : uint8_t(0)};
        in.timestamp = DisplayServer::load32(event + 4);
    }
    return count;
}

void clear_midi_in(uint8_t* page) {
    std::memset(page + in_midi, 0, midi_in_slots * 8);
}

uint32_t usb_midi_packet(uint8_t cable, const uint8_t* bytes, size_t size) {
    if (size < 1 || !(bytes[0] & 0x80) || bytes[0] >= 0xf0) return 0;
    uint8_t code = bytes[0] >> 4; // for channel messages the code index is the status's kind
    if (size < code_index_size(code)) return 0;
    uint32_t packet = uint32_t((cable & 0x0f) << 4 | code) | uint32_t(bytes[0]) << 8;
    if (size > 1) packet |= uint32_t(bytes[1] & 0x7f) << 16;
    if (size > 2 && code_index_size(code) > 2) packet |= uint32_t(bytes[2] & 0x7f) << 24;
    return packet;
}

void write_midi_out(uint8_t* page, const uint32_t* packets, unsigned count) {
    count = std::min(count, midi_out_slots);
    for (unsigned slot = 0; slot < midi_out_slots; ++slot)
        DisplayServer::store32(page + out_midi + slot * 4, slot < count ? packets[slot] : 0);
}

void read_audio_in(const uint8_t* page, AudioBlock& block) {
    for (unsigned n = 0; n < audio_frames * audio_channels; ++n) {
        int16_t sample = int16_t(uint16_t(page[in_audio + 2 * n]) | uint16_t(page[in_audio + 2 * n + 1]) << 8);
        block[n] = int32_t(sample) * 65536;
    }
}

float Dither::uniform() {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; // xorshift32
    return float(state) * (1.0f / 4294967296.0f);
}

float Dither::next() { return uniform() + uniform() - 1.0f; }

void write_audio_out(uint8_t* page, const AudioBlock& block, Dither& dither) {
    bool silent = std::all_of(block.begin(), block.end(), [](int32_t sample) { return sample == 0; });
    for (unsigned n = 0; n < audio_frames * audio_channels; ++n) {
        long value = 0;
        if (!silent) {
            value = std::lrint(double(block[n]) * (1.0 / 65536.0) + double(dither.next()));
            value = std::clamp(value, -32768L, 32767L);
        }
        uint16_t bits = uint16_t(int16_t(value));
        page[out_audio + 2 * n] = uint8_t(bits);
        page[out_audio + 2 * n + 1] = uint8_t(bits >> 8);
    }
}

} // namespace move::spi
