# USB audio: the Move as an 18-channel sound card

With Melodee running, a computer on the Move's USB-C port can record 18
channels from it, as melodee-sample gives the MPC Sample:

| Channels | Carry |
| --- | --- |
| 1-2 | The main mix: at full volume (Setup's USB VOLUME: FULL, the default), or as the Volume knob has it (KNOB) |
| 3-18 | Eight stereo submixes, 1 on 3-4 up to 8 on 17-18 |

Each of the 16 tracks and each send bus A-D is on one submix, 1-8, or on
none. A track or bus on a submix stays in the main mix: the submix is a copy
for recording, and the jacks and speaker are untouched. A track is taken as it
joins the mix: after its own chain, its level, pan and mute (so in stereo,
panned), with the kit pad that keys a Sidechain; a bus as its return gives it.
Unless a project says otherwise, tracks 1-8 are on submixes 1-8 and tracks
9-16 and the buses on none (Kerem, 2026-11-20). Each project keeps its own, in
its Move section (`usb T N`, `usb_bus B N`, only where not the default); a
moved track takes its submix along; a change is an undo step of its own.

On the Move: the Mixer (Shift + Step 4), its last page **USB**. Knobs 1-8
are eight tracks' submixes, a step a detent from OFF to 8; Left and Right walk
1-8, 9-16 and the buses A-D (knobs 1-4). Each strip shows its submix in a
chip (`-` for none), the detail line `SUB 3  CH 7-8` or `OFF  MIX ONLY`; a
touched knob shows the submix large and its channels under it (docs/UI.md,
`docs/ui/mixer.png`). Setup (Shift + Step 2) has **USB AUDIO**, read only:
`OFF` (the gadget not set up), `NO COMPUTER`, `18 CH` (or `FAILED`, a device
it could not open); and **USB VOLUME**, `FULL` or `KNOB`.

Not in any submix, only in 1-2: Knob FX's return, the Looper's strips, the
master chain and compressor, a Sidechain's ducking (applied to the mix as a
whole), and the monitored input. With a latent effect on a track (plug-in
delay compensation), the tracks' submixes run ahead of the buses' by the
returns' delay, as melodee-sample's exported stems do.

The computer sees the device as the Move's USB network gadget's name
("Ableton Move USB Ethernet"), beside Ableton's own "Ableton Move Audio"
(stereo, the Move's XMOS, Ableton's app only). It runs at 44.1 kHz, 32-bit,
the Move's own rate: set the computer's side to 44.1 kHz. From the Move's
audio to the computer is some 45 ms (the ring's 1024 frames, the device's
896, the converter's 32); a DAW recording against its own playback wants that
much of an offset.

## Turning it on

Ableton's kernel has no sound support (no ALSA) and no USB audio gadget, so
Melodee brings both as kernel modules, loaded for as long as the Move is on.

1. Build them (Docker, some 15 minutes the first time), and check them
   against the Move. Both read the Move only:

   ```sh
   tools/kernel-modules.sh move.local
   tools/check-kernel.sh move.local
   ```

   `./build.sh` then packs them with Melodee (`dist/*/kmod/`).
2. Deploy as usual (`tools/deploy-stock.sh` or
   `tools/install-schwung-tool.sh`).
3. Once after each boot, as root, over Wi-Fi SSH (the USB network drops for
   a second while the gadget is rebound):

   ```sh
   ssh root@move.local sh /data/UserData/melodee-move/usb-audio.sh on
   ```

   (or `/data/UserData/schwung/modules/tools/melodee-move/usb-audio.sh` for
   the Schwung tool). Melodee finds the sound card by itself, also when it is
   already running; the status line says `usb_audio=waiting` until the
   computer records, then `usb_audio=running`.

`usb-audio.sh off` takes it out again (quit Melodee first: its device holds
the modules), `status` says what is loaded. Nothing is written to the Move's
root filesystem: a reboot is the stock Move, and deleting the package's folder
removes the rest.

## How it works

**The kernel.** The Move runs 5.15.92-rt57-v8, built by Ableton with Yocto,
whose source Ableton does not publish. Its configuration is in the Move's own
`configs.ko` (`CONFIG_IKCONFIG=m`), gzipped between `IKCFG_ST` and
`IKCFG_ED`, readable without loading it. The kernel is Raspberry Pi's
`rpi-5.15.y` at `14b35093` (meta-raspberrypi kirkstone's pin for 5.15.92) with
kernel.org's `patch-5.15.92-rt57`, which applies without a reject.
`tools/kernel-modules.sh` builds that tree with the Move's configuration plus
`CONFIG_SND=m`, `SND_PCM`, `SND_TIMER` and `USB_CONFIGFS_F_UAC2`, every other
sound driver off, and `LOCALVERSION=` (else the release gains a `+`). The
kernel has `MODVERSIONS` and no module signing, so it takes a module whose
every imported symbol carries the CRC it exports. `tools/check-kernel.sh`
makes that check beforehand. The booted kernel is the running root slot's
`/boot/Image-5.15.92-rt57-v8` (U-Boot `ext4load`s it), hidden under the FAT
`/boot` mount, whose `Image` is another build. The script reads the slot
whole, takes the Image out with debugfs, and reads its export tables where
`/proc/kallsyms` places them. Checked on 2026-11-20: all 9672 exports have the
same CRC, the Move's `usb_f_ncm`, `usb_f_fs` and `libcomposite` built again
have the same `srcversion`, and every import of the five modules agrees.

**The gadget.** `usb-audio.sh on` adds a UAC2 function to Ableton's gadget
`g1` (until then NCM networking only) on the CM4's own USB controller
(`fe980000.usb`, dwc2, high speed, dedicated FIFOs): `p_chmask 0x3ffff`, 44.1
kHz, 4 bytes, no capture, no volume or mute controls. The 5.15 `f_uac2`
allows 27 channels a direction; 18 at 32 bits is 432 bytes a microframe of
high speed's 1024, in a 2 KB IN FIFO. `u_audio` offers its PCM in that one
format and channel count, at most a page a period and 16 a buffer: 56-frame
periods, 896 frames.

**The writer** (`src/move/usb_audio.*`). The audio thread hands each block,
the mix and the sixteen submix channels, to a lock-free ring
(sample-base's `FrameRing`, compiled from melodee-sample's `base/`); a thread
of its own opens the gadget's PCM through ALSA's ioctls (the Move has no
libasound), primes 1024 frames and writes. The Move's clock (its XMOS) and
the computer's USB clock drift apart by tens of parts per million;
`drift_trim()` nudges a band-limited converter by the ring's fill, as on the
MPC. The 5.15 `u_audio` has no Playback Rate control, so the device's buffer
draining says the computer records, and 250 ms without room says it stopped
(the ring is let go meanwhile). `u_audio` sends its last packet again while
its PCM stands still, so the writer plays out silence before it closes. The
thread runs at FIFO 20 on cores 0-2 as root, as any thread for `ableton`.

**The submixes** (`src/app/usb_submix.h`, `Studio::render`). While the
computer records, Melodee's engine writes its live stems every block
(`EngineHost::set_live_stems`, the stems its renders take, one computation for
both): each track after its chain, level, pan and mute, each bus's return.
The audio thread adds each into its submix's pair. The routes reach it as
atomics, copied by the service thread whenever the project's differ. Nothing
is tapped while no computer records.

The status line: `usb_audio=` `stopped` (no ALSA here), `missing` (no gadget),
`waiting`, `running` or `failed`, then `usb_channels`, `usb_bits`, `usb_fill`
(frames in the ring), `usb_trim_ppm`, `usb_underruns`, `usb_xruns`.

Not yet heard on a Move: the plan's hardware check (the computer lists 18
inputs; a track on its submix, muted, moved; bus A's reverb; ten minutes
without an underrun; `off` giving the gadget back).
