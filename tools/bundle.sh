#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Packs build/melodee-move for the Move, in the build image (./build.sh):
#   dist/melodee-move/              run over SSH with tools/deploy-stock.sh
#                                   (or on Armbian: ./melodee-move --backend jack)
#   dist/schwung-tool/melodee-move/ a Schwung standalone tool
#                                   (tools/install-schwung-tool.sh)
# The shared libraries Ableton's firmware lacks go in lib/; glibc and the
# loader are the Move's own.
set -eu
cd "$(dirname "$0")/.."
OUT=dist/melodee-move
TOOL=dist/schwung-tool/melodee-move
rm -rf dist
mkdir -p "$OUT/bin" "$OUT/lib" "$TOOL"
cp build/melodee-move "$OUT/bin/"
ldd build/melodee-move | awk '$3 ~ /^\// { print $3 }' \
    | grep -vE '/(libc|libm|libdl|libpthread|librt)\.so|/ld-linux' \
    | while read -r library; do cp -L "$library" "$OUT/lib/"; done
# fontconfig wants a configuration; melodee-move draws with its own pixel font.
printf '<?xml version="1.0"?>\n<fontconfig></fontconfig>\n' > "$OUT/lib/fonts.conf"
# The web page carries Rubik (tools/web_bundle.py); its licence goes with it.
mkdir -p "$OUT/licenses"
[ -f build/deps/rubik/OFL.txt ] && cp build/deps/rubik/OFL.txt "$OUT/licenses/Rubik-OFL.txt"
cp tools/launcher.sh "$OUT/melodee-move"
cp tools/run-stock.sh "$OUT/run-stock.sh"
cp -R "$OUT/bin" "$OUT/lib" "$OUT/licenses" "$TOOL/"
cp module/module.json module/standalone "$TOOL/"
chmod 755 "$OUT/melodee-move" "$OUT/run-stock.sh" "$TOOL/standalone" "$OUT/bin/melodee-move" "$TOOL/bin/melodee-move"
echo "bundle: $OUT ($(ls "$OUT/lib" | wc -l) libraries), $TOOL"
