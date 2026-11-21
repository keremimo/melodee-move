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
# USB audio (docs/USB_AUDIO.md): the script that turns it on, and the kernel
# modules tools/kernel-modules.sh built, when it has: unmodified, with the
# configuration they were built with and the kernel's licence.
cp tools/usb-audio.sh "$OUT/usb-audio.sh"
if [ -f build/kmod/usb_f_uac2.ko ] && [ -f build/kmod/GPL-2.0.txt ]; then
    mkdir -p "$OUT/kmod"
    for module in snd snd-timer snd-pcm u_audio usb_f_uac2; do cp "build/kmod/$module.ko" "$OUT/kmod/"; done
    cp build/kmod/kernel.config "$OUT/kmod/kernel.config"
    cp build/kmod/GPL-2.0.txt "$OUT/licenses/Linux-GPL-2.0.txt"
fi
cp -R "$OUT/bin" "$OUT/lib" "$OUT/licenses" "$TOOL/"
[ -d "$OUT/kmod" ] && cp -R "$OUT/kmod" "$TOOL/"
cp "$OUT/usb-audio.sh" "$TOOL/"
cp module/module.json module/standalone module/boot-entry.sh "$TOOL/"
# Power-on's entry (module/boot-entry.sh) starts again through it.
cp build/unblock-signals "$TOOL/bin/"
chmod 755 "$OUT/melodee-move" "$OUT/run-stock.sh" "$OUT/usb-audio.sh" "$TOOL/usb-audio.sh" "$TOOL/standalone" "$TOOL/boot-entry.sh" \
    "$OUT/bin/melodee-move" "$TOOL/bin/melodee-move" "$TOOL/bin/unblock-signals"
echo "bundle: $OUT ($(ls "$OUT/lib" | wc -l) libraries$([ -d "$OUT/kmod" ] && echo ', USB audio modules')), $TOOL"
