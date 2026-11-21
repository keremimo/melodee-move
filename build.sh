#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Build melodee-move for the Move (linux/arm64) in its build image, then:
# the unit tests, the sound self-test, the screen previews, a run of the
# whole runtime against a simulated Move (the spike: a pad, the jogwheel to
# LATENCY, the meter timing clicks through a 300-frame cable, Shift + Back to
# quit; Melodee: load, chop, To Pads, saved and restored, transport and
# tempo, the pad layout set to MPC banks and kept, the synth track saved and
# restored, the busy mix playing),
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
# Melodee, with the Move's keys (docs/MOVE_CONTROLS.md): the wheel's push at
# 50 opens Load for the pad, its push at 60 loads the loop onto A01; a hit at
# 400; Shift + push at 500 opens Sample Options, its push at 510 Chop (Auto:
# 16 slices), slice 2 at 520; two pushes at 600 and 620 put them on pads
# from A02; Shift + Back at 1300.
MELODEE_SCRIPT='50:0xb0,3,127;51:0xb0,3,0;60:0xb0,3,127;61:0xb0,3,0;400:0x90,68,110;430:0x80,68,0;500:0xb0,49,127;501:0xb0,3,127;502:0xb0,3,0;503:0xb0,49,0;510:0xb0,3,127;511:0xb0,3,0;520:0x90,69,100;540:0x80,69,0;600:0xb0,3,127;601:0xb0,3,0;620:0xb0,3,127;621:0xb0,3,0;1300:0xb0,49,127;1301:0xb0,51,127'
MELODEE_EXPECTED='page=kit-play .*pad=A02 samples=1 pads=17 .*slices=16'
# Saving: the run above quits within three seconds of its last change, so
# it saves on the way out; the next start reads the project back (A01 and
# the 16 slices, A02 last), knob 1 tunes A02 at 100, and the autosave
# follows three seconds after.
RESTORE_SCRIPT='100:0xb0,71,2'
RESTORE_EXPECTED='page=kit-play .*pad=A02 samples=1 pads=17 loading=0 saves=1 unsaved=0 .*slices=0'
# Transport and tempo, on that project, with the Move's own keys: Play at
# 100; Shift + Step 5 at 300 opens the tempo, the jogwheel two steps up
# (122 BPM), its push confirms; Shift + Step 6 at 360 the metronome on; Back
# at 380. Still playing at the end.
TRANSPORT_SCRIPT='100:0xb0,85,127;101:0xb0,85,0;300:0xb0,49,127;301:0x90,20,127;302:0x80,20,0;303:0xb0,49,0;320:0xb0,14,2;340:0xb0,3,127;341:0xb0,3,0;360:0xb0,49,127;361:0x90,21,127;362:0x80,21,0;363:0xb0,49,0;380:0xb0,51,127;381:0xb0,51,0'
TRANSPORT_EXPECTED='page=kit-play .*playing=1 tick=[1-9][0-9]* tempo=122.0 click=1'
# The pad layouts, on that project (A01 the loop, A02 to B01 its 16
# slices): Shift + Step 2 at 100 opens Setup > PADS, push edits LAYOUT, the
# wheel right turns it to Banks, push sets it, Back at 180; the bottom row's
# fifth pad at 300 is B01 in Banks (A05 in Rows). The next start keeps Banks.
PADS_SCRIPT='100:0xb0,49,127;101:0x90,17,127;102:0x80,17,0;103:0xb0,49,0;120:0xb0,3,127;121:0xb0,3,0;140:0xb0,14,1;160:0xb0,3,127;161:0xb0,3,0;180:0xb0,51,127;181:0xb0,51,0;300:0x90,72,100;330:0x80,72,0'
PADS_EXPECTED='page=kit-play .*pad=B01 .*layout=banks'
# Kit depth, on that project (A01 the loop, A02-A17 its slices): Plus at 100
# shows banks B-C and Minus at 160 back; Shift + Step 8 at 240 turns 16
# Pitches on, a pitch pad at 300, off again at 480; Shift + turn at 700 opens
# the pad pages (TRIM), knob 1 moves the start, Back at 780; Copy held with
# A01 then B16 at 900 copies A01 onto the empty B16 (18 pads).
KIT_SCRIPT='100:0xb0,55,127;101:0xb0,55,0;160:0xb0,54,127;161:0xb0,54,0;240:0xb0,49,127;241:0x90,23,127;242:0x80,23,0;243:0xb0,49,0;300:0x90,72,100;330:0x80,72,0;480:0xb0,49,127;481:0x90,23,127;482:0x80,23,0;483:0xb0,49,0;700:0xb0,49,127;701:0xb0,14,1;702:0xb0,49,0;720:0xb0,71,2;780:0xb0,51,127;781:0xb0,51,0;900:0xb0,60,127;901:0x90,68,100;902:0x80,68,0;903:0x90,99,100;904:0x80,99,0;905:0xb0,60,0'
KIT_EXPECTED='page=kit-play .*pads=18 .* bank=0 pitches=0 copy_from=-1'
# The synth track: Track 2 tapped at 50, the wheel's push at 60 lists the
# modules (none loaded yet), its push at 80 loads the one in
# build/test-modules (the tests' stand-in), a pad plays it, knob 1 turns its
# level up three, Plus an octave. The next start has it back as it was.
SYNTH_SCRIPT='50:0xb0,42,127;51:0xb0,42,0;60:0xb0,3,127;61:0xb0,3,0;80:0xb0,3,127;81:0xb0,3,0;400:0x90,68,110;430:0x80,68,0;500:0xb0,71,3;520:0xb0,55,127;521:0xb0,55,0'
SYNTH_EXPECTED='kind=synth .*synth=fake octave=1'
SYNTH_FLAGS='--backend simulated --samples build/simulated-samples --home build/simulated-synth --modules build/test-modules'
# The mix: build/mixer-tests writes a project of sixteen kit tracks with six
# effects on every track, bus and the master (126 in all); Play at 200, five
# seconds of it through the whole runtime, its process_us in the log.
MIXER_SCRIPT='200:0xb0,85,127;201:0xb0,85,0'
MIXER_EXPECTED='process_us=.* playing=1 .*effects=126 '
# shellcheck disable=SC2086
docker run --rm --platform linux/arm64 -v "$PWD:/work" -v "$(cd "$MELODEE_SAMPLE" && pwd):/melodee:ro" -e MELODEE=/melodee \
    $MOUNT melodee-move-builder sh -c "
    make -j4 &&
    build/move-tests &&
    build/controls-tests &&
    build/mixer-tests &&
    build/pad-layout-tests &&
    build/pads-app-tests &&
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
    grep -qE '$MELODEE_EXPECTED' build/simulated-melodee.log &&
    test -s build/simulated-home/Projects/Move.mldp &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 5 --script '$RESTORE_SCRIPT' 2> build/simulated-restore.log || true; } &&
    tail -n 1 build/simulated-restore.log &&
    grep -qE '$RESTORE_EXPECTED' build/simulated-restore.log &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 3 --script '$TRANSPORT_SCRIPT' 2> build/simulated-transport.log || true; } &&
    tail -n 1 build/simulated-transport.log &&
    grep -qE '$TRANSPORT_EXPECTED' build/simulated-transport.log &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 4 --script '$KIT_SCRIPT' 2> build/simulated-kit.log || true; } &&
    tail -n 1 build/simulated-kit.log &&
    grep -qE '$KIT_EXPECTED' build/simulated-kit.log &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 3 --script '$PADS_SCRIPT' 2> build/simulated-pads.log || true; } &&
    tail -n 1 build/simulated-pads.log &&
    grep -qE '$PADS_EXPECTED' build/simulated-pads.log &&
    grep -q '^pads 1' build/simulated-home/Move.settings &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 2 2> build/simulated-pads-restore.log || true; } &&
    grep -q 'layout=banks' build/simulated-pads-restore.log &&
    rm -rf build/simulated-synth &&
    { build/melodee-move $SYNTH_FLAGS --seconds 5 --script '$SYNTH_SCRIPT' 2> build/simulated-synth.log || true; } &&
    tail -n 1 build/simulated-synth.log &&
    grep -qE '$SYNTH_EXPECTED' build/simulated-synth.log &&
    grep -qa 'param level 0.53' build/simulated-synth/Projects/Move.mldp &&
    { build/melodee-move $SYNTH_FLAGS --seconds 3 2> build/simulated-synth-restore.log || true; } &&
    tail -n 1 build/simulated-synth-restore.log &&
    grep -qE '$SYNTH_EXPECTED' build/simulated-synth-restore.log &&
    { build/melodee-move --backend simulated --samples build/test-samples-mixer --home build/bench-home-busy --seconds 5 --script '$MIXER_SCRIPT' 2> build/simulated-mixer.log || true; } &&
    tail -n 1 build/simulated-mixer.log | grep -o 'process_us=[^ ]* of [0-9]*' &&
    grep -qE '$MIXER_EXPECTED' build/simulated-mixer.log &&
    build/melodee-move --melodee-previews build/melodee-previews --modules build/test-modules > /dev/null &&
    build/melodee-move --probe-modules build/test-modules &&
    tools/bundle.sh &&
    $CRUSH"
