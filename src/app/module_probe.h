// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>

// Loads each of Schwung's sound generators under `root` as Melodee's synth
// track would, prints what it found (parameters, pages, presets), plays a
// chord through it and times its renders. Offline: the Move's device is
// not touched, so it runs beside Ableton's app. 1 when one would not load
// or stayed silent.
namespace app {

int probe_modules(const std::string& root);

} // namespace app
