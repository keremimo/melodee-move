// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <memory>

namespace melodee { struct Project; }

namespace app {

// A new, empty melodee::Project. (Made in its own file while the build was
// on GCC 11, which spent about 90 s compiling each inlined construction;
// the image's GCC 12 takes under a second.)
std::unique_ptr<melodee::Project> new_project();

} // namespace app
