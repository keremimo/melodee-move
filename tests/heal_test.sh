#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# The tool's privileged helper (src/boot/heal.c, build/realtime-helper) off
# the Move: what it refuses, and that it acts for melodee-move beside it on
# that program's own thread. A copy of sh named bin/melodee-move stands in
# for melodee-move, its own pid the thread. Docker gives root no CAP_SYS_NICE,
# so there the last step is SCHED_FIFO refused by the kernel, after every
# check of the helper's has passed; on a machine that allows it, done.
#   sh tests/heal_test.sh
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
HELPER="$ROOT/build/realtime-helper"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
FAILED=0
check() { # check WHAT EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected '$2', got '$3'"; FAILED=1; fi
}

"$HELPER" > /dev/null 2>&1
check "no argument: usage" 2 $?
"$HELPER" 12x > "$WORK/out" 2>&1
check "a TID that is not a number" "1 heal: TID is not a thread id; nothing changed" "$? $(cat "$WORK/out")"
"$HELPER" 0123 > "$WORK/out" 2>&1
check "a TID with a leading zero" 1 $?
"$HELPER" $$ > "$WORK/out" 2>&1
check "run by another program" "1 heal: only melodee-move beside it may run it; nothing changed" "$? $(cat "$WORK/out")"

mkdir -p "$WORK/tool/bin"
cp "$HELPER" "$WORK/tool/bin/heal"
cp "$(command -v sh)" "$WORK/tool/bin/melodee-move"
# Not its thread: pid 1 is never one of the stand-in's.
"$WORK/tool/bin/melodee-move" -c "\"$WORK/tool/bin/heal\" 1" > "$WORK/out" 2>&1
check "another process's thread" "1 heal: TID is not a thread of melodee-move; nothing changed" "$? $(cat "$WORK/out")"
# Its own thread: every check passes; the kernel has the last word.
"$WORK/tool/bin/melodee-move" -c "\"$WORK/tool/bin/heal\" \$\$" > "$WORK/out" 2>&1
result="$? $(cat "$WORK/out")"
case $result in
"0 " | "1 heal: SCHED_FIFO 70 refused: Operation not permitted") check "melodee-move's own thread" ok ok ;;
*) check "melodee-move's own thread" "0, or SCHED_FIFO refused by the kernel" "$result" ;;
esac
# A melodee-move elsewhere is not the one beside it.
mkdir -p "$WORK/other"
cp "$(command -v sh)" "$WORK/other/melodee-move"
"$WORK/other/melodee-move" -c "\"$WORK/tool/bin/heal\" \$\$" > "$WORK/out" 2>&1
check "a melodee-move not beside it" "1 heal: only melodee-move beside it may run it; nothing changed" "$? $(cat "$WORK/out")"
exit $FAILED
