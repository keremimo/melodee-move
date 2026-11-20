// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

// The Move's own device, /dev/ablspi0.0 (spi_protocol.h), or a simulation
// of it for tests and the build image.
namespace move {

class SpiDevice {
public:
    virtual ~SpiDevice() = default;
    // The shared 4096-byte page.
    virtual uint8_t* page() = 0;
    // Exchanges the page with the XMOS: blocks until the next 128-frame
    // slot and returns once the new RX half is in. False on a failure.
    virtual bool transfer() = 0;
    // How long the driver says the last transfer took, in nanoseconds.
    virtual uint64_t transfer_ns() const { return 0; }
};

// Opens the real device. Throws with what to do when it cannot, above all
// when Ableton's app still holds it (only one process may).
std::unique_ptr<SpiDevice> open_move_device();

// A simulated XMOS. It paces transfers at 2.9 ms (or as fast as asked for,
// with `paced` false), asks for the screen's chunks 1 to 6 in turn and
// assembles them, plays out what was written into its input
// `loopback_frames` later (a cable from the outputs to the inputs), and
// sends a script of control messages ("frame:status,data1,data2;...");
// a message marked x ("frame:x0x90,60,100") comes from the external MIDI
// port (cable 2) instead, and what goes out there is kept for tests.
class FakeSpiDevice : public SpiDevice {
public:
    FakeSpiDevice(bool paced, unsigned loopback_frames, std::string script);
    ~FakeSpiDevice() override;
    uint8_t* page() override;
    bool transfer() override;

    // What the XMOS side saw, for tests.
    struct Seen;
    const Seen& seen() const;

private:
    struct State;
    std::unique_ptr<State> state;
};

struct FakeSpiDevice::Seen {
    uint64_t transfers = 0;
    uint64_t led_messages = 0, last_led_packet = 0;
    uint64_t external_messages = 0;   // packets out on cable 2 (MIDI out)
    std::vector<uint32_t> external;   // the first 4096 of them, as sent
    std::vector<uint64_t> external_at; // the transfer each went out with
    uint64_t chunks_answered = 0, chunks_wrong = 0, frames_completed = 0;
    uint8_t frame[1024] {};          // the last complete screen
    int32_t output_peak = 0;         // int16 units
};

} // namespace move
