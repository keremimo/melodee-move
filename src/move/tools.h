// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/app.h"
#include <string>

// Offline modes: they run without JACK or a Move, in the build image.
namespace move {

// The press of a control as the surface would send it, for tests and the
// offline modes. `bytes` is the raw message.
SurfaceEvent make_event(uint8_t status, uint8_t number, uint8_t value);

// Renders a few seconds with pads struck and let go, writes them to a
// 16-bit WAV and fails (returns 1) when the app stayed silent.
int self_test(App& app, const std::string& wav_path);

// The app's screen as PNGs, four times size, and the frames as sent to the
// Move (.bin): at rest, and with pads held and knobs turned.
int write_previews(App& app, const std::string& directory);

// Another screen's PNGs (a file, or every PNG in a folder but the @2x ones)
// crushed to the Move's screen (display.h, crush), as PNGs four times size:
// <name>.letterbox.png and <name>.stretch.png thresholded, and
// <name>.dither.png stretched and dithered.
int crush_pictures(const std::string& input, const std::string& directory);

// A made-up two-bar drum loop at 120 BPM (kicks, snares, hats), as a 16-bit
// stereo WAV: something for Load and Chop to work on in tests and the
// simulated Move.
bool write_test_loop(const std::string& path);

} // namespace move
