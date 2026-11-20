# melodee-move

Melodee on the Ableton Move: move-base (`src/move/`), a runtime on
poundhard-armbian's `jackd -d move`, and the apps on it (`src/spike/` for
now). README.md says what each part does and how to run it.

- Realtime: `render()` and JACK's process callback never allocate, lock,
  wait, log or touch files. Threads share state through atomics and the
  queues in `src/move/queues.h`.
- Build for the Move's Cortex-A72 (`-mcpu=cortex-a72`, ARMv8.0-A). Never
  use the MPC Sample's A55 tuning: its ARMv8.2 instructions crash the A72.
- Lights go out on MIDI channel 16 only; jack_move drops the rest.
- The screen has one writer at a time: stop the launcher menu first.
- Never ship or commit `jack_move.so`, `/opt/move` or anything else taken
  from a Move; keep downloads, captures and device data in ignored `build/`,
  `dist/` or `.local/`.
- Melodee itself is changed in melodee-sample, and sample-base in
  sample-base; never edit their files from here.
- Four-space indentation, `SPDX-License-Identifier: AGPL-3.0-only` headers,
  Conventional Commits.

Run `./build.sh` (build, unit tests, self-test, previews, crush) and
`tools/deploy.sh HOST` to run on a Move.

## Credit

Melodee is the work of Kerem Kiliç (Ellic Studio). Keep his name, Ellic
Studio and the copyright and SPDX headers as they are, and name Melodee by
Kerem Kiliç (Ellic Studio) as the origin wherever a fork describes where it
comes from.
