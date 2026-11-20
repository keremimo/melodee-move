// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/kit_player.h"
#include "melodee/model/chop_slices.h"
#include "melodee/model/sampler.h"
#include <array>
#include <string>
#include <vector>

// The Move's kit as a Melodee project file (".mldp", melodee-sample's
// model/project_file.h, compiled unchanged), so a project made on the Move
// is a Melodee project: track 1 a sampler kit whose pads 1-32 are the
// Move's 32 pads, its samples by path. Melodee on the MPC opens it (where
// the paths lead to the same files).
//
// Melodee keeps a sample's chop in the sample's own WAV file, as the MPC
// does, not in the project. On the Move the samples are the owner's own
// library, which this leaves untouched: each sample's chop goes beside the
// project instead, in "<project>.chops", a line a sample.
namespace app {

struct KitFile {
    std::array<melodee::PadSound, pad_count> pads {};
    unsigned last_pad = 0;
    struct Sample { uint16_t slot = 0; std::string path, name; uint32_t frames = 0; melodee::SampleChop chop; };
    std::vector<Sample> samples;
};

// Writes `kit` to `path` (and its chops beside it), each file whole or not
// at all: a temporary file beside it, flushed, then renamed over it. Empty
// on success.
std::string save_kit(const KitFile& kit, const std::string& path);
// Reads `path` (and its chops) into `kit`. Empty on success; "missing" when
// there is no file.
std::string load_kit(const std::string& path, KitFile& kit);
// The synth track: which of Schwung's modules it plays and how it was set,
// beside the project in "<project>.synth" (a Melodee project has no place
// for a Schwung module). Text, a line a setting:
//
//   melodee-move synth 1
//   track 1            the track last played (0 the kit)
//   octave 0
//   module obxd        Schwung's module id; none: no line
//   preset 12          where the module has a preset list
//   param cutoff 0.5   each parameter as set_param() takes it
struct SynthFile {
    unsigned track = 0;
    int octave = 0;
    std::string module;
    int preset = -1;
    std::vector<std::pair<std::string, std::string>> values;
};
std::string save_synth(const SynthFile& synth, const std::string& project); // empty on success
void load_synth(const std::string& project, SynthFile& synth);              // a missing file: none
std::string synth_text(const SynthFile& synth);
void read_synth(const std::string& text, SynthFile& synth);

// The chops' own lines, apart, for tests.
std::string chops_text(const KitFile& kit);
void read_chops(const std::string& text, KitFile& kit);

// Running as root (over SSH), a file or folder made here is given to the
// owner of `reference` (the Move's /data/UserData: `ableton`), so Melodee
// can write it again when Schwung starts it as that user.
void give_to_owner_of(const std::string& path, const std::string& reference);

} // namespace app
