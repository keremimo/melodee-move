// SPDX-License-Identifier: AGPL-3.0-only
// The precompiled header (Makefile, PCH): JUCE's core and graphics, which
// most files read through widgets.h, studio.h or their own includes, parsed
// once instead of in each (about 0.6 s a file). Every file of ours is given
// it with -include; the few linked without JUCE go without it.
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
