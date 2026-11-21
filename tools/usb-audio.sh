#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# The Move as an 18-channel sound card to a computer on USB-C
# (docs/USB_AUDIO.md). Runs on the Move, as root, from the package's folder:
#
#   sh usb-audio.sh on       ALSA's core and the UAC2 gadget function loaded
#                            from kmod/ beside this script, and an audio
#                            function added to Ableton's USB gadget (g1),
#                            beside its network
#   sh usb-audio.sh off      all of it taken out again
#   sh usb-audio.sh status
#
# Nothing is written to the Move's root filesystem, and a reboot undoes it
# all. Adding or taking out the function rebinds the gadget: the USB network
# (SSH over the cable) and the computer's view of the Move drop for about a
# second, so run it over Wi-Fi SSH. The modules are built for one kernel
# (tools/kernel-modules.sh, checked by tools/check-kernel.sh); a kernel they
# do not match refuses them, and nothing changes. Melodee finds the sound
# card by itself, whenever it appears.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
KMOD="$HERE/kmod"
RELEASE=5.15.92-rt57-v8
GADGET=/sys/kernel/config/usb_gadget/g1
FUNCTION=$GADGET/functions/uac2.melodee
LINK=$GADGET/configs/c.1/uac2.melodee
MODULES="snd snd-timer snd-pcm u_audio usb_f_uac2"
UDC=

loaded() { grep -q "^$(echo "$1" | tr - _) " /proc/modules; }

# configfs takes no change to a bound gadget: let go of the controller, change,
# give it back.
unbind() {
    UDC=$(cat "$GADGET/UDC")
    if [ -n "$UDC" ]; then echo "" > "$GADGET/UDC"; fi
}
bind() {
    if [ -n "$UDC" ]; then echo "$UDC" > "$GADGET/UDC"; fi
}

status() {
    for module in $MODULES; do
        if loaded "$module"; then echo "$module: loaded"; else echo "$module: -"; fi
    done
    if [ -d "$FUNCTION" ]; then
        echo "gadget: uac2.melodee, channel mask $(cat "$FUNCTION/p_chmask"), $(cat "$FUNCTION/p_srate") Hz, $(cat "$FUNCTION/p_ssize") bytes"
    else
        echo "gadget: no audio function"
    fi
    echo "controller: $(cat "$GADGET/UDC" 2>/dev/null)"
    grep -s UAC2_Gadget /proc/asound/cards || echo "sound card: none"
}

on() {
    [ "$(uname -r)" = "$RELEASE" ] || { echo "This kernel is $(uname -r); the modules here are for $RELEASE." >&2; exit 1; }
    [ -d "$GADGET" ] || { echo "No USB gadget g1 here: is this a Move on Ableton's firmware?" >&2; exit 1; }
    for module in $MODULES; do
        loaded "$module" || insmod "$KMOD/$module.ko"
    done
    if [ ! -d "$FUNCTION" ]; then
        unbind
        mkdir "$FUNCTION"
        # 18 channels to the computer (1-2 the mix, 3-18 eight stereo
        # submixes), 32-bit at the Move's 44.1 kHz; none from it; no volume
        # or mute of its own.
        echo 0x3ffff > "$FUNCTION/p_chmask"
        echo 44100 > "$FUNCTION/p_srate"
        echo 4 > "$FUNCTION/p_ssize"
        echo 0 > "$FUNCTION/c_chmask"
        echo 0 > "$FUNCTION/p_volume_present"
        echo 0 > "$FUNCTION/p_mute_present"
        ln -s "$FUNCTION" "$LINK"
        bind
    fi
    # Melodee runs as `ableton` from Schwung's Tools menu: it opens the card too.
    sleep 1
    chmod 666 /dev/snd/* 2>/dev/null || true
    status
}

off() {
    if [ -d "$FUNCTION" ]; then
        unbind
        rm -f "$LINK"
        rmdir "$FUNCTION"
        bind
    fi
    for module in usb_f_uac2 u_audio snd-pcm snd-timer snd; do
        if loaded "$module" && ! rmmod "$(echo "$module" | tr - _)"; then
            echo "$module is in use: quit Melodee, then run this again (or reboot)." >&2
            exit 1
        fi
    done
    status
}

[ "$(id -u)" = 0 ] || { echo "Run as root." >&2; exit 1; }
case "${1:-status}" in
on) on ;;
off) off ;;
status) status ;;
*) echo "usage: $0 on|off|status" >&2; exit 2 ;;
esac
