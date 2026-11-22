// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/model/chop_slices.h"
#include "melodee/model/limits.h"
#include "melodee/model/print.h"
#include "melodee/model/project.h"
#include "melodee/model/sampler.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The project as a file: Melodee's own ".mldp" (melodee-sample's
// model/project_file.h, compiled unchanged), the whole of it: sixteen
// tracks, their patterns, the samples by path and each one's chop (CHOP).
// Melodee on the MPC opens it, where the paths lead to the same files, and
// the Move opens the MPC's.
//
// A track playing one of Schwung's modules is a PLUGIN track: its entry of
// Project::plugins names the module's folder, and the entry's state (the
// file's PLUG section, where Melodee keeps a plug-in's own state) is the
// module's, as text (synth_text()). On the MPC the track stays named and
// silent; here it opens as it was.
namespace app {

using PluginStates = std::array<std::vector<uint8_t>, melodee::max_plugins>;

// The Move's own part of a project, beside Melodee's: kept in the file's
// Move section ("MVPJ", text: `colour N`, `track_colour T N`), which
// Melodee's reader skips, and covered by undo (history.h).
struct MoveExtras {
    std::array<uint8_t, melodee::track_count> track_colour {}; // a palette index; 0: the track's own (ui/track_style.h)
    uint8_t colour = 0;                                         // the project's, on its Set Overview pad; 0: none picked
    uint8_t spare[15] {};
};

// Each file whole or not at all: a temporary file beside it, flushed,
// renamed over it. Empty on success.
std::string write_whole(const std::string& path, const void* bytes, size_t size);
// `prints`: the project's prints and frozen tracks (PRNT, FRZN; model/print.h), when it has any.
std::vector<uint8_t> project_file_bytes(const melodee::Project& project, const melodee::PatternBank& patterns, const PluginStates& states,
                                        const MoveExtras* extras = nullptr, const melodee::PrintTable* prints = nullptr);
// Empty on success; "missing" when there is no file.
std::string read_project_file(const std::string& path, melodee::Project& project, melodee::PatternBank& patterns, PluginStates& states,
                              MoveExtras* extras = nullptr, melodee::PrintTable* prints = nullptr);
// The same from bytes (an MPC project imported, a test).
std::string read_project_bytes(const std::vector<uint8_t>& bytes, melodee::Project& project, melodee::PatternBank& patterns, PluginStates& states,
                               MoveExtras* extras = nullptr, melodee::PrintTable* prints = nullptr);

// A Schwung module's state, as its PLUG entry keeps it. Text, a line a
// setting (and, from the first scope, the whole of "<project>.synth"):
//
//   melodee-move synth 1
//   track 1            the track (the first scope's: the one played last)
//   octave 0           the bottom-left pad's octave, 0 for C2
//   module obxd        Schwung's module id; none: no line
//   preset 12          where the module has a preset list
//   param cutoff 0.5   each parameter as set_param() takes it
//   page browse {...}  each of the module's own pages: what it keeps in
//                      ctx.state, as JSON (a provider picked, where its
//                      menus are), by the page's parameter key
//   user_preset {...}  the user preset it is on, as Schwung's My Presets
//                      keeps it ({"name", "hash"}, schwung_pages.h)
struct SynthFile {
    unsigned track = 0;
    int octave = 0;
    std::string module;
    int preset = -1;
    std::vector<std::pair<std::string, std::string>> values;
    std::vector<std::pair<std::string, std::string>> pages;
    std::string user_preset;
};
std::string synth_text(const SynthFile& synth);
bool read_synth(const std::string& text, SynthFile& synth); // false: not a module's state (synth left empty)
std::vector<uint8_t> synth_state(const SynthFile& synth);
bool read_synth_state(const std::vector<uint8_t>& state, SynthFile& synth);

// The first scope's files, before tracks: "<project>" held the kit on
// track 1 alone, each sample's chop in "<project>.chops" (a line a
// sample) and the synth track in "<project>.synth". migrate() folds them
// into `project` (track 2 the synth, its module's folder found by
// `module_folder`) and `states`, and says which track was played last;
// false when there was nothing to fold in. The caller saves, then
// retire_side_files() keeps the old ones as "<file>.migrated".
struct KitFile { // the first scope's chops, read against the project's samples
    struct Sample { uint16_t slot = 0; std::string path, name; uint32_t frames = 0; melodee::SampleChop chop; };
    std::vector<Sample> samples;
};
std::string chops_text(const KitFile& kit);
void read_chops(const std::string& text, KitFile& kit);
bool migrate(const std::string& path, melodee::Project& project, PluginStates& states,
             std::string (*module_folder)(const std::string& id, const void* context), const void* context, unsigned& played);
void retire_side_files(const std::string& path);

// Running as root (over SSH), a file or folder made here is given to the
// owner of `reference` (the Move's /data/UserData: `ableton`), so Melodee
// can write it again when Schwung starts it as that user.
void give_to_owner_of(const std::string& path, const std::string& reference);

} // namespace app
