// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/app.h"
#include <string>

// Runs one App on the Move, as a JACK client of `jackd -d move`
// (poundhard-armbian's native server; README.md). It owns:
//   audio    system:capture_1/2 in, system:playback_1/2 out, cut into
//            128-frame blocks for render() on JACK's process thread;
//   controls system:midi_capture, decoded for surface() on the MIDI thread;
//            system:midi_capture_ext for midi();
//   lights   lights() every 5 ms, changes only, to system:midi_playback;
//   screen   draw() up to 60 times a second, changed frames only (and once
//            a second regardless), to system:display.
// Shift + Back, SIGINT, SIGTERM, SIGHUP, --seconds, App::finished() or the
// JACK server going away all stop it; on the way out every light goes off
// and the screen is cleared.
namespace move {

struct RunOptions {
    std::string client_name = "melodee-move";
    double seconds = 0; // 0: until stopped
};

int run_app(App& app, const RunOptions& options);

// What the runtime notes in the session for each control before the app
// sees it: held pads, the last pad and its velocity, Shift. The offline
// tools call it too, so an app behaves the same there.
void track_input(Session& session, const Input& input);

} // namespace move
