#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Build melodee-move for the Move (linux/arm64) in its build image, then:
# the unit tests, the sound self-test, the screen previews, a run of the
# whole runtime against a simulated Move (a pad, the jogwheel to LATENCY,
# the meter timing clicks through a 300-frame cable, Shift + Back to quit),
# and the packages in dist/. When Melodee's own page previews are at hand
# (MELODEE_PREVIEWS, by default ../melodee-sample/build/melodee-preview),
# also crush them to the Move's 128 x 64 screen in build/crushed.
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
# Transfers are 2.9 ms: a pad at 20, the jogwheel two steps right at 80, its
# push at 90, Shift + Back at 1300 (about 3.8 s).
SCRIPT='20:0x90,68,100;60:0x80,68,0;80:0xb0,14,2;90:0xb0,3,127;91:0xb0,3,0;1300:0xb0,49,127;1301:0xb0,51,127'
# The meter hears its click 300 frames of cable plus one block after it
# left render(): the block a render waits for the next exchange.
EXPECTED='loop_frames=428/428'
# shellcheck disable=SC2086
docker run --rm --platform linux/arm64 -v "$PWD:/work" $MOUNT melodee-move-builder sh -c "
    make -j4 &&
    build/move-tests &&
    build/melodee-move --self-test build/self-test.wav &&
    build/melodee-move --preview build/preview &&
    { build/melodee-move --backend simulated --loopback 300 --seconds 8 --script '$SCRIPT' 2> build/simulated.log || true; } &&
    tail -n 1 build/simulated.log &&
    grep -q 'Shift + Back' build/simulated.log &&
    grep -q '$EXPECTED' build/simulated.log &&
    tools/bundle.sh &&
    $CRUSH"
