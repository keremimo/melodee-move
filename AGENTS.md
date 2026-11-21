# melodee-move

Melodee on the Ableton Move: move-base (`src/move/`), a runtime with two
hosts, the Move's own device `/dev/ablspi0.0` on Ableton's firmware (`spi`)
and `jackd -d move` on poundhard-armbian (`jack`), and the apps on it
(`src/spike/` for now). README.md says what each part does and how to run it.

- The goals are Melodee's sampler and Chop on the Move, and the shortest
  way from a pad to the speaker. Never add a thread, queue or block between
  a pad and its sound: pads go to `App::audio_event()` on the audio thread
  in the cycle they were read.
- Realtime: `render()`, `audio_event()`, the SPI cycle and JACK's process
  callback never allocate, lock, wait, log or touch files. Threads share
  state through atomics and the queues in `src/move/queues.h`.
- The device: one ioctl per 128-frame block; answer only the screen chunk
  the XMOS asks for; clear MIDI in once read; lights go out on channel 1,
  cable 0 (JACK's jack_move wants channel 16; Lights writes 16 and the SPI
  host moves it).
- Build on Ubuntu 22.04 (glibc 2.35, Ableton's newest) with its GCC 12
  (`tools/Dockerfile`; its default GCC 11 spends minutes on Melodee's
  model) and libstdc++ linked in, for the Move's Cortex-A72
  (`-mcpu=cortex-a72`, ARMv8.0-A); never the MPC Sample's A55 tuning.
  libjack is loaded at run time only.
- Never ship or commit `jack_move.so`, `/opt/move` or anything else taken
  from a Move; keep downloads, captures, probe reports and device data in
  ignored `build/`, `dist/` or `.local/`.
- Never read or adapt code from bobbydigitales/move-anything (CC BY-NC-SA,
  forbids AI-made adaptations). Schwung and schwung-spi are MIT; EvilGenius
  is MPL-2.0: learn from them, write our own, credit them in THIRD_PARTY.md.
  Never use FM-1 firmware code (EvilGenius's core is one).
- Nothing here may change a Move for good: scripts stop and start Ableton's
  app, add folders under `/data/UserData` and say how to remove them.
- Melodee's engine and model (`src/melodee/engine`, `src/melodee/model`,
  and header-only pieces such as `shell/chop.h`) are compiled unchanged from
  the melodee-sample checkout beside this one (`MELODEE`, `MELODEE_SAMPLE`).
  Melodee itself is changed in melodee-sample, and sample-base in
  sample-base; never edit their files from here.
- Four-space indentation, `SPDX-License-Identifier: AGPL-3.0-only` headers,
  Conventional Commits.

Run `./build.sh quick` (build, unit tests) while working and `./build.sh`
(also self-test, previews, the simulated Move, packages, crush) before a
merge to main. On a Move: `tools/deploy-stock.sh HOST` or
`tools/install-schwung-tool.sh HOST`; `tools/probe-stock.sh HOST` reads one.

## Credit

Melodee is the work of Kerem Kiliç (Ellic Studio). Keep his name, Ellic
Studio and the copyright and SPDX headers as they are, and name Melodee by
Kerem Kiliç (Ellic Studio) as the origin wherever a fork describes where it
comes from.
