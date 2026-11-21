#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Puts dist/schwung-tool/melodee-move into Schwung's tools on a Move
# (/data/UserData/schwung/modules/tools/melodee-move, owned by ableton as
# every module is), so Schwung's Tools menu can launch it with the Move to
# itself. Schwung reads its tools folder each time the Tools menu opens, so
# no restart.
#
# Where Schwung has its boot selector (1.9 and later), Melodee is also
# registered as a boot target (/data/UserData/boot-targets/melodee-move,
# Schwung's docs/BOOT_TARGETS.md): one more row in Schwung's choice of what
# the Move starts in, nothing more. What it starts in stays the user's
# choice, made in Schwung: Back during the 2 s "Loading ..." window at
# power-on, or Schwung Manager's Boot page (http://move.local:7700/boot).
# --remove takes the tool and its boot target out again, and gives
# power-on back to Schwung if it was Melodee's.
#
#   tools/install-schwung-tool.sh [HOST] [--previews DIR] [--remove]
set -eu
# macOS tar would add AppleDouble ._ files for extended attributes.
export COPYFILE_DISABLE=1
cd "$(dirname "$0")/.."
HOST=move.local
PREVIEWS=""
REMOVE=0
while [ $# -gt 0 ]; do
    case "$1" in
    --previews) PREVIEWS=$2; shift 2 ;;
    --remove) REMOVE=1; shift ;;
    *) HOST=$1; shift ;;
    esac
done
REMOTE=/data/UserData/schwung/modules/tools/melodee-move
TARGETS=/data/UserData/boot-targets
TARGET=$TARGETS/melodee-move
SELECTOR=/data/UserData/schwung/bin/boot-select
if [ "$REMOVE" = 1 ]; then
    # As Schwung's own uninstall does. The selector runs as ableton and
    # rewrites `default` when its picker chooses, so it stays ableton's.
    ssh "root@$HOST" "rm -rf $REMOTE $TARGET
        if [ \"\$(head -n 1 $TARGETS/default 2>/dev/null)\" = melodee-move ]; then
            echo schwung > $TARGETS/default; chown ableton:users $TARGETS/default
        fi"
    echo "Removed $REMOTE and its boot target."
    exit 0
fi
[ -x dist/schwung-tool/melodee-move/standalone ] || { echo "Build it first: ./build.sh" >&2; exit 1; }
ssh "root@$HOST" "[ -d /data/UserData/schwung/modules/tools ] || { echo 'Schwung is not installed on this Move.' >&2; exit 1; }; mkdir -p $REMOTE/previews"
tar -C dist/schwung-tool/melodee-move -cf - . | ssh "root@$HOST" "tar -C $REMOTE -xf -"
if [ -n "$PREVIEWS" ]; then
    (cd "$PREVIEWS" && ls -- *.png | grep -v '@2x' | tar -cf - -T -) | ssh "root@$HOST" "tar -C $REMOTE/previews -xf -"
fi
# The registry entry as Schwung's readers take it: flat, one string field a
# line. A hand-made entry has no "owner"; Schwung Manager adopts it when it
# sees module.json's boot_target, whose exec is the same file.
REGISTERED=$(printf '{\n  "name": "Melodee",\n  "exec": "%s"\n}\n' "$REMOTE/boot-entry.sh" | ssh "root@$HOST" "chown -R ableton:users $REMOTE
    if [ -f $SELECTOR ]; then
        mkdir -p $TARGET && cat > $TARGET/boot.json && chown -R ableton:users $TARGET && echo yes
    fi")
echo "Installed $REMOTE. Open Schwung's Tools menu (Shift + Volume + Step 13): Melodee."
if [ "$REGISTERED" = yes ]; then
    echo "Melodee is one of the things the Move can start in: choose it in Schwung (Back while \"Loading ...\" shows at power-on, or http://$HOST:7700/boot)."
else
    echo "This Schwung has no boot selector (1.9 and later have it), so the Move cannot start in Melodee at power-on."
fi
