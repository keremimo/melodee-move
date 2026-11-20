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
# Melodee's engine comes from a melodee-sample checkout beside this one
# (MELODEE_SAMPLE), compiled as it is.
MELODEE_SAMPLE=${MELODEE_SAMPLE:-../melodee-sample}
[ -f "$MELODEE_SAMPLE/src/melodee/engine/sampler.h" ] && [ -f "$MELODEE_SAMPLE/base/src/surface/midi.h" ] || {
    echo "Needs melodee-sample, with its base submodule, at $MELODEE_SAMPLE (or set MELODEE_SAMPLE)." >&2
    exit 1
}
echo "melodee-sample: $(git -C "$MELODEE_SAMPLE" describe --always --dirty 2>/dev/null || echo unknown)"
docker build --platform linux/arm64 -t melodee-move-builder tools
PREVIEWS=${MELODEE_PREVIEWS:-../melodee-sample/build/melodee-preview}
MOUNT=""
CRUSH=":"
if [ -d "$PREVIEWS" ]; then
    MOUNT="-v $(cd "$PREVIEWS" && pwd):/previews:ro"
    CRUSH="build/melodee-move --crush /previews build/crushed"
fi
# The spike, its transfers 2.9 ms apart: a pad at 20, the jogwheel two steps
# right at 80, its push at 90, Shift + Back at 1300 (about 3.8 s).
SCRIPT='20:0x90,68,100;60:0x80,68,0;80:0xb0,14,2;90:0xb0,3,127;91:0xb0,3,0;1300:0xb0,49,127;1301:0xb0,51,127'
# The meter hears its click 300 frames of cable plus one block after it
# left render(): the block a render waits for the next exchange.
EXPECTED='loop_frames=428/428'
# Melodee: Menu at 50, the jogwheel's push at 60 loads the loop onto A01; a
# hit at 400; Loop at 500 chops it (Auto: 16 slices), slice 2 at 520; two
# pushes at 600 and 620 put them on pads from A02; Shift + Back at 1300.
MELODEE_SCRIPT='50:0xb0,50,127;60:0xb0,3,127;400:0x90,68,110;430:0x80,68,0;500:0xb0,58,127;520:0x90,69,100;540:0x80,69,0;600:0xb0,3,127;620:0xb0,3,127;1300:0xb0,49,127;1301:0xb0,51,127'
MELODEE_EXPECTED='page=play pad=A02 samples=1 slices=16 pads=17'
# Saving: the run above quits within three seconds of its last change, so
# it saves on the way out; the next start reads the kit back (A01 and the 16
# slices, A02 last), knob 1 tunes A02 at 100, and the autosave follows three
# seconds after.
RESTORE_SCRIPT='100:0xb0,71,2'
RESTORE_EXPECTED='page=play pad=A02 samples=1 slices=0 pads=17 .* saves=1 unsaved=0'
# The synth track: Row 2, Menu, the jogwheel's push loads the one module in
# build/test-modules (the tests' stand-in), a pad plays it, knob 1 turns its
# level up three, Up an octave. The next start has it back as it was.
SYNTH_SCRIPT='50:0xb0,42,127;60:0xb0,50,127;80:0xb0,3,127;400:0x90,68,110;430:0x80,68,0;500:0xb0,71,3;520:0xb0,55,127'
SYNTH_EXPECTED='track=synth synth=fake octave=1'
SYNTH_FLAGS='--backend simulated --samples build/simulated-samples --home build/simulated-synth --modules build/test-modules'
# shellcheck disable=SC2086
docker run --rm --platform linux/arm64 -v "$PWD:/work" -v "$(cd "$MELODEE_SAMPLE" && pwd):/melodee:ro" -e MELODEE=/melodee \
    $MOUNT melodee-move-builder sh -c "
    make -j4 &&
    build/move-tests &&
    build/melodee-move --self-test build/self-test.wav &&
    build/melodee-move --preview build/preview &&
    { build/melodee-move --app spike --backend simulated --loopback 300 --seconds 8 --script '$SCRIPT' 2> build/simulated.log || true; } &&
    tail -n 1 build/simulated.log &&
    grep -q 'Shift + Back' build/simulated.log &&
    grep -q '$EXPECTED' build/simulated.log &&
    rm -rf build/simulated-samples build/simulated-home && mkdir -p build/simulated-samples &&
    build/melodee-move --test-loop build/simulated-samples/loop.wav &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 8 --script '$MELODEE_SCRIPT' 2> build/simulated-melodee.log || true; } &&
    tail -n 1 build/simulated-melodee.log &&
    grep -q 'Shift + Back' build/simulated-melodee.log &&
    grep -q '$MELODEE_EXPECTED' build/simulated-melodee.log &&
    test -s build/simulated-home/Projects/Move.mldp &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 5 --script '$RESTORE_SCRIPT' 2> build/simulated-restore.log || true; } &&
    tail -n 1 build/simulated-restore.log &&
    grep -qE '$RESTORE_EXPECTED' build/simulated-restore.log &&
    rm -rf build/simulated-synth &&
    { build/melodee-move $SYNTH_FLAGS --seconds 5 --script '$SYNTH_SCRIPT' 2> build/simulated-synth.log || true; } &&
    tail -n 1 build/simulated-synth.log &&
    grep -q '$SYNTH_EXPECTED' build/simulated-synth.log &&
    grep -q 'param level 0.53' build/simulated-synth/Projects/Move.mldp.synth &&
    { build/melodee-move $SYNTH_FLAGS --seconds 3 2> build/simulated-synth-restore.log || true; } &&
    tail -n 1 build/simulated-synth-restore.log &&
    grep -q '$SYNTH_EXPECTED' build/simulated-synth-restore.log &&
    build/melodee-move --melodee-previews build/melodee-previews --modules build/test-modules > /dev/null &&
    build/melodee-move --probe-modules build/test-modules &&
    tools/bundle.sh &&
    $CRUSH"
