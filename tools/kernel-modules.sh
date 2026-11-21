#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Builds the kernel modules USB audio needs (docs/USB_AUDIO.md) for the
# Move's stock kernel, 5.15.92-rt57-v8: ALSA's core (snd, snd-timer,
# snd-pcm) and the UAC2 gadget function (u_audio, usb_f_uac2).
#
# Ableton publishes no source for that kernel. It is Raspberry Pi's
# rpi-5.15.y at 14b35093 (meta-raspberrypi kirkstone's pin for 5.15.92) with
# kernel.org's patch-5.15.92-rt57, built with the configuration the Move
# carries in its configs.ko. This build adds only ALSA's core and the UAC2
# function to that configuration, as modules, so every symbol the running
# kernel exports keeps its CRC: tools/check-kernel.sh checks it against a
# Move before the modules go near one.
#
#   tools/kernel-modules.sh [HOST]   the configuration read from that Move
#                                    (read only; default move.local) unless
#                                    build/kmod/move.config is already there
#
# In Docker (linux/arm64, Ubuntu 22.04); the source and build tree stay in
# the named volume melodee-move-kernel, about 2 GB. Some 15 minutes the
# first time, a minute after. Writes build/kmod/: the five modules, the
# configuration built with (kernel.config), the kernel's licence
# (GPL-2.0.txt), and the same kernel's own usb_f_ncm, usb_f_fs and
# libcomposite with its Module.symvers (reference/) for the check.
set -eu
cd "$(dirname "$0")/.."
HOST=${1:-move.local}
OUT=build/kmod
COMMIT=14b35093ca68bf2c81bbc90aace5007142b40b40
RT_PATCH=https://cdn.kernel.org/pub/linux/kernel/projects/rt/5.15/older/patch-5.15.92-rt57.patch.xz
RT_SHA256=abced24190e8ecc8289aa68bf002e52aecef1f5ef500e870961c71d518592da0
mkdir -p "$OUT/reference"

if [ ! -f "$OUT/move.config" ]; then
    # configs.ko holds the configuration gzipped between IKCFG_ST and IKCFG_ED;
    # read from the file, never loaded.
    ssh "root@$HOST" 'cat "/lib/modules/$(uname -r)/kernel/kernel/configs.ko"' > "$OUT/configs.ko"
    python3 -I - "$OUT/configs.ko" "$OUT/move.config" <<'EOF'
import gzip, sys
data = open(sys.argv[1], 'rb').read()
start, end = data.find(b'IKCFG_ST'), data.find(b'IKCFG_ED')
if start < 0 or end < 0: sys.exit('no configuration in ' + sys.argv[1])
open(sys.argv[2], 'wb').write(gzip.decompress(data[start + 8:end]))
EOF
    rm -f "$OUT/configs.ko"
fi
grep -q '^CONFIG_LOCALVERSION="-v8"' "$OUT/move.config" || { echo "$OUT/move.config is not the Move's 5.15 configuration" >&2; exit 1; }

docker volume create melodee-move-kernel > /dev/null
docker run --rm --platform linux/arm64 -v melodee-move-kernel:/work -v "$PWD/$OUT:/out" \
    -e COMMIT="$COMMIT" -e RT_PATCH="$RT_PATCH" -e RT_SHA256="$RT_SHA256" ubuntu:22.04 sh -eu -c '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends build-essential gcc-11-plugin-dev bc bison flex libssl-dev libelf-dev \
        git xz-utils python3 kmod cpio ca-certificates curl patch > /dev/null
    cd /work
    if [ "$(cat linux/.melodee-source 2>/dev/null)" != "$COMMIT rt57" ]; then
        rm -rf linux
        git init -q linux
        git -C linux fetch -q --depth 1 https://github.com/raspberrypi/linux "$COMMIT"
        git -C linux checkout -q FETCH_HEAD
        curl -sfL "$RT_PATCH" -o rt.patch.xz
        echo "$RT_SHA256  rt.patch.xz" | sha256sum -c - > /dev/null
        xz -dc rt.patch.xz | (cd linux && patch -s -p1 --forward)
        echo "$COMMIT rt57" > linux/.melodee-source
    fi
    cd linux
    cp /out/move.config .config
    # ALSA s core and the UAC2 gadget function as modules; every sound driver off.
    ./scripts/config --module SND --module SND_PCM --module SND_TIMER --enable USB_CONFIGFS_F_UAC2 \
        --disable SND_DRIVERS --disable SND_ARM --disable SND_SPI --disable SND_USB --disable SND_SOC --disable SND_PCI \
        --disable SND_OSSEMUL --disable SND_SEQUENCER --disable SND_HRTIMER --disable SND_BCM2835 --disable SND_DEBUG
    make -s olddefconfig
    echo "Configuration changes from the Move s:"
    ./scripts/diffconfig /out/move.config .config
    # LOCALVERSION= keeps the release 5.15.92-rt57-v8 (no "+" for a tree off a tag).
    make -s -j"$(nproc)" LOCALVERSION= Image modules
    [ "$(cat include/config/kernel.release)" = 5.15.92-rt57-v8 ]
    cp .config /out/kernel.config
    cp LICENSES/preferred/GPL-2.0 /out/GPL-2.0.txt
    cp Module.symvers /out/reference/
    cp sound/core/snd.ko sound/core/snd-timer.ko sound/core/snd-pcm.ko \
       drivers/usb/gadget/function/u_audio.ko drivers/usb/gadget/function/usb_f_uac2.ko /out/
    cp drivers/usb/gadget/function/usb_f_ncm.ko drivers/usb/gadget/function/usb_f_fs.ko drivers/usb/gadget/libcomposite.ko /out/reference/
'
ls -l "$OUT"/*.ko
echo "Check them against a Move before use: tools/check-kernel.sh $HOST"
