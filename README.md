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
  Shift + Mute + a track button solos it.
- **Pads**, in either of two layouts (Setup > PADS > LAYOUT, Setup on Shift
  + Step 2): the Move's rows of eight, 1-8 the bottom row, or two MPC 4x4 banks
  side by side, 1-16 the left bank from its bottom left and 17-32 the right.
  Every pad job follows the one chosen: the kit's pads, Chop's slices, the
  notes (in the banks 1-32 up the semitones, as the MPC's pads run up a
  scale), the typing keyboard and the lights. The choice is the Move's, kept
  in `Move.settings` ([docs/UI.md](docs/UI.md), "Pads").
- **The sequencer** (docs/UI.md, Sequencer), on the Move's own keys:
  the steps are the pattern's bar (a tap a note, a step held its velocity,
  length, transpose, nudge, a knob a lock, push Melodee's TRIG: chance,
  conditions, ratchets, accent, slide, tie); Loop is Loop Mode (the bars,
  the loop); Note/Session the Patterns grid (launch, chains, copy, delete,
  sequences); Rec Seq Record with its count-in and a free length; Capture
  Melodee's Recall; Shift + Step 3 Workflow, 11 Repeat, 14 a new pattern,
  15 double, 16 quantize; Loop held Fill; Shift + Loop Melodee's SEQ pages
  (Pattern, step entry, Roll, Motion, MOD, macros, feel, follow actions,
  lanes and Euclid). A project from the MPC plays and edits its patterns.
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
- **Kits at Melodee's depth** (docs/UI.md, "Kits"): 128 pads in eight banks,
  the 32 pads two of them, Plus and Minus moving 16; Shift + the jogwheel
  the nine pad pages (TRIM, LAYERS, TUNE with Warp and Follow, AMP ENV,
  PLAY, CLOUD, MIX with kit volume and COLOUR, FILTER, KIT FILT); Shift +
  Step 8 16 Pitches of the last pad in the project's key; Copy + a pad and
  another copies it, Delete + a pad clears its notes, its sound or its gap;
  Sample Options: Chop, Reverse, Trim Sample, Normalize, Save Sample; bank
  files and MPC drum programs from the browser; samples over ten seconds
  (up to six hours) played from storage, long MP3, FLAC, Ogg and AIFF
  decoded once into `Melodee/Cache`; a load memory has no room for refused
  (`MEMORY FULL`).
- **Load** (the wheel's push on the pad, the Move's Device View): WAV,
  AIFF, FLAC, Ogg and MP3 files onto the last pad,
  read by Melodee's own `read_audio`. It opens on a list of places: My
  Samples, Recordings, Ableton's Core Library (`/data/CoreLibrary/Samples`)
  and all of `/data/UserData` (or only `--samples DIR`); STARRED and RECENT
  lead them, and MELODEE (its takes, edits, expansions). The jogwheel picks,
  and each file plays as it lands (PREVIEW); its push opens a place or folder,
  loads, or installs an MPC expansion (`.xpn`, into `Samples/Expansions`);
  Shift + push stars a file; Delete asks, then deletes (in Melodee's folder
  and the Move's User Library, never the Core Library or what the project
  plays); a pad picks the pad it loads onto. Back goes up, then back to the
  places.
- **Chop** (Shift + the wheel's push on the pad: Sample Options, Chop, where
  the Move has Slice): the last pad's sample, cut by Melodee's Auto, Beats,
  Regions or Threshold (knob 3 the type, knob 4 or the jogwheel the amount).
  The pads play the slices, the steps show where the cuts fall, the pads or
  Left / Right pick a slice, knobs 1 and 2 move its start and end. Push the
  jogwheel for **To Pads** (Poly, Mono, Thru), onto the first empty pads.
  Back leaves Chop without changing the pads. Past the first scope: Manual
  (knob 3; the first pad plays, the red pad cuts), slices 33-64 on Plus and
  Minus, knobs 1 and 2 close in on the cut and a moved cut plays its slice,
  Shift + the wheel from hit to hit, Delete + a pad takes its cut out, a step
  plays from its sixteenth, Shift + push the slice options (Extract, Split,
  Merge, Remove all cuts), To Pads KIT (a pad a sound) and To Pads PATTERN
  (its last row: a note a slice into the next empty pattern). Sample Options
  also has TRIM (the region a file of its own) and SAVE SAMPLE (named on the
  pad keyboard).
- **Sampling** (the Sampling button, on a kit; docs/UI.md "Sampling"):
  Melodee's Sample Record. A pad records onto itself, at once or from
  START's threshold, or one pass of the pattern with LENGTH SEQ; the pad
  again, Sampling, the push or Play stopping ends it; held, it records until
  let go; another pad carries the take on onto that pad. The wheel walks
  SOURCE, GAIN, MONITOR, START, LENGTH; knobs 1-6 set them; Shift + the
  wheel the SAMPLING settings. The Move's one input is the mic, or the line
  in once a cable is in (CC 114): SOURCE offers what the jack has, then
  RESAMPLE (Melodee's mix) and each TRACK, whose sound the pads then play.
  The mic heard through the speaker turns Monitor off (CC 115). Takes are
  written as they are recorded into `Samples/Recorded` (`TAKE n`,
  `RESAMPLE n`), then onto their pads. **Audio Recall** is Shift + Sampling:
  the input's last 25 s onto the next empty pad (`RECALL n`). A new track's
  KIT asks where its first sound comes from: LIBRARY, EMPTY, INPUT.
- **Audio tracks**: Sampling opens the track's INPUT (source, monitor, gain);
  Rec arms it, and Play records one pass of its pattern into
  `<project> Audio/Take NNN.wav`, which the pattern then plays. Its Play
  screen has the take's CLIP (knobs 1-6: level, start, end, fades, reverse)
  and, on Shift + the wheel, its WARP (warp, BPM, semitones, cents).
- **Synth** (a synth track, track 2 of a new project): one of Schwung's sound
  generators, the ones the Move's Schwung install has
  (`/data/UserData/schwung/modules/sound_generators`, or `--modules DIR`).
  The jogwheel walks the Device View (the module, its preset, its effects);
  its push on the module lists them, and a push loads one. Pads are notes
  as Keys & Scale lays them out (In-Key in octaves at first, C2 at the bottom
  left; in MPC banks, 1-32 up the scale; Instruments, below); Plus and Minus
  move an octave (a drum module such as 9w9 takes Drums, notes from 36);
  pressure is polyphonic
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
- **Effects and the mix** (docs/UI.md, "Mixer and effects"; mockups in
  `docs/ui/mixer.png`): Melodee's 41 effects and Schwung's audio effects
  (`/data/UserData/schwung/modules/audio_fx`, loaded as Schwung's Signal
  Chain loads them: `<id>/<id>.so` through its audio_fx v2 API, played in
  place on the audio thread) in Melodee's chains, six on each track, four
  on each kit pad, six on each of the buses A-D and on the master before
  its compressor. On Play the wheel walks them as the Move walks a Track
  Preset's devices, the knobs following; push the browser, Shift + push the
  effect's options; Mute, Delete or Copy held + push bypasses, removes or
  copies it. The track item's knobs are its level, pan and four sends.
  **Shift + Step 4** opens the Mixer (eight tracks' levels, pans or sends on
  the knobs, Left / Right the other eight, the steps the sixteen tracks,
  MAIN the returns and the compressor; the push a bus's or the master's
  chain); again Knob FX, Pad FX (the pads as punches, from the audio
  thread), Flex Beat. Mute held + a kit pad mutes it, Shift + Mute alone
  lets every mute go; touching the Volume knob shows the main meter.
- **Song** (Shift + Step 13; Melodee's Song, MELODEE.md › Sequencing,
  Song): a lane per track of the bank in view, clips of patterns, markers,
  the loop brace and the tempo lane, kept in the project. The pads are the
  four lanes over eight bars, the steps sixteen bars (a step held and
  another: the loop); the wheel moves the cursor, its push places a clip or
  opens a clip's jobs (open notes, duplicate, split, own pattern, repeat to
  the marker, loop, mute, delete), Shift + push a marker; Copy, Delete and
  Loop act on the clip; Plus / Minus the bank, Shift + Left / Right the
  markers; knobs the pattern, length, zoom, the bar's tempo and its shape.
  Play plays the song from the cursor, Rec writes the patterns launched into
  it. **Sampling on Song exports** (the Move's Resampling records its whole
  output): a mixdown (32-bit float, or 24 or 16-bit dithered) or stems, the
  song played once through in real time; a MIDI file; an MPC 3 project
  (synth tracks as their prints, made first); an Ableton Live Set, to go on
  with the project in Live 12.1 or later (each pattern a Session View clip,
  the song laid out in Arrangement View, kits as Drum Racks of Simplers,
  synth tracks as their notes and their prints, levels, sends and tempo;
  not the effects); the song into a new pattern. They go under
  `/data/UserData/Melodee`: `Mixdowns/`, `Stems/<name>/`, `MIDI/`,
  `MPC 3/`, as Melodee writes them on the MPC's card, and
  `Ableton/<name> Project/`, which the web page downloads as a zip (any
  folder's download button). A `.mid` in the samples browser loads its notes
  onto the track's pattern.
- **Print and freeze** (a track button held + Sampling: PRINT): a kit or
  synth track's patterns printed by a print host of Melodee's (its lean
  engine host and a second instance of the Schwung module) on a thread at
  the lowest priority on the last core, faster than real time, into
  `<project> Prints/`; frozen, the track plays its prints instead of its
  engine (pads still live, level, pan and mute live), switching on a bar
  line. Deep freeze lets the module go too; unfreezing is at once, the
  prints kept, a deep-frozen track thawing first. A frozen track shows a
  snowflake; an edit on it asks first. The prints and freezes are kept in
  the project (Melodee's PRNT, FRZN), so it opens frozen, on the MPC too.
- **Instruments** (docs/UI.md, "Instruments"; `docs/ui/instruments.png`): a
  keygroup track plays an SFZ, an MPC keygroup program (`.xpm`, MPC 3's
  `.xty`), an EXS24 instrument, a SoundFont preset (an `.sf2` opens as a
  folder of its presets), a DecentSampler preset or one sample, played from
  the note found in it; read by Melodee's own readers, its zones streamed
  from storage as Melodee streams them (a file that is not a PCM WAV is laid
  out in `Melodee/Cache` and read in place too). Its pages (Shift + the
  wheel): ZONES or TRIM, PLAY, AMP ENV, FILTER, ARP, CHORD. Every note track
  plays as the Move's **Keys & Scale** (Shift + Step 9) lays its pads out:
  In-Key or Chromatic, in octaves or 4ths, the project's key and the Move's
  35 scales, with Melodee's Prog (MPC 3 progressions, a chord a pad, chords
  stored from the pads held as Mine N) and Drums (notes from 36); CHORD
  plays a chord of the key a pad, ARP arpeggiates, Shift + Step 10 is Full
  Velocity, Shift + Plus / Minus a note of the key. A module track opens on
  **MACRO** (Melodee's FILTER, ECHO and SPACE, the mod wheel, pitch bend).
  **AUTO SAMPLE** (the last row of a module's menu) plays its own instance
  of the module a key at a time, faster than real time at the lowest
  priority, after Melodee's check, into an MPC keygroup program
  (`Melodee/Samples/Keygroups/<name>/<name>.xpm`) on the first empty track,
  with the module track's patterns. The pads reach the audio thread as one
  published table a track (`app/keys.h`, a triple buffer): a chord's notes,
  the arp's keys or Note Repeat's, in the cycle the pad was read.
- The master knob is the volume; Shift + Back goes back to Move.
- **The web page** (`src/web/`), while Melodee runs:
  **http://move.local:8080** (Ableton's own page keeps port 80, Schwung's
  manager 7700 and its mirror 7681). **Screen** mirrors the Move's screen
  live; **Files** is Melodee's folder (`/data/UserData/Melodee`): play,
  download, upload, rename, delete, new folders, nothing outside it;
  **Guide** is [docs/GUIDE.md](docs/GUIDE.md), the Move's controls. A
  browser on the USB cable opens it at once. One on Wi-Fi asks for a code:
  the Move shows six digits for 90 seconds, the browser types them and is
  paired until Melodee stops; Back on the Move, time running out or five
  wrong tries refuse (a browser refused waits five minutes to ask again).
  Shift + Capture shows the page's addresses. Load lists
  Melodee's folder as a place (MELODEE), so a sample uploaded there loads
  onto a pad. The page runs on threads of its own, never the audio thread;
  `--web-port` moves it, `--no-web` leaves it off.
- **Projects** (Shift + Step 1, the Move's Set Overview): a folder's
  projects on the 32 pads, each keeping its pad, folders grey, the one
  picked pulsing. A pad picks; Back or a track button opens it (the sound
  fades out, the audio thread waits while it is built, a Loading sheet
  counts its files, the sound comes back); an empty pad, then Back, makes a
  new one there; Play opens and plays it. The wheel's push (or Shift + a
  pad) has its options: Save (the sample slots nothing plays freed), Save
  as new, Rename, Colour, Duplicate, Move to folder, New folder, Open at
  start; on another, Open, Load kit (its first kit onto the selected
  track), Delete. Copy + a pad, then a pad copies one; Delete + a pad asks,
  the pad again deletes it. MPC projects (`.xpj`) open as a copy. A mark in
  the rail's corner says the open project has changes not saved yet.
- **Undo** (Undo; Shift + Undo redoes; held, the undo list): the last 32
  steps of anything in the project, each named on the screen (`UNDO TUNE
  A05`). A press is a step, a knob's turns are one (until another control
  or two seconds still), a load is one; mutes, the octave and the pad
  played are not. docs/MOVE_CONTROLS.md, "Undo steps", says how a screen
  marks one.
- **Setup** (Shift + Step 2) is Melodee's Settings: the pads' response and
  pressure, pad lights, pad fade, the lights' brightness, a light show in
  time with the tempo, a usage log (`Usage/`, what the hands
  do, no names), About, and Back to Move. **Shift + a track button** is the
  track's options: its colour and name. Names are typed the Move's way: the
  wheel walks the characters, its push types one, OK at the end of the
  strip; the pads type letters at once.
- Before anything is lost (a track deleted or replaced, a project deleted,
  a synth's module replaced) one question sheet asks, saying what goes and
  whether undo brings it back.
- The master knob is the volume; Shift + Back goes back to Move.
- **Saving**: the whole project is one Melodee project file in
  `/data/UserData/Melodee/Projects` (`--home` moves the folder; the first
  one is `Move.mldp`), saved three seconds after the last change, as
  Melodee autosaves on the MPC, and on the way out; the next start opens
  the one set to open at start, else the one open last. Melodee on the MPC opens
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
  not the project's (the track played last, the metronome, the pad
  layout) is in `/data/UserData/Melodee/Move.settings`. Each file is written whole or not
  at all (a temporary file, flushed, renamed). A project saved by the first
  scope (`Move.mldp` with `Move.mldp.chops` and `Move.mldp.synth` beside it)
  is folded into one file on the first start; the old side files stay as
  `*.migrated`.

- **MIDI and sync** ([docs/MIDI.md](docs/MIDI.md)): the Move gives Melodee
  one MIDI port, USB-MIDI cable 2 of its device page, where a class-compliant
  device on USB-A and a computer on USB-C (the "Ableton Move Standalone
  Port") both arrive, in and out. MIDI in plays the selected track, or a
  track by its own channel (Shift + a track button: MIDI IN, MIDI OUT), and
  Rec writes it as it writes the pads (the SEQ pages' ENTRY at the caret); MIDI
  Config (Setup, Shift + Step 2) has the channels, pad MIDI in and out, MIDI
  clock and MTC in and out, Thru, program change, the Remote CCs and Panic.
  A MIDI track plays outside gear: its pages (DEVICE, PROGRAM, three of CC
  slots, EXPRESS, PADS, AUDIO, SYSEX) on the knobs, Shift + the wheel
  walking them, its Sounds (`.mlds`) on the wheel's push. MIDI goes out with
  the exchange that carries the audio it is heard in (a block, 2.9 ms);
  `melodee-move --ctl 'melodee-midi 90 3c 64'` puts a message in,
  `--ctl 'melodee-midi panic'` Panics (Remote, below).


Not yet: Record, locks and Motion on a Schwung module's own parameters, auto sampling an
instrument outside (MIDI out and the line in at once); docs/PARITY.md has the rest.
Song's own: typed marker names, punches written into the song (with Pad
FX), freezing from the patterns grid (with Session).

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

### Remote

`melodee-move --ctl 'COMMAND'` (over SSH, on the Move) talks to the running
Melodee through its socket, `/data/UserData/Melodee/.melodee-move.sock`
(`--home DIR` or `--control SOCKET` for another), as `device.py ctl` does
on the MPC:

- `status`: the status line, now;
- `melodee-press SPEC`: presses played into the Move's input as if a hand
  made them (they reach the pads' audio path and the controls alike):
  `play`, `shift+step1`, `delete+pad5`, `track1+copy+step3`, `wheel+2`,
  `knob1-3`, `volume+5`, `hold-shift` / `release-shift`, `long-undo`;
  buttons are `play rec shift back note-session capture sampling loop mute
  undo copy delete plus minus left right wheel-push track1`..`track4`, pads
  `pad1`..`pad32` (bottom left first), steps `step1`..`step16`;
- `melodee-render serial|parallel`: the render workers left asleep or used;
- `melodee-midi HEX...` / `melodee-midi panic`: a message into MIDI in as if
  it came on the USB port (`90 3c 64`), or MIDI Config's Panic (docs/MIDI.md).

The runtime (`src/move/remote.*`) owns the socket; an app answers its own
commands through `App::command()`.

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
| `src/move/` | move-base. `app.h` the `App` interface (sample-base's hooks plus `audio_event`); `runtime.*` the threads; `host.*` what a host shares with them; `spi_host.cpp`, `spi_device.*`, `spi_protocol.*` the Move's device and a simulated one; `jack_host.cpp` the JACK host (libjack loaded at run time); `audio.h` blocks and the reblocker; `controls.*`, `lights.*`, `display.*`, `font.*`, `latency.h`, `tools.*`; `realtime.*` the helper asked for realtime, `speech.*` and `schwung_tts.h` Schwung's speech |
| `src/ui/` | the interface kit for the Rail design (docs/UI.md), and its mockups (`--ui-mockups DIR`) |
| `src/web/` | Melodee's web page: `http.*` the server, `pairing.*` who may use it (the USB cable, or a code), `web_page.*` its answers and Melodee's folder, `web_layer.*` the page around the app (the screen's frames, its screens on the Move, Shift + Capture), `screens.*` those screens, `web_bundle.*` and `static/` the page itself |
| `src/app/` | Melodee on the Move: `melodee_app.*` the app (thin: what every screen shares), `studio.*` Melodee's `Project` and `EngineHost` and what the screens share, `sequencer.*` and `seq_edit.*` the sequencer, `screen.*` a screen and the registry, `screens/` one file a screen, `project_store.*` the project file, `schwung_api.h` and `module_host.*` Schwung's modules (behind Melodee's `PluginRunner`), `canvas_page.*` their own pages' scripts in QuickJS, `module_probe.*` trying them, `previews.*` its screens offline |
| `src/spike/` | the spike app |
| `src/boot/` | `unblock_signals.c`: `module/boot-entry.sh` starts again through it, since MoveLauncher hands its children INT, USR1 and TERM blocked; `heal.c`: the tool's privileged helper, SCHED_FIFO for melodee-move's audio thread ("Realtime there too") |
| `module/` | the Schwung standalone tool: `module.json`, `standalone`, `boot-entry.sh` (Melodee as a boot target of Schwung's boot selector), and `help.json`, its topics in Schwung's Help viewer |
| `tests/` | unit tests, the device host against the simulated Move, the sequencer (`sequencer_tests.cpp`), `song_tests.cpp` (Song, exports, print and freeze, against the whole app), and `fake_module/`, a stand-in Schwung module |
| `tools/` | build image, packaging, deploy and probe scripts |

## Build

Needs Docker and Python 3.

```sh
./build.sh
```

It builds `build/melodee-move` for linux/arm64 on Ubuntu 22.04 with its GCC
12: glibc 2.35 is the newest Ableton's firmware has, libstdc++ is linked in,
and the code is tuned for the Move's Cortex-A72. Then: the unit tests (and
the web page's,
`build/web-tests`), the sound self-test,
the screen previews, runs against the simulated Move (the spike: pad,
jogwheel to LATENCY, the meter timing clicks through a 300-frame cable,
Shift + Back; Melodee: load, chop, To Pads, saved and restored; Play, the
tempo and the metronome; the pad layout set to MPC banks and kept; the
synth track with the stand-in module, saved and restored; the web page,
driven by `tests/web_check.py` as a browser on the Move and one as on
Wi-Fi, which pairs with the code it reads off the mirrored screen), and
the packages:

- `dist/melodee-move/`: the binary, the libraries Ableton's firmware lacks,
  a launcher and `run-stock.sh`;
- `dist/schwung-tool/melodee-move/`: the same as a Schwung standalone tool,
  with the licences of all it carries (`licenses/`) and `SOURCE.txt`, the
  commits it was built from;
- `dist/melodee-move-module.tar.gz`: that tool as Schwung's module catalog
  downloads it, its programs stripped, checked by `tools/check_package.py`
  ([Release](#release)).

When `../melodee-sample/build/melodee-preview` exists (or `MELODEE_PREVIEWS`
names another folder) Melodee's pages are crushed to 128 x 64 in
`build/crushed/`.

`./build.sh quick` is the build and the unit tests alone, for while you
work; the whole `./build.sh` before a merge to main. `tools/check.sh` (run
in the image) does the work: `make` on every core, the tests and runs side
by side where they share no folder (each lane's output in
`build/check/`). Compiles go through ccache, its cache shared by every
checkout and worktree on the machine (`~/.cache/melodee-move/ccache`, or
`MELODEE_CCACHE`), so a source compiled in one is not compiled again in
another. JUCE's headers are precompiled (`src/pch.h`). The compiler is
22.04's GCC 12, not its default GCC 11 (`tools/Dockerfile`): GCC 11 spends
minutes at -O2 wherever Melodee's `Project` is made or its patterns reset,
and a clean build takes 53 s instead of 226 s.

## Release

Schwung's module catalog (Schwung's `docs/MODULES.md`, "Publishing to the
Module Catalog") lists a module by an entry in its `module-catalog.json`
naming this repository and `melodee-move-module.tar.gz`. Schwung Manager
reads `release.json` from this repository's main branch for the version and
where to download it, unpacks the asset into
`/data/UserData/schwung/modules/tools/melodee-move` (a merge, file by file:
the log stays), and registers `module.json`'s boot target, never as the
default. A release:

1. `module/module.json`'s version, the same in `release.json` (its
   `download_url` the release's asset), and its section in `CHANGELOG.md`;
   committed.
2. `tools/release.sh` builds it from that commit, never from the working
   tree: the commit exported into `build/release/vX.Y.Z/src`, the full check
   run there against melodee-sample exported the same way at a commit on
   GitHub (`MELODEE_SAMPLE_REF`, by default `origin/main`) with the
   sample-base it names (they are this build's source too), whatever the
   checkout beside this one is on. It leaves the asset, its
   SHA-256, `release.json` and the release's notes in `build/release/vX.Y.Z/`,
   and prints the tag and upload it would make.
3. The tag `vX.Y.Z` on that commit, pushed with main, and a GitHub release
   of it carrying the asset.

The catalog's entry is made once; a new version needs only steps 1-3.

## Run on the Move

**From Schwung's Tools menu** (Schwung installed, root SSH to the Move):

```sh
tools/install-schwung-tool.sh move.local
```

Then Tools > Melodee (Shift + Volume + Step 13). Schwung stops Move, runs Melodee
and starts Move again when Shift + Back ends it. Output goes to
`melodee-move.log` in the tool's folder. `--remove` takes the tool out again.

**Realtime there too.** Schwung runs tools as the `ableton` user, which may
not take realtime priority itself. Melodee has its own privileged helper,
the one Schwung lets a standalone tool have (Schwung's `docs/MODULES.md`,
"a privileged helper of their own"): `src/boot/heal.c`, shipped as
`bin/realtime-helper`. `module/standalone` stages it as `bin/heal.new` and
runs Schwung's `schwung-heal`, which installs it as `bin/heal`, root-owned
04755; it does so again whenever `bin/heal` is not that helper, root's and
setuid (an update, or an install that gave the folder back to `ableton`).
When SCHED_FIFO is refused, the audio thread runs `bin/heal` with its own
thread id (`src/move/realtime.cpp`), and the helper does one thing: SCHED_FIFO
70, Move's own audio priority, for that thread, after checking that its
parent is the `bin/melodee-move` beside it and that the thread is that
parent's. The log says `realtime by the tool's helper: ...` and the status
line `realtime=yes`.

**The screen reader.** With Schwung's screen reader on (Global Settings,
Screen Reader), Melodee speaks through Schwung's own speech, in the user's
voice, speed and volume: it starts Schwung's `schwung-tts` sidecar as its
child (the shim, which starts it under Move, is not running while a
standalone tool has the Move), writes requests into the segment Schwung's
`src/host/tts_shm.h` describes, and mixes the speech into its output on the
audio thread (`src/move/speech.*`). What it says (`src/app/screen_reader.*`),
once things have been still for 300 ms: at the start "Melodee", the view,
the track and how to quit (Shift and Back); a new view with its track; a new
track; a touched or turned knob's name and value; the message line. With
the screen reader off it says nothing.

**At power-on** (Schwung 1.9 or later, which has a boot selector): the
install registers Melodee as a boot target (`/data/UserData/boot-targets/melodee-move`),
one more row in Schwung's choice of what the Move starts in. It never
makes Melodee the start: that stays the user's choice, in Schwung. At
power-on Schwung shows `Loading <name> - press Back to change` for 2 s;
Back opens its picker, and the row chosen there is what the Move starts in
from then on. Schwung Manager's Boot page (http://move.local:7700/boot)
sets the same. Started this way Melodee is the `ableton` user, as from the
Tools menu, and realtime by the same helper. Shift + Back, or a crash, goes to Schwung, and
a Move restarted before the next power-on (after a Tools menu run,
Melodee's too) stays Schwung: `module/boot-entry.sh` starts Melodee only
as a power-on's first start of Move (its own mark and Schwung's control
block in `/dev/shm` say otherwise). The 2 s window at each power-on is
always the way back to Schwung or stock Move.

**Over SSH, as root**:

```sh
tools/deploy-stock.sh move.local --previews ../melodee-sample/build/melodee-preview
```

It copies the package to `/data/UserData/melodee-move` and runs it in your
terminal: Ableton's app is stopped as the Move's own shutdown stops it, the
spike runs with realtime priority, and Ableton's app comes back however it
ends (Shift + Back, Ctrl-C, the connection dropping, a crash; a reboot
always comes back to what the Move starts in). As with any quit of Move, save your Set first.
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
- The pad layouts (`build/pad-layout-tests`, `build/pads-app-tests`): the
  map both ways in Rows and Banks, notes for both readings of a scale, the
  typing keyboard; through the app, a kit's pads, Shift + pad, their lights,
  Chop's slices and a synth's notes in both layouts, Setup > PADS and
  `Move.settings`. In the simulated Move, with `Move.settings` in Banks,
  the bottom row's fifth pad is B01, and the next start is still in Banks.
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
- The mix and effects (`build/mixer-tests`): effects heard in a track's,
  a pad's, a bus's and the master's chains, bypass, move, copy and paste,
  KEY, sends and returns, pad mutes and solo, the compressor and its
  sidechain, Knob FX, punches and Flex Beat from the audio thread, KEEP, a
  stand-in Schwung audio effect (`tests/fake_fx`) loaded, heard, set, copied
  with its track, all saved and opened again; and every new key through
  the app. The cycle with busy chains, in the simulated Move (Docker on a
  Mac shared with other builds, not realtime: its peaks are the
  container's): sixteen kit tracks each playing a loop, 69-114 µs a cycle
  on average with no effects; with six effects on every track (two holding
  delay memory), six on every bus (every track sending into all four), six
  on the master, the compressor and Knob FX over every track (126 effects),
  471-849 µs. Melodee's render workers (`MELODEE_RENDER_THREADS=2`) made it
  slower there (784-1516 µs); they stay off until measured on a Move.
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
- The Looper on a Move (2026-11-21, a new project of two tracks, driven
  over `--ctl`): turned on, Melodee went from 247 to 419 MB resident and
  the Move's used memory from 324 to 497 MB of 1849 (1290 available). L1-L4
  recorded two bars each, at once, from the input, the mix, Track 1 and L1,
  and all four played: the cycle's work 115 µs on average, 237-254 at most
  (54/205 with the Looper off), `late=1` throughout (the one at the start).
- Sampling, in the simulated Move: a take of Melodee's own mix (RESAMPLE)
  with the kit played through the pads onto A06, written to
  `Samples/Recorded`, and Audio Recall onto A07 (`./build.sh`); the unit
  tests take the mic and the line by the jack, takes across pads, a pad held,
  START, LENGTH SEQ, the browser's star, recent, preview, Delete and an
  expansion, Trim, Save, Chop's manual take and options, and an audio
  track's take recorded, played and read back. Against the commit before
  (the same Melodee script, five runs each, Docker on a Mac): the cycle's
  own work 18-30 µs on average before, 19-29 after, 18-24 while a take
  records; a pad from read to rendered 27-48 µs before, 22-43 after; no late
  exchanges. Not measured on a Move yet.
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

Not yet tested on a Move: typing into a page, a download onto a kit pad,
the web page (what to check: below).

The web page, in `./build.sh`'s simulated run: the page and its guide; from
the Move itself the status, the screen's stream and Melodee's folder (a
folder made, a sample uploaded, read back whole, renamed, deleted; the
folder itself refused); from the machine's own address, as on Wi-Fi,
everything but the page refused (401), a code asked for, read off the
mirrored screen, one wrong try (4 left), then paired: status, an upload and
the screen let in, and the cookie refused from another address.

### The web page on a Move

1. The log's `web page at http://move.local:8080 (USB ...: open; Wi-Fi ...:
   asks a code)` line names a `usb0` address: the USB network gadget's
   interface is `usb*` on the Move as assumed. `tools/probe-stock.sh` lists
   what listens (8080 free) and the addresses.
2. On the USB cable, http://move.local:8080 (or the USB address that Shift +
   Capture shows) opens with no code. If `move.local` resolves to the Wi-Fi
   address instead, the page asks for a code: use the USB address.
3. On Wi-Fi: the code on the Move, a wrong one, Back refusing, then pairing.
4. The Screen tab follows the Move while you play; `late` stays where it was
   without the page open.
5. Files: upload a WAV into Melodee's folder, then Menu > MELODEE loads it
   onto a pad. Run from Schwung's Tools menu after a run over SSH: files the
   page made as root belong to the Move's user, so they can be renamed.

- Song, its exports, print and freeze (`build/song-tests`, 163 checks):
  the clip jobs, markers and tempo lane; the song played from a bar; Rec
  writing launches; a MIDI file out and back in; the song into a pattern;
  a 16-bit mixdown, stems with the same frames, a stopped export dropped,
  an MPC 3 project with its samples; a kit and a synth track printed by the
  print host, frozen, playing their prints, opened frozen again, unchanged
  prints never made twice, a deep freeze letting the module go and a thaw
  bringing it back; every Song, EXPORT and PRINT control through the
  bindings table. In the simulated Move: Song opened, a clip placed, the
  song played and mixed down into `Mixdowns/Move.wav`. Not yet on a Move.

## What the crush shows

Melodee's pages do not survive the trip: at 128 x 64 one-bit the small text,
waveforms and colour cues vanish, and only large numbers stay readable
(`build/crushed/chop.stretch.png`). The engine moves across; the pages have
to be designed again for this screen.

## Next

- The engine host made lighter still. melodee-sample's
  `engine-skip-empty-tracks`, now in its main, took the first step: an empty
  track's buffers are no longer cleared every block, and a silent Pad FX
  punch records its history a block at a time. In the simulated Move the cycle's work went from 87-114
  µs to 16-19 µs with the first alone and 14-19 µs with both (three runs
  each). On a Move it is less: the engine host alone, timed on the Move's
  own CPU, 53.3 µs a block before and 37.6 after for a kit and a synth
  (45.9 and 29.8 for an empty project); Melodee played by hand, the cycle's
  work 57-59 µs on average, 124-166 at most (66-69 and 148-191 before), a
  pad 59 µs from read to rendered, 9 late exchanges in 41,357 (Webstream
  streaming, its page open), 285 MB resident. What is left is the engine
  host's fixed work a block (the Looper, the input, the mix); melodee-sample
  is the place to lighten it further.
- The latency meter on a Move (LATENCY page, jogwheel push), against
  Ableton's firmware and Schwung, with a cable from the headphone output
  into the line in: the speaker into the microphone heard none of 80
  clicks (2026-11-21, nothing plugged in).
- The Looper on a Move, what is left: IN DELAY with that cable (the number
  it keeps, against the spike's LATENCY page); a take from the line in
  played back against the metronome; L1-L4 together while sixteen tracks
  play (`process_us`, `late`).
- Wave 2, from docs/PARITY.md: sequencer, sampling, kit depth, instruments,
  mixer & effects, song & export, looper, MIDI & sync & undo & projects.

## Licence

AGPL-3.0-only, as Melodee. Third-party parts: [THIRD_PARTY.md](THIRD_PARTY.md).
