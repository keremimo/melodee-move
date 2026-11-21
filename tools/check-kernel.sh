#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Checks, before they go near it, that a Move's running kernel takes the
# modules tools/kernel-modules.sh built into build/kmod (docs/USB_AUDIO.md):
# tools/kernel_check.py makes insmod's own check here.
#
#   tools/check-kernel.sh [HOST]      default move.local; root SSH
#
# Read only on the Move: nothing is mounted, loaded or written there. The
# booted kernel is the running root slot's /boot/Image (U-Boot loads it from
# there; the FAT /boot holds another build), hidden under the FAT mount, so
# the slot is read whole, compressed (some 250 MB), and the Image taken out
# of it here with debugfs, in Docker. Kept in build/kernel-check/.
set -eu
cd "$(dirname "$0")/.."
HOST=${1:-move.local}
OUT=build/kernel-check
KMOD=build/kmod
[ -f "$KMOD/usb_f_uac2.ko" ] || { echo "No modules in $KMOD: run tools/kernel-modules.sh first." >&2; exit 1; }
mkdir -p "$OUT"

ssh "root@$HOST" 'uname -r' > "$OUT/release"
RELEASE=$(cat "$OUT/release")
ssh "root@$HOST" 'grep -E " (_text|_end|__(start|stop)___(ksymtab|ksymtab_gpl|kcrctab|kcrctab_gpl))$" /proc/kallsyms' > "$OUT/kallsyms.txt"
for module in kernel/drivers/usb/gadget/libcomposite.ko kernel/drivers/usb/gadget/function/usb_f_ncm.ko \
              kernel/drivers/usb/gadget/function/usb_f_fs.ko; do
    ssh "root@$HOST" "cat /lib/modules/$RELEASE/$module" > "$OUT/$(basename "$module")"
done
# The root slot: the last root= on the command line (U-Boot adds the one it booted).
ROOT=$(ssh "root@$HOST" 'tr " " "\n" < /proc/cmdline | sed -n "s/^root=//p" | tail -n 1')
case "$ROOT" in /dev/mmcblk0p[0-9]) ;; *) echo "Unexpected root device: $ROOT" >&2; exit 1 ;; esac
echo "Reading $ROOT from $HOST (read only, about 250 MB)..."
ssh "root@$HOST" "gzip -1 -c $ROOT" > "$OUT/root.img.gz"
docker run --rm --platform linux/arm64 -v "$PWD/$OUT:/k" ubuntu:22.04 sh -eu -c "
    apt-get update -qq > /dev/null && apt-get install -y -qq e2fsprogs > /dev/null 2>&1
    gunzip -c /k/root.img.gz > /tmp/root.img
    debugfs -R 'dump /boot/Image-$RELEASE /k/Image' /tmp/root.img 2> /dev/null"
rm -f "$OUT/root.img.gz"
[ -s "$OUT/Image" ] || { echo "No /boot/Image-$RELEASE in $ROOT" >&2; exit 1; }
python3 -I tools/kernel_check.py "$OUT" "$KMOD"
