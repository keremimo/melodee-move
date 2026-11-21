// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/model/print.h"
#include "melodee/model/project.h"
#include <string>

// A Melodee project as an Ableton Live Set, to go on with it in Live (Live
// 12.1 or later): "<name> Project/<name>.als" (gzip around Live's XML) and
// "<name> Project/Samples/Imported/" beside it, holding a copy of every file
// it plays. Paths in the set are relative to it, so the folder goes to a
// computer as it is (the web page downloads it as a zip).
//
// Each pattern a track has is a clip in Session View, in the scene of its
// number (A1 the first scene, B1 the seventeenth), each scene named and timed
// as the project's; the song is laid out in Arrangement View, each of its
// clips a looped clip of its pattern, its markers as locators and its loop
// brace as Live's loop, the tempo lane as tempo automation. Notes are written
// as they play: the pattern's feel and speed, the project's swing, ratchets
// as their hits and a kit lane's own length repeated through the pattern.
//
// A kit is a Drum Rack, a Simpler on each pad (its region, level, pan, tune,
// envelope, one shot or gate, loop, reverse, warp to the song, velocity
// layers; its mute group a choke group), pad n on note 36 + n (past 127 round
// again from 0). A pad its patterns play at other pitches (16 Levels) is a
// track of its own with a Simpler, its notes 60 + their pitch, as the MPC 3
// export makes it a keygroup track. A keygroup track is a Simpler, or a Sampler
// for an instrument of zones (an SFZ; Sampler is Live Standard's and Suite's).
// A synth track (a Schwung module) is a MIDI track of its notes, no
// instrument on it, and an audio track of its prints beside it; a MIDI track
// is a MIDI track of its notes, its program sent by each clip; an audio
// track's takes are audio clips. Each track's level, pan, mute, solo and sends;
// the buses it sends to are return tracks, at their level.
//
// Not carried: effects (Melodee's own and Schwung's), Motion, pad filters
// and sends, Cloud, cycle and random layers, trig conditions, the Looper.
// Reading and copying files: never on the MIDI or audio thread.
namespace app {

struct LiveExport {
    unsigned tracks = 0;      // Live tracks written (returns not counted)
    unsigned clips = 0;       // Session View clips
    unsigned arranged = 0;    // Arrangement View clips
    unsigned scenes = 0;
    unsigned notes = 0;       // in Session View's clips
    unsigned samples = 0;     // files copied into Samples/Imported
    unsigned missing = 0;     // files not where the project says
    unsigned skipped = 0;     // pads, layers, zones or takes not written (a SoundFont's sounds, a reversed take, ...)
};

// Writes <folder>/<name> Project/. Empty on success, else why not. `prints` and `prints_dir`: the project's prints
// (model/print.h prints_folder()), for its synth tracks; `audio_dir` (model/audio_track.h audio_folder()): its
// audio tracks' takes.
std::string export_live_set(const melodee::Project& project, const melodee::PatternBank& patterns, const std::string& folder,
                            const std::string& name, LiveExport& report, const melodee::PrintTable* prints = nullptr,
                            const std::string& prints_dir = {}, const std::string& audio_dir = {});

// Live's time signature number: numerator - 1 + 99 * log2(denominator) (4/4 is 201).
int live_time_signature(unsigned beats, unsigned unit);

} // namespace app
