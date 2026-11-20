// SPDX-License-Identifier: AGPL-3.0-only
#include "app/screen.h"

namespace app {
namespace {

std::vector<ScreenEntry>& registry() {
    static std::vector<ScreenEntry> entries; // made on first use, before any registration needs it
    return entries;
}

} // namespace

const std::vector<ScreenEntry>& registered_screens() { return registry(); }

RegisterScreen::RegisterScreen(std::string id, ScreenFactory make, std::vector<Trigger> triggers, uint32_t home_of) {
    registry().push_back({std::move(id), std::move(make), std::move(triggers), home_of});
}

std::string home_screen(TrackKind kind) {
    for (const auto& entry : registry())
        if (entry.home_of & kind_bit(kind)) return entry.id;
    for (const auto& entry : registry())
        if (entry.home_of & kind_bit(TrackKind::Empty)) return entry.id;
    return {};
}

} // namespace app
