#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Puts dist/schwung-tool/melodee-move into Schwung's tools on a Move
# (/data/UserData/schwung/modules/tools/melodee-move, owned by ableton as
# every module is), so Schwung's Tools menu can launch it with the Move to
# itself. It adds only that folder: --remove takes it out again. Schwung
# finds new modules when it starts, so restart Move (or the Move) after.
#
#   tools/install-schwung-tool.sh [HOST] [--previews DIR] [--remove]
set -eu
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
if [ "$REMOVE" = 1 ]; then
    ssh "root@$HOST" "rm -rf $REMOTE"
    echo "Removed $REMOTE"
    exit 0
fi
[ -x dist/schwung-tool/melodee-move/standalone ] || { echo "Build it first: ./build.sh" >&2; exit 1; }
ssh "root@$HOST" "[ -d /data/UserData/schwung/modules/tools ] || { echo 'Schwung is not installed on this Move.' >&2; exit 1; }; mkdir -p $REMOTE/previews"
tar -C dist/schwung-tool/melodee-move -cf - . | ssh "root@$HOST" "tar -C $REMOTE -xf -"
if [ -n "$PREVIEWS" ]; then
    (cd "$PREVIEWS" && ls -- *.png | grep -v '@2x' | tar -cf - -T -) | ssh "root@$HOST" "tar -C $REMOTE/previews -xf -"
fi
ssh "root@$HOST" "chown -R ableton:users $REMOTE"
echo "Installed $REMOTE. Restart Move, then open Schwung's Tools menu: Melodee Spike."
