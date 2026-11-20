#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Runs on a Move on Ableton's firmware, as root, from tools/deploy-stock.sh.
# Ableton's app is stopped the way the Move's own shutdown stops it
# (/etc/init.d/move stop), whatever is left holding /dev/ablspi0.0 is ended
# (as Schwung's standalone launcher does), melodee-move runs with the device
# to itself and realtime priority, and Ableton's app is started again
# however it ends: Shift + Back, Ctrl-C, the SSH connection dropping, or a
# crash. Nothing is installed or changed; a reboot is stock Move, always.
# Arguments go to melodee-move.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" = 0 ] || { echo "Run as root." >&2; exit 1; }
[ -e /dev/ablspi0.0 ] || { echo "No /dev/ablspi0.0: this is not a Move on Ableton's firmware." >&2; exit 1; }

holders() {
    for process in /proc/[0-9]*; do
        for descriptor in "$process"/fd/*; do
            case "$(readlink "$descriptor" 2>/dev/null)" in
            /dev/ablspi*) echo "${process#/proc/}"; break ;;
            esac
        done
    done
}
restore() {
    pidof MoveLauncher >/dev/null 2>&1 || /etc/init.d/move start
}

spike=""
# Hang-up, Ctrl-C and TERM are passed to melodee-move, which turns the
# lights off and blanks the screen; one still running 5 seconds later is
# stuck, and is killed. The EXIT trap brings Ableton's app back.
stop_spike() {
    [ -n "$spike" ] || return 0
    kill -TERM "$spike" 2>/dev/null || return 0
    (sleep 5; kill -KILL "$spike" 2>/dev/null) &
}
trap stop_spike HUP INT TERM
trap restore EXIT

echo "Stopping Ableton's app (it comes back when melodee-move ends)..."
/etc/init.d/move stop
for name in MoveMessageDisplay MoveLauncher Move MoveOriginal schwung shadow_ui; do
    pids=$(pidof "$name") && kill $pids 2>/dev/null
done
tries=0
while [ -n "$(holders)" ] && [ "$tries" -lt 50 ]; do sleep 0.1; tries=$((tries + 1)); done
for pid in $(holders); do kill -9 "$pid" 2>/dev/null; done
[ -n "$(holders)" ] && sleep 0.5
if [ -n "$(holders)" ]; then
    echo "Something still holds /dev/ablspi0.0; giving up." >&2
    exit 1
fi

# In the background, so a signal reaches this script at once.
"$HERE/melodee-move" --backend spi "$@" &
spike=$!
status=0
while kill -0 "$spike" 2>/dev/null; do
    wait "$spike"
    status=$?
done
exit "$status"
