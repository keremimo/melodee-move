// SPDX-License-Identifier: AGPL-3.0-only
// melodee-move's privileged helper, the one Schwung lets a standalone tool
// have (Schwung's docs/MODULES.md, "standalone tools, and a privileged
// helper of their own"). module/standalone stages it from
// bin/realtime-helper as bin/heal.new and runs Schwung's schwung-heal, which
// installs it as bin/heal, root-owned 04755.
//
// One job: Move's own audio priority, SCHED_FIFO 70, for one thread of the
// melodee-move that runs it (src/move/realtime.cpp, from its audio thread).
//
//   heal TID
//
// It changes nothing, and says why on stderr, unless
// - its parent is bin/melodee-move beside it (/proc/PPID/exe), and
// - TID is one of that parent's threads (/proc/PPID/task/TID).
// The parent waits on it, so neither can go away in between. It takes no
// other argument, reads no environment and no file outside /proc, and writes
// nothing: no other process, priority or policy is within its reach.
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { audio_priority = 70 }; // src/move/spi_host.cpp's, and MoveOriginal's

static int refuse(const char* why) {
    fprintf(stderr, "heal: %s; nothing changed\n", why);
    return 1;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: heal TID\n");
        return 2;
    }
    const char* text = argv[1];
    size_t length = strlen(text);
    if (length == 0 || length > 9 || strspn(text, "0123456789") != length || text[0] == '0')
        return refuse("TID is not a thread id");
    int tid = 0;
    for (size_t n = 0; n < length; ++n) tid = tid * 10 + (text[n] - '0');

    pid_t parent = getppid();
    if (parent <= 1) return refuse("no program ran it");
    // melodee-move beside this helper, as the kernel names both.
    char expected[PATH_MAX], running[PATH_MAX], path[64];
    ssize_t size = readlink("/proc/self/exe", expected, sizeof expected - 1);
    if (size <= 0) return refuse("cannot see where it is");
    expected[size] = '\0';
    char* slash = strrchr(expected, '/');
    const char program[] = "melodee-move";
    if (!slash || (size_t)(slash + 1 - expected) + sizeof program > sizeof expected) return refuse("cannot see where it is");
    memcpy(slash + 1, program, sizeof program);
    snprintf(path, sizeof path, "/proc/%d/exe", (int)parent);
    size = readlink(path, running, sizeof running - 1);
    if (size <= 0) return refuse("cannot see what ran it");
    running[size] = '\0';
    if (strcmp(running, expected) != 0) return refuse("only melodee-move beside it may run it");

    struct stat thread;
    snprintf(path, sizeof path, "/proc/%d/task/%d", (int)parent, tid);
    if (stat(path, &thread) != 0) return refuse("TID is not a thread of melodee-move");

    struct sched_param priority = {.sched_priority = audio_priority};
    if (sched_setscheduler(tid, SCHED_FIFO, &priority) != 0) {
        fprintf(stderr, "heal: SCHED_FIFO %d refused: %s\n", audio_priority, strerror(errno));
        return 1;
    }
    return 0;
}
