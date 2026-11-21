#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Build melodee-move for the Move (linux/arm64) in its build image and check
# it (tools/check.sh, in the image).
#
#   ./build.sh         the build, the unit tests, the sound self-test, the
#                      screen previews, runs of the whole runtime against a
#                      simulated Move (the spike: a pad, the jogwheel to
#                      LATENCY, the meter timing clicks through a 300-frame
#                      cable, Shift + Back to quit; Melodee: load, chop, To
#                      Pads, saved and restored, transport and tempo, Song and
#                      its mixdown, sampling and Audio Recall, MIDI in and out
#                      on the USB port, the sequencer's steps, Play and Rec,
#                      the pad layout set to MPC banks and kept, the synth
#                      track saved and restored, the busy mix playing, the web
#                      page, a new project from the projects, Setup, and a
#                      press over the control channel), and the packages in
#                      dist/. Before a merge to main.
#   ./build.sh quick   the build and the unit tests alone. While working.
#
# Compiles go through ccache, its cache on this machine shared by every
# checkout and worktree (MELODEE_CCACHE, by default
# ~/.cache/melodee-move/ccache; remove it to start over). When Melodee's own
# page previews are at hand (MELODEE_PREVIEWS, by default
# ../melodee-sample/build/melodee-preview), the full check also crushes them
# to the Move's 128 x 64 screen in build/crushed.
set -eu
cd "$(dirname "$0")"
MODE=${1:-full}
case $MODE in
quick | full) ;;
*)
    echo "usage: ./build.sh [quick | full]" >&2
    exit 2
    ;;
esac
python3 tools/dependencies.py
# Melodee's engine comes from a melodee-sample checkout beside this one
# (MELODEE_SAMPLE), compiled as it is.
MELODEE_SAMPLE=${MELODEE_SAMPLE:-../melodee-sample}
[ -f "$MELODEE_SAMPLE/src/melodee/engine/sampler.h" ] && [ -f "$MELODEE_SAMPLE/base/src/surface/midi.h" ] || {
    echo "Needs melodee-sample, with its base submodule, at $MELODEE_SAMPLE (or set MELODEE_SAMPLE)." >&2
    exit 1
}
echo "melodee-sample: $(git -C "$MELODEE_SAMPLE" describe --always --dirty 2>/dev/null || echo unknown)"
# What the packages say they were built from (SOURCE.txt, tools/bundle.sh):
# each checkout's commit, "-dirty" when it has changes, and the commit's
# time for the files in the catalog's asset. tools/release.sh passes a
# release's own.
commit_of() {
    commit=$(git -C "$1" rev-parse HEAD 2> /dev/null) || { echo unknown; return; }
    [ -z "$(git -C "$1" status --porcelain 2> /dev/null)" ] || commit="$commit-dirty"
    echo "$commit"
}
MELODEE_MOVE_COMMIT=${MELODEE_MOVE_COMMIT:-$(commit_of .)}
MELODEE_SAMPLE_COMMIT=${MELODEE_SAMPLE_COMMIT:-$(commit_of "$MELODEE_SAMPLE")}
SAMPLE_BASE_COMMIT=${SAMPLE_BASE_COMMIT:-$(commit_of "$MELODEE_SAMPLE/base")}
MELODEE_MOVE_EPOCH=${MELODEE_MOVE_EPOCH:-$(git log -1 --format=%ct 2> /dev/null || date +%s)}
docker build --quiet --platform linux/arm64 -t melodee-move-builder tools > /dev/null
PREVIEWS=${MELODEE_PREVIEWS:-../melodee-sample/build/melodee-preview}
MOUNT=""
if [ -d "$PREVIEWS" ]; then
    MOUNT="-v $(cd "$PREVIEWS" && pwd):/previews:ro"
fi
CCACHE=${MELODEE_CCACHE:-$HOME/.cache/melodee-move/ccache}
mkdir -p "$CCACHE"
# shellcheck disable=SC2086
docker run --rm --platform linux/arm64 -v "$PWD:/work" -v "$(cd "$MELODEE_SAMPLE" && pwd):/melodee:ro" -e MELODEE=/melodee \
    -e MELODEE_MOVE_COMMIT="$MELODEE_MOVE_COMMIT" -e MELODEE_SAMPLE_COMMIT="$MELODEE_SAMPLE_COMMIT" \
    -e SAMPLE_BASE_COMMIT="$SAMPLE_BASE_COMMIT" -e MELODEE_MOVE_EPOCH="$MELODEE_MOVE_EPOCH" \
    -v "$CCACHE:/ccache" $MOUNT melodee-move-builder tools/check.sh "$MODE"
