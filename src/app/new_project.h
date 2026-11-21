// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <memory>

namespace melodee { struct Project; }

namespace app {

// A new, empty melodee::Project. Build one here, never with
// std::make_unique<melodee::Project>() where it is needed: its default
// constructor (sixteen tracks of pads, chains and patterns' defaults,
// inlined) costs GCC -O2 about 90 s of compiling in every file that has
// one, and only new_project.cpp pays it (once, then ccache keeps it).
// Copying one is cheap anywhere.
std::unique_ptr<melodee::Project> new_project();

} // namespace app
