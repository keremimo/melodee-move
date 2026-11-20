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
show "ls /usr/lib/openssh/sftp-server /usr/libexec/sftp-server /usr/libexec/openssh/sftp-server 2>/dev/null || echo 'no sftp-server (scp may need -O)'"

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

section "Processes (realtime threads below)"
LIMIT=120 show "ps -eo pid,ppid,cls,rtprio,nlwp,rss,args 2>/dev/null || ps w 2>/dev/null || ps"
LIMIT=80 show "ps -eLo pid,tid,cls,rtprio,comm 2>/dev/null | awk 'NR == 1 || (\$3 != \"TS\" && \$3 != \"-\")'"

section "How Ableton's app starts"
show "ls /etc/init.d"
LIMIT=200 show "cat /etc/init.d/move"
show "ls /etc/rc*.d 2>/dev/null | grep -i move"
show "cat /etc/inittab 2>/dev/null | grep -v '^#'"
show "have() { command -v \"\$1\" >/dev/null 2>&1; }; have systemctl && systemctl list-units --type=service --no-pager --plain | grep -iE 'move|ableton|rnbo|jack' || true"
show "grep -rlsiE 'takeover|shadow|rnbo|schwung' /etc 2>/dev/null"

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
for driver in $(printf '%s\n' "$jack_files" | grep -E '/jack_move\.so$'); do
    show "ls -l '$driver'; ldd '$driver'"
done
for script in $(find /data/UserData/rnbo -maxdepth 3 -name '*.sh' 2>/dev/null | head -n 6); do
    LIMIT=80 show "cat '$script'"
done

section "JACK server"
show "ps w 2>/dev/null | grep -E '[j]ackd' || echo 'no jackd running'"
if ps w 2>/dev/null | grep -qE '[j]ackd' && have jack_lsp; then
    # Lists ports of the running server only; never starts one.
    if have timeout; then show "JACK_NO_START_SERVER=1 timeout 5 jack_lsp -t"; else show "JACK_NO_START_SERVER=1 jack_lsp -t"; fi
fi

printf '\n== Done. Nothing on the Move was changed.\n'
