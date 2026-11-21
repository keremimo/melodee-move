#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Melodee as a boot target of Schwung's boot selector (Schwung 1.9 and
# later; its docs/BOOT_TARGETS.md). At power-on /opt/move/Move, Schwung's,
# shows "Loading Melodee - press Back to change" for 2 s, then execs this as
# the `ableton` user in place of Ableton's app: nothing holds /dev/ablspi0.0
# and the pads are dark. Melodee runs as from the Tools menu (`standalone`,
# its output in melodee-move.log). When it ends (Shift + Back, or a crash)
# the Move goes to Schwung, as after the Tools menu, so a quit never leaves
# the Move dark.
#
# Only the first start in a power-on is Melodee's. The selector runs again
# whenever Move is restarted: after any standalone tool from the Tools menu
# ends, Melodee too, and when Schwung restarts Move. Those come back to
# Schwung, or Melodee would open again on every quit. Two marks say a start
# is not the power-on's first, both in /dev/shm, which a reboot empties:
# this script's own, and Schwung's control block, which Schwung's shim
# makes when Move runs and leaves when it stops (Melodee chosen on Schwung
# Manager's Boot page with Schwung running starts at the next power-on).
#
# The shell stays in front of Melodee: `/etc/init.d/move stop` (a shutdown)
# signals this process, and the signal is passed on; a stop ends here,
# without starting Schwung. Ending through Schwung also keeps this process
# alive, so Schwung's watchdog never counts a Melodee that failed as a failed
# boot, and the 2 s window at the next power-on still chooses another target.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LOG="$HERE/melodee-move.log"
# For tests/boot_entry_test.sh; nothing on the Move sets these.
SCHWUNG_ENTRY=${SCHWUNG_ENTRY:-/data/UserData/schwung/schwung-entry.sh}
MOVE_ORIGINAL=${MOVE_ORIGINAL:-/opt/move/MoveOriginal}
BOOTED=${MELODEE_BOOTED:-/dev/shm/melodee-move-booted}
SCHWUNG_RAN=${MELODEE_SCHWUNG_RAN:-/dev/shm/schwung-control}

# MoveLauncher, which runs the selector, blocks INT, USR1 and TERM, and a
# blocked signal stays blocked through exec: this shell would never see a
# stop's TERM, and MoveLauncher would wait on it with Melodee holding the
# device. So the script starts once more with none blocked.
if [ -z "${MELODEE_UNBLOCKED:-}" ] && [ -x "$HERE/bin/unblock-signals" ]; then
    export MELODEE_UNBLOCKED=1
    exec "$HERE/bin/unblock-signals" /bin/sh "$0"
fi

note() { echo "boot-entry: $(date '+%F %T') $*" >> "$LOG" 2>/dev/null; }

to_schwung() {
    [ -x "$SCHWUNG_ENTRY" ] && exec "$SCHWUNG_ENTRY"
    exec "$MOVE_ORIGINAL"
}

if [ -e "$BOOTED" ] || [ -e "$SCHWUNG_RAN" ]; then
    note "Move restarted: to Schwung"
    to_schwung
fi
: > "$BOOTED" 2>/dev/null
[ -x "$HERE/standalone" ] || { note "no standalone beside this script: to Schwung"; to_schwung; }
note "power-on start"

"$HERE/standalone" &
melodee=$!
stop() {
    kill -TERM "$melodee" 2>/dev/null
    tries=0
    while kill -0 "$melodee" 2>/dev/null && [ "$tries" -lt 50 ]; do
        sleep 0.1
        tries=$((tries + 1))
    done
    kill -KILL "$melodee" 2>/dev/null
    note "stopped"
    exit 0
}
trap stop TERM INT
wait "$melodee"
note "Melodee ended ($?): to Schwung"
to_schwung
