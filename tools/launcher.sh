#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# dist/melodee-move/melodee-move: runs the binary with the libraries packed
# beside it (Ableton's firmware lacks them). Arguments go to melodee-move.
HERE=$(cd "$(dirname "$0")" && pwd)
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export FONTCONFIG_FILE="$HERE/lib/fonts.conf"
exec "$HERE/bin/melodee-move" "$@"
