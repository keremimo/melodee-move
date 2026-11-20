// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/app.h"
#include "move/display.h"
#include "move/queues.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

// What carries bytes between the runtime and the Move: JACK (`jackd -d
// move` on Armbian) or the Move's own device, /dev/ablspi0.0, on Ableton's
// firmware. A host runs App::render() on its audio thread, hands pad hits
// to App::audio_event() on that same thread before the render they sound
// in, and queues every message for the runtime's MIDI thread; the MIDI and
// screen threads send lights and frames back through the queues here.
namespace move {

class SpiDevice;

struct MidiIn {
    std::array<uint8_t, 3> bytes {};
    uint8_t size = 0;
    bool external = false;
    uint64_t timestamp_ns = 0;
};
using LedMessage = std::array<uint8_t, 3>; // as Lights writes them, channel 16

class Host {
public:
    virtual ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    virtual const char* name() const = 0;
    // Starts the exchange; throws with a reason when it cannot.
    virtual void start() = 0;
    // False once the server or device has gone away.
    virtual bool alive() const = 0;
    // Lets the last lights and a blank screen out, then stops, within `limit`.
    virtual void finish(std::chrono::milliseconds limit) = 0;

    int wake_descriptor() const { return wake_fd; }
    SpscRing<MidiIn, 1024> midi_in;
    SpscRing<LedMessage, 1024> leds;
    TripleBuffer<DisplayMessage> display;

protected:
    Host(App& app, Session& session);
    void wake();
    // Audio thread: one message from the Move's controls or an external
    // port. Pads go to App::audio_event() at once; everything is queued for
    // the MIDI thread. Sets `arrived` when the MIDI thread should wake.
    void incoming(const uint8_t* bytes, size_t size, bool external, uint64_t timestamp_ns, bool& arrived);
    static uint64_t monotonic_ns();

    App& app;
    Session& session;

private:
    int wake_fd = -1;
};

std::unique_ptr<Host> make_jack_host(App& app, Session& session, const std::string& client_name);
std::unique_ptr<Host> make_spi_host(App& app, Session& session, std::unique_ptr<SpiDevice> device);

} // namespace move
