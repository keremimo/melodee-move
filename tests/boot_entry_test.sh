#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# module/boot-entry.sh off the Move: a copy beside a stand-in `standalone`,
# with stand-ins for Schwung's entry and Ableton's app that write what ran.
#   sh tests/boot_entry_test.sh
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
FAILED=0
check() { # check WHAT EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected '$2', got '$3'"; FAILED=1; fi
}
ran() { cat "$WORK/ran" 2>/dev/null | tr '\n' ' ' | sed 's/ $//'; }

# A stand-in: records its name in $WORK/ran, then ends.
stand_in() {
    printf '#!/bin/sh\necho %s >> "%s/ran"\n' "$2" "$WORK" > "$1"
    chmod 755 "$1"
}
# A stand-in Melodee that runs until a TERM reaches it.
until_stopped() {
    cat > "$WORK/standalone" << EOF
#!/bin/sh
trap 'echo stopped >> "$WORK/ran"; exit 0' TERM
echo melodee >> "$WORK/ran"
while :; do sleep 0.1; done
EOF
    chmod 755 "$WORK/standalone"
}
# stop_entry PID: once Melodee runs, TERM to boot-entry.sh, as
# /etc/init.d/move stop sends it; its exit status in $status, 137 when it was
# still there 10 s later.
stop_entry() {
    tries=0
    while [ ! -s "$WORK/ran" ] && [ "$tries" -lt 50 ]; do sleep 0.1; tries=$((tries + 1)); done
    kill -TERM "$1"
    ( sleep 10 && kill -KILL "$1" ) 2> /dev/null &
    dog=$!
    wait "$1"
    status=$?
    kill "$dog" 2> /dev/null
    wait "$dog" 2> /dev/null
}
cp "$ROOT/module/boot-entry.sh" "$WORK/boot-entry.sh"
export SCHWUNG_ENTRY="$WORK/schwung-entry.sh" MOVE_ORIGINAL="$WORK/MoveOriginal" MELODEE_BOOTED="$WORK/booted" \
    MELODEE_SCHWUNG_RAN="$WORK/schwung-control"
stand_in "$SCHWUNG_ENTRY" schwung
stand_in "$MOVE_ORIGINAL" stock
reset() { rm -f "$WORK/ran" "$MELODEE_BOOTED" "$MELODEE_SCHWUNG_RAN"; }

# Power-on: Melodee, then Schwung when it ends.
reset
stand_in "$WORK/standalone" melodee
sh "$WORK/boot-entry.sh"
check "power-on runs Melodee, then Schwung" "melodee schwung" "$(ran)"
check "power-on leaves its mark" yes "$([ -e "$MELODEE_BOOTED" ] && echo yes)"

# Move restarted in the same power-on (a Tools menu quit): Schwung only.
rm -f "$WORK/ran"
sh "$WORK/boot-entry.sh"
check "a restart goes to Schwung" "schwung" "$(ran)"

# Melodee chosen on Schwung Manager's Boot page while Schwung runs: Move
# restarted before the next power-on still goes to Schwung.
reset
stand_in "$WORK/standalone" melodee
: > "$MELODEE_SCHWUNG_RAN"
sh "$WORK/boot-entry.sh"
check "a restart after Schwung ran goes to Schwung" "schwung" "$(ran)"

# No Schwung entry: Ableton's app.
reset
chmod 644 "$SCHWUNG_ENTRY"
sh "$WORK/boot-entry.sh"
check "without Schwung's entry, Ableton's app" "melodee stock" "$(ran)"
chmod 755 "$SCHWUNG_ENTRY"

# No standalone: never a dark Move.
reset
rm -f "$WORK/standalone"
sh "$WORK/boot-entry.sh"
check "without standalone, Schwung" "schwung" "$(ran)"

# A stop (/etc/init.d/move stop) reaches Melodee and starts nothing.
reset
until_stopped
sh "$WORK/boot-entry.sh" &
stop_entry $!
check "a stop ends Melodee, without Schwung" "melodee stopped" "$(ran)"
check "a stop ends the entry cleanly" 0 "$status"

# Started as MoveLauncher starts it, with INT, USR1 and TERM blocked: the
# entry starts again through bin/unblock-signals, so a stop still arrives.
HELPER="$ROOT/build/unblock-signals"
if "$HELPER" true 2> /dev/null && command -v python3 > /dev/null 2>&1; then
    reset
    until_stopped
    mkdir -p "$WORK/bin"
    cp "$HELPER" "$WORK/bin/unblock-signals"
    python3 -I -c 'import os, signal, sys
signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT, signal.SIGUSR1, signal.SIGTERM})
os.execv("/bin/sh", ["sh", sys.argv[1]])' "$WORK/boot-entry.sh" &
    stop_entry $!
    check "with TERM blocked by its parent, a stop ends Melodee" "melodee stopped" "$(ran)"
    check "with TERM blocked by its parent, the entry ends cleanly" 0 "$status"
else
    echo "skip a stop with TERM blocked by its parent (needs build/unblock-signals and python3)"
fi

[ "$FAILED" = 0 ] && echo "boot-entry: all passed"
exit "$FAILED"
