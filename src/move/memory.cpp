// SPDX-License-Identifier: AGPL-3.0-only
#include "move/memory.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MCL_ONFAULT
#define MCL_ONFAULT 4
#endif

namespace move {

std::string lock_memory() {
    // What is here now, read in and locked; then what comes, locked as it is touched.
    if (mlockall(MCL_CURRENT)) return std::string("mlockall failed: ") + std::strerror(errno);
    if (mlockall(MCL_CURRENT | MCL_FUTURE | MCL_ONFAULT) == 0) return "onfault";
    if (errno != EINVAL) return std::string("mlockall failed: ") + std::strerror(errno);
    return mlockall(MCL_CURRENT | MCL_FUTURE) == 0 ? "all" : std::string("mlockall failed: ") + std::strerror(errno);
}

void pin_loaded_code() {
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void*) {
        const uintptr_t page = uintptr_t(sysconf(_SC_PAGESIZE));
        for (int n = 0; n < info->dlpi_phnum; ++n) {
            const auto& header = info->dlpi_phdr[n];
            if (header.p_type != PT_LOAD || !header.p_memsz) continue;
            uintptr_t start = (info->dlpi_addr + header.p_vaddr) & ~(page - 1);
            uintptr_t end = (info->dlpi_addr + header.p_vaddr + header.p_memsz + page - 1) & ~(page - 1);
            mlock(reinterpret_cast<const void*>(start), end - start); // already locked: nothing to do; refused: as before
        }
        return 0;
    }, nullptr);
}

__attribute__((noinline)) void prefault_stack() {
    volatile char room[256 * 1024];
    for (size_t at = 0; at < sizeof room; at += 4096) room[at] = 0;
}

} // namespace move
