#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Copies the spike to a Move running poundhard-armbian and runs it there, in
# this terminal. Needs key-based root SSH to the Move, as poundhard's
# installer does. Ctrl-C, Shift + Back on the Move or the connection
# dropping stops it and brings the launcher menu back. Nothing it does
# outlasts a reboot except the files in /data/UserData/melodee-move.
#
#   tools/deploy.sh [HOST] [--previews DIR]
#
# HOST defaults to move.local. --previews copies a folder of Melodee page
# previews (320 x 240 PNGs, such as melodee-sample's build/melodee-preview)
# for the spike to show crushed on the Move's screen.
set -eu
# macOS tar would add AppleDouble ._ files for extended attributes.
export COPYFILE_DISABLE=1
cd "$(dirname "$0")/.."
HOST=move.local
PREVIEWS=""
while [ $# -gt 0 ]; do
    case "$1" in
    --previews) PREVIEWS=$2; shift 2 ;;
    *) HOST=$1; shift ;;
    esac
done
REMOTE=/data/UserData/melodee-move
[ -x build/melodee-move ] || { echo "Build it first: ./build.sh" >&2; exit 1; }

ssh "root@$HOST" "mkdir -p $REMOTE/previews"
scp build/melodee-move tools/run-spike.sh "root@$HOST:$REMOTE/"
ARGUMENTS=""
if [ -n "$PREVIEWS" ]; then
    # Only the 1x pictures; the spike crushes them on the Move as it starts.
    (cd "$PREVIEWS" && ls -- *.png | grep -v '@2x' | tar -cf - -T -) | ssh "root@$HOST" "tar -C $REMOTE/previews -xf -"
    ARGUMENTS="--previews $REMOTE/previews"
fi
# exec: run-spike.sh becomes the SSH session's leader, the process a dropped
# connection signals, so it can stop the spike and bring the menu back.
# -tt: a terminal on the Move even when this one has none (run from a
# script or a background task). Without it, a dropped connection sends the
# Move side no hang-up: the run's traps never fire, the program keeps
# running, and nothing would start Ableton's app again.
ssh -tt "root@$HOST" "exec sh $REMOTE/run-spike.sh $ARGUMENTS"
