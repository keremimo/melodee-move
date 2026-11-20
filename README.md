# melodee-move

Melodee, the groovebox by Kerem Kiliç (Ellic Studio), on the **Ableton Move**:
its sampler and Chop on the Move's 32 pads, with the Move to itself and the
shortest way from a pad to the speaker.

Three parts so far:

- **move-base**, the runtime that hosts an app on the Move as
  [sample-base](../melodee-sample/base) hosts Melodee on the MPC Sample. It
  runs on a Move on Ableton's firmware (realtime, 1 late block in 98,000).
- **Melodee** (`src/app/`), the first scope of the Rail design
  ([docs/UI.md](docs/UI.md)): one kit track of 32 pads on Melodee's own
  sampler and Chop, compiled unchanged from melodee-sample. It runs against
  a simulated Move; **not yet on a Move**.
- The **spike** (`--app spike`), which exercises every path through the
  runtime, with a latency meter.

## Melodee, first scope

- **Play**: the last pad's sample. Knobs 1-8: tune, level, pan, start, end,
  attack, decay, cutoff (Shift: finer). Touch a knob to see it large.
- **Load** (Menu): WAV files from the Move's sample library
  (`/data/UserData/UserLibrary/Samples`, or `--samples DIR`) onto the last
  pad. The jogwheel picks, its push opens a folder or loads, Back goes up.
- **Chop** (Loop): the last pad's sample, cut by Melodee's Auto, Beats,
  Regions or Threshold (knob 3 the type, knob 4 the amount). The pads play
  the slices, the steps show where the cuts fall, the jogwheel or Left /
  Right picks a slice, knobs 1 and 2 move its start and end. Push the
  jogwheel for **To Pads** (Poly, Mono, Thru), onto the first empty pads.
  Back or Loop leaves Chop without changing the pads.
- The master knob is the volume; Shift + Back goes back to Move.

Not yet: saving (everything is gone when it quits), formats other than WAV,
samples over 60 seconds, Record, Capture, Schwung's modules, more tracks.

Melodee's engine and model are compiled from a melodee-sample checkout
beside this one (`../melodee-sample`, or `MELODEE_SAMPLE`), unchanged.
`build/melodee-move --melodee-previews DIR` draws its real screens.

## Two ways onto the Move

| | Ableton's firmware (first choice) | poundhard-armbian |
| --- | --- | --- |
| Host | `spi`: the Move's own device, `/dev/ablspi0.0` | `jack`: a client of `jackd -d move` |
| Changes to the Move | none: Ableton's app is stopped while melodee-move runs, started again after | the OS conversion |
| Launched from | Schwung's Tools menu, or `tools/deploy-stock.sh` over SSH | `tools/deploy.sh` over SSH |

On Ableton's firmware the Move's XMOS I/O processor and the CM4 swap one
4096-byte page every 128 frames (44.1 kHz, 2.9 ms) through a blocking ioctl
(`src/move/spi_protocol.h`): 16-bit stereo audio both ways, 20 MIDI messages
out (lights) and 31 in (pads, knobs, buttons), and one chunk of the 1-bit
screen the XMOS asks for. Only one process may hold the device, so Ableton's
app has to be stopped first: Schwung's standalone launcher does that for a
tool, `tools/run-stock.sh` for a run over SSH.

## Latency

The device host runs the whole cycle on one thread, in the order that gives
a pad the shortest way to the speaker:

```
lights out -> exchange (blocks) -> screen chunk -> MIDI in -> pads to App::audio_event()
           -> render() -> audio out (sent with the next exchange)
```

A pad read in one exchange is in the audio sent with the next: no thread, no
queue, no extra block in between (`App::audio_event()`, `src/move/app.h`).
What is left is the Move's own: the XMOS's pad scan and its buffers to the
converters.

The spike measures both halves. Its LATENCY page times a quiet click from
`render()` out and back in (a cable from the output to the input, or the
speaker into the microphone: the XMOS's buffering both ways plus the one
block above), and shows the time from reading a pad to the end of its
render. The status line (every 5 s, and at exit) has `loop_frames`, `pad_us`,
`process_us` and `late` (exchanges more than 0.4 ms behind time).

To compare with Ableton's firmware and with Schwung, record a fingernail
tap on a pad playing a short sound with a phone held close: the gap between
the tap's click and the sound in the waveform is what you hear.

## Layout

| Folder | What it holds |
| --- | --- |
| `src/move/` | move-base. `app.h` the `App` interface (sample-base's hooks plus `audio_event`); `runtime.*` the threads; `host.*` what a host shares with them; `spi_host.cpp`, `spi_device.*`, `spi_protocol.*` the Move's device and a simulated one; `jack_host.cpp` the JACK host (libjack loaded at run time); `audio.h` blocks and the reblocker; `controls.*`, `lights.*`, `display.*`, `font.*`, `latency.h`, `tools.*` |
| `src/ui/` | the interface kit for the Rail design (docs/UI.md), and its mockups (`--ui-mockups DIR`) |
| `src/app/` | Melodee on the Move: `melodee_app.*` the app, `kit_player.*` Melodee's SamplerEngine on the audio thread, `previews.*` its screens offline |
| `src/spike/` | the spike app |
| `module/` | the Schwung standalone tool: `module.json`, `standalone` |
| `tests/` | unit tests, and the device host against the simulated Move |
| `tools/` | build image, packaging, deploy and probe scripts |

## Build

Needs Docker and Python 3.

```sh
./build.sh
```

It builds `build/melodee-move` for linux/arm64 on Ubuntu 22.04: glibc 2.35 is
the newest Ableton's firmware has, libstdc++ is linked in, and the code is
tuned for the Move's Cortex-A72. Then: the unit tests, the sound self-test,
the screen previews, a run against the simulated Move (pad, jogwheel to
LATENCY, the meter timing clicks through a 300-frame cable, Shift + Back),
and the packages:

- `dist/melodee-move/`: the binary, the libraries Ableton's firmware lacks,
  a launcher and `run-stock.sh`;
- `dist/schwung-tool/melodee-move/`: the same as a Schwung standalone tool.

When `../melodee-sample/build/melodee-preview` exists (or `MELODEE_PREVIEWS`
names another folder) Melodee's pages are crushed to 128 x 64 in
`build/crushed/`.

## Run on the Move

**From Schwung's Tools menu** (Schwung installed, root SSH to the Move):

```sh
tools/install-schwung-tool.sh move.local --previews ../melodee-sample/build/melodee-preview
```

Restart Move, then Tools > Melodee Spike. Schwung stops Move, runs the
spike and starts Move again when Shift + Back ends it. Output goes to
`melodee-move.log` in the tool's folder. Schwung runs tools as the `ableton`
user, which may not take realtime priority (`realtime=no` in the log); run
over SSH for realtime. `--remove` takes the tool out again.

**Over SSH, as root**:

```sh
tools/deploy-stock.sh move.local --previews ../melodee-sample/build/melodee-preview
```

It copies the package to `/data/UserData/melodee-move` and runs it in your
terminal: Ableton's app is stopped as the Move's own shutdown stops it, the
spike runs with realtime priority, and Ableton's app comes back however it
ends (Shift + Back, Ctrl-C, the connection dropping, a crash; a reboot
always comes back to stock). As with any quit of Move, save your Set first.

**On poundhard-armbian**: `tools/deploy.sh move.local` (the JACK host; the
launcher menu is stopped while it runs).

On the Move: pads play sines, C3 upward, pressure adds the octave. Play runs
a 16-step click, steps toggle it, knob 1 is the tempo, knob 2 the overtone,
the master knob the volume. The jogwheel turns the pages: HOME, INPUTS
(every control names itself), LATENCY (jog push starts the clicks), then
Melodee's pages crushed to the screen (jog push: letterbox or stretch; hold
Shift for the name).

What to check, in order:

1. Sound from the speaker and headphones; `late=0` in the status line.
2. INPUTS names every pad, step, knob, touch and button correctly.
3. Pads, steps, Play, the row buttons, Shift and Back light as described.
4. LATENCY: the round trip, and `pad_us`.
5. The screen keeps up while playing.
6. Pad pressure arrives (`PRESS` lines on INPUTS).

## Probing a Move

`tools/probe-stock.sh move.local` reads, and changes nothing: the kernel,
glibc, free space, libraries, who holds `/dev/ablspi0.0`, how Ableton's app
starts, Schwung and RNBO, whether the kernel could make the Move a USB sound
card. The report stays in `.local/probe/`.

## Tested so far

- Unit tests: control decoding, lights and palette, the screen format, the
  crush, the reblocker, the queues, the device protocol (MIDI both ways,
  audio with dither, the screen's pull handshake), the latency meter, and the
  device host against the simulated Move (lights out on channel 1, every
  screen chunk answered, a blank screen on the way out, the pad sounding).
- `./build.sh`'s simulated run: the meter timed 8 of 8 clicks at exactly 428
  frames (300 of cable plus the block a render waits for the next exchange);
  a pad went from read to rendered in 37 µs; the cycle's own work took 33 µs
  of 2902.
- The package runs on a bare Ubuntu 22.04 (Ableton's glibc) with only its
  own libraries.
- The JACK host against a dummy JACK server, earlier; the scripts' signal
  handling in a pseudo-terminal.

Not tested: anything on a Move.

## What the crush shows

Melodee's pages do not survive the trip: at 128 x 64 one-bit the small text,
waveforms and colour cues vanish, and only large numbers stay readable
(`build/crushed/chop.stretch.png`). The engine moves across; the pages have
to be designed again for this screen.

## Next

- The latency meter on a Move (LATENCY page, jogwheel push), against
  Ableton's firmware and Schwung.
- Melodee's engine on move-base, the sampler and Chop first.
- The interface: the Rail design ([docs/UI.md](docs/UI.md)), Chop first.
- Loading Schwung's modules as instruments and effects.

## Licence

AGPL-3.0-only, as Melodee. Third-party parts: [THIRD_PARTY.md](THIRD_PARTY.md).
