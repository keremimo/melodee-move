#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Copies dist/melodee-move to a Move on Ableton's firmware and runs it there
# in this terminal, as root, with Ableton's app stopped while it runs and
# started again after (tools/run-stock.sh). Needs root SSH to the Move.
# Ctrl-C, Shift + Back or the connection dropping ends it. It only adds
# /data/UserData/melodee-move; remove that folder to undo.
#
#   tools/deploy-stock.sh [HOST] [--previews DIR] [-- MELODEE-MOVE ARGUMENTS]
#
# HOST defaults to move.local. --previews copies Melodee's page previews
# (320 x 240 PNGs, such as melodee-sample's build/melodee-preview) for the
# spike to show crushed on the Move's screen.
set -eu
# macOS tar would add AppleDouble ._ files for extended attributes.
export COPYFILE_DISABLE=1
cd "$(dirname "$0")/.."
HOST=move.local
PREVIEWS=""
while [ $# -gt 0 ]; do
    case "$1" in
    --previews) PREVIEWS=$2; shift 2 ;;
    --) shift; break ;;
    *) HOST=$1; shift ;;
    esac
done
REMOTE=/data/UserData/melodee-move
[ -x dist/melodee-move/melodee-move ] || { echo "Build it first: ./build.sh" >&2; exit 1; }

ssh "root@$HOST" "mkdir -p $REMOTE/previews"
tar -C dist/melodee-move -cf - . | ssh "root@$HOST" "tar -C $REMOTE -xf -"
ARGUMENTS="$*"
if [ -n "$PREVIEWS" ]; then
    (cd "$PREVIEWS" && ls -- *.png | grep -v '@2x' | tar -cf - -T -) | ssh "root@$HOST" "tar -C $REMOTE/previews -xf -"
    ARGUMENTS="--previews $REMOTE/previews $ARGUMENTS"
fi
# exec: run-stock.sh becomes the SSH session's leader, the process a dropped
# connection signals, so it can stop melodee-move and bring Move back.
# -tt: a terminal on the Move even when this one has none (run from a
# script or a background task). Without it, a dropped connection sends the
# Move side no hang-up: the run's traps never fire, the program keeps
# running, and nothing would start Ableton's app again.
ssh -tt "root@$HOST" "exec sh $REMOTE/run-stock.sh $ARGUMENTS"
