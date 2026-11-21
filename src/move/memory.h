// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>

namespace move {

// Memory the audio thread never waits for, without filling what is only set
// aside. Everything mapped at the start is read in and locked; what is mapped
// later is locked as it is first touched (MCL_ONFAULT), not when it is made:
// Melodee's stream pager sets aside room for a whole sample and fills it a
// chunk at a time (engine/stream_pager.h), and MCL_FUTURE alone would make
// all of that memory at once and keep the pager from giving any back.
// Returns what was done, for the log: "onfault", "all" (a kernel without
// MCL_ONFAULT) or why it failed.
std::string lock_memory();

// Every loaded library's code and data read in and locked now: called after a
// dlopen() (Schwung's modules, libjack), so their first call on the audio
// thread reads nothing from the card.
void pin_loaded_code();

// The calling thread's stack touched and so locked to this depth: first thing
// on a thread that renders, so it never faults a page in mid-block.
void prefault_stack();

} // namespace move
