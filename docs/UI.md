# Melodee on the Move: the interface

The design is **Rail**, chosen from three directions (Panel, Full Bleed,
Rail) on 2026-11-20. The first scope is Melodee's sampler, Chop, and loading
Schwung's modules as instruments and effects; then sixteen tracks,
transport and tempo ("Tracks", below). The screens are mockups first, drawn
by the UI kit (`src/ui/widgets.*`) at the screen's real size, each set in a
file of its own (`src/ui/mockups*.cpp`); the app's screens are built to
them. `build/melodee-move --ui-mockups DIR` renders them again
(`docs/ui/rail.png`, `docs/ui/tracks.png`); `--melodee-previews DIR` draws
the app's own screens.

![The Rail screens](ui/rail.png)

## Principles

- **Big and focused.** One subject a screen and one number that matters,
  large. A knob's detail shows when it is touched, not all the time.
- **The rail answers "where am I"**, on every screen, in the same place.
- **The pads, steps and lights carry as much as the screen.** The screen
  never repeats what the pads already show.
- **The Move's controls keep Ableton's meaning** (Kerem, 2026-11-20). Every
  key and shortcut does what Ableton's Move manual says it does: Loop is
  Loop Mode, Note/Session switches Play and the Patterns grid, Shift + Step 5
  is Tempo. Melodee's own jobs take the controls the Move leaves free
  (Shift + push for Chop, Shift + Sampling for Audio Recall, Shift + Steps 4,
  12 and 13), as melodee-sample keeps the MPC Sample's printed labels.
  [MOVE_CONTROLS.md](MOVE_CONTROLS.md) is the map and its decisions.
- **Nothing between a pad and its sound.** Pads sound from the audio thread
  in the cycle they were read (`App::audio_event()`); the interface runs
  beside that path, never on it.

## The screen

128 x 64 pixels, one bit, drawn in whole pixels with the 5 x 7 font: scale 1
advances 6 pixels a character, 2 advances 12, 3 advances 18.

```
 0      23 26                                      127
 +--------+------------------------------------------+  0
 | TOP    |                                          |
 |        |              the subject                 |
 |  BIG   |     waveform, list, meter, knob page     |
 |        |                                          |
 |        |                                          | 51
 | BOTTOM | detail, left                detail, right| 56
 +--------+------------------------------------------+ 63
```

**The rail** (x 0-23, white):

| Place | Holds | Examples |
| --- | --- | --- |
| Top | what you are in, up to 4 characters | `KIT`, `PAD`, `SLC`, `TAKE`, `REC`, `CAP`, `LOAD`, `KNOB`, a module's short name (`OBXD`) |
| Big | the one number: up to 2 characters large, or 4 small | the track, the slice, the knob, the take; `A05` small |
| Bottom | context, up to 4 characters | the pad, `/16`, the page (`2/5`), the source (`MIC`) |

**The subject** (x 26-127, y 0-51, 102 x 52) is one of: a waveform, a list,
a meter, a big parameter, or a page of knob cells under a large title.

**The detail line** (y 56) has at most 16 characters in all, left and right.

**A touched knob** takes the subject: its name, its value large, a bar, the
unit under it; the rail says `KNOB` and its number. The screen goes back a
moment after the knob is let go.

**Sheets** (a choice such as TO PADS, NEW TRACK) take the subject; the
jogwheel picks, a push confirms, Back leaves.

## Screens

| Screen | Rail | Subject | Detail |
| --- | --- | --- | --- |
| Play, a kit track | `KIT` / track / pad | the last pad's waveform, start and end | sample name, length |
| Knob touched | `KNOB` / knob / pad or module | name, value, bar | unit |
| Pad pages (5) | `PAD` / pad / page | page title, 8 knob cells | |
| Chop | `SLC` / slice / count | waveform, cuts, the selected slice inverted | chop type, tempo |
| Chop, close up | `SLC` / slice / `STRT` or `END` | the cut zoomed, the hits marked below | offset, zoom |
| Chop, a manual take | `TAKE` / cuts so far / `TAP` | waveform, cuts made, playhead | which pad cuts next |
| To pads | `SLC` / slices / `PADS` | Poly, Mono, Thru, Kit | first pad, pattern |
| Record, armed | `REC`, the record light / source | meter, threshold | length, target pad, threshold |
| Record, taking | `REC`, the record light / pad | time, the take growing | length, bar |
| Capture | `CAP` / seconds / `SEC` | the last phrase | target pad, push to keep |
| Load samples | `LOAD` / target pad / `SMP` | folders and files | |
| Load modules | `LOAD` / track / `SYN` or `FX` | Schwung's modules | |
| A module's page | module / track / page | page title, 8 knob cells | |

The pad pages are a first sort of Melodee's Stage pages onto eight knobs,
to settle while building: **PLAY** (tune, level, pan, start, end, attack,
decay, filter), **SHAPE** (attack, decay, curve, velocity, mode, mute group,
link, offset), **FILTER** (cutoff, resonance, type, envelope attack, decay,
depth, kit filter), **TIME** (warp, beats, follow, loop, loop point,
reverse), **CLOUD** (position, size, density, scan, spread, on).

A Schwung module's pages come from its `module.json`: `ui_hierarchy` where it
has one, else its `chain_params` eight at a time. Names are cut to the cell
(3 to 5 characters); the touched knob shows the full name. A chooser (a preset
list, an items list) is never a knob; a `null` in `knobs` is an empty knob,
and knobs past eight go to the level's next page.

A page the module draws itself (a `canvas` parameter: Radio Garden's
browser, Webstream's search) takes the whole screen, its own script drawing
it as under Schwung, with a footer line for the module and `PUSH: IN` /
`BACK: OUT`. Pushed, the page has the jogwheel until Back brings you out of
its top level. A page asking for text gets the keyboard screen: its title,
the text so far, and the pads lit as keys (letters grey, space blue,
backspace red), the steps as digits.

The jogwheel's push opens the module's menu (`MENU` on the rail), its
levels as lists: the preset first where the level has one, then values
(`CUTOFF      57`), levels (`ENVELOPE >`) and lists to choose from (the
current one marked `*`). Push a value to edit it (`[57]`), push a level to
open it; Back goes up, then closes the menu.

## Tracks

Sixteen tracks, as in Melodee, picked as design **A** (chosen on
2026-11-20). Every Move key keeps the meaning Ableton's manual gives it
(Kerem, 2026-11-20; the whole map is docs/MOVE_CONTROLS.md): Play, Mute,
Shift + Step 5 / 6 / 7, a track button with the Volume knob work as on the
Move, and Melodee's own jobs take keys the Move leaves free (a track button
held with the steps is one). Where a Move job and a Melodee job want the
same key (the track buttons and sixteen tracks, Menu and Load, Loop and
Chop), Kerem decides with that map. `docs/ui/tracks.png` is the sheet
(`src/ui/mockups_tracks.cpp`); the screens are built to it.

![The tracks screens](ui/tracks.png)

- **The track buttons** are the four tracks of the selected track's bank
  (1-4, 5-8, 9-12, 13-16), each in its track's colour (`ui/track_style.h`),
  the selected one bright, an empty one dark, a muted one grey, a soloed one
  blue (the Move's). A tap picks one. Held with the Volume knob: that
  track's volume in dB (`VOL` / track), as on the Move.
- **Mute**, as on the Move: alone, mutes the selected track; with a track
  button, mutes that track; Shift + Mute + a track button solos it. Its
  light is bright while the selected track is muted.
- **A track button held** shows the sixteen as a grid on the screen, a row a
  bank, each cell the track's kind (a synth's module), the selected one
  inverted; the rail says `TRKS` / the track / `/16`. The 16 steps are the
  sixteen tracks, each in its colour, the selected one white, an empty one
  grey. A step picks its track; an empty step opens the new-track sheet for
  it (shown as the button is let go).
- **Copy held**, with a track button: a step, then another, copies the first
  track onto the second: its sound, kit and patterns; the second keeps its
  mix strip; a synth gets its own instance of the module. Onto a track that
  has something it asks first (`COPY` / from / `> to`, push yes, Back no).
- **Delete held**, with a track button: a step asks to delete that track
  (`DEL`), push yes, Back no. Its mix strip stays, as Melodee's Delete.
- **The new-track sheet** (`NEW` / track / `TRK`): kit, synth (a Schwung
  module), keygroup, MIDI, audio; the jogwheel picks, push makes it. A kit
  goes on to Load, a synth to its modules. Push on an empty track's screen
  opens it too.
- **Each kind's Play screen**: a kit's (`KIT`) and a synth's (the module's
  short name) as before; a keygroup (`KEYS`), a MIDI track (`MIDI`), an
  audio track (`AUD`) and another host's plug-in (`PLUG`) show what they are
  and play (pads as notes on the note tracks, Up / Down the octave), their
  own pages coming in wave 2 (`docs/PARITY.md`); an empty track (`----`)
  says `PUSH: NEW TRACK`.
- **Transport**: Play plays and stops the patterns, as on the Move; while it
  plays the steps show the selected track's playhead (a sixteenth a step)
  and the detail line the bar and beat (`3.2`). Shift + Play is the Move's
  retrigger, left to the bindings table.
- **The Move's settings on Shift + a step**: with Shift held, Steps 5, 6
  and 7 light (6 white while the metronome is on).
  - **Tempo** (Shift + Step 5): the BPM large; the jogwheel sets it (Shift:
    tenths), knob 1 too; push confirms.
  - **Metronome** (Shift + Step 6): turns it on or off and shows it; the
    jogwheel sets its level (Melodee's: the Move's manual gives none); push
    confirms.
  - **Groove** (Shift + Step 7): the Move's groove, 0 % straight to 100 %
    triplet sixteenths and on to 150 % (Melodee's swing 50 to 75 %); the
    jogwheel sets it; push confirms.
  The steps count the bar's beats on these, the one playing white; Back
  leaves too.

| Screen | Rail | Subject | Detail |
| --- | --- | --- | --- |
| Track button held | `TRKS` / track / `/16` | the sixteen as a grid, a row a bank | track, kind, name; `COPY: A STEP`, `DELETE: A STEP` |
| New track | `NEW` / track / `TRK` | kit, synth, keygroup, MIDI, audio | `PUSH: MAKE` |
| Copy onto a used track | `COPY` / from / `> to` | the question | `PUSH YES`, `BACK NO` |
| Delete | `DEL` / track / kind | the question | `PUSH YES`, `BACK NO` |
| Empty track | `----` / track | `EMPTY` | `PUSH: NEW TRACK` |
| Keygroup, MIDI, audio, plug-in | `KEYS`, `MIDI`, `AUD`, `PLUG` / track / lowest note or channel | what it plays | kind; the bar and beat while playing |
| Track volume (track button held, Volume knob) | `VOL` / track / `TRK` | the track's volume in dB, a bar | |
| Tempo (Shift + Step 5) | `TMPO` / / signature | BPM large | `PUSH: OK`; bar and beat |
| Metronome (Shift + Step 6) | `MET` | `ON` or `OFF` large, its level | `PUSH: OK` |
| Groove (Shift + Step 7) | `GRV` | the groove in % large | `PUSH: OK` |

## Sequencer

Melodee's sequencer on the Move's own keys (Ableton's manual: Step
Sequencing 11, Loop Mode 12, Workflow 13, Recording 14, Capture 14.3,
Session 17), Melodee's patterns underneath (96 ticks a quarter, 128 a
track); its own pages on Shift + Loop, which the Move leaves free (Kerem,
2026-11-20). `docs/ui/sequencer.png` is the sheet
(`src/ui/mockups_sequencer.cpp`, drawn with `src/ui/seq_widgets.*`, which
the screens draw with too); `build/melodee-move --melodee-previews DIR`
draws the real screens into `DIR/sequencer/`.

![The sequencer screens](ui/sequencer.png)

- **The steps** are a bar of the selected pattern (a page of sixteen; twelve
  on a triplet Q), as on the Move: a tap puts the last pad's note on the
  step (a kit: the steps are that pad's lane; a note track: the notes held,
  else the last), a tap again clears it. White with a note, the track's
  colour dim where empty, brighter under a long note, grey past the loop,
  the playhead green. Left / Right the bar (Shift: sixteen bars); a step
  past the end makes the pattern longer (Melodee's spare steps).
- **A step held** shows its card (`STEP` / step / bar): the note, its
  velocity large, its length and nudge in steps, Melodee's marks on it
  (accent, condition, ratchet, chance, slide, tie, locks). Held with the
  Volume knob its velocity, the wheel its length, a later step its length up
  to it, Plus / Minus a semitone (long: an octave; a kit pad's pitch),
  Left / Right a tenth of a step (Shift: a tick; long: a step), a pad that
  pad's note in or out. **A knob** writes a lock of that knob's lane on the
  step (the Move's per-step automation; `LOCK` / step), its range red on
  the steps. **Push** turns the knobs to **TRIG** (Kerem, 2026-11-20):
  chance, condition, ratchet, ramp, pitch ramp, accent, slide, tie; push
  again, back to the locks.
- **Loop** (Loop Mode, `LOOP` / bars / `BARS`): the pattern's bars as
  blocks, the steps its bars (the selected one white, the loop's in the
  track's colour, the bar playing pulsing). A step selects the bar (Note
  Mode's steps show it after), a step held + another the loop between them
  (from bar 1, the pattern's length; from a later bar an editor's loop), a
  double press a one-bar loop; a bar held takes the wheel, the Volume knob,
  Plus / Minus, Left / Right and a pad for its notes; Copy + a bar, then
  another; Delete + a bar. **Loop held + the wheel** is the length (Shift:
  sixteenths) on any screen. **Loop held 0.4 s while playing** is Fill.
- **Note/Session** (Session, `PATT` / pattern / page): a row of pads a track
  of the bank in view (the first on top), a column a pattern number, eight
  at a time (Left / Right; Plus / Minus the bank). Patterns with notes in
  the track's colour dim, playing bright, waiting for the bar green
  (blinking), about to stop blinking in its colour, the selected one
  pulsing white; on the screen the same cells as the pads sit, a follow
  action's arrow, the playing one's progress. A pad launches (the next bar
  while playing, at once stopped); a pad held and others of its row tapped
  chain them; Copy + a pad, then another, asks before replacing; Delete + a
  pad clears it at once (17.1.4). The wheel moves the playhead by Q
  (stopped: Play continues there); its push opens the column's sheet
  (`SCNE`): knob 1 its tempo, knob 2 a ready name, pads of other columns
  chain sequences, push launches. Held, Note/Session peeks.
- **Rec**: from stopped a bar's count-in (`REC` / `CNT`, the beat large,
  Rec flashing), then the take from the top; while playing at once. Notes
  land at the tick the pad was heard (the audio thread stamps it), with Rec
  Quantize on the pattern's Q; overdub; an empty pattern is free and grows a
  bar at a time (`REC` / bars, `FREE`), the take ending at the end of the
  bar playing. Knob turns while recording are written as Motion. Rec again
  or Stop ends the take.
- **Capture** (Melodee's Recall): every track's last phrase into its next
  empty pattern, launched; played stopped, the transport starts and three
  tempos the phrase fits come up (`CAP` / BPM; the wheel picks). Lit while
  there is something to keep; Shift + Capture clears it, as do Play, Stop
  and a track change.
- **Shift + Step 3, Workflow** (`WORK`): quantize amount, step grid (the
  pattern's Q), count-in, Rec Quantize, time signature, a free take's
  longest, the metronome Always or on Rec only, BPM per pattern. **Shift +
  Step 11, Repeat** (`RPT`): Note Repeat on and its screen (style Repeat or
  the arp's Up, Down, Random on a note track; the rate large; pressure as
  velocity), again off. **Shift + Step 14** the next empty pattern, **15**
  twice as long, **16** the Move's Quantize (a tap, Workflow's amount; held
  + a pad, that pad's lane). **Tempo** (Shift + Step 5): the steps tap the
  tempo, Left / Right halve and double it.
- **Shift + Loop, Melodee's SEQ pages** (Shift + the wheel the page; the
  rail names it and says `n/9`): **PATTERN** (length, Q, signature, the
  track's speed, this pattern's BPM, free; push: Half and Double Seq and
  Speed, Thinner, Busier, Humanised, Reversed, an octave up or down into the
  next empty pattern, knob 1 the amount), **ENTRY** (Melodee's Step Edit
  PLAY: pads write at the caret, which moves on as they are let go; the
  wheel the caret, push a rest, Delete back a step), **ROLL** (the notes
  over the bar, the wheel the note in focus; knobs its length, velocity,
  chance, nudge, ratchet, octave, place by Q, key; push the jobs: marks,
  mute, pull to the grid, legato, split, reverse, the arp as notes, loop
  the bar, ramps, insert and delete a step; Copy the marked notes and Copy
  again to paste; Delete erases), **MOTION** (the pattern's lanes; push
  off or on, Delete clears), **MOD** (eight modulators: kind, shape, rate,
  sync, depth, target, its range), **MACROS** (eight, a knob each; push the
  lanes), **FEEL** (Time Correct heard live: strength, swing, humanize,
  velocity, a groove from another pattern, its amount; push lays it on the
  track's others), **FOLLOW** (the follow action; push puts it on the
  column), **LANE** (a kit pad's own lane length; Euclid's hits, steps,
  rotation; push lays it).

| Screen | Rail | Subject | Detail |
| --- | --- | --- | --- |
| A step held | `STEP` / step / bar | the note, velocity large, length, nudge, marks | `PUSH: TRIG`, locks |
| A step held + a knob | `LOCK` / step / pad | the lane's name on the step, its value, a bar | `DELETE+TOUCH: CLR` |
| A step held, TRIG | `TRIG` / step / bar | eight cells: chance, condition, ratchet, ramps, accent, slide, tie | `PUSH: LOCKS` |
| Loop Mode | `LOOP` / bars / `BARS` | the bars as blocks, the loop framed | bar, loop |
| Loop held + wheel | `LOOP` / bars / `BARS` | the length large | `SHIFT: 1/16S` |
| Session | `PATT` / pattern / page | four tracks' eight cells | track, pattern |
| A chain in the making | `CHN` / track / `TRK` | the chain large | `A TAP ENDS IT` |
| A column's sheet | `SCNE` / pattern | tempo, name | `PUSH: LAUNCH` |
| Count-in | `REC`, the dot / `CNT` | the beat large, the beats | pattern |
| A free take | `REC` / bars / `BARS` | the bars growing | bar and beat |
| Capture | `CAP` / bars or BPM | the notes kept, or three tempos | pattern |
| Workflow | `WORK` / / `SEQ` | eight settings | |
| Repeat | `RPT` / `ON` | the rate large, style, rate, pressure | |
| SEQ pages | the page's tag / pattern / `n/9` | a title, eight cells or the page's picture | the push's job |

## Screens in code

Each screen is a file of its own in `src/app/screens/`, a `Screen`
(`src/app/screen.h`) registered there with a `RegisterScreen`: its id, how
it is made, the jobs of the bindings table that open it (a `Trigger`:
`Job::Tempo`, Shift + Step 5, opens `tempo`), or the track kinds whose Play
screen it is. A screen answers jobs, never buttons; the app runs every
control through the bindings table ([MOVE_CONTROLS.md](MOVE_CONTROLS.md),
"How the app runs the map") and keeps the jobs that are the same
everywhere: the track buttons and the sixteen tracks, transport, the
Volume knob, mute and solo, a touched knob shown large, typing for a
module's page. A new screen is a new file; no other file changes. What the screens share is the studio
(`src/app/studio.h`): Melodee's project and engine host, samples, modules,
transport, saving.

## Controls

As on the Move, by the rule above; [MOVE_CONTROLS.md](MOVE_CONTROLS.md)
has every control and shortcut, where each one stands, and the tests that
hold the code to it. In short:

| Control | Does |
| --- | --- |
| Pads | Play: a kit's 32 pads (two banks of 16), a module's notes; Chop: its slices; with a step held, notes on the step; Session: patterns |
| Steps | Step Edit (a pattern's sixteenths; in Loop Mode its bars); held with a pad, the volume knob, the wheel, Plus/Minus or Left/Right, that step's notes |
| Shift + steps | 1 projects, 2 Setup, 3 Workflow, **4 Mixer**, 5 Tempo, 6 metronome, 7 swing, 8 16 Pitches, 9 Keys & Scales, 10 Full Level, 11 Repeat and Arp, **12 Looper**, **13 Song**, 14 a new pattern, 15 double, 16 Time Correct (Melodee's own in bold). Shift held lights them; the screen shows nothing |
| Track buttons | the four tracks of the bank in view; held, the sixteen, and a step picks one (Tracks, above); Shift + one, its options |
| Knobs 1-8 | the screen's eight parameters; touch one to see it large; Shift for fine |
| Volume knob | the main volume; with a pad held its level, a track button its level, a step its velocity |
| Wheel | on Play, turn to walk the track's items (the track, its sound, its effects) and push to open that item's browser; Shift + turn the pages; Shift + push Sample Options (Chop) or a module's menu; in lists, turn and push |
| Note/Session | Play or the Patterns grid; Shift + it, the 16 tracks |
| Play / Shift + Play | start and stop / restart every pattern |
| Rec | Seq Record |
| Capture / Shift + Capture | Recall the notes just played / clear them |
| Sampling / Shift + Sampling | Record onto a pad / Audio Recall onto the next empty pad |
| Loop | Loop Mode; held with the wheel, the pattern's length |
| Mute | the selected track; held with a track or a pad, mutes it; Shift + Mute + track solos |
| Undo / Shift + Undo / held | undo / redo / the undo list |
| Copy, Delete | the pattern, or held with a step, pad or track what it names |
| Plus / Minus | a synth's octave, a kit's 16 pads; with a step held, transpose |
| Left / Right | the pattern's bar; in Chop, the slice |
| Back | out a level; held, back to Play; Shift + Back, back to Ableton's Move app |

## Lights

- **Pads**: a kit in its pads' colours, the last hit white; in Chop the
  slices in alternating colours, the selected one white, and in a manual take
  the pad that cuts next blinking red; on a module track every C in the
  track's colour, the other naturals dim, a held pad white.
- **Steps**: in Chop, the parts where a cut falls; on Play the pattern's
  bar (white notes, the track's colour dim where empty, brighter under a
  long note, grey past the loop, the playhead green, a held step's locks
  red); in Loop Mode its bars.
- **Track buttons**: the bank's four, each its track's colour, dim; the
  selected one bright; an empty one dark.
- **Steps, a track button held**: the sixteen tracks, each its colour, the
  selected one white, an empty one grey; Copy and Delete lit.
- **Steps, Shift held**: Steps 5, 6, 7 (tempo, metronome, groove), 6 white
  while the metronome is on.
- **Steps, on tempo, metronome and groove**: the bar's beats, the one
  playing white.
- **Track buttons, muted and soloed**: grey and blue, as on the Move.
- **Mute**: bright while the selected track is muted.
- **Play**: green while playing.
- **Knob lights**: lit for the knobs the screen uses, dark for the rest.
- **Buttons**: lit where they do something on this screen.

## Open questions

- The pad pages' exact contents.
- A module track's note layout: rows of fourths for now, C2 at the bottom
  left (Ableton's names); a scale or chromatic later.
- Realtime priority when Schwung launches melodee-move as `ableton`: a
  small root helper, as EvilGenius uses, or not.
