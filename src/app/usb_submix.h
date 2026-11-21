// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/usb_audio.h"
#include <algorithm>
#include <atomic>
#include <cstdint>

// The USB submixes a computer records on channels 3-18 (docs/USB_AUDIO.md),
// made on the audio thread from the engine's live stems
// (EngineHost::set_live_stems): each track's and bus's stereo, after its
// chain, level, pan and mute, added into the pair it is on. A copy: the main
// mix is left as it is.
namespace app {

// A stem's submix, from the project's own or the audio thread's copy.
inline uint8_t route_of(uint8_t value) { return value; }
inline uint8_t route_of(const std::atomic<uint8_t>& value) { return value.load(std::memory_order_relaxed); }

// `taps`: `stems` stereo taps, `taps[(stem * 2 + side) * frames + f]`.
// `route`: each stem's submix, 1-8, 0 for none. `submix`: the sixteen rows
// UsbAudioOut::push() takes, `submix[(pair * 2 + side) * frames + f]`, made
// here from silence. No allocation, no locks.
template <class Route>
void sum_submixes(const float* taps, const Route* route, unsigned stems, unsigned frames, float* submix) {
    std::fill(submix, submix + 2 * move::usb_submixes * frames, 0.0f);
    for (unsigned stem = 0; stem < stems; ++stem) {
        unsigned to = unsigned(route_of(route[stem]));
        if (!to || to > move::usb_submixes) continue;
        for (unsigned side = 0; side < 2; ++side) {
            const float* from = taps + (stem * 2 + side) * frames;
            float* into = submix + ((to - 1) * 2 + side) * frames;
            for (unsigned f = 0; f < frames; ++f) into[f] += from[f];
        }
    }
}

} // namespace app
