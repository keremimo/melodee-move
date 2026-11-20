#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Copies the spike to a Move running poundhard-armbian and runs it there, in
# this terminal. Needs key-based root SSH to the Move, as poundhard's
# installer does. Ctrl-C or Shift + Back on the Move stops it and brings the
# launcher menu back.
#
#   tools/deploy.sh [HOST] [--previews DIR]
#
# HOST defaults to move.local. --previews copies a folder of Melodee page
# previews (320 x 240 PNGs, such as melodee-sample's build/melodee-preview)
# for the spike to show crushed on the Move's screen.
set -eu
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
ssh -t "root@$HOST" "sh $REMOTE/run-spike.sh $ARGUMENTS"
