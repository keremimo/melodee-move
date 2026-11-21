// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The USB sound card's sizes (move/usb_audio.h has the writer), apart so
// that what only sizes its arrays by them (app/studio.h) is not rebuilt
// each time the writer changes.
namespace move {

constexpr unsigned usb_channels = 18, usb_submixes = 8, usb_block_frames = 128;

} // namespace move
