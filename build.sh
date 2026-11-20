#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Build melodee-move for the Move (linux/arm64) in its build image, then run
# the unit tests, the sound self-test and the screen previews. When Melodee's
# own page previews are at hand (MELODEE_PREVIEWS, by default
# ../melodee-sample/build/melodee-preview), also crush them to the Move's
# 128 x 64 screen in build/crushed.
set -eu
cd "$(dirname "$0")"
python3 tools/dependencies.py
docker build --platform linux/arm64 -t melodee-move-builder tools
PREVIEWS=${MELODEE_PREVIEWS:-../melodee-sample/build/melodee-preview}
MOUNT=""
CRUSH=":"
if [ -d "$PREVIEWS" ]; then
    MOUNT="-v $(cd "$PREVIEWS" && pwd):/previews:ro"
    CRUSH="build/melodee-move --crush /previews build/crushed"
fi
# shellcheck disable=SC2086
docker run --rm --platform linux/arm64 -v "$PWD:/work" $MOUNT melodee-move-builder sh -c "
    make -j4 &&
    build/move-tests &&
    build/melodee-move --self-test build/self-test.wav &&
    build/melodee-move --preview build/preview &&
    $CRUSH"
