# melodee-move

Melodee, the groovebox by Kerem Kiliç (Ellic Studio), on the **Ableton Move**:
its sampler and Chop on the Move's 32 pads, with the Move to itself and the
shortest way from a pad to the speaker.

Three parts so far:

- **move-base**, the runtime that hosts an app on the Move as
  [sample-base](../melodee-sample/base) hosts Melodee on the MPC Sample. It
  runs on a Move on Ableton's firmware (realtime, 1 late block in 98,000).
- **Melodee** (`src/app/`) in the Rail design ([docs/UI.md](docs/UI.md)):
  sixteen tracks on Melodee's own engine host and project, compiled
  unchanged from melodee-sample: kits of 32 pads on Melodee's sampler and
  Chop, synth tracks playing Schwung's sound generators, keygroup, MIDI and
  audio tracks; Play, tempo and metronome; the whole project in Melodee's
  `.mldp`. It runs on a Move. What is left of Melodee's features, and who
  takes it on, is [docs/PARITY.md](docs/PARITY.md).
- The **spike** (`--app spike`), which exercises every path through the
  runtime, with a latency meter.

## Melodee

- **Sixteen tracks**, as in Melodee, on its own `EngineHost` (sixteen
  engines and mix strips, buses, master, the sequencer's clock, the
  metronome) and `Project`. The track buttons are the four tracks of the
  selected track's bank (1-4, 5-8, 9-12, 13-16), in their colours; hold one
  to see all sixteen and pick one on the steps. With a track button held,
  an empty step opens the new-track sheet (kit, synth, keygroup, MIDI,
  audio), Copy + a step and another copies a track, Delete + a step deletes
  one. A new project has a kit on track 1 and a synth on track 2.
- **Transport and tempo**, on the Move's own keys (Ableton's manual):
  Play plays and stops the patterns (the steps follow the playhead); Shift
  + Step 5 the tempo (the jogwheel, Shift: tenths; push confirms), Shift +
  Step 6 the metronome on or off (the jogwheel its level), Shift + Step 7
  the groove. A track button held with the Volume knob sets that track's
  volume; Mute mutes the selected track, Mute + a track button that one,
  Shift + Mute + a track button solos it. Editing patterns comes next
  (docs/PARITY.md, sequencer); a project from the MPC plays its patterns.
- **Looper** (Shift + Step 12; docs/UI.md, "Looper"): Melodee's four
  tempo-synced loops, L1-L4, each on a loop track of its own. A pad row is
  a loop (L1 on top: record / overdub, play / stop, undo, held to clear),
  the right 16 keep playing the selected track; the track buttons pick,
  mute and solo the loops; Rec records the selected one; knobs 1-8 its
  source, length, level, feedback, pan, send, quantize and after take;
  Shift + the wheel the four strips; the wheel's push its sheet (play / stop,
  commit to a kit pad, clear, off, IN DELAY: the input's round trip
  measured through a cable). Loops play on the beat at any tempo (a
  stretch), are fitted to a new one before an overdub, and are kept with
  the project as `Projects/Move Loops/Loop N.wav`.
- **Play** (a kit track): the last pad's sample. Knobs 1-8: tune, level, pan, start, end,
  attack, decay, cutoff (Shift: finer). Touch a knob to see it large.
- **Load** (the wheel's push on the pad, the Move's Device View): WAV,
  AIFF, FLAC, Ogg and MP3 files onto the last pad,
  read by Melodee's own `read_audio`. It opens on a list of places: My
  Samples, Recordings, Ableton's Core Library (`/data/CoreLibrary/Samples`)
  and all of `/data/UserData` (or only `--samples DIR`). The jogwheel picks,
  its push opens a place or folder or loads, Back goes up, then back to the
  places.
- **Chop** (Shift + the wheel's push on the pad: Sample Options, Chop, where
  the Move has Slice): the last pad's sample, cut by Melodee's Auto, Beats,
  Regions or Threshold (knob 3 the type, knob 4 or the jogwheel the amount).
  The pads play the slices, the steps show where the cuts fall, the pads or
  Left / Right pick a slice, knobs 1 and 2 move its start and end. Push the
  jogwheel for **To Pads** (Poly, Mono, Thru), onto the first empty pads.
  Back leaves Chop without changing the pads.
- **Synth** (a synth track, track 2 of a new project): one of Schwung's sound
  generators, the ones the Move's Schwung install has
  (`/data/UserData/schwung/modules/sound_generators`, or `--modules DIR`).
  The jogwheel walks the Device View (the module, its preset, its effects);
  its push on the module lists them, and a push loads one. Pads are notes
  in rows of fourths, C2 at the bottom left; Plus and Minus move an octave (a drum module
  such as 9w9 wants notes from 36: one octave down); pressure is polyphonic
  aftertouch. Knobs 1-8 are the module's page (from its `ui_hierarchy`, or
  its parameters eight at a time), Shift + the jogwheel its pages; the
  jogwheel's push on the preset browses its presets (loaded as the jogwheel
  turns). Shift + push for the module's menu, as its `ui_hierarchy`
  lays it out: turn to a row; push a value to edit it with the jogwheel,
  push a level to open it (the knobs follow to its page), or a list (a bank,
  a soundfont, a table) to choose from; Back goes up, then closes the menu.
- **A module's own pages** (Radio Garden's station browser, Webstream's
  search): the scripts Schwung runs for them (`browser.js`), run here in
  QuickJS, the engine Schwung uses, drawn in Schwung's own font
  (`/data/UserData/schwung/host/font.png`). Such a page is one of the
  module's pages (Shift + the jogwheel), or a row at the end of its menu. Push to
  enter it: the jogwheel and its push are then the page's, Back goes up a
  level and finally out. When a page asks for text the pads are a keyboard
  (letters top-left to bottom-right, space, `-.'&`, backspace on the last
  pad; Back or Delete, as on the Move), the steps `1234567890()/:,!`,
  Shift for capitals; push is done, Back held cancels. A file the page hands over (Webstream's download)
  goes onto the first empty kit pad, to play and chop. An action such as
  play/pause or stop is a button: push its menu row, or turn its knob up.
  What a page keeps for itself (Webstream's provider, where its menus are)
  is saved with the module and comes back on the next start; using a page
  counts as a change, saved three seconds on.
- The master knob is the volume; Shift + Back goes back to Move.
- **Saving**: the whole project is one Melodee project file,
  `/data/UserData/Melodee/Projects/Move.mldp` (`--home` moves the folder),
  saved three seconds after the last change, as Melodee autosaves on the
  MPC, and on the way out; the next start opens it. Melodee on the MPC opens
  it, and the Move opens the MPC's (where the samples' paths lead to the
  same files). Samples are kept by path; one that has gone keeps its pads,
  silent, until it is back. Melodee keeps a sample's chop in the WAV file
  itself; here the samples are the Move's own library, left untouched, so
  each chop goes into the project, in a section of the Move's own (`MVCH`)
  that Melodee's reader skips. A synth track is a PLUGIN track: its entry of
  the project's plug-ins names the module's folder, and the entry's state
  (Melodee's PLUG section) holds the module by id, its preset and each
  parameter's value; on the MPC the track stays named and silent, and a
  module that has gone stays named until it is back. What is the Move's and
  not the project's (the track played last, the metronome) is in
  `/data/UserData/Melodee/Move.settings`. Each file is written whole or not
  at all (a temporary file, flushed, renamed). A project saved by the first
  scope (`Move.mldp` with `Move.mldp.chops` and `Move.mldp.synth` beside it)
  is folded into one file on the first start; the old side files stay as
  `*.migrated`.

Not yet: more than one project, editing patterns, samples over 60 seconds,
Record, Capture, Schwung's effects, mixer pages; docs/PARITY.md has the rest.

### How a Schwung module is hosted

Each synth is its own `dsp.so`, loaded through Schwung's v2 plug-in API
(`src/app/schwung_api.h`, declared from Schwung's `plugin_api_v1.h`), as
Schwung's Signal Chain loads one; [Movy](https://github.com/DimaDake/schwung-movy),
which hosts Schwung's modules in a standalone program too, was the guide
for what a host outside Schwung must provide (`src/app/module_host.*`):

- A complete host table: 44.1 kHz, 128 frames, the device page with each
  block's input where a module reads it, the tempo through `get_bpm()`, and
  no Move tracks to send MIDI to.
- `schwung_move_info()`, exported from the binary: modules look it up in the
  process for the tempo before falling back to what the last Schwung
  session left in shared memory.
- A module is opened, created and asked for its parameters on the service
  thread, then handed to the audio thread, which alone calls it from then
  on (notes in the cycle the pad was read, parameter sets and reads, the
  render). One replaced is handed back and destroyed off the audio thread;
  its library is never closed (modules start threads).
- A source, not an instrument (a module that takes no MIDI, `"midi_in":
  false`: Webstream, Radio Garden), plays on a thread of its own instead,
  every call into it there, rendering twelve blocks (35 ms) ahead; the audio
  thread only takes its next finished block. Webstream holds its render up
  to 22 ms (seen on a Move): at worst a moment of the stream missing
  (`stream_late` in the status line), never a late kit or pad. On a Move,
  webstream before: 132 late exchanges in 15 minutes, its stalls on the
  audio thread; after: 6, all as a stream started, the audio thread's work
  0.2 ms at most.
- SIGPIPE is ignored: a streaming module's broken pipe must not end Melodee.
- A call into a module that holds the audio thread 1 ms or more is logged
  (`slow synth call: surge set preset=12 took 24.1 ms`) and counted in the
  status line (`slow_calls`, `slowest_ms`).

`melodee-move --probe-modules /data/UserData/schwung/modules [PICTURES]`
loads each one beside Ableton's app (the device is not touched), lists its
pages, plays a chord and times the renders; with PICTURES it also starts
each module's own pages and draws them there as PNGs.

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
| `src/app/` | Melodee on the Move: `melodee_app.*` the app (thin: what every screen shares), `studio.*` Melodee's `Project` and `EngineHost` and what the screens share, `screen.*` a screen and the registry, `screens/` one file a screen, `project_store.*` the project file, `schwung_api.h` and `module_host.*` Schwung's modules (behind Melodee's `PluginRunner`), `canvas_page.*` their own pages' scripts in QuickJS, `module_probe.*` trying them, `previews.*` its screens offline |
| `src/spike/` | the spike app |
| `module/` | the Schwung standalone tool: `module.json`, `standalone` |
| `tests/` | unit tests, the device host against the simulated Move, and `fake_module/`, a stand-in Schwung module |
| `tools/` | build image, packaging, deploy and probe scripts |

## Build

Needs Docker and Python 3.

```sh
./build.sh
```

It builds `build/melodee-move` for linux/arm64 on Ubuntu 22.04: glibc 2.35 is
the newest Ableton's firmware has, libstdc++ is linked in, and the code is
tuned for the Move's Cortex-A72. Then: the unit tests, the sound self-test,
the screen previews, runs against the simulated Move (the spike: pad,
jogwheel to LATENCY, the meter timing clicks through a 300-frame cable,
Shift + Back; Melodee: load, chop, To Pads, saved and restored; Play, the
tempo and the metronome; the synth track with the stand-in module, saved
and restored), and the packages:

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
`tools/stop-stock.sh move.local` ends a run from another terminal, as Shift
+ Back would. The SSH session always has a terminal on the Move (`ssh -tt`),
so a run started from a script or a background task ends when that is
stopped, too: without one the Move gets no hang-up and the run carries on.
Run this way, Melodee is root, and so are the Schwung modules it plays: what
they write in `/data/UserData/schwung` during the run (logs, caches) is given
back to the Move's user on the way out, so Schwung can write it again.

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
- Sixteen tracks on Melodee's EngineHost, against the first scope's one
  SamplerEngine, the same Melodee script five times each in the simulated
  Move (Docker on a Mac, not realtime: its `late` counts are the
  container's, the same for both): the cycle's own work 18-23 µs on average
  before, 87-114 µs after; a pad from read to rendered 31-49 µs before,
  52-243 µs after. The engine host takes about 6 µs a block while its
  caches are warm (`build/bench`); the rest is its memory (sixteen tracks'
  buffers, eases and meters each block) coming back cold after each 2.9 ms
  wait. A pad still sounds in the block after the exchange it was read in:
  this is CPU time, not delay. Resident memory 48 MB before, 222 MB after
  (the engine host's buffers for Record, Audio Recall, renders and the
  Looper).
- On a Move (Ableton's firmware 4.1), the same project played by hand (a kit
  of 21 samples, webstream on the synth track), the first scope's build
  then this one: the cycle's own work 9-10 µs on average, 27 at most, before;
  66-69 µs, 191 at most, after (2.4 % and 6.6 % of the 2902 µs block); a pad
  from read to rendered 19 µs, 42 at most, before; 81-110 µs, 193 at most,
  after; 3 late exchanges in 20,690 before, 3 in 31,024 after (225 pads);
  resident memory 85 MB before, 259 MB after, with 1.1 GB left free. The
  first start folded the first scope's project into one file, as
  intended.
- The Looper, simulated: a bar taken from the input through a 300-frame
  cable, with the input delay measured from the sheet (428 frames: the
  cable and the runtime's one block), has its first click at frame 3-4
  (at 413 without it); Melodee's metronome itself clicks on block starts,
  so its beats 2-4 come up to a block early in any take. Fitted from 120 to
  140 BPM (88200 to 75600 frames), overdubbed, undone and redone from the
  pads; kept, restored, committed to a pad with its note. With the Looper
  on, L1 playing from Track 1 and the right 16 hit: the cycle's own work
  32-56 µs on average, against 25-43 with it off in the same busy
  container; a pad from read to rendered 27-61 µs (15-41 off); resident
  memory 389 MB against 219 (its eight 22.5 MB buffers).
- The package runs on a bare Ubuntu 22.04 (Ableton's glibc) with only its
  own libraries.
- The JACK host against a dummy JACK server, earlier; the scripts' signal
  handling in a pseudo-terminal.

On a Move (Ableton's firmware 4.1, Schwung installed):

- The spike: realtime, 1 late exchange in 98,000; a pad read to rendered
  in 55 µs at most.
- Melodee, kit track: 310,000 exchanges (15 minutes), 2 late; the cycle's
  work 6 µs on average, 19 at most; pad to rendered 20 µs, 54 at most; 15
  autosaves, and the next start restored the kit.
- `--probe-modules`, beside Ableton's app: all 11 of Schwung's v2 synths
  load (8w8, 9w9, aphex, chonk, clementine-xt, hush1, obxd, radiogarden,
  surge, webstream, wurl; Surge in 0.3 s with 675 presets, OB-Xd with 128);
  8 sound from a chord at C2 (9w9 wants its drum notes, from 36; radiogarden
  and webstream are streams). Renders 3-320 µs a block on average, not
  realtime and sharing the CPU with Ableton's app. Linein (the older v1
  API) is not loaded yet.

- The synth track played from the pads, the modules' menus and presets,
  by hand: 15 minutes with webstream and others, 10 late exchanges in
  310,000; one call into a module over 1 ms (a webstream render, 1.4 ms).
- `--probe-modules` with pictures: Radio Garden's and Webstream's own pages
  start and draw in QuickJS, in Schwung's font, and follow the jogwheel.

Not yet tested on a Move: typing into a page, a download onto a kit pad.

## What the crush shows

Melodee's pages do not survive the trip: at 128 x 64 one-bit the small text,
waveforms and colour cues vanish, and only large numbers stay readable
(`build/crushed/chop.stretch.png`). The engine moves across; the pages have
to be designed again for this screen.

## Next

- The engine host made lighter, on melodee-sample's branch
  `engine-skip-empty-tracks` (until it is merged, build with
  `MELODEE_SAMPLE` set to a checkout of it): an empty track's buffers are no
  longer cleared every block, and a silent Pad FX punch records its history
  a block at a time. In the simulated Move the cycle's work went from 87-114
  µs to 16-19 µs with the first alone and 14-19 µs with both (three runs
  each); not measured on a Move yet.
- The latency meter on a Move (LATENCY page, jogwheel push), against
  Ableton's firmware and Schwung.
- The Looper on a Move: memory (`free -m`) with it on; IN DELAY with a
  cable from the headphone output into the line in (the number it keeps,
  against the spike's LATENCY page); a take from the line in played back
  against the metronome; L1-L4 together from tracks, the mix and the input
  while sixteen tracks play (`process_us`, `late`).
- Wave 2, from docs/PARITY.md: sequencer, sampling, kit depth, instruments,
  mixer & effects, song & export, looper, MIDI & sync & undo & projects.

## Licence

AGPL-3.0-only, as Melodee. Third-party parts: [THIRD_PARTY.md](THIRD_PARTY.md).
