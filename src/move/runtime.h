// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/app.h"
#include <string>

// Runs one App on the Move, through one of two hosts (host.h):
//   spi   the Move's own device, /dev/ablspi0.0, on Ableton's firmware
//         with Ableton's app stopped: launched from Schwung's Tools menu
//         (module/) or tools/run-stock.sh;
//   jack  a client of `jackd -d move` on poundhard-armbian.
// Either way: render() and audio_event() on the host's audio thread,
// surface()/midi()/lights() on a MIDI thread, draw() on a screen thread up
// to 60 times a second (changed frames only), service() on this thread.
// Shift + Back, SIGINT, SIGTERM, SIGHUP, --seconds, App::finished() or the
// host going away all stop it; on the way out every light goes off and the
// screen is cleared.
namespace move {

enum class Backend { Auto, Spi, Jack, SpiSimulated };

struct RunOptions {
    Backend backend = Backend::Auto; // Auto: spi where /dev/ablspi0.0 exists, else jack
    std::string client_name = "melodee-move";
    double seconds = 0;              // 0: until stopped
    // SpiSimulated: a cable from the outputs to the inputs this many frames
    // long, and control messages to play (spi_device.h, FakeSpiDevice).
    unsigned simulated_loopback_frames = 300;
    std::string simulated_script;
};

int run_app(App& app, const RunOptions& options);

// What the runtime notes in the session for each control before the app
// sees it: held pads, the last pad and its velocity, Shift. The offline
// tools call it too, so an app behaves the same there.
void track_input(Session& session, const Input& input);

} // namespace move
