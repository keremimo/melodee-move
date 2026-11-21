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

## Kits

Melodee's kits at full depth on the Move (wave 2, kit depth; Kerem's
decisions of 2026-11-20 in [MOVE_CONTROLS.md](MOVE_CONTROLS.md)).
`docs/ui/kit.png` is the sheet (`src/ui/mockups_kit.cpp`), approved before
the screens were built; the screens are `src/app/screens/kit_play.cpp`,
`kit_pages.cpp`, `sample_options.cpp`, `load_kit.cpp`, the lights
`kit_common.h`.

![The kit screens](ui/kit.png)

- **128 pads in eight banks** (A-H), as Melodee's kits. The 32 pads are two
  banks, the lower one bottom; **Plus and Minus move them 16 pads** (A-B,
  B-C ... G-H), **Shift + Plus and Minus four** (Control Live's four pads:
  A05-C04), a message naming the pads in view (`PADS C01-D16`), Plus and
  Minus lit where they can move. The bank the view starts in is kept with
  the kit; the four-pad step only for the run. With the Banks pad layout
  (`app/pad_layout.h`) the 32 are two MPC 4x4 banks side by side. The rail's pad (`C05`) carries its
  bank's letter.
- **The pad pages** (Melodee's Stage, the Move's parameter banks): **Shift +
  turn** from Play walks nine pages of the last pad, past either end back to
  Play; Back too. Each page is eight knobs: TRIM (start, end, loop point,
  Loop Lock, gain; a picture of the region with its flags), LAYERS (the
  layer in hand, its velocities, tune, fine, level, pan, how a hit picks; a
  row a layer; the wheel's push loads a sample onto the layer in hand),
  TUNE (tune, fine, Warp, # Beats, and the kit's Follow, its mode Beats,
  Tones or Tape (Tape is Warp's Pitch mode), Pitch), AMP ENV (attack, decay,
  sustain, release, velocity sense, decay from), PLAY (One Shot or Note On,
  Mono or Poly, mute group, offset, Pad Link, loop, reverse, the loop's
  crossfade), CLOUD (on, position, size, density, scan, spread; the cloud
  over the region), MIX (level, pan, kit volume, COLOUR: the track's, one
  of the sixteen track colours, or the pad's own), FILTER (type, cutoff,
  resonance, envelope amount and its four stages) and KIT FILT (the filter
  after every pad: type, cutoff, resonance). A page draws its knobs as
  cells under its title, three letters each; a knob touched shows its full
  name and value large. On a pad holding an instrument TRIM becomes ZONES
  (drawn, waiting for the instruments' loading).
- **Shift + a knob touched** (the Move's second parameter): a kit knob's
  fine view. TUNE shows its cents; START, END and LOOP POINT their place
  close up, 32 times nearer (the Move's Sample Start zoom); Shift + turn
  moves them finely.
- **16 Pitches** (Shift + Step 8, as on the Move): the right 16 pads play the
  last pad up the project's key from its own note (Melodee's 16 Levels
  Scale), pad 1 bottom left; Plus and Minus their octave; the left 16 one
  bank of the kit as the MPC lays it (bottom left first, rows of four), a pad
  there making it the pad the pitches play. The rail says `PTCH`, the
  detail line the key and the octave. The pitches leave from the audio
  thread in the cycle they were read, as any pad.
- **Copy held**, a pad then another: the first's sound (sample, values,
  chain, mute) onto the second; the first blinks while it waits; onto a pad
  that holds a sample it asks first (`COPY` / from / to, push yes, Back no).
- **Delete held + a pad**, at once (Melodee's Erase + pad): its notes in the
  pattern go first; a pad with none is emptied; an empty pad closes its gap,
  the pads after it to the end of the next bank moving down one with their
  notes (`A08-B16 DOWN ONE`).
- **Sample Options** (Shift + push on the pad's sample): CHOP >, REVERSE
  (on or off), TRIM SAMPLE (the region as `Samples/Edits/<NAME>-Trimmed.wav`,
  the pad onto all of it, its values kept), NORMALIZE (the pad's gain, its
  loudest frame to full scale), SAVE SAMPLE (a name typed, then
  `Samples/<name>.wav`). On LAYERS with layer 2-4 in hand they are the
  layer's, and REMOVE LAYER takes it off.
- **The browser** lists bank files (`.mldb`, tagged `BANK`), which load into
  the bank of the pad in view, and MPC drum programs (`.xpm`, `.xty`, tagged
  `KIT`), whose push opens **LOAD KIT**: WHOLE KIT, a row a bank of the kit
  (its push steps through the program's filled banks, then none) and LOAD
  BANKS. **Shift held** in the browser opens its options: Save Sample, Save
  Bank for the two banks in view.
- **Long samples** (over ten seconds, up to six hours, or shorter ones memory
  has no room for) play from storage as Melodee's do (`engine/stream_pager.h`):
  half a second where each pad starts held in memory, the pager reading
  ahead; a long MP3, FLAC, Ogg or AIFF is decoded once into
  `Melodee/Cache` and read from there. The detail line gives a long
  sample's length in minutes. **Memory full**: a load that would leave less
  than 256 MB free is refused, and says so.

| Screen | Rail | Subject | Detail |
| --- | --- | --- | --- |
| Play, banks moved | `KIT` / track / pad | the last pad's waveform | `PADS C01-D16`, for a moment |
| A pad page | `PAD` / pad / page (`3/9`) | the page's title, eight cells; TRIM, LAYERS, CLOUD a picture | TRIM the region's length; LAYERS the layer's velocities and level |
| A knob Shift + touched | `KNOB` / knob / `FINE`, `STRT`, `END` | the value large, or the place close up | the place, `X32` |
| 16 Pitches | `PTCH` / track / pad | the last pad's waveform | key and scale, octave |
| Copy held | `COPY` / pad / `PAD` | the pad, `ONTO A PAD` | the sample |
| Copy onto a used pad | `COPY` / from / to | `REPLACE`, the pad | `PUSH YES`, `BACK NO` |
| Sample Options | `OPT` / pad / `SMP` or `LYR2`-`LYR4` | the list | |
| Load Kit | `KIT` / track / `LOAD` | WHOLE KIT, INTO A-H, LOAD BANKS | |
| Browser options | `OPT` / pad / `LOAD` | Save Sample, Save Bank | |
| Memory full | `KIT` / track / pad | `MEMORY FULL` | |

**Kit lights** (Kerem's rule, 2026-11-20): a pad with a sample dim in its
colour (COLOUR, an MPC kit's own, or the track's), bright while it sounds (the
engine's voice taps); the last pad white; a muted pad dark grey (the Move's dim
white); an empty pad off; a pad waiting (the copy's first, the pad a sample
loads onto) blinks white. With 16 Pitches the right 16 light the key's roots
in the track's colour and its other notes light grey. Copy is bright while
held; Plus and Minus are lit where they move.

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
| Pads | Play: a kit's 32 pads (two of its eight banks of 16; with 16 Pitches, one bank and the last pad's pitches), a module's notes; Chop: its slices; with a step held, notes on the step; Session: patterns |
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
- **Steps**: in Chop, the parts where a cut falls; the playhead while playing.
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

- ~~The pad pages' exact contents.~~ Settled with kit depth: nine pages on
  Shift + turn ("Kits", above).
- A module track's note layout: rows of fourths for now, C2 at the bottom
  left (Ableton's names); a scale or chromatic later.
- Realtime priority when Schwung launches melodee-move as `ableton`: a
  small root helper, as EvilGenius uses, or not.
