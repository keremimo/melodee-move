#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Ends a melodee-move run on a Move from here, as Shift + Back would: the
# program is asked to stop (it turns the lights off and blanks the screen),
# killed if still there after 5 seconds, and Ableton's app is started again
# if it is not running. Changes nothing else.
#
#   tools/stop-stock.sh [HOST]
set -eu
HOST=${1:-move.local}
ssh "root@$HOST" '
running=$(pidof melodee-move) || { echo "melodee-move is not running."; }
if [ -n "${running:-}" ]; then
    kill -TERM $running
    tries=0
    while pidof melodee-move >/dev/null && [ "$tries" -lt 50 ]; do sleep 0.1; tries=$((tries + 1)); done
    if pidof melodee-move >/dev/null; then kill -KILL $(pidof melodee-move); echo "melodee-move killed."; else echo "melodee-move stopped."; fi
    sleep 2 # run-stock.sh starts Ableton'"'"'s app on its way out
fi
pidof MoveLauncher >/dev/null || /etc/init.d/move start
pidof MoveLauncher >/dev/null && echo "Ableton'"'"'s app is running."
'
