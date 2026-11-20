# melodee-move

Melodee, the groovebox by Kerem Kiliç (Ellic Studio), on the **Ableton Move**.

This is the first step: **move-base**, a runtime that hosts an app on the
Move the way [sample-base](../melodee-sample/base) hosts Melodee on the MPC
Sample, and a **spike** app that exercises each path through it.
It builds and its offline tests pass. **It has not run on a Move yet.**

## The platform

The Move runs [poundhard-armbian](https://github.com/nzimas/poundhard-armbian):
Armbian trixie, a 6.18 PREEMPT_RT kernel on the Move's Compute Module 4, and
`jackd -d move`, whose driver (`jack_move.so`, copied from the owner's own
RNBO install, never shipped) exposes the whole instrument as JACK ports:

| Port | Carries |
| --- | --- |
| `system:playback_1/2`, `system:capture_1/2` | audio, 44.1 kHz, 128-frame periods |
| `system:midi_capture` | pads, steps, knobs, buttons, jogwheel (MIDI channel 16) |
| `system:midi_playback` | pad, step and button lights (channel 16 only) |
| `system:display` | the 128 x 64 screen: `MOVEDISP` + 1024 bytes, one event a frame |
| `system:midi_capture_ext` | external MIDI |

melodee-move is a JACK client on those ports, with no Node, Python or
Schwung in between.

## Layout

| Folder | What it holds |
| --- | --- |
| `src/move/` | move-base. `app.h` the `App` interface (sample-base's hooks: `render`, `surface`, `lights`, `draw`, `service`); `runtime.*` the JACK client and its threads; `audio.h` 128-frame S32 blocks cut from JACK's float periods; `controls.*` the Move's MIDI map; `lights.*` LEDs and the 128-colour palette; `display.*` the screen format and the crush; `font.*` a 5 x 7 pixel font; `tools.*` the offline modes |
| `src/spike/` | the spike app |
| `tests/` | unit tests for the pure parts |
| `tools/` | build image, dependencies, deploy scripts |

The threads follow sample-base: `render()` on JACK's process thread,
`surface()`/`lights()` on a MIDI thread, `draw()` on a screen thread,
`service()` on the main thread. They share state through atomics and
lock-free queues (`queues.h`); the process callback never locks or
allocates.

## Build

Needs Docker and Python 3.

```sh
./build.sh
```

This builds `build/melodee-move` for linux/arm64 on Debian trixie (the
Move's release), tuned for the Move's Cortex-A72 and never for the MPC
Sample's A55, then runs the unit tests, the sound self-test
(`build/self-test.wav`), the screen previews (`build/preview/`) and, when
`../melodee-sample/build/melodee-preview` exists (or `MELODEE_PREVIEWS` names
another folder), crushes Melodee's 320 x 240 page previews to 128 x 64 in
`build/crushed/`.

## Run on the Move

The Move must already run poundhard-armbian, with key-based root SSH. With
the launcher menu idle (no appliance running):

```sh
tools/deploy.sh move.local --previews ../melodee-sample/build/melodee-preview
```

It copies the binary and `run-spike.sh` to `/data/UserData/melodee-move`,
stops the launcher menu (it redraws the screen every period), runs the spike
in your terminal and starts the menu again afterwards. **Shift + Back** on
the Move, Ctrl-C, or the SSH connection dropping stops it and brings the
menu back; a spike that does not stop within 5 seconds is killed.

Nothing here changes the Move for good. The menu is only stopped, never
disabled, so a reboot always brings it back (or
`systemctl start move-launcher-menu`); no boot files, services, packages or
user data are touched. To remove the spike:
`ssh root@move.local rm -rf /data/UserData/melodee-move`. A status line every 5 seconds shows blocks,
xruns, JACK's DSP load, the process callback's own mean and longest time
(`process_us=mean/peak of period`), MIDI in, lights and frames sent.

On the Move:

- **Pads** play sines, C3 upward a semitone a pad; pressure adds the octave.
  Held pads light white.
- **Play** runs a 16-step click; **steps** toggle it; **knob 1** is the
  tempo, **knob 2** the overtone; the **master knob** is the volume.
- **Jogwheel**: HOME, INPUTS (every control names itself as you touch it),
  then Melodee's pages crushed to the screen. **Jog push** swaps letterbox
  and stretch; hold **Shift** to see the page's name.

What to check, in order:

1. Sound from the speaker and headphones; `xruns=0` in the status line.
2. The INPUTS page names every pad, step, knob, touch and button correctly.
3. Pads, steps, Play, the row buttons, Shift and Back light as described.
4. The HOME page's `IN` level moves with the microphone (is
   `system:capture_1/2` the microphone, the line input, or silent?).
5. The screen keeps up while playing (`frames_failed=0`).
6. Pad pressure arrives (`PRESS` lines on INPUTS).

## Stock firmware?

move-base is only a JACK client of `jackd -d move`, so it may not need
Armbian at all: RNBO's takeover already runs that server, with the same
`jack_move.so`, on Ableton's own firmware. Before trying, ask a Move on stock
firmware what it has, without changing it:

```sh
tools/probe-stock.sh move.local
```

`tools/probe-stock-remote.sh` goes over SSH on stdin (nothing is copied to
the Move) and only reads: the kernel (PREEMPT_RT or not), glibc and
libstdc++ versions, free space and which partitions are writable, which of
melodee-move's libraries exist, who holds `/dev/ablspi0.0`, Ableton's
processes and their realtime threads, how `/etc/init.d/move` starts the app,
and whether RNBO's `jackd` and `jack_move.so` are installed. The report is
saved in `.local/probe/` (ignored by git).

## Tested so far

- Unit tests: control decoding, lights and palette, the screen format, the
  crush, the reblocker, the queues, the spike's hooks.
- The self-test renders sound; the previews draw.
- Against a real JACK server (the dummy driver, in the build image): the
  client starts, sends lights and frames, cuts 100-frame periods into
  128-frame blocks with 128 frames of latency and no underruns, stops on
  `--seconds` and on the server going away. The process callback took about
  50 µs of a 2.9 ms period on an Apple-silicon Mac (expect more on the
  Move's A72). That container had no realtime scheduling, so its xruns say
  nothing about the Move.
- `run-spike.sh` in a pseudo-terminal, as `ssh -t` runs it, with
  `systemctl` faked: the spike timing out, Ctrl-C, the terminal hanging up,
  and a spike that ignores signals (killed after 5 seconds). Each time the
  menu was stopped once and started once, and no spike was left running.
- The stock probe in Debian trixie and Alpine (BusyBox) containers: it runs
  to the end in both, and the containers' filesystems were the same after
  it as before.

Not tested: anything on a Move.

## What the crush shows

Melodee's pages do not survive the trip: at 128 x 64 one-bit the small text,
waveforms and colour cues vanish, and only large numbers stay readable
(`build/crushed/chop.stretch.png`). The engine moves across; the pages have
to be designed again for this screen.

## Next

- Run the spike on a Move and settle the checks above.
- An adapter from sample-base's `App` to move-base's, so Melodee's engine,
  model and plug-in host build here unchanged.
- Melodee's pages and controls designed for the 128 x 64 screen, 32 pads,
  16 steps and 8 knobs.
- A launcher menu entry instead of stopping the menu.

## Licence

AGPL-3.0-only, as Melodee. Third-party parts: [THIRD_PARTY.md](THIRD_PARTY.md).
