#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Runs on the Move (poundhard-armbian), from tools/deploy.sh. The launcher
# menu draws the screen every JACK period, so it is stopped (never disabled)
# while the spike runs, and started again however the spike ends: Shift +
# Back, Ctrl-C, the SSH connection dropping, or a crash. Only if this script
# itself is killed does the menu stay stopped; a reboot, or
# `systemctl start move-launcher-menu`, brings it back. Arguments go to
# melodee-move.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
POUNDHARD_LIB=/data/UserData/poundhard/lib

if pgrep -f 'run-stack.sh|supernova|scsynth|sclang|phhost' >/dev/null 2>&1; then
    echo "An appliance is running. Leave it first (Back on the Move), then try again." >&2
    exit 1
fi
missing=$(LD_LIBRARY_PATH=$POUNDHARD_LIB ldd "$HERE/melodee-move" | grep 'not found')
if [ -n "$missing" ]; then
    echo "melodee-move needs libraries this Move does not have:" >&2
    echo "$missing" >&2
    echo "Install them with: apt-get install --no-install-recommends libfreetype6 libfontconfig1" >&2
    exit 1
fi

spike=""
# Hang-up (the SSH connection dropped), Ctrl-C and TERM are passed on to the
# spike as TERM: it turns its lights off and clears the screen, then the
# loop below sees it gone and the EXIT trap brings the menu back. A spike
# still running 5 seconds later is stuck, and is killed.
stop_spike() {
    [ -n "$spike" ] || return 0
    kill -TERM "$spike" 2>/dev/null || return 0
    (sleep 5; kill -KILL "$spike" 2>/dev/null) &
}
trap stop_spike HUP INT TERM
trap 'systemctl start move-launcher-menu.service' EXIT
systemctl stop move-launcher-menu.service

# In the background, so a signal reaches this script at once: a shell
# waiting on a foreground command holds its traps until that command ends,
# and on a hang-up only this script, the session leader, is told. The same
# libjack as jackd-move.service, so client and server match.
LD_LIBRARY_PATH=$POUNDHARD_LIB "$HERE/melodee-move" --backend jack "$@" &
spike=$!
status=0
while kill -0 "$spike" 2>/dev/null; do
    wait "$spike"
    status=$?
done
exit "$status"
