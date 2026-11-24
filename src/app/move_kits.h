// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/shell/drum_rack.h"

// The Move's own drum kits (docs/MOVE_CONTROLS.md, Decisions, "Move drum
// kits"): Ableton's Drum Rack Track Presets, read by ellic-core
// (shell/drum_rack.h) from the Move's libraries. The Core Library's are in
// /data/CoreLibrary/Track Presets/Drums/<style>/ (read only), the User
// Library's in /data/UserData/UserLibrary/Track Presets/.
namespace app {

// Where the Move keeps the libraries an "ableton:/" sample URI names.
inline const melodee::AbletonLibraries& move_libraries() {
    static const melodee::AbletonLibraries libraries {"/data/CoreLibrary", "/data/UserData/UserLibrary"};
    return libraries;
}

} // namespace app
