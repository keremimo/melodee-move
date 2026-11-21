// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>

namespace move {

// Realtime priority for the calling thread from the tool's privileged helper
// (src/boot/heal.c): bin/heal beside the program, which Schwung's
// schwung-heal installs root-owned and setuid (module/standalone stages it).
// For the `ableton` user Schwung runs tools as, which may not take SCHED_FIFO
// itself. Waits for the helper; call it before the thread's realtime work,
// never in it. True: the thread is SCHED_FIFO now. `why` says what happened.
bool realtime_by_helper(std::string& why);

// bin/heal beside `program` (a path to the running binary).
std::string helper_beside(const std::string& program);

} // namespace move
