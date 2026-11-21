// SPDX-License-Identifier: AGPL-3.0-only
#include "app/pad_layout.h"
#include <atomic>

namespace app {
namespace {

std::atomic<PadLayout> chosen {PadLayout::Rows};
static_assert(std::atomic<PadLayout>::is_always_lock_free, "the audio thread reads it");

} // namespace

PadLayout pad_layout() { return chosen.load(std::memory_order_relaxed); }

void set_pad_layout(PadLayout layout) { chosen.store(layout, std::memory_order_relaxed); }

const char* pad_layout_name(PadLayout layout) { return layout == PadLayout::Banks ? "banks" : "rows"; }

} // namespace app
