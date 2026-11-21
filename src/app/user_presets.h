// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>
#include <vector>

// A module's user presets as Schwung keeps them (its shadow_ui_presets.mjs),
// shared with Schwung: one file a preset, <folder>/<module-id>/<name>.json,
// {"name", "module", "version": 1, "state"}, where state is the module's own
// "state" blob (parsed when it is JSON, else the text). A preset saved here
// is in Schwung's My Presets and the other way round. Plain files, any thread
// but the audio thread.
namespace app {

// /data/UserData/schwung/presets on the Move: beside Schwung's shared code
// (MELODEE_SCHWUNG_ROOT's presets/ elsewhere, the tests'). Empty: none.
std::string user_presets_folder();

struct UserPreset { std::string name, file; };
// A module's presets, by name (case aside).
std::vector<UserPreset> user_presets(const std::string& folder, const std::string& module);
// One's state blob, as the module takes it; empty when it cannot be read.
std::string user_preset_state(const std::string& file);
// Saved under `name`, a name already taken getting a number ("Fat Brass 2"),
// as Schwung's Save As does; `overwrite`: the file of that name replaced
// (Save). The name it was saved as; empty when it could not be written.
std::string save_user_preset(const std::string& folder, const std::string& module, const std::string& name,
                             const std::string& state, bool overwrite);
bool delete_user_preset(const std::string& folder, const std::string& module, const std::string& name);

} // namespace app
