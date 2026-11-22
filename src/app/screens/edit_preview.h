// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/studio.h"
#include "melodee/engine/command.h"
#include <chrono>
#include <cstdint>

// A start or end moved by a knob, heard (Melodee's Chop, docs/UI.md "Kits"):
// once it has been still for 150 ms the sample plays once on the track's
// audition voice, a start from 300 ms before it on to the region's end, an
// end the region's last second. Each move puts it off again; nothing
// repeats. Chop's cuts, a kit pad's START and END (its Device View and
// TRIM) and a keygroup's one sample.
namespace app::screens {

struct EditPreview {
    using Clock = std::chrono::steady_clock;
    static constexpr auto still = std::chrono::milliseconds(150); // heard once still this long
    static constexpr uint32_t lead = 13230, tail = 44100;         // 300 ms before a start; an end's last second

    // What is heard of the region `start`-`end` once its start or end moved.
    struct Stretch { uint32_t from = 0, to = 0; };
    static Stretch stretch(uint32_t start, uint32_t end, bool end_moved) {
        if (end_moved) return {end > tail && end - tail > start ? end - tail : start, end};
        return {start > lead ? start - lead : 0, end};
    }

    // A start (false) or end (true) moved on `on_track`: heard once still.
    void moved(unsigned on_track, bool end_moved) {
        track = on_track;
        end = end_moved;
        due = Clock::now() + still;
    }
    // Every lights(): true once, when the moved one has been still long enough (`end` says which).
    bool ready() {
        if (due == Clock::time_point {} || Clock::now() < due) return false;
        due = {};
        return true;
    }
    // The region `start`-`region_end` of `sample` heard, as `stretch` has it.
    void play(Studio& studio, uint16_t sample, uint32_t start, uint32_t region_end) {
        auto heard = stretch(start, region_end, end);
        melodee::Command command {melodee::Command::Type::Audition, uint8_t(track), 0, 0, 100};
        command.pad.sample = sample;
        command.pad.start = heard.from;
        command.pad.end = heard.to;
        playing = studio.send(command);
    }
    // Gone (the screen left, the track changed): one still to come never
    // plays, one playing stops. Nothing else on the audition voice is
    // stopped (the browser's preview, Chop's take).
    void stop(Studio& studio) {
        due = {};
        if (!playing) return;
        playing = false;
        melodee::Command command {melodee::Command::Type::Audition, uint8_t(track)};
        command.pad.sample = melodee::no_sample;
        studio.send(command);
    }

    unsigned track = 0;
    bool end = false;     // the end moved (else the start)
    bool playing = false; // one was heard and not stopped
    Clock::time_point due {};
};

} // namespace app::screens
