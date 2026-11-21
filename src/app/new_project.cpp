// SPDX-License-Identifier: AGPL-3.0-only
// The one file that default-constructs melodee::Project (new_project.h).
#include "app/new_project.h"
#include "melodee/model/project.h"

namespace app {

std::unique_ptr<melodee::Project> new_project() { return std::make_unique<melodee::Project>(); }

} // namespace app
