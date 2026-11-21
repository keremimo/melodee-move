#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Build melodee-move for the Move (linux/arm64) in its build image, then:
# the unit tests, the sound self-test, the screen previews, a run of the
# whole runtime against a simulated Move (the spike: a pad, the jogwheel to
# LATENCY, the meter timing clicks through a 300-frame cable, Shift + Back to
# quit; Melodee: load, chop, To Pads, saved and restored, transport and
# tempo, Song and its mixdown, sampling and Audio Recall, MIDI in and out on
# the USB port, the sequencer's steps, Play and Rec, the pad layout set to
# MPC banks and kept, the synth track saved and restored, the busy mix
# playing, the web page, a new project from the projects, Setup, and a press
# over the control channel),
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
# 50 opens Load for the pad, its push at 60 loads the loop onto A01, Back at
# 70 leaves it (Shift + push there stars a file); a hit at 400; Shift + push at 500 opens Sample Options, its push at 510 Chop (Auto:
# 16 slices), slice 2 at 520; two pushes at 600 and 620 put them on pads
# from A02; Shift + Back at 1300.
MELODEE_SCRIPT='50:0xb0,3,127;51:0xb0,3,0;60:0xb0,3,127;61:0xb0,3,0;70:0xb0,51,127;71:0xb0,51,0;400:0x90,68,110;430:0x80,68,0;500:0xb0,49,127;501:0xb0,3,127;502:0xb0,3,0;503:0xb0,49,0;510:0xb0,3,127;511:0xb0,3,0;520:0x90,69,100;540:0x80,69,0;600:0xb0,3,127;601:0xb0,3,0;620:0xb0,3,127;621:0xb0,3,0;1300:0xb0,49,127;1301:0xb0,51,127'
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
# The sequencer (docs/UI.md, Sequencer), on that project: Steps 1, 5, 9, 13
# tapped at 50-81 (A02's lane), Play at 100, Rec at 200 (recording at
# once while playing), A02 hit four times from 300; still recording at the end.
SEQUENCER_SCRIPT='50:0x90,16,127;51:0x80,16,0;60:0x90,20,127;61:0x80,20,0;70:0x90,24,127;71:0x80,24,0;80:0x90,28,127;81:0x80,28,0;100:0xb0,85,127;101:0xb0,85,0;200:0xb0,86,127;201:0xb0,86,0;300:0x90,69,100;330:0x80,69,0;500:0x90,69,100;530:0x80,69,0;700:0x90,69,100;730:0x80,69,0;900:0x90,69,100;930:0x80,69,0'
SEQUENCER_EXPECTED='playing=1 .*seq_notes=[5-9] .*rec=1'
# The pad layouts, on that project (A01 the loop, A02 to B01 its 16
# slices), with Move.settings saying MPC banks (as Setup > PADS > LAYOUT
# leaves it): the bottom row's fifth pad at 100 is B01 in Banks (A05 in
# Rows); the save on the way out keeps Banks, and so does the next start.
PADS_SCRIPT='100:0x90,72,100;130:0x80,72,0'
PADS_EXPECTED='page=kit-play .*pad=B01 .*layout=banks'
# Kit depth, on that project (A01 the loop, A02-A17 its slices): Plus at 100
# shows banks B-C and Minus at 160 back; Shift + Step 8 at 240 turns 16
# Pitches on, a pitch pad at 300, off again at 480; Shift + turn at 700 opens
# the pad pages (TRIM), knob 1 moves the start, Back at 780; Copy held with
# A01 then B16 at 900 copies A01 onto the empty B16 (18 pads).
KIT_SCRIPT='100:0xb0,55,127;101:0xb0,55,0;160:0xb0,54,127;161:0xb0,54,0;240:0xb0,49,127;241:0x90,23,127;242:0x80,23,0;243:0xb0,49,0;300:0x90,72,100;330:0x80,72,0;480:0xb0,49,127;481:0x90,23,127;482:0x80,23,0;483:0xb0,49,0;700:0xb0,49,127;701:0xb0,14,1;702:0xb0,49,0;720:0xb0,71,2;780:0xb0,51,127;781:0xb0,51,0;900:0xb0,60,127;901:0x90,68,100;902:0x80,68,0;903:0x90,99,100;904:0x80,99,0;905:0xb0,60,0'
KIT_EXPECTED='page=kit-play .*pads=18 .* bank=0 first=A01 pitches=0 copy_from=-1'
# Song, on a copy of that project (docs/MOVE_CONTROLS.md, Song): Shift + Step 13 at
# 100 opens it; knob 2 three down at 130 (the next clip one bar); the top
# row's first pad at 150 (track 1, bar 1); the wheel's push at 200 places A1
# there; Play at 250 plays the song, at 600 stops it; Sampling at 700 opens
# EXPORT, its push at 750 mixes the song down, once through, into
# Mixdowns/Move.wav.
SONG_SCRIPT='100:0xb0,49,127;101:0x90,28,127;102:0x80,28,0;103:0xb0,49,0;130:0xb0,72,125;150:0x90,92,100;160:0x80,92,0;200:0xb0,3,127;201:0xb0,3,0;250:0xb0,85,127;251:0xb0,85,0;600:0xb0,85,127;601:0xb0,85,0;700:0xb0,118,127;701:0xb0,118,0;750:0xb0,3,127;751:0xb0,3,0'
SONG_EXPECTED='page=song-export .*song_clips=1 .*export=Mixdowns/Move.wav export_busy=0 export_done=1'
# Sampling (docs/UI.md "Sampling"), with the Move's keys: Load A01 (pushes
# at 50 and 60); Sampling at 300; knob 1 three on (RESAMPLE) at 320; A06 at
# 400 arms and records at once; A01 at 450, 600 and 800 plays the kit
# through the pads into it; Sampling at 1000 ends it, written to
# Samples/Recorded and onto A06; Shift + Sampling at 1100: Audio Recall onto
# A07; Shift + Back at 1300.
SAMPLING_SCRIPT='50:0xb0,3,127;51:0xb0,3,0;60:0xb0,3,127;61:0xb0,3,0;300:0xb0,118,127;301:0xb0,118,0;320:0xb0,71,3;400:0x90,73,100;410:0x80,73,0;450:0x90,68,110;500:0x80,68,0;600:0x90,68,110;650:0x80,68,0;800:0x90,68,110;850:0x80,68,0;1000:0xb0,118,127;1001:0xb0,118,0;1100:0xb0,49,127;1101:0xb0,118,127;1102:0xb0,118,0;1103:0xb0,49,0;1300:0xb0,49,127;1301:0xb0,51,127'
SAMPLING_EXPECTED='pad=A07 samples=3 pads=3 .*rec=idle source=RSMP .*last_take=RESAMPLE 1'
# The synth track: Track 2 tapped at 50, the wheel's push at 60 lists the
# modules (none loaded yet), its push at 80 loads the one in
# build/test-modules (the tests' stand-in), a pad plays it, Shift + the wheel
# past MACRO to its first page, knob 1 turns its level up three, Plus an
# octave. The next start has it back as it was.
# MIDI on the Move's USB port (cable 2; docs/MIDI.md), on that project with
# MIDI clock out and Pad MIDI Out on: a note and its release in on cable 2
# at 100 and 130 (A01 plays), Play at 200 (Start and the clock go out), a CC
# in at 300, a pad at 400 (its note goes out). Three in, the clock and the
# pad's note out, nothing dropped.
MIDI_SCRIPT='100:x0x90,36,100;130:x0x80,36,0;200:0xb0,85,127;201:0xb0,85,0;300:x0xb0,7,100;400:0x90,68,110;430:0x80,68,0'
MIDI_EXPECTED='playing=1 .* melodee_midi_in=3 melodee_midi_out=[1-9][0-9]+ melodee_midi_dropped=0'
SYNTH_SCRIPT='50:0xb0,42,127;51:0xb0,42,0;60:0xb0,3,127;61:0xb0,3,0;80:0xb0,3,127;81:0xb0,3,0;400:0x90,68,110;430:0x80,68,0;480:0xb0,49,127;481:0xb0,14,1;482:0xb0,49,0;500:0xb0,71,3;520:0xb0,55,127;521:0xb0,55,0'
SYNTH_EXPECTED='kind=synth .*synth=fake octave=1'
SYNTH_FLAGS='--backend simulated --samples build/simulated-samples --home build/simulated-synth --modules build/test-modules'
# The mix: build/mixer-tests writes a project of sixteen kit tracks with six
# effects on every track, bus and the master (126 in all); Play at 200, five
# seconds of it through the whole runtime, its process_us in the log.
MIXER_SCRIPT='200:0xb0,85,127;201:0xb0,85,0'
MIXER_EXPECTED='process_us=.* playing=1 .*effects=126 '
# The Looper (docs/UI.md, "Looper") through a 300-frame cable: Shift + Step
# 6 the metronome on, Back; Shift + Step 12 at 60, its push at 80 turns it
# on; the push at 200 opens L1's sheet, the wheel to its last item (IN
# DELAY) and the push at 240 measures the input's round trip (428: the
# cable and one block); Back at 1100, knob 2 at 1120 a bar's length, Rec
# at 1140 arms L1, Play at 1160 takes it. The next start has it back.
LOOPER_SCRIPT='20:0xb0,49,127;21:0x90,21,127;22:0x80,21,0;23:0xb0,49,0;40:0xb0,51,127;41:0xb0,51,0;60:0xb0,49,127;61:0x90,27,127;62:0x80,27,0;63:0xb0,49,0;80:0xb0,3,127;81:0xb0,3,0;200:0xb0,3,127;201:0xb0,3,0;220:0xb0,14,7;240:0xb0,3,127;241:0xb0,3,0;1100:0xb0,51,127;1101:0xb0,51,0;1120:0xb0,72,3;1140:0xb0,86,127;1141:0xb0,86,0;1160:0xb0,85,127;1161:0xb0,85,0'
LOOPER_EXPECTED='page=looper .*looper=on .*loop_latency=428 .*loop1=PLAY,len=88200,bars=1,layers=1,src=input'
LOOPER_RESTORED='looper=on .*loop_latency=428 .*loop1=(PLAY|STOP),len=88200,'
LOOPER_FLAGS='--backend simulated --loopback 300 --samples build/simulated-samples --home build/simulated-looper'
# The web page (src/web/): Melodee runs while tests/web_check.py opens its
# page as a browser on the Move itself (let in, as one on the USB cable) and
# as one at the machine's own address (as on Wi-Fi: refused until it types
# the code it reads off the Move's mirrored screen).
WEB_FLAGS='--backend simulated --samples build/simulated-samples --home build/simulated-web --web-port 8080 --seconds 12'
# Projects and Setup, with the Move's keys (docs/MOVE_CONTROLS.md): Shift +
# Step 1 at 100 opens the projects; an empty pad (the overview's second, pad
# 26) at 300; Back at 400 makes a new project there and opens it, the engine
# parked while it is built; Shift + Step 2 at 1000 opens Setup, its push at
# 1100 the PADS settings. Then a run is driven over the control channel:
# melodee-press shift+step1 opens the projects, and status says so.
PROJECTS_SCRIPT='100:0xb0,49,127;101:0x90,16,127;102:0x80,16,0;103:0xb0,49,0;300:0x90,93,100;301:0x80,93,0;400:0xb0,51,127;401:0xb0,51,0;1000:0xb0,49,127;1001:0x90,17,127;1002:0x80,17,0;1003:0xb0,49,0;1100:0xb0,3,127;1101:0xb0,3,0'
PROJECTS_EXPECTED='page=setup .*project=Project_1 opening=0'
# shellcheck disable=SC2086
docker run --rm --platform linux/arm64 -v "$PWD:/work" -v "$(cd "$MELODEE_SAMPLE" && pwd):/melodee:ro" -e MELODEE=/melodee \
    $MOUNT melodee-move-builder sh -c "
    make -j4 &&
    build/move-tests &&
    build/sequencer-tests &&
    build/controls-tests &&
    build/mixer-tests &&
    build/midi-tests &&
    build/projects-tests &&
    build/pad-layout-tests &&
    build/pads-app-tests &&
    build/web-tests &&
    build/song-tests &&
    build/instruments-tests &&
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
    rm -rf build/simulated-song && cp -R build/simulated-home build/simulated-song &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-song --seconds 8 --script '$SONG_SCRIPT' 2> build/simulated-song.log || true; } &&
    tail -n 1 build/simulated-song.log &&
    grep -qE '$SONG_EXPECTED' build/simulated-song.log &&
    test -s build/simulated-song/Mixdowns/Move.wav &&
    rm -rf build/simulated-midi && cp -R build/simulated-home build/simulated-midi &&
    printf 'midi_sync_out 1\npad_midi_out 1\n' >> build/simulated-midi/Move.settings &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-midi --seconds 3 --script '$MIDI_SCRIPT' 2> build/simulated-midi.log || true; } &&
    tail -n 1 build/simulated-midi.log &&
    grep -qE '$MIDI_EXPECTED' build/simulated-midi.log &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 4 --script '$SEQUENCER_SCRIPT' 2> build/simulated-sequencer.log || true; } &&
    tail -n 1 build/simulated-sequencer.log &&
    grep -qE '$SEQUENCER_EXPECTED' build/simulated-sequencer.log &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 4 --script '$KIT_SCRIPT' 2> build/simulated-kit.log || true; } &&
    tail -n 1 build/simulated-kit.log &&
    grep -qE '$KIT_EXPECTED' build/simulated-kit.log &&
    echo 'pads 1' >> build/simulated-home/Move.settings &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 3 --script '$PADS_SCRIPT' 2> build/simulated-pads.log || true; } &&
    tail -n 1 build/simulated-pads.log &&
    grep -qE '$PADS_EXPECTED' build/simulated-pads.log &&
    grep -q '^pads 1' build/simulated-home/Move.settings &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-home --seconds 2 2> build/simulated-pads-restore.log || true; } &&
    grep -q 'layout=banks' build/simulated-pads-restore.log &&
    rm -rf build/simulated-sampling &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-sampling --seconds 6 --script '$SAMPLING_SCRIPT' 2> build/simulated-sampling.log || true; } &&
    tail -n 1 build/simulated-sampling.log &&
    grep -qE '$SAMPLING_EXPECTED' build/simulated-sampling.log &&
    test -s 'build/simulated-sampling/Samples/Recorded/RESAMPLE 1.wav' && test -s 'build/simulated-sampling/Samples/Recorded/RECALL 1.wav' &&
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
    rm -rf build/simulated-looper &&
    { build/melodee-move $LOOPER_FLAGS --seconds 8 --script '$LOOPER_SCRIPT' 2> build/simulated-looper.log || true; } &&
    tail -n 1 build/simulated-looper.log &&
    grep -qE '$LOOPER_EXPECTED' build/simulated-looper.log &&
    test -s 'build/simulated-looper/Projects/Move Loops/Loop 1.wav' &&
    { build/melodee-move $LOOPER_FLAGS --seconds 2 2> build/simulated-looper-restore.log || true; } &&
    tail -n 1 build/simulated-looper-restore.log &&
    grep -qE '$LOOPER_RESTORED' build/simulated-looper-restore.log &&
    rm -rf build/simulated-web &&
    { build/melodee-move $WEB_FLAGS 2> build/simulated-web.log & } &&
    python3 -I tests/web_check.py 8080 src/move/font.cpp &&
    rm -rf build/simulated-projects build/simulated-remote &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-projects --seconds 5 --script '$PROJECTS_SCRIPT' 2> build/simulated-projects.log || true; } &&
    tail -n 1 build/simulated-projects.log &&
    grep -qE '$PROJECTS_EXPECTED' build/simulated-projects.log &&
    test -s 'build/simulated-projects/Projects/Project 1.mldp' &&
    { build/melodee-move --backend simulated --samples build/simulated-samples --home build/simulated-remote --seconds 4 2> build/simulated-remote.log & } &&
    sleep 2 &&
    build/melodee-move --ctl 'melodee-press shift+step1' --home build/simulated-remote | grep -q ok &&
    build/melodee-move --ctl status --home build/simulated-remote | grep -q 'page=projects' &&
    wait &&
    build/melodee-move --melodee-previews build/melodee-previews --modules build/test-modules > /dev/null &&
    build/melodee-move --probe-modules build/test-modules &&
    tools/bundle.sh &&
    $CRUSH"
