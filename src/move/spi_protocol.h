// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/audio.h"
#include "move/display.h"
#include <array>
#include <cstddef>
#include <cstdint>

// The Move's own link between the CM4 and its XMOS I/O processor, as the
// `ablspi` kernel driver exposes it on /dev/ablspi0.0: one 4096-byte page,
// mmap'd, exchanged with the XMOS by one blocking ioctl per 128-frame block
// (44.1 kHz, 2.9 ms). Facts from Schwung's docs/SPI_PROTOCOL.md and
// schwung-spi (MIT; THIRD_PARTY.md), which read them from Ableton's GPL
// ablspi driver source.
//
//   TX (to the XMOS)          RX (from the XMOS)
//   0     MIDI out 20 x 4     2048  MIDI in 31 x 8 (USB-MIDI + timestamp)
//   80    display index       2296  display index requested
//   84    display chunk 172   2304  audio in 128 x 2 x int16
//   256   audio out 128 x 2 x int16
//   4088  (driver) u64 nanoseconds the last transfer took
//
// MIDI is USB-MIDI: byte 0 is the code index (low nibble) and the cable
// (high nibble). Cable 0 is the Move's own controls and lights, on MIDI
// channel 1; cable 2 is external USB MIDI.
//
// The screen is pulled: the XMOS asks for chunk N (1-6) in the RX index,
// the host answers with N and that chunk in the next TX. Unasked chunks are
// ignored. The 1024 bytes are the same page layout as a JACK MOVEDISP frame.
namespace move::spi {

constexpr const char* device_path = "/dev/ablspi0.0";
constexpr size_t page_size = 4096, transfer_size = 768;
constexpr unsigned long speed_hz = 20000000;
constexpr unsigned long ioctl_wait_and_send_with_size = 10, ioctl_set_speed = 11;
constexpr unsigned audio_rate = 44100;

constexpr size_t out_midi = 0, out_display_index = 80, out_display_data = 84, out_audio = 256;
constexpr size_t in_midi = 2048, in_display_index = 2296, in_audio = 2304, transfer_time = 4088;
constexpr unsigned midi_out_slots = 20, midi_in_slots = 31;
constexpr size_t display_chunk = 172;
constexpr unsigned display_chunks = 6;
constexpr uint8_t cable_controls = 0, cable_external = 2;

struct MidiIn {
    uint8_t cable = 0;
    std::array<uint8_t, 3> bytes {};
    uint8_t size = 0;
    uint32_t timestamp = 0; // The XMOS's own clock; units not yet known
};

// The MIDI events in an RX page, up to the first empty slot. The XMOS
// heartbeat (no status byte) and SysEx pieces are left out; `skipped`
// counts them.
unsigned read_midi_in(const uint8_t* page, std::array<MidiIn, midi_in_slots>& events, unsigned& skipped);
// Events stay in the RX page until overwritten: clear them once read.
void clear_midi_in(uint8_t* page);

// A channel message as a USB-MIDI packet on `cable`; 0 when it is not one.
uint32_t usb_midi_packet(uint8_t cable, const uint8_t* bytes, size_t size);
// Writes up to 20 packets and zeroes the rest of the MIDI-out slots.
void write_midi_out(uint8_t* page, const uint32_t* packets, unsigned count);

// Audio in: int16 to the app's S32.
void read_audio_in(const uint8_t* page, AudioBlock& block);
// Audio out: the app's S32 to int16, with triangular dither, except that an
// all-zero block stays digital silence.
class Dither {
public:
    float next(); // -1 .. 1 least significant bits, triangular
private:
    uint32_t state = 0x9e3779b9u;
    float uniform();
};
void write_audio_out(uint8_t* page, const AudioBlock& block, Dither& dither);

// Answers the XMOS's screen requests. Chunk 1 latches the newest frame, so
// the six chunks of one screen always come from one picture.
class DisplayServer {
public:
    // `newest` is called on chunk 1 and returns the frame to latch.
    template <class Newest> unsigned serve(uint8_t* page, Newest&& newest) {
        uint32_t index = load32(page + in_display_index);
        if (index < 1 || index > display_chunks) return 0;
        if (index == 1) latched = newest();
        size_t offset = (index - 1) * display_chunk;
        size_t length = index == display_chunks ? frame_bytes - offset : display_chunk;
        store32(page + out_display_index, index);
        for (size_t n = 0; n < length; ++n) page[out_display_data + n] = latched[offset + n];
        return index;
    }

    static uint32_t load32(const uint8_t* at) { return uint32_t(at[0]) | uint32_t(at[1]) << 8 | uint32_t(at[2]) << 16 | uint32_t(at[3]) << 24; }
    static void store32(uint8_t* at, uint32_t value) { for (int n = 0; n < 4; ++n) at[n] = uint8_t(value >> (8 * n)); }

private:
    Frame latched {};
};

} // namespace move::spi
