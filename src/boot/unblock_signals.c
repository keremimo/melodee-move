// SPDX-License-Identifier: AGPL-3.0-only
// unblock-signals COMMAND [ARGUMENT...]: runs COMMAND with no signal blocked.
//
// A blocked signal stays blocked through exec, and the Move's MoveLauncher,
// which starts Schwung's boot selector and so Melodee's boot entry, blocks
// INT, USR1 and TERM. A shell cannot unblock them itself; without this,
// module/boot-entry.sh would never see the TERM of `/etc/init.d/move stop`,
// and the stop would hang with Melodee holding the Move's device.
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: unblock-signals COMMAND [ARGUMENT...]\n");
        return 2;
    }
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    execvp(argv[1], argv + 1);
    fprintf(stderr, "unblock-signals: %s: %s\n", argv[1], strerror(errno));
    return 127;
}
