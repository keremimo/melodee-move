#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Runs on a stock Ableton Move, sent on stdin by tools/probe-stock.sh, so
# nothing is copied to the Move. It only reads: it writes no file, starts or
# stops nothing, opens no device and installs nothing. It answers what a
# native JACK client (move-base) would need on the stock firmware: the
# kernel and C library, room on disk, which libraries exist, how Ableton's
# app starts and holds the hardware, and whether RNBO's jackd and
# jack_move.so are installed. POSIX sh, for BusyBox too.

section() { printf '\n== %s\n' "$1"; }
# Shows a command, then its first $LIMIT lines (60 by default).
show() { printf '$ %s\n' "$1"; sh -c "$1" 2>&1 | head -n "${LIMIT:-60}"; }
have() { command -v "$1" >/dev/null 2>&1; }

section "System"
show "uname -a"
show "cat /etc/os-release"
show "tr -d '\\000' < /sys/firmware/devicetree/base/model; echo"
show "cat /proc/1/comm"
show "[ -e /sys/kernel/realtime ] && echo realtime=\$(cat /sys/kernel/realtime) || echo 'no /sys/kernel/realtime (not PREEMPT_RT, or not exposed)'"
show "zcat /proc/config.gz 2>/dev/null | grep -E '^CONFIG_(PREEMPT|PREEMPT_RT|PREEMPT_DYNAMIC|HZ|HIGH_RES_TIMERS|COMPAT|NO_HZ_FULL)=' || echo 'no /proc/config.gz'"

section "CPU and memory"
show "grep -E '^(model name|Hardware|Revision|Model)' /proc/cpuinfo | sort -u"
show "grep -c ^processor /proc/cpuinfo"
show "grep -E '^(MemTotal|MemAvailable|SwapTotal)' /proc/meminfo"

section "Storage (ro or rw)"
show "df -h"
show "grep -E ' (/|/data|/data/UserData|/opt|/tmp) ' /proc/mounts"

section "C and C++ runtime"
show "ldd --version 2>&1 | head -n 1"
show "getconf GNU_LIBC_VERSION"
libs=$(find / -xdev \( -name '*.so' -o -name '*.so.*' \) 2>/dev/null)
libc=$(printf '%s\n' "$libs" | grep -E '/libc\.so\.6$' | head -n 1)
libstdcxx=$(printf '%s\n' "$libs" | grep -E '/libstdc\+\+\.so\.6$' | head -n 1)
printf 'libc: %s\n' "${libc:-not found}"
[ -n "$libc" ] && printf 'newest GLIBC symbol: %s\n' "$(tr -c 'A-Za-z0-9_.' '\n' < "$libc" | grep -E '^GLIBC_2\.[0-9]+(\.[0-9]+)?$' | sort -u -t. -k2,2n -k3,3n | tail -n 1)"
printf 'libstdc++: %s\n' "${libstdcxx:-not found}"
[ -n "$libstdcxx" ] && printf 'newest GLIBCXX symbol: %s\n' "$(tr -c 'A-Za-z0-9_.' '\n' < "$libstdcxx" | grep -E '^GLIBCXX_3\.4\.[0-9]+$' | sort -u -t. -k3,3n | tail -n 1)"

section "Libraries melodee-move links (root filesystem)"
for lib in libjack.so.0 libfreetype.so.6 libfontconfig.so.1 libpng16.so.16 libz.so.1 libexpat.so.1 \
           libbz2.so.1.0 libbrotlidec.so.1 libbrotlicommon.so.1 libdb-5.3.so libasound.so.2; do
    found=$(printf '%s\n' "$libs" | grep -F "/$lib" | head -n 2 | tr '\n' ' ')
    printf '%-22s %s\n' "$lib" "${found:-missing}"
done

section "Tools"
for tool in systemctl service chrt taskset pgrep ps timeout python3 tar gzip rsync busybox ldd sshd dropbear jackd jack_lsp; do
    if have "$tool"; then printf '%-10s %s\n' "$tool" "$(command -v "$tool")"; else printf '%-10s missing\n' "$tool"; fi
done
show "ls -l /bin/sh"
show "for f in /usr/lib/openssh/sftp-server /usr/libexec/sftp-server /usr/libexec/openssh/sftp-server; do [ -e \"\$f\" ] && echo \"\$f\"; done; true"
# No ldd on the stock firmware: the dynamic loader lists a binary's libraries itself.
loader=$(ls /lib/ld-linux-aarch64.so.1 2>/dev/null)
libraries_of() { [ -n "$loader" ] && show "'$loader' --list '$1' 2>&1 | grep -E 'not found|=> /(data|opt)' || echo '(all from the system)'"; }

section "Realtime limits"
show "ulimit -r; ulimit -l"
show "cat /proc/sys/kernel/sched_rt_runtime_us"

section "Audio and the Move's hardware"
show "ls -l /dev/ablspi* /dev/snd 2>&1"
show "cat /proc/asound/cards"
printf '$ processes holding /dev/ablspi*\n'
for process in /proc/[0-9]*; do
    for descriptor in "$process"/fd/*; do
        case "$(readlink "$descriptor" 2>/dev/null)" in
        *ablspi*) printf '%s %s\n' "${process#/proc/}" "$(cat "$process/comm" 2>/dev/null)"; break ;;
        esac
    done
done

section "USB audio gadget (24-bit to a computer over USB-C)"
# The audio jacks are fixed at 44.1 kHz / 16-bit by the XMOS link; the CM4's
# own USB-C port (dwc2, in gadget mode) is not. It could be a USB sound card
# if the kernel has the UAC2 gadget function (f_uac2) and ALSA's core, which
# that function needs. Read only: configfs is listed if mounted, never mounted.
release=$(uname -r)
modules=/lib/modules/$release
show "ls -d '$modules' /boot 2>&1; ls /boot 2>&1 | head -n 20"
LIMIT=80 show "find '$modules' -name '*.ko*' 2>/dev/null | grep -E '/usb/gadget/|/sound/' | sed 's|.*/||' | sort || true"
LIMIT=80 show "grep -E 'usb/gadget|/sound/' '$modules/modules.builtin' 2>/dev/null | sed 's|.*/||' | sort || echo 'no gadget or sound entries in modules.builtin'"
show "for c in /boot/config-$release /boot/config*; do [ -f \"\$c\" ] && { echo \"\$c\"; grep -E '^CONFIG_(USB_CONFIGFS_F_UAC[12]|USB_F_UAC[12]|USB_CONFIGFS_F_MIDI|USB_LIBCOMPOSITE|SND|SND_PCM)=' \"\$c\"; break; }; done; true"
show "grep -E ' (afunc_bind|u_audio_start_capture|u_audio_start_playback|f_midi_bind|snd_pcm_new|snd_card_new)\$' /proc/kallsyms | head -n 8 || true"
show "grep -E '^(usb_f_|u_audio|libcomposite|snd|g_)' /proc/modules || echo 'no gadget or sound modules loaded'"
show "ls /proc/asound 2>&1 | head -n 5"
show "grep configfs /proc/mounts || echo 'configfs not mounted'"
show "for u in /sys/class/udc/*; do [ -e \"\$u\" ] && echo \"\$u: \$(cat \"\$u/state\" 2>/dev/null)\"; done; true"
show "for g in /sys/kernel/config/usb_gadget/*; do [ -d \"\$g\" ] || continue; echo \"-- \$g UDC=\$(cat \"\$g/UDC\" 2>/dev/null)\"; ls \"\$g/functions\"; ls -l \"\$g\"/configs/*/ 2>/dev/null | grep -- '->'; done; true"
show "find /sys/firmware/devicetree/base -name dr_mode 2>/dev/null | while read -r f; do echo \"\$f: \$(tr -d '\\000' < \"\$f\")\"; done"
LIMIT=120 show "cat /etc/init.d/setup-usb-network-gadget"

section "Processes, kernel threads left out (realtime threads below)"
LIMIT=120 show "ps -eo pid,ppid,cls,rtprio,nlwp,rss,user,args 2>/dev/null | awk 'NR == 1 || \$2 != 2' || ps w 2>/dev/null || ps"
LIMIT=80 show "ps -eLo pid,tid,cls,rtprio,comm 2>/dev/null | awk 'NR == 1 || (\$3 != \"TS\" && \$3 != \"-\")'"

section "How Ableton's app starts"
show "ls /etc/init.d"
LIMIT=200 show "cat /etc/init.d/move"
show "ls /etc/rc*.d 2>/dev/null | grep -i move"
show "cat /etc/inittab 2>/dev/null | grep -v '^#'"
show "have() { command -v \"\$1\" >/dev/null 2>&1; }; have systemctl && systemctl list-units --type=service --no-pager --plain | grep -iE 'move|ableton|rnbo|jack' || true"
show "grep -rlsiE 'takeover|shadow|rnbo|schwung' /etc 2>/dev/null"

section "Ableton's app as it runs"
for pid in $(pgrep -f '/opt/move/|MoveOriginal' 2>/dev/null); do
    printf -- '-- pid %s (%s), user %s\n' "$pid" "$(cat "/proc/$pid/comm" 2>/dev/null)" "$(awk '/^Uid:/ {print $2}' "/proc/$pid/status" 2>/dev/null)"
    printf 'command: %s\n' "$(tr '\000' ' ' < "/proc/$pid/cmdline" 2>/dev/null)"
    # Only the loader's variables, which show a shim preloaded into the app.
    printf 'LD_ variables: %s\n' "$(tr '\000' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -E '^LD_' | tr '\n' ' ')"
    printf 'libraries from outside /lib and /usr/lib:\n%s\n' "$(awk '$6 ~ /\.so/ {print $6}' "/proc/$pid/maps" 2>/dev/null | sort -u | grep -vE '^/(lib|usr/lib)/')"
done
show "ls -l /opt/move/Move /opt/move/MoveOriginal"
show "dd if=/opt/move/Move bs=2 count=1 2>/dev/null | grep -q '#!' && cat /opt/move/Move || echo '/opt/move/Move is a binary'"
LIMIT=40 show "ls -la /dev/shm"

section "Ableton's software"
LIMIT=40 show "ls -la /opt/move"
show "find /opt/move -maxdepth 2 -iname '*version*' 2>/dev/null | head -n 5 | while read -r f; do echo \"-- \$f\"; head -c 300 \"\$f\"; echo; done"
show "ls /etc | grep -iE 'move|ableton|version|release'"

section "Third-party installs on /data"
show "ls -la /data/UserData"
show "ls -la /data/UserData/rnbo"
show "ls /data/UserData/move-anything /data/UserData/schwung 2>&1 | head -n 20"
jack_files=$(find /data/UserData -maxdepth 6 \( -name 'jack_*.so' -o -name jackd -o -name 'libjack*.so*' -o -name 'libjackserver*.so*' \) 2>/dev/null)
printf '$ JACK files under /data/UserData\n%s\n' "${jack_files:-none}"
for driver in $(printf '%s\n' "$jack_files" | grep -E '/jack_[a-z]+\.so$'); do
    show "ls -l '$driver'"
    libraries_of "$driver"
    # The names a driver gives its ports and shared memory say what it carries.
    LIMIT=80 show "tr -c '\\040-\\176' '\\n' < '$driver' | grep -iE 'system|capture|playback|display|midi|shm|shadow|ablspi|port' | grep -vE '^.{0,3}\$' | sort -u"
done

section "Schwung and RNBO"
# Small text files are shown whole (they say how each starts and what it
# runs); binaries and larger files only by name. Printable ASCII is spelled
# out in octal: BusyBox's tr may lack the [:print:] class.
text_file() { [ -f "$1" ] && [ "$(wc -c < "$1")" -lt 20000 ] && [ "$(tr -d '\040-\176\t\n\r' < "$1" | wc -c)" -eq 0 ]; }
for folder in /data/UserData/schwung /data/UserData/rnbo; do
    [ -d "$folder" ] || continue
    LIMIT=150 show "cd '$folder' && find . -maxdepth 3 ! -path './cache*' ! -path './espeak-ng-data*' ! -path './Sentry*' | sort"
done
for file in /data/UserData/schwung/launch-standalone.sh /data/UserData/schwung/jack_state /data/UserData/schwung/host/*.sh \
            /data/UserData/schwung/bin/*.sh /data/UserData/rnbo/scripts/* /data/UserData/rnbo/config/*; do
    text_file "$file" && LIMIT=120 show "cat '$file'"
done
for program in /data/UserData/schwung/bin/* /data/UserData/schwung/display-server; do
    [ -f "$program" ] && [ -x "$program" ] && libraries_of "$program"
done
LIMIT=30 show "tail -n 30 /data/UserData/schwung/heal-boot.log"

section "JACK server"
show "ps w 2>/dev/null | grep -E '[j]ackd' || echo 'no jackd running'"
if ps w 2>/dev/null | grep -qE '[j]ackd' && have jack_lsp; then
    # Lists ports of the running server only; never starts one.
    if have timeout; then show "JACK_NO_START_SERVER=1 timeout 5 jack_lsp -t"; else show "JACK_NO_START_SERVER=1 jack_lsp -t"; fi
fi

printf '\n== Done. Nothing on the Move was changed.\n'
