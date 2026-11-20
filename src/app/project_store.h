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
// The chops' own lines, apart, for tests.
std::string chops_text(const KitFile& kit);
void read_chops(const std::string& text, KitFile& kit);

// Running as root (over SSH), a file or folder made here is given to the
// owner of `reference` (the Move's /data/UserData: `ableton`), so Melodee
// can write it again when Schwung starts it as that user.
void give_to_owner_of(const std::string& path, const std::string& reference);

} // namespace app
