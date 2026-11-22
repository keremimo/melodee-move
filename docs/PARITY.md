# Melodee on the Move: feature parity

Every feature of Melodee on the MPC Sample (melodee-sample) as a row: where
it stands on the Move, the Move controls that will carry it, and a box to
tick. Each wave-2 session takes the rows of its area as its to-do list.
References name melodee-sample's docs: `MELODEE.md`, its `README` and the
user guide (`guide/*.md`).

**Status**, one of:

- `done`: on the Move now, or built by the core session (wave 1). On the
  Move now: one kit of 32 pads on Melodee's SamplerEngine (pads 1-32 are
  kit pads A01-B16), Load samples (WAV, AIFF, FLAC, Ogg, MP3 from the Move's
  library), the pad sound knobs (tune, level, pan, start, end, attack,
  decay, cutoff), Chop (Auto, Beats, Regions, Threshold; slice start and end
  on knobs) and To Pads (Poly, Mono, Thru), Schwung's synth modules as
  instruments with their menus, presets and own pages, autosave, the master
  volume. In the core session: Melodee's EngineHost with 16 tracks; kit,
  PLUGIN (a Schwung module), keygroup, MIDI and audio tracks as engines (the
  screens of the last three are wave 2); new, delete and copy track; track
  select; Play / Stop; tempo, swing, metronome; the whole project as
  Melodee's `.mldp`.
- A wave-2 area: `sequencer`, `sampling`, `kit depth`, `instruments`,
  `mixer & effects`, `song & export`, `looper`,
  `MIDI & sync & undo & projects`.
- `web page`: the wave-1 session running beside the core.
- `schwung`: covered by Schwung on the Move, nothing for Melodee to build
  (Kerem, 2026-11-20): Stems by Schwung's own separator, plug-ins by
  Schwung's modules, which are the Move's plug-ins (Melodee hosts them on its
  PLUGIN tracks). Where Melodee still has to wire a Schwung piece in (its
  audio effects on a chain), the row names that wave-2 area instead.
- `n/a`: no place on the Move. The Move controls cell says why, and what
  stands in where something does.

**Move controls** are proposals, to settle while building. Every key the
Move prints keeps the meaning Ableton's manual gives it (Kerem, 2026-11-20;
"Ableton Move parity", below, and [MOVE_CONTROLS.md](MOVE_CONTROLS.md)):
Melodee's own jobs go to what the Move leaves free. Where a proposal below
names a Move job (Shift + Step 1-3, 5-11, 15, 16; Menu, the Note/Session
toggle; Loop; Shift + Play; Shift + a track button), MOVE_CONTROLS.md's
bindings map decides, and overrides the cell here.

- Row1-4 pick among the four tracks of the bank in view (the Move's track
  select). A track button held shows the 16 tracks and turns the steps into
  them (free on the Move): a step picks that track, Copy + step copies it,
  Delete + step asks to delete it (the step again says yes), an empty step
  opens the new-track sheet.
- Play plays and stops; Shift + Step 5 / 6 / 7 are tempo, metronome and
  groove, as on the Move; a track button held with the Volume knob is that
  track's volume; Mute (+ Shift) + a track button mutes (solos) it.
- Otherwise the steps are the sequencer's 16 steps.
- **Shift + Step n** carries the job the MPC prints under pad n (Shift +
  Pad n).
- **Places > X**: Melodee's Menu, opened by holding the jogwheel's push (as
  the MPC's encoder is held), its places laid on the pads. **HERE** is the
  same Menu's list of the page's own jobs.
- **Pattern layer**: Shift held, the pads are the selected track's
  patterns, 32 a bank, Up / Down for the next bank (the MPC's Seq held).
- Back is the MPC's B1 (back, cancel), the jogwheel's push its B3 verb,
  Left / Right step a track's pages (B1-B3's cycles), knobs 1-8 carry K1-K3
  and their Shift layer, Up / Down the MPC's − / +.

**Ticking**: change `[ ]` to `[x]` once the feature works on the Move and
Kerem has checked it there. `n/a` rows have no box.

## Playing and tracks

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **16 tracks**, one selected, picked from any page (README › Features; MELODEE.md › The panel, Pad Bank) | done | Row1-4 the bank of four; track button held + step | [ ] |
| **16-track overview** while picking: each track's engine, pattern, mute (MELODEE.md › Pad lights) | done | track button held: screen and step lights | [ ] |
| **Track types**: kit, keygroup, plug-in, MIDI and audio engines (MELODEE.md › Sound engines; guide/tracks.md) | done | new-track sheet | [ ] |
| **New-track sheet**: the types as a carousel (its undo: MIDI & sync & undo & projects) (MELODEE.md › Playing, An empty track) | done | track button held + empty step; jogwheel, push | [ ] |
| **Drum sources**: Card (opens the browser), Empty, Input (opens Record) (guide/tracks.md) | done | new-track sheet: KIT, then LIBRARY (Load), EMPTY, INPUT (Sampling) | [ ] |
| **MIDI track ports** on the new-track sheet: Out jack, USB-C, All USB (guide/tracks.md) | n/a | one port on the Move: a device on USB-A and a computer on USB-C arrive together on cable 2, so there is nothing to pick (docs/MIDI.md) | — |
| **Delete a track**: sound, kit, patterns; its mix strip stays; asks (its undo: MIDI & sync & undo & projects) (MELODEE.md › The panel, Pad Bank) | done | track button held + Delete + step, the step again | [ ] |
| **Copy a track**: sound, kit, 128 patterns, motion; the target keeps its strip; a synth gets its own instance of the module (MELODEE.md › Across every page, Copy) | done | track button held + Copy + step, then a step | [ ] |
| **Move a track**: trades places; song clips, keys, lanes and sources follow it (MELODEE.md › Across every page, Move) | done | track button held + Shift + Copy + step, then a step (or Shift + Copy + steps on the 16 tracks); a track there trades places; one undo | [ ] |
| **Stage**: each track's home pages, B1 source, B2 how it plays, B3 where it goes (MELODEE.md › Playing) | done | Shift + the jogwheel: a kit's nine pad pages (TRIM, LAYERS, TUNE, AMP ENV, PLAY, CLOUD, MIX, FILTER, KIT FILT, docs/UI.md "Kits"), a module's pages; the Play screen's knobs first | [ ] |
| **Synth presets**: walked, loaded a moment after the last turn (MELODEE.md › Playing, A synth) | done | jogwheel (Schwung modules) | [ ] |
| **Synth EDIT**: the instrument's own parameters and page map (MELODEE.md › Playing, Edit) | done | knobs 1-8, Left / Right; push: the module's menu | [ ] |
| **Macro knobs FILTER, ECHO, SPACE** on every plug-in track, saved, recorded as motion (MELODEE.md › Playing, A synth) | n/a | Dropped (Kerem, 2026-11-20): the MPC Sample's; a module track's knobs are Schwung's controls, and a project's macros are let go as it opens | [ ] |
| **ARP**: Up, Down, Up-down, Random, Order; rate 1/4-1/32T; octaves; gate; hold (MELODEE.md › Playing) | done | ARP page (Shift + turn), knobs 1-6; Shift + Step 11's Arp styles with the sequencer's Repeat | [ ] |
| **CHORD**: a pad a chord of the scale; type, voicing, inversion, hold (MELODEE.md › Playing) | done | CHORD page (Shift + turn), knobs 1-5 | [ ] |
| **MIX → SENDS → FX pages** of a track: level, pan, send A, meter; four sends and returns; the chain's slot in hand (MELODEE.md › Playing) | done | the Device View's track item: knobs 1-6 level, pan, sends A-D, its meter and CPU share; its effects the next items; the returns on the Mixer | [ ] |
| **Pads as notes** on a synth: rows of fourths, octave, pressure as poly aftertouch (MELODEE.md › Playing, Keys) | done | 32 pads; Up / Down the octave | [ ] |
| **Keys**: root, scale, layout (Scale, Chromatic, Chords, Prog, Drums), transpose ±12, roots lit (MELODEE.md › Playing, Keys) | done | Shift + Step 9, the Move's Keys & Scale: MODE (In-Key, Chromatic, Prog, Drums), ROWS, KEY, SCALE (the Move's 35), TRANSPOSE; Shift + Plus / Minus a note of the key; pad lights | [ ] |
| **Project key and scale**, set from any track (MELODEE.md › Playing, Tempo) | done | Shift + Step 9 on any track (a kit: KEY and SCALE) | [ ] |
| **Prog**: MPC 3 `.progression` files, a chord a pad (48 at most), key, voicing (MELODEE.md › Playing, Prog) | done | Keys & Scale, MODE Prog: PROG lists them (loaded as the wheel turns), VOICING, KEY SHIFT; 32 pads, Shift + Plus / Minus 16 more | [ ] |
| **Chord builder**: held keys stored onto a pad, saved as Mine N (MELODEE.md › Playing, Prog) | done | Keys & Scale: pads held + push on STORE CHORD, then the pad it goes on | [ ] |
| **Drums layout**: a drum plug-in's or MIDI drum machine's pads from note 36, voices named (MELODEE.md › Playing, Drums) | done | Keys & Scale, MODE Drums: 32 pads are notes 36-67, Plus / Minus 16 more; the drum hit named (General MIDI) | [ ] |
| **Pads page**: the bank's pads each drawing its waveform (MELODEE.md › Playing, Pads) | n/a | 128 x 64 cannot show 32 waveforms; the pads light in their colours and the screen shows the last pad | — |
| **Sounds**: a MIDI track's saved sounds (`.mlds`), favourites (MELODEE.md › Playing, Sounds) | done | the wheel's push on a MIDI track's sound: its own and the saved `.mlds` (Melodee/Sounds), loaded as the wheel turns; Shift held: save, star, show starred or recent | [ ] |
| **16 Levels**: the last pad or note on every pad as Velocity, Filter, Tune, Scale or Zones; ends, octave; a MIDI keyboard plays the pad on every key (MELODEE.md › Playing, 16 Levels) | done | as the Move's 16 Pitches (Shift + Step 8): the last pad on the right 16 in the project's key (Melodee's Scale; Kerem, 2026-11-20), Plus / Minus the octave; the left 16 one bank. Velocity, Filter, Tune and Zones are not on the Move's Layout | [ ] |
| **Note Repeat**: 1/4-1/64 and triplets, swung, pressure as velocity, written under Seq Record (MELODEE.md › Playing, Note Repeat) | done | Shift + Step 11: on, its screen (style Repeat / arp Up, Down, Random; rate 1/4-1/64T; pressure as velocity); again off; repeats written under Rec | [ ] |
| **Tempo**: BPM, swing, metronome level, metronome on / off (MELODEE.md › Playing, Tempo) | done | Shift + Step 5 / 6 / 7: tempo, metronome, groove, as on the Move | [ ] |
| **Tap tempo, halve, double; metronome Rec** (clicks only while recording) (MELODEE.md › The panel; Playing, Tempo) | done | Tempo (Shift + Step 5): tap the steps, Left / Right halve, double; Workflow: Metronome Always / Rec only (Kerem, 2026-11-20) | [ ] |
| **Track speed**: ×½, ×¾, ×1, ×1½, ×2 against the tempo (MELODEE.md › Playing, Tracks) | done | SEQ pages (Shift + Loop), PATTERN: SPD, knob 4 | [ ] |
| **Keygroup track pages**: TRIM / ZONES, TUNE, AMP ENV, FILTER, FILT ENV, PLAY, ARP, CHORD (guide/instrument-and-midi-tracks.md) | done | Shift + turn: ZONES or TRIM, PLAY (TUNE in it), AMP ENV, FILTER (FILT ENV in it), ARP, CHORD; knobs 1-8; the wheel's push on the instrument loads | [ ] |
| **A keygroup of one sample** played from the note found in it (guide/instrument-and-midi-tracks.md) | done | pads as keys, from the note found in it | [ ] |
| **Audio track INPUT**: source, monitor, gain; armed by Seq Record, one pass of the pattern recorded (guide/audio-tracks.md) | done | Sampling on an audio track opens INPUT (the wheel, knobs 1-3; the source MIC or LINE as the jack has it); Rec arms, Play records one pass | [ ] |
| **Audio track takes**: CLIP (level, start, end), FADES (in, out, reverse), WARP (Stretch, Repitch, BPM, semi); kept in `<project> Audio` (guide/audio-tracks.md) | done | the audio track's Play screen: CLIP knobs 1-6 (with the fades and reverse), Shift + the wheel WARP knobs 1-4; takes held in memory once read | [ ] |

## Looper

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Looper on / off**: asks first; 45 MB a loop (MELODEE.md › Looper) | done | Shift + Step 12 asks, push turns it on; off from the loop's sheet (asks while loops hold audio) | [ ] |
| **Four loops L1-L4** on loop tracks of their own, each with a mix strip and a chain of six (MELODEE.md › Looper) | done | the track buttons are L1-L4 on the Looper; the chains are the engine's and the project's (effects into them with mixer & effects' FX pages) | [ ] |
| **Loop states**: ARM, REC, DUB, PLAY, STOP, FIT; record → close → overdub ⇄ play (MELODEE.md › Looper) | done | Rec (the selected loop), or a loop's record pad | [ ] |
| **Loop settings**: length (Free, 1-16 bars), level, feedback, Quantize (Bar, Beat, Off), After take (Play, Overdub, Stop) (MELODEE.md › Looper) | done | knobs 1-8 on the Looper: source, length, level, feedback, pan, send, quantize, after take | [ ] |
| **Loop pads**: record / overdub, play / stop on the boundary, undo, redo, held to clear (MELODEE.md › Looper, Pads) | done | a pad row a loop, L1 on top, its four left pads (Rows: 25-28 to 1-4; Banks: the left bank, 13-16 to 1-4); the right 16 play the selected track | [ ] |
| **Loop sources**: Input, a track after its chain, another loop, Mix; each written behind the play head by its delay (MELODEE.md › Looper, Sources) | done | knob 1: Input (line in or mic as the Move routes it), its left or right side, a track, another loop, Mix; the input's delay measured by the sheet's IN DELAY (428 frames on the simulated Move's 300-frame cable) | [ ] |
| **Looper MIX**: level, pan, send, bus; mute and solo a loop (MELODEE.md › Looper) | done | Shift + the wheel: MIX, knobs 1-8; the bus on the sheet; Mute (+ Shift) + a track button; a track button held + the Volume knob | [ ] |
| **MORE sheet**: Play / Stop on the boundary or at once, Clear (asks) (MELODEE.md › Looper) | done | the wheel's push: a list the wheel walks | [ ] |
| **Commit to pad**: the loop onto the kit's next empty pad, with a note at bar 1 or not (MELODEE.md › Looper, Kept) | done | the sheet's COMMIT TO, COMMIT + NOTE | [ ] |
| **Transport**: Play lines the loops up with bar 1, armed takes start on the downbeat after the count-in; Stop closes takes (MELODEE.md › Looper) | done | Play; the metronome clicking only while a loop records waits for the sequencer's Rec metronome | [ ] |
| **Tempo follow**: a WSOLA stretch keeps the pitch; Fit renders the loop at the tempo now (MELODEE.md › Looper, Tempo) | done | automatic; Rec on a loop at another tempo fits it, then overdubs | [ ] |
| **Loops kept with the project** as `<project> Loops/Loop N.wav` (MELODEE.md › Looper, Kept) | done | `Projects/Move Loops/Loop N.wav`, written as it autosaves and on the way out | [ ] |

## Sequencing

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Patterns as note lists**: tick (96 a quarter), length, key, velocity, chance, accent, slide, tie, ratchet (MELODEE.md › Sequencing) | done | the steps; a step held + push: TRIG knobs; SEQ pages' Roll | [ ] |
| **128 patterns a track** in banks (MELODEE.md › Sequencing) | done | Session (Note/Session): eight a page, Left / Right over all 128; Shift + Step 14 the next empty | [ ] |
| **Pattern length, Q, time signature**: 1-128 bars, Q 1/4-1/64 with triplets, 1/4-15/16 (MELODEE.md › Sequencing, Patterns) | done | Loop held + wheel (Shift: sixteenths); Loop Mode; SEQ pages' PATTERN; Workflow: Step Grid, Time Sig | [ ] |
| **Swing** 50-75 % (MELODEE.md › Sequencing) | done | Shift + Step 7: the Move's groove | [ ] |
| **Free length**: an empty pattern grows a bar at a time as it is recorded (MELODEE.md › Sequencing) | done | Rec on an empty pattern: it grows a bar at a time; Workflow's Max Length | [ ] |
| **Launch a pattern**: at the next bar, at once when stopped; an empty one as long as the one playing (MELODEE.md › Sequencing, Seq held) | done | Session: a pad | [ ] |
| **Pattern chains**: up to eight patterns, each played once, round again (CHAN) (MELODEE.md › Sequencing, Seq held) | done | Session: a pad held, then more of its row | [ ] |
| **Copy and clear patterns**, across banks; clearing asks (MELODEE.md › Sequencing, Seq held) | done | Session: Copy + pad, then a pad (asks before replacing); Delete + pad clears at once, as the Move (17.1.4); Copy, Delete on Play | [ ] |
| **Patterns page**: cells as the pads sit, NEXT, follow arrows, the other tracks' dots, progress (MELODEE.md › Sequencing, Patterns) | done | Session: cells as the pads sit, queued dashed, follow arrows, the bank's four tracks, progress under the playing one | [ ] |
| **Playhead by Q and Continue** from it (Shift + wheel, Shift + Play) (MELODEE.md › Sequencing, Patterns) | done | Session: the wheel moves it by Q; stopped, Play continues there (Shift + Play stays the Move's restart) | [ ] |
| **Variations** into the next empty pattern: Thinner, Busier, Humanised, Reversed, Octave up and down (VARY) (MELODEE.md › Sequencing, Patterns) | done | SEQ pages' PATTERN: push, the variations; knob 1 the amount | [ ] |
| **Sequences and sequence chains**: pattern n of every track, a name and tempo of its own; several in one hold (SCNE) (MELODEE.md › Sequencing, Sequences) | done | Session: push, the column's sheet: knob 1 its tempo, knob 2 a ready name, knob 3 its colour; pads of other columns chain sequences; push launches | [ ] |
| **Sequence colour** (issue 19): none or a track colour, its cell and pad lit in it (SCNE) (guide/sequencing.md) | done | Session's column sheet: knob 3 COLOUR; the steps under the eight columns light in their sequences' colours, the sequence in hand bright | [ ] |
| **Sequence links**: a sequence keeps a pattern or off for each track; a pattern launched by hand kept in the sequence in hand; a track off from the next bar (SEQL) (guide/sequencing.md) | done | Session: a pad launches and keeps; the playing pattern's pad again, the track off; the column sheet shows what the sequence keeps, Capture keeps all, Delete clears, push launches (one that keeps nothing: its column, as Session's grid) | [ ] |
| **Follow actions**: Next, Previous, Any, Again, Stop; after 1-64 bars or plays; chance; OR (FOLW) (MELODEE.md › Sequencing) | done | SEQ pages' FOLLOW: knobs 1-5; push: the column | [ ] |
| **BPM per pattern** (MELODEE.md › Sequencing, Patterns) | done | SEQ pages' PATTERN: BPM; Workflow: BPM/Pattern on | [ ] |
| **Seq Record**: armed, then Play; at once while playing; overdub; notes at the tick heard; a take is one undo (MELODEE.md › Sequencing) | done | Rec (14.1): from stopped a count-in and the take, playing at once; overdub; the tick the pad was heard (the audio thread stamps it). One undo a take: MIDI & sync & undo & projects' undo | [ ] |
| **Count-In**: a bar of clicks before recording (MELODEE.md › Shift + pads) | done | Workflow (Shift + Step 3): Count-In, as on the Move (13.3) | [ ] |
| **Rec Quantize**: notes land on the pattern's Q (MELODEE.md › Sequencing, Seq Record) | done | Workflow (Shift + Step 3): Rec Quantize | [ ] |
| **Fill**: FILL conditions play while held (MELODEE.md › The panel, Play) | done | Loop held while playing (Kerem, 2026-11-20) | [ ] |
| **Chance and trig conditions**: ALWAYS, n:m, FILL, FIRST, PRE, NEI and their NOTs (COND) (MELODEE.md › Sequencing) | done | a step held + push: TRIG, knobs 1-2 | [ ] |
| **Ratchets**: 1-8 hits, velocity ramp, pitch ramp on a kit pad (RTCH) (MELODEE.md › Sequencing) | done | a step held + push: TRIG, knobs 3-5; Roll: RTC | [ ] |
| **Accent, Slide, Tie**: about 4 dB louder; a glide of 60 ms; legato; CC 84 and CC 68 to synths (MELODEE.md › Sequencing, Roll) | done | a step held + push: TRIG, knobs 6-8 | [ ] |
| **Step Edit, STEPS**: the 16 steps of a bar for one pad or note, lit by velocity, the playhead; Tie and Insert (MELODEE.md › Sequencing, Step Edit) | done | the Move's steps: a bar of the last pad's lane (a kit) or of the notes, the playhead green; Left / Right the bar; TIE on TRIG; Insert on Roll's push | [ ] |
| **Step Edit, PLAY**: pads write at the caret; REST, ERASE; length, velocity, chance (MELODEE.md › Sequencing, Step Edit) | done | SEQ pages' ENTRY: pads write at the caret (the wheel), push a rest, Delete back; knobs length, velocity, chance | [ ] |
| **Parameter locks**: hold a step, turn a knob (MELODEE.md › Sequencing, Step Edit) | done | a step held + a knob (the Move's per-step automation): a kit pad's values and the mix's lanes (level, pan, sends, the track's and the master's effects, the returns). A Schwung module's own parameters take none, as Melodee's plug-ins take none (MELODEE.md › Plug-ins, Not yet): the modules are the Move's plug-ins | [ ] |
| **Lane length**: a kit pad's lane loops on its own, polymeter (LANE) (MELODEE.md › Sequencing, Step Edit) | done | SEQ pages' LANE: knob 1 | [ ] |
| **Euclid**: hits, steps 1-64, rotation on a lane (EUCL) (MELODEE.md › Sequencing, Step Edit) | done | SEQ pages' LANE: knobs 2-4, push lays it | [ ] |
| **Spare steps**: a step written past the end makes the pattern longer (MELODEE.md › Sequencing, Step Edit) | done | Right past the last bar, then a step | [ ] |
| **Roll**: notes at their ticks over the bars; the note in focus with length, velocity, chance, nudge (MELODEE.md › Sequencing, Roll) | done | SEQ pages' ROLL: the wheel the note, knobs 1-8 | [ ] |
| **Roll marks and jobs**: mark notes; move by Q or a row, mute, ratchet, velocities and ramp, pull to the grid, octave; split, legato, reverse, the arp as notes, loop the bars in view (MELODEE.md › Sequencing, Roll) | done | SEQ pages' ROLL: push, the jobs (marks, mute, pull to grid, legato, split, reverse, arp as notes, loop the bar, ramps, insert); knobs move by Q (MOV) or a row (KEY), octave, ratchet | [ ] |
| **Copy notes**: marked notes pasted at the cursor; a key's notes onto another; a step onto another (MELODEE.md › Across every page, Copy) | done | Roll: Copy the marked notes, Copy again pastes at the focus; MARK THE KEY then KEY moves a key's notes; Copy + a step, then a step | [ ] |
| **Erase notes**: a pad's notes while playing, a key's notes, the note in focus (MELODEE.md › Across every page, Erase) | done | Delete + pad (a kit); Roll: MARK THE KEY, Delete; Delete on Roll the note in focus | [ ] |
| **Motion**: knob moves recorded in each pattern, 8192 points, lanes reset at the pattern's start; a lane cleared (MELODEE.md › Sequencing, Motion) | done | Rec + knob turns (14.2) on a kit's pages, the mix (Mix in Motion) and the track's level; Delete + knob touch clears, Mute + touch off and on; SEQ pages' MOTION. A Schwung module's own parameters take none, as Melodee's plug-in parameters take none (MELODEE.md › Plug-ins, Not yet) | [ ] |
| **Mix in Motion**: level, pan, sends, master chain and returns recorded (MELODEE.md › Sequencing) | done | Rec + the selected track's level, pan and sends A-D (track button + Volume knob, the Mixer's VOL, PAN, SNDA-SNDD, the Device View's track item), its chain's effects (the Device View), the master chain's (the Mixer's MAIN, push) and the returns (MAIN's knobs 1-4, a bus chain's RETURN): lanes of the selected track's pattern; another track's knob on the Mixer writes none | [ ] |
| **MOD**: eight modulators, an LFO (seven shapes, free or synced) or a follower, up to four lanes each (MODS) (MELODEE.md › Sequencing) | done | SEQ pages' MOD: the wheel the modulator, knobs kind, shape, rate, sync, depth, target, low, high; push the target slot | [ ] |
| **Macros**: eight, each one knob over up to eight lanes (MACR) (MELODEE.md › Sequencing, MOD) | done | SEQ pages' MACROS: a knob each; push: the lanes the last one moves | [ ] |
| **A pattern's feel**: STRENGTH, SWING, HUMANIZE, HUMAN VEL, GROOVE from another pattern (FEEL) (MELODEE.md › Sequencing) | done | SEQ pages' FEEL: knobs 1-6 | [ ] |
| **Time Correct**: heard live, no commit; ALL lays the feel on the other patterns (MELODEE.md › Sequencing) | done | SEQ pages' FEEL (heard live), push: ALL; Shift + Step 16 is the Move's Quantize (it rewrites the notes, Workflow's amount) | [ ] |
| **Half Seq, Double Seq, Half Speed, Double Speed** (MELODEE.md › Sequencing) | done | Shift + Step 15 doubles (the Move's); SEQ pages' PATTERN push: Half Seq, Double Seq, Half Speed, Double Speed | [ ] |
| **Recall**: every note played is kept; the last phrase into empty patterns, one undo (MELODEE.md › Sequencing, Recall) | done | Capture (14.3): every track's last phrase into its next empty pattern, playing; from stopped three tempos on the wheel; Shift + Capture clears | [ ] |
| **Song**: a lane per track, clips of patterns, markers, loop brace, cursor; play from the cursor (MELODEE.md › Sequencing, Song) | done | Shift + Step 13 (again: back); the pads the bank's four lanes over eight bars, the steps sixteen bars; the wheel by the bar; knobs 1-3 pattern, length, zoom; Play from the cursor | [ ] |
| **Song clip jobs**: place, duplicate, mute, own pattern, open notes, split, delete, repeat to a marker, loop its bars (MELODEE.md › Sequencing, Song) | done | the wheel's push on a clip: its list (open notes, duplicate, split, own pattern, repeat to the marker, loop its bars, mute, delete); Copy, Delete, Loop; Copy, Delete or Mute held + a pad | [ ] |
| **Markers**: add, name, jump (MELODEE.md › Sequencing, Song) | done | Shift + push (on one: the next of Melodee's ready names, INTRO to OUTRO, then gone); Shift + Left / Right jump; typed names wait for a naming sheet | [ ] |
| **Tempo lane**: tempo points by the bar, step or ramp, 64 points (TMPL) (MELODEE.md › Sequencing, Song) | done | knob 5 the cursor's bar's tempo (under 30 BPM: none), knob 6 Step or Ramp; notches on Song, the tempo in its detail | [ ] |
| **Record into the song**: launches written as clips, punches too (guide/song.md) | done | Rec on Song: launches written as clips, a bar at a time; punches wait for Pad FX (mixer & effects) | [ ] |
| **Jam capture**: bars played outside Song logged, laid after the song's end as clips, markers and tempo steps (guide/song.md) | done | Capture on Song (TAKE JAM), lit while there is a jam; JAM n on the detail line | [ ] |
| **Mixdown export**: 32-bit float, or 24 or 16-bit dithered; tails up to 10 s (MELODEE.md › Sequencing, Song) | done | Sampling on Song: EXPORT, MIXDOWN 32 FLOAT, 24 or 16 BIT; Mixdowns/<project>.wav | [ ] |
| **Stems export**: `Mix.wav`, each track and each bus after its chain (MELODEE.md › Sequencing, Song) | done | EXPORT: STEMS; Stems/<project>/Mix.wav, NN <track>.wav, Bus X.wav | [ ] |
| **MIDI file export**: format 1, 96 ticks, tempo and time signature (MELODEE.md › Sequencing, Song) | done | EXPORT: MIDI FILE; MIDI/<project>.mid | [ ] |
| **MPC 3 project export**: drum, keygroup, audio and MIDI tracks; plug-in tracks as their prints (guide/song.md) | done | EXPORT: MPC 3 PROJECT (synth tracks printed first); MPC 3/<project>.xpj | [ ] |
| **Song into a new pattern**, laid end to end (MELODEE.md › Sequencing, Song) | done | EXPORT: SONG TO PATTERN | [ ] |
| **A `.mid` file loaded** onto the selected track's pattern (MELODEE.md › Sequencing, Song) | done | a `.mid` in the samples browser (a kit's pad), push; a synth track's browser is the instruments area's | [ ] |

## Print and freeze

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Freeze**: each pattern printed; the track plays its prints; pads still live; level, pan, sends, mute live; a kit's bus sends printed too (MELODEE.md › Print and freeze) | done | a track button held + Sampling: PRINT (Kerem, 2026-11-20); kit and synth tracks | [ ] |
| **PRINT sheet**: patterns on the pads, ALL, TAP Post / Pre FX, TAIL Loop / Ring / Cut, room needed, CPU saved (MELODEE.md › Print and freeze) | done | pads the patterns (Plus / Minus: 32 a bank); knob 1 TAP, knob 2 TAIL; the wheel FREEZE, DEEP FREEZE, ALL, NONE, UNFREEZE; push | [ ] |
| **Printing**: progress a pattern, the switch on a bar line, called off (MELODEE.md › Print and freeze) | done | the PRINT sheet shows the pattern, how far, the speed; Back calls it off | [ ] |
| **Freeze patterns from the pattern layer** (Seq held + Sample Record) (MELODEE.md › Print and freeze) | done | Note/Session held + Sampling (Session peeking, the patterns grid): PRINT for the selected track, its patterns on the pads; push freezes them | [ ] |
| **A frozen track shown**: a snowflake on the overview and pages; values a print holds dim (MELODEE.md › Print and freeze) | done | a snowflake in its Play screen's rail and on the sixteen tracks, its step ice; a knob or step asks first (one bit: no dim values) | [ ] |
| **Deep freeze**: the track's plug-ins unloaded too; thawing restores their state (MELODEE.md › Print and freeze) | done | PRINT: DEEP FREEZE (the wheel); the module let go once the print plays, thawed as it was | [ ] |
| **Unfreeze and edit**: at once, prints kept; an edit asks first; unchanged patterns keep their prints (MELODEE.md › Print and freeze) | done | PRINT: UNFREEZE; an edit on a frozen track asks UNFREEZE TRACK N?; unchanged patterns keep their prints | [ ] |
| **Print host**: faster than real time on a spare core, lowest priority, into `<project> Prints` (MELODEE.md › Print and freeze) | done | automatic: SCHED_IDLE on the last core, a second instance of the module, into `<project> Prints` | [ ] |
| **Auto sample a plug-in** into an MPC keygroup program (`.xpm`, `.xty`) on a new keygroup track (MELODEE.md › Print and freeze, Auto sample) | done | AUTO SAMPLE, the last row of a module track's menu (Shift + push): the .xpm and its WAVs in Melodee/Samples/Keygroups. The .xty (MPC 3's track file) is not written on the Move: Melodee's writer (shell/mpc3_export) needs its sample library | [ ] |
| **Auto sample check**: drums, mono and glide, accent, velocity layers, takes, splits, sustain and loops, release fit (MELODEE.md › Print and freeze) | done | automatic: drums, mono and glide, accent and velocity layers, takes, sustain and loops, hold, release fit; a split (two sounds side by side) is not found yet: such a module samples as one sound | [ ] |
| **AUTO SAMPLE sheet**: KEYS (low, high, step), NOTE (layers, hold, tail), SOUND (tap, loop, level) (MELODEE.md › Print and freeze) | done | knobs 1-8 LOW, HIGH, STEP, LAYERS, HOLD, TAIL, TAP, LOOP; Shift + turn: LEVEL; push samples, Back stops | [ ] |
| **Auto sample an external instrument** on a MIDI track, heard at the input (MELODEE.md › Print and freeze, External instruments) | instruments | not yet: MIDI out on cable 2 and the engine's input ring (Audio Recall's) are there now, but Melodee's external path is its own note source (engine/external_notes.h: the input's silence and the note's delay measured, each note cut to where its sound starts, posted notes sent by the MIDI thread as the ring reaches them), which the Move's auto sampler (a module's instance rendered faster than real time, app/auto_sampler.h NoteSource) does not take yet; then a check with an instrument on the line in | [ ] |
| **Keygroup glide**: Mono, portamento, Legato, Mono Retrig (MELODEE.md › Print and freeze, Keygroup glide) | done | automatic: an MPC program's Mono, Portamento, Legato and Mono Retrig reach Melodee's sampler | [ ] |
| **Delete from storage**: a file, folder, program or plug-in; what the project plays is refused (MELODEE.md › Print and freeze, Deleting) | done | Delete on the Load page, asked first; in Melodee's folder and the Move's User Library (Kerem), never the Core Library; a program with its `_[TrackData]`; plug-ins are Schwung's | [ ] |

## Sampler

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **One kit of 32 pads** on Melodee's SamplerEngine, A01-B16 (MELODEE.md › Sampler) | done | pads | [ ] |
| **Kits of 128 pads** in banks (the MPC's A-H of 16; here four of 32) (MELODEE.md › Sampler) | done | the 32 pads two of eight banks of 16 (Kerem, 2026-11-20); Plus / Minus move 16, Shift + Plus / Minus four; the rail's pad names its bank | [ ] |
| **No count of samples**: memory is the limit; a load that would leave too little is refused, "Memory full" (MELODEE.md › Sampler) | done | automatic: Melodee's shell/sample_memory.h, 256 MB left free; `MEMORY FULL` | [ ] |
| **Load samples**: WAV, AIFF, FLAC, Ogg, MP3, from places (My Samples, Recordings, Core Library, /data/UserData) (MELODEE.md › Sampler, Samples) | done | Menu; jogwheel, push; Back up | [ ] |
| **Long samples streamed** from storage, up to 6 hours; long MP3, FLAC, Ogg, AIFF decoded once into Cache (MELODEE.md › Sampler) | done | automatic: over 10 s (up to 6 hours), or what memory has no room for, read in place by Melodee's stream pager; long MP3, FLAC, Ogg, AIFF via Cache | [ ] |
| **PREVIEW**: each file plays as the cursor lands on it, streamed (MELODEE.md › Sampler, Samples) | done | as the wheel lands (Kerem); on or off on Sampling's settings page; read whole (30 s at most) before it plays, not streamed | [ ] |
| **Load onto a chosen pad** (the MPC: Shift + pad onto the next empty one) (MELODEE.md › Sampler, Samples) | done | Load page: a pad picks the pad it loads onto (pulsing), as the Move's browser | [ ] |
| **STARRED and RECENT**: star a file; the last 16 loaded (MELODEE.md › Sampler, Samples) | done | Load page: Shift + push stars (Kerem); STARRED and RECENT lead the places; `Starred.txt`, `Recent.txt` in Melodee's folder | [ ] |
| **SFZ instruments**: zones, round robin, layers, release samples, crossfades, keyswitches (MELODEE.md › Sampler) | done | the instrument browser: the wheel's push on a keygroup's instrument; streamed from storage | [ ] |
| **MPC keygroup programs** `.xpm`, and MPC 3 programs `.xty` (MELODEE.md › Sampler) | done | the instrument browser; streamed from storage | [ ] |
| **EXS24 instruments** `.exs` (MELODEE.md › Sampler) | done | the instrument browser; streamed from storage | [ ] |
| **SoundFonts** `.sf2`, presets listed as a folder (MELODEE.md › Sampler) | done | the instrument browser, an .sf2 a folder of its presets; its sounds laid out in Melodee/Cache and read in place | [ ] |
| **DecentSampler presets** `.dspreset` (MELODEE.md › Sampler) | done | the instrument browser; streamed from storage | [ ] |
| **An instrument on a kit pad**: its zones by key and velocity; ZONES page, zone tune and level trims (ZTRM) (MELODEE.md › Sampler, SFZ) | done | Load on a kit: SFZ, EXS and DecentSampler presets listed with the samples, tagged; push loads one onto the pad, its files landing; TRIM's place is ZONES (docs/ui/kit.png): knob 1 the zone, 2 ZONE TUNE (cents), 3 ZONE LEVEL, on that pad only, kept with the project (ZTRM). A SoundFont's presets and MPC keygroup programs load on keygroup tracks | [ ] |
| **MPC drum programs**: LOAD KIT sheet, the whole kit or banks, layers, pad colours (MELODEE.md › Sampler) | done | the browser lists them tagged KIT; push: LOAD KIT (whole kit, or a program bank into each kit bank, LOAD BANKS); pads of 2-4 layers take them as their own, more (an instrument) wait for the instruments' loading | [ ] |
| **MPC expansions** `.xpn`: installed into Expansions; program previews (MELODEE.md › Sampler) | done | Load page: push installs into Samples/Expansions/<title>, then opens it; a program's `[Previews]` file plays as the wheel lands (its programs load with kit depth and instruments) | [ ] |
| **Banks** `.mldb`: Save Bank, load into the bank in view (MELODEE.md › Sampler, Banks) | done | Shift held in the browser: Save Bank (the two banks in view); `.mldb` files listed tagged BANK, a push loads one into the bank of the pad in view | [ ] |
| **Pad sound knobs**: tune, level, pan, start, end, attack, decay, cutoff (MELODEE.md › Sampler, sample pages) | done | PLAY page, knobs 1-8; Shift finer | [ ] |
| **TRIM**: loop point, zoom at each handle, Loop Lock, Normalize (MELODEE.md › Sampler, Trim) | done | TRIM page: start, end, loop point, Loop Lock, gain; Shift + a touch: the handle close up; Normalize in Sample Options. Past Melodee (whose Trim is silent): a moved START or END is heard once still, as Chop's cuts, on TRIM, the Play screen's knobs 4 and 5 and a keygroup's one sample | [ ] |
| **LAYERS**: four samples a pad, Velocity, Cycle or Random; each layer's tune, level, velocities (MELODEE.md › Sampler, Layers) | done | LAYERS page: knob 1 the layer, its velocities, tune, fine, level, pan, how a hit picks; push loads the layer's sample; Sample Options removes it | [ ] |
| **Kit volume** (MELODEE.md › Sampler, Mix) | done | MIX page, knob 3 | [ ] |
| **Fine tune and Warp**: cents; Warp Off, 50-200 %, Seq from # Beats (MELODEE.md › Sampler, Tune) | done | TUNE page: tune, fine, Warp (Off, Seq, 50-200 %), # Beats; Shift + touch TUNE on Play: cents | [ ] |
| **Warp mode**: Time stretch or Pitch for the kit (MELODEE.md › Shift + pads) | done | TUNE page, Follow's mode (Tape is Pitch, Beats and Tones Time stretch): Shift + Step 15 is the Move's double loop | [ ] |
| **Amp envelope**: release on Note On, velocity sensitivity, decay from start or end (MELODEE.md › Sampler, Amp Env) | done | AMP ENV page: attack, decay, sustain, release, velocity sense, decay from | [ ] |
| **Play modes**: Mono / Poly, mute group 1-16, offset, Note On, reverse, loop, Pad Link chains (MELODEE.md › Sampler, Play) | done | PLAY page: One Shot / Note On, Mono / Poly, mute group, offset, Pad Link, loop, reverse | [ ] |
| **Loop seam crossfade** XFADE, 0-250 ms (MELODEE.md › Sampler, Play) | done | PLAY page, knob 8 | [ ] |
| **Filter type, resonance and envelope**: Classic, LPF2/4, HPF2/4, BPF2/4; up to seven octaves (MELODEE.md › Sampler, Filter) | done | FILTER page: type, cutoff, resonance, envelope amount, attack, decay, sustain, release | [ ] |
| **Follow**: the kit keeps the project's tempo, Beats, Tones or Tape; # Beats (MELODEE.md › Sampler) | done | TUNE page: Follow, its mode (Beats, Tones, Tape), Pitch; # Beats; Follow on (or Warp to Seq with no # Beats) sets each pad's own tempo from its sample's found tempo, exact at whole bars (core follow_tempo.h) | [ ] |
| **Cloud**: a pad as a grain cloud; position, size, density, scan, spread (MELODEE.md › Playing, A kit) | done | CLOUD page: on, position, size, density, scan, spread | [ ] |
| **Kit filter**: one filter after every pad (MELODEE.md › Sampler) | done | KIT FILT page: type, cutoff, resonance | [ ] |
| **COLOUR**: a pad's light colour, the track's, one of sixteen, or the kit's own (MELODEE.md › Playing, A kit) | done | MIX page, knob 4: the track's, one of the sixteen track colours, or the pad's own; pad lights | [ ] |
| **Full Level**: every hit at 127 (MELODEE.md › Shift + pads) | done | Shift + Step 10, the Move's Full Velocity (move-instruments): `Studio::full_level`, read by the kit's sends (pads and 16 Pitches) and a note track's pads alike | [ ] |
| **Pad sends** into buses A-D, and the kit's send (MELODEE.md › Playing, A kit) | done | a kit's track item, Shift + turn: the last pad's own sends on knobs 1-4; the kit's (the track's) on its first page | [ ] |
| **A pad's own chain** of four effects (MELODEE.md › Effects) | done | the Device View: after the pad, its own effects and their `+` (a dotted underline) | [ ] |
| **Chop**: Auto, Beats, Regions, Threshold; a slice's start and end (MELODEE.md › Sampler, Chop) | done | Loop; knobs 1-4; jogwheel or Left / Right the slice | [ ] |
| **Chop Manual**: pads tapped as it plays, cuts pulled onto hits (MELODEE.md › Sampler, Chop) | done | Chop: knob 3 on Manual; the first pad plays, the red pad cuts, a lit pad plays again from its slice | [ ] |
| **Slices 33-64** on a second bank (MELODEE.md › Sampler, Chop) | done | Plus / Minus in Chop | [ ] |
| **Chop close up**: the view follows the cut; a moved cut plays its slice; Shift + wheel jumps from hit to hit (MELODEE.md › Sampler, Chop) | done | knobs 1 and 2 close in on the cut (the wheel is the amount, as the Move's); Shift + the wheel from hit to hit | [ ] |
| **Extract, Split, Merge**; a slice taken out; every cut out (MELODEE.md › Sampler, Chop) | done | Shift + push in Chop: Extract, Split, Merge, Remove all cuts; Delete + a pad takes its cut out | [ ] |
| **CLEAN**: cut on the sample's drums stem (MELODEE.md › Sampler, Chop) | schwung | Schwung's separator writes the stems as files: Load the drums stem and Chop it | — |
| **To Pads**: Poly, Mono, Thru (MELODEE.md › Sampler, Chop) | done | Chop: push; jogwheel | [ ] |
| **To Pads KIT**: a pad a sound (MELODEE.md › Sampler, Chop) | done | To Pads sheet, its fourth row | [ ] |
| **To Pads PATTERN**: a note for each slice into the pattern (MELODEE.md › Sampler, Chop) | done | To Pads' last row PATTERN (push flips it, kept for next time): a note a slice where it falls (KIT: on its sound's pad) into the track's pattern when it is empty, else the first empty one, which then plays; one undo with the pads | [ ] |
| **Slice hits** (issue 24): Chop's pads recorded as their slices while Seq Record runs (SLCE), the kit untouched; hits follow the chop; a renumbering change asks; the MPC 3 export lays them on free pads (guide/kits-and-sampling.md) | done | Chop's pads while Rec runs; RENUMBER SLICES? (push yes, Back no), once a visit | [ ] |
| **Slices kept with the sample** (in its WAV on the MPC; here in the project, its own `MVCH` section, so the Move's library is never written) (MELODEE.md › Sampler, Chop) | done | automatic | [ ] |
| **Zero Snap and 2 ms edge fades** on every cut (MELODEE.md › Sampler, Chop) | done | automatic | [ ] |
| **A sample's note** found as it loads: a pitch, or a chord's root (MELODEE.md › Sampler) | done | automatic, every sample read (kept in the project) | [ ] |
| **Trim Sample**: the region into a new file, the pad onto all of it (MELODEE.md › Sampler) | done | Sample Options (Shift + push on the pad): TRIM (Shift + Step 13 is the Move-free Song) | [ ] |
| **Save Sample** under a name (MELODEE.md › Sampler) | done | Sample Options: SAVE SAMPLE, its name on the pad keyboard | [ ] |
| **Input Config**: source, rec length (Free, Seq, 1-16 bars), threshold, gain, Post / Pre FX, monitor Off / Auto / On, Rec Input FX (MELODEE.md › Sampler) | done | Sampling: the wheel walks SOURCE, GAIN, MONITOR, START, LENGTH, TAP (Post / Pre FX), knobs 1-6; the source as the jack has it (Kerem). Rec Input FX waits for Knob FX (mixer & effects) | [ ] |
| **USB audio in** (USB, USB L, USB R) (MELODEE.md › Sampler, Input Config) | n/a | no USB audio on the Move's stock firmware | — |
| **Record**: tap a pad to record onto it; threshold start; Seq length; other pads cut the take; written as recorded, up to 6 hours (MELODEE.md › Sampler, Record) | done | Sampling, then a pad (held: until let go); another pad carries the take on onto it; the file holds it all, a pad plays its first 60 s until long samples stream (kit depth) | [ ] |
| **Sampling Melodee's own sound**: a track or Resample as the source, the pads playing it (MELODEE.md › Sampler, Record) | done | SOURCE RESAMPLE or TRACK n; once a pad picks the take's, the pads play the source | [ ] |
| **Audio Recall**: the input's last 25 s onto the next empty pad (MELODEE.md › Sampler) | done | Shift + Sampling (Kerem; Capture is Melodee's Recall of notes) | [ ] |
| **Resample**: the pattern with every track onto a pad; on Song, the whole song (MELODEE.md › Sampler) | done | Sampling, SOURCE RESAMPLE, LENGTH SEQ, then a pad: Play starts, one pass of the pattern (Shift + Step 11 is the Move's Repeat). On Song, the whole song comes with song & export | [ ] |
| **Stems**: drums, bass, music and vocals onto four empty pads; BEST (MELODEE.md › Sampler, Stems) | schwung | Schwung's separator; Load the stems it writes onto pads | — |
| **Stems program**: melodee-stems and its 80 MB model built for the Move's A72; the memory check (MELODEE.md › Sampler, Stems) | schwung | Schwung's own, already built for the Move | — |

## Effects

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Chains**: six slots on each track, four buses A-D with six slots and a return, the master's six before the compressor (MELODEE.md › Effects) | done | the Device View's effect items (the wheel the slot, knobs 1-6 its values); a bus's and the master's chain from the Mixer's push | [ ] |
| **41 built-in effects**: reverbs, delays, modulation, dirt, filters and EQ, dynamics, Spring, Ensemble, Multi-chorus, Auto-wah, Formant, Width (MELODEE.md › Effects) | done | push on an effect: the browser by family, each loading as the wheel turns | [ ] |
| **Schwung's audio FX modules** in chains (melodee-move README › Next) | done | the browser's SCHWUNG list (`modules/audio_fx`), push loads; its pages on the knobs, Shift + turn | [ ] |
| **FX Select**: Track → Sends → Bus → Master; the slot strip; the effect sheet (change, presets, bypass, move, remove); copy an effect (MELODEE.md › Effects) | done | the Device View's row; push the browser, Shift + push the options; Mute / Delete / Copy held + push; Mixer push: a bus, the master (Melodee's effects have no presets of their own; a Schwung effect's are its own pages) | [ ] |
| **Mixer**: the 16 tracks' levels, pans, meters, CPU share, the strip in hand (MELODEE.md › Playing, Tracks) | done | Shift + Step 4: knobs 1-8 eight tracks, Left / Right 1-8 or 9-16, the steps the tracks; a touched knob that strip large; CPU share on the track item | [ ] |
| **Sends page**: every track's send into one bus, its return (MELODEE.md › Effects) | done | the Mixer's SNDA-SNDD: eight tracks' sends, the return last; its push the bus's chain, its return on knob 1 | [ ] |
| **Keyed Compressor and Gate**: KEY from a track or a bus (FXKY) (MELODEE.md › Effects) | done | knob 7 on a Compressor or Gate: off, a track, a bus | [ ] |
| **SYNC on the delays**: 1/32-1/1, triplets and dotted, following the tempo (MELODEE.md › Effects) | done | the delay's SYNC knob, shown as the note value | [ ] |
| **Knob FX**: the MPC's 28 and Melodee's 15; settings A and B and the move between them; per kit pad or per track (MELODEE.md › Effects) | done | Shift + Step 4 twice: the wheel and push the effect, knobs 1-6, 7 A or B, 8 A to B (the fader's job); steps the tracks, pads the kit pads | [ ] |
| **Pad FX**: 47 punches (the MPC's 16, Melodee 1 and 2), up to four held, pressure as depth, LATCH, master or track, QUANTIZE (MELODEE.md › Effects) | done | Shift + Step 4 three times: punches on the 32 pads (two banks), from the audio thread; Shift + push latch, target, quantize, tail | [ ] |
| **Pad FX KEEP**: punches laid into the pattern and looping with it (KEPT); taken out again (MELODEE.md › Effects) | done | Pad FX: push keeps; Delete + pad takes that punch's out; the steps show where they fall | [ ] |
| **Flex Beat**: a time effect over the master for a bar, 15 effects, One Shot / Loop, mix, quantize (MELODEE.md › Effects) | done | Shift + Step 4 four times: pads 1-16, knobs 1-3 mode, mix, quantize | [ ] |
| **Master Compressor**: attack, release, amount, In Boost, Color, Bypass (MELODEE.md › Effects) | done | the Mixer's MAIN, push: the master's chain, COMP last, knobs 1-6; its amount also on MAIN's knob 8 (Shift + Step 5 is the Move's Tempo) | [ ] |
| **Compressor Sidechain**: one track or one kit pad keys it (MELODEE.md › Effects) | done | COMP: knob 7 the key track (off, 1-16), knob 8 its kit pad (whole kit, A01-B16) | [ ] |

## Fader

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **The fader follows the pot turned last**, PIN keeps it (MELODEE.md › Fader) | n/a | no fader; every value has a knob on its page | — |
| **Fader page**: what it follows, favourites, stock targets (Pad Volume to Kit Volume) (MELODEE.md › Fader) | n/a | no fader; the pad pages' knobs hold those values; Shift + Step 9 goes to Note Repeat | — |
| **Mod wheel** from the panel (MELODEE.md › Fader) | n/a | Went with the MACRO page (Kerem, 2026-11-20); a MIDI controller's still reaches the module | [ ] |
| **Pitch bend** from the panel, back to the middle when let go (MELODEE.md › Fader) | n/a | Went with the MACRO page (Kerem, 2026-11-20); a MIDI controller's still reaches the module | [ ] |
| **Fader takeover**: Pickup, Scaled, Instant (MELODEE.md › Fader) | n/a | relative knobs have nothing to pick up | — |
| **The fader's lane and light**: Seq Record writes it, Erase + fader clears it; its light shows its value (MELODEE.md › Fader) | n/a | no fader; knobs record motion (Sequencing) | — |

## MIDI and sync

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **MIDI Config**: port, in and out channels, sync, thru, program change, takeover rows, panic, reset (MELODEE.md › MIDI and sync) | done | Setup's MIDI and MIDI SYNC rows (Shift + Step 2): channels, pad in and out, sync in and out, Thru, program change, Remote, PANIC. Port: one; takeover: none, the knobs are endless encoders; reset: Settings (move-projects) | [ ] |
| **MIDI jacks** (the External port) (MELODEE.md › MIDI and sync) | n/a | the Move has no MIDI jacks; USB-C only | — |
| **USB MIDI**: a computer or USB devices on USB-C (MELODEE.md › MIDI and sync) | done | USB-A devices and a computer on USB-C (the Standalone Port), both on cable 2 (docs/MIDI.md); to check on the Move | [ ] |
| **MIDI in**: notes on the in channel play the selected track; a kit hears 36 as A01; pressure, mod wheel, pitch wheel, sustain pedal, CCs, All Notes Off (MELODEE.md › MIDI and sync; guide/midi-and-sync.md) | done | automatic, every page and track kind; a MIDI track echoes it | [ ] |
| **MIDI in recorded**: under Seq Record; keys write on Step Edit and Roll (guide/midi-and-sync.md) | done | MIDI notes on the tracks they reach: under Rec written where heard (within the block they came in), as long as they were held; on the SEQ pages' ENTRY at the caret, a chord a step; Capture keeps them | [ ] |
| **Track channel, Track MIDI in, Pad MIDI in / out**: a controller a track (guide/midi-and-sync.md) | done | Shift + a track button: MIDI IN and MIDI OUT (Kerem); Pad MIDI in and out in MIDI Config; Track MIDI in's ports: one port | [ ] |
| **Remote CCs**: select a track, launch a pattern or a sequence (CC 102-104) (guide/midi-and-sync.md) | done | MIDI Config: Remote channel and CCs | [ ] |
| **Program change** launches that pattern (MELODEE.md › MIDI and sync) | done | MIDI Config: PROG CHANGE | [ ] |
| **Scale on white keys**: a keyboard's white keys play the scale on a Scale or Chords track, its black keys nothing (guide/midi-and-sync.md) | done | MIDI Config: SCALE KEYS, after PAD IN | [ ] |
| **MIDI out**: pads, patterns, the arp, each at the frame it is heard (MELODEE.md › MIDI and sync, Out) | done | automatic: with the exchange that carries the block it is heard in (block timing, 2.9 ms: the device takes MIDI once an exchange); played by hand at once | [ ] |
| **MIDI clock in and out**: Start, Continue, Stop, Song Position, tempo (MELODEE.md › MIDI and sync) | done | MIDI Config: SYNC IN, SYNC OUT; Tempo shows the tempo heard | [ ] |
| **MTC in and out**: in at 24, 25, 29.97 df, 30 fps; out at 25 (MELODEE.md › MIDI and sync) | done | MIDI Config: SYNC IN, SYNC OUT (a cue while stopped leaves the playhead) | [ ] |
| **MIDI Thru** (MELODEE.md › MIDI and sync) | done | MIDI Config: THRU (one port: what comes in goes back out on it) | [ ] |
| **MIDI Panic and all notes off** (MELODEE.md › MIDI and sync; The panel, Stop) | done | MIDI Config: PANIC; `melodee-midi panic`; no Move key (Kerem) | [ ] |
| **CV / sync out**, base and division (MELODEE.md › MIDI and sync) | n/a | no CV or sync jacks on the Move | — |
| **MIDI track DEVICE**: port and channel, USB devices by name, OFFLINE, DELAY ±50 ms (MELODEE.md › MIDI and sync, MIDI tracks) | done | DEVICE page: channel, delay. Port, USB devices by name, OFFLINE: one port, no names reach Linux (n/a) | [ ] |
| **MIDI track PROGRAM**: bank MSB, LSB, program; a pattern's own program (MELODEE.md › Playing, A MIDI track) | done | PROGRAM page: bank MSB, LSB, program, this pattern's | [ ] |
| **MIDI track CC and SLOTS**: 24 controls (CC, 14-bit CC, bend, pressure, NRPN), recorded as motion (MELODEE.md › MIDI and sync) | done | CC 1-8, 9-16, 17-24: knobs the values, Shift + a knob what it sends (Kerem); motion lanes with Seq Record (move-seq) | [ ] |
| **MIDI track EXPRESS**: a bend lane; pressure out as Poly, Channel or Off (MELODEE.md › MIDI and sync) | done | EXPRESS page: bend, pressure out | [ ] |
| **MIDI track PADS**: Keys or Drums with General MIDI names (MELODEE.md › MIDI and sync) | done | PADS page: Keys or Drums, the last pad's note and its General MIDI name; the pads play the map through move-instruments' pad layouts (Melodee's drum_key()) | [ ] |
| **MIDI in echoed** out on a MIDI track's port and channel, without loops (MELODEE.md › MIDI and sync) | done | automatic, with the loop guard | [ ] |
| **MIDI track AUDIO**: the instrument heard back through the track's strip and chain (MELODEE.md › MIDI and sync) | done | AUDIO page: the Move's input, L, R or both (line in or mic, as the Move routes it), monitor, level; USB as an input: none | [ ] |
| **SysEx**: a `.syx` sent, ON LOAD (MELODEE.md › MIDI and sync) | done | SYSEX page: knob 1 the file, 2 on load, 3 turned up sends; paced, the rest waits | [ ] |

## Project and system

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **The whole project as a `.mldp`** (MELODEE.md › Files on the card) | done | automatic | [ ] |
| **Autosave** a moment after the last change and on the way out (melodee-move README › Saving) | done | automatic | [ ] |
| **Autosave off** (issue 18): only Save writes the project; Revert goes back to its file; New, Load and Exit leave unsaved changes behind (guide/projects-and-files.md) | done | Setup > AUTOSAVE; the open project's options: SAVE, REVERT (push twice); OPEN of another with changes says CHANGES LOST (push twice); the Overview's Back says DISCARDS | [ ] |
| **Project page**: Save, Open, New (or KIT: a factory kit on track 1) (MELODEE.md › Project and system) | done | Shift + Step 1, the Move's Set Overview: projects on the pads; the wheel's push or Shift + pad: Save, Open, New (an empty pad, then Back) | [ ] |
| **Project list**: folders, each project's tracks and tempo, sorting, the open one marked (MELODEE.md › Project and system) | done | the pads, 32 a page (Plus / Minus), each project keeping its pad as a Set; folders grey (push in, Back out); its tracks and tempo, OPEN on the open one; pads in place of sorting | [ ] |
| **Project sheet**: Save, Save as new, Rename, Duplicate, Move to folder, New folder, Open at start, Delete (MELODEE.md › Project and system) | done | the wheel's push or Shift + pad: Save, Save as new, Rename, Colour, Duplicate, Move to folder, New folder, Open at start, Delete (asks); Copy + pad, then a pad (asks before replacing); Delete + pad, the pad again | [ ] |
| **Load kit (no patterns)** from another project (MELODEE.md › Project and system) | done | the project sheet of another project: Load kit, onto the selected track, one undo | [ ] |
| **A save frees unused sample slots** (MELODEE.md › Project and system) | done | the project sheet's Save (a slot no pad plays and no undo can bring back) | [ ] |
| **UNSAVED** shown while there are changes (MELODEE.md › Project and system) | done | a mark in the rail's top right corner; OPEN UNSAVED on the projects | [ ] |
| **Loading**: the sound fades, the audio thread waits while the project builds; the Loading sheet counts files (MELODEE.md › Project and system) | done | the engine parked (faded out, silent) while the project is built, then faded in; the Loading sheet counts samples and synths, presses wait | [ ] |
| **Card folders read ahead** (the MPC's exFAT driver) (MELODEE.md › Project and system) | n/a | a fix for the MPC's card driver; the Move's storage is internal | — |
| **MPC Sample projects** `.xpj` opened as a copy (MELODEE.md › Project and system) | done | listed on the pads, opened as a copy (Melodee's importer, compiled unchanged), saved as a .mldp beside it, the .xpj untouched; tested with an MPC Sample project as Melodee's own tests write one (projects-tests) | [ ] |
| **MPC 3 projects** opened, keygroup and audio tracks too (guide/projects-and-files.md) | done | as the MPC Sample's (the same importer); a keygroup track opens as a keygroup track (projects-tests: an MPC 3 project with one), audio tracks as sampling's | [ ] |
| **SD card access**: the card lent to a computer (MELODEE.md › Project and system) | n/a | no card; the web page's Files tab reaches the Move's storage | — |
| **Settings**: eight doors, a short list each; Reset Factory Settings (MELODEE.md › Project and system) | done | Shift + Step 2, the Move's Setup: doors PADS, LIGHTS (and move-midi's MIDI, SYNC, move-web's WEB PAGE); the wheel the row, push edits; Reset Factory Settings left out: the Move's own is Ableton's, Melodee's settings are few | [ ] |
| **PADS settings**: pad response (soft, medium, hard, full level), pad pressure on / off, Double Seq (MELODEE.md › Project and system) | done | Setup > PADS: response (soft, medium, hard, full level), pressure on / off; Double Seq's copy is the sequencer's | [ ] |
| **KNOBS settings**: takeover (MELODEE.md › Project and system) | n/a | relative knobs have nothing to pick up | — |
| **SCREEN settings**: palette, fonts, brightness, the place bar's place (MELODEE.md › Project and system; guide/settings.md) | n/a | a one-bit screen with one font (UI.md) | — |
| **Screen Mirror**: the MPC as a USB webcam of its screen (MELODEE.md › Project and system) | n/a | no USB video gadget on the Move's kernel; a screen view on the web page could stand in | — |
| **AUDIO settings**: sample rate 44.1, 48, 96 kHz; buffer 256-1024 frames (MELODEE.md › Project and system) | n/a | the Move runs 44.1 kHz in blocks of 128 frames, set by its XMOS link | — |
| **USB volume** (MELODEE.md › Project and system) | n/a | no USB audio beyond Ableton's own stereo (USB audio out) | — |
| **USB audio out**: what a computer records over USB-C, the mix on 1-2 and the tracks on 3-18 (MELODEE.md › Project and system; base HARDWARE.md, Stems) | n/a | Ableton's own USB audio is the XMOS's, stereo, fixed by its firmware; a second 18-channel device of Melodee's own was built and dropped (Kerem, 2026-11-21; MOVE_CONTROLS.md, Decisions) | — |
| **SAMPLING settings**: Recall grab, gap, trim end; Take auto end, after, silence below (MELODEE.md › Project and system) | done | Sampling's settings page (Shift + the wheel), kept in Move.settings | [ ] |
| **ABOUT**: each core's load, dropouts, storage room and stream misses, plug-ins' cost; versions, licence (MELODEE.md › Project and system) | done | Setup > ABOUT: the audio cycle's share and longest, late exchanges, room left, synths and their slow calls, the build | [ ] |
| **Usage log**: what the hands do, no names, a file each start (MELODEE.md › Files on the card; guide/settings.md) | done | Setup > USAGE LOG: Usage/usage-<start>.txt, a line a job (the time, the screen, the job), no names, written every 10 s | [ ] |
| **Name sheet**: pads type like phone keys (MELODEE.md › Project and system) | done | the Move's own typing (Kerem): the wheel walks a strip, push types, OK ends; pads letters, steps digits; Back deletes, held cancels | [ ] |

## Across every page

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Screen grammar and header**: a glyph and two words, back on B1, the verb on B3, sheets as cards, where you are always shown (MELODEE.md › Across every page) | done | the Rail (UI.md): rail, subject, detail line, sheets | [ ] |
| **Questions before a loss**: one sheet, what goes, whether undo brings it back (MELODEE.md › Across every page) | done | one sheet (ui::system::question): what goes, the undo chip; push yes (or the pad again), Back no | [ ] |
| **Place bar**: the bars of the pattern playing, how far it has played, coloured by state (MELODEE.md › Across every page) | done | the steps' playhead (green), Loop Mode's bars, Session's progress | [ ] |
| **Mute mode**: pads mute kit pads or tracks, Shift + pad solos, Shift + Mute unmutes all; kit pad mutes saved (MELODEE.md › Across every page) | done | Mute held + pad (the Move's 16.5), Shift + Mute held + pad solos it, Shift + Mute alone unmutes all; a tap of Mute stays the Move's (the selected track), so no latch | [ ] |
| **Mute and solo tracks** from the overview (MELODEE.md › Pad lights, Tracks) | done | track button held + Mute (+ Shift) + step; the same on the sixteen tracks latched and on the Mixer | [ ] |
| **Erase a kit pad**, stopped (MELODEE.md › Across every page, Erase) | done | Delete + pad: its notes first, then its sound (Kerem, 2026-11-20: at once) | [ ] |
| **Close a gap**: an empty pad erased moves the pads after it down, notes and lanes too (MELODEE.md › Across every page, Erase) | done | Delete + an empty pad: the pads after it to the end of the next bank down one, notes too (Melodee's shell/pad_gap.h) | [ ] |
| **Erase + pot**: the value back to its start (MELODEE.md › Across every page, Erase) | done | Delete + knob touch: the motion deleted where the knob has some (the Move's), else the value back to its start (a kit pad's; a module's values have no start known yet) | [ ] |
| **Erase tapped alone**: the page's selection (a note, step, pattern, track, loop, effect, slice, layer, project) (MELODEE.md › Across every page, Erase) | done | Delete tapped: on the projects, the one picked (asks); on Play, the Move's (the pattern, the sequencer's); each area's page its own selection | [ ] |
| **Copy kit pads**: the first pad, then the target (MELODEE.md › Across every page, Copy) | done | Copy held + pad, then a pad; onto a pad with a sample it asks | [ ] |
| **Undo and redo**: the last 32 steps; a take, a control's turns, a press each one (MELODEE.md › Across every page, Undo) | done | Undo; Shift + Undo: 32 steps, a press each, a knob's turns one, a load one; history.h | [ ] |
| **Undo list**: the steps with their glyphs and age, back to any one (MELODEE.md › Across every page, Undo) | done | Undo held: the steps with their track and age, struck through above the cursor; the wheel, push goes there | [ ] |
| **Knob takeover**: Pickup, Scaled, Instant (MELODEE.md › Across every page) | n/a | relative knobs have nothing to pick up | — |
| **Pad expression**: a pad's pressure as each note's poly aftertouch (MELODEE.md › Across every page) | done | pad pressure (Schwung modules) | [ ] |
| **Text never under 11 px**, never cut (MELODEE.md › Across every page) | n/a | one 5 x 7 font at 128 x 64 (UI.md) | — |
| **The Menu**: HERE, the page's jobs with their shortcuts; GO, the 16 places as the pads (guide/basics.md) | n/a | Kerem (2026-11-20): no Menu on the Move; Shift + the steps are the places | [ ] |
| **Lessons**: First beat, Sequences, Mix a track, Shape a pad (guide/basics.md) | n/a | Kerem (2026-11-20): no Menu on the Move, so no Learn | [ ] |
| **Basic and Full**: Basic shows the stock pages plus tracks, plug-ins, the mixer and Song (guide/settings.md) | n/a | Kerem (2026-11-20): the Move is always Full; no FEATURES in Setup | [ ] |

## Sound engines and synths

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Sampler engine** for kits (MELODEE.md › Sound engines) | done | pads | [ ] |
| **PLUGIN engine on Schwung's modules**: menus, presets, own pages in QuickJS, sources on their own thread (MELODEE.md › Sound engines; melodee-move README) | done | Row button, Menu loads; jogwheel; knobs | [ ] |
| **MIDI, keygroup and audio engines** (MELODEE.md › Sound engines) | done | new-track sheet | [ ] |
| **EngineHost**: 16 tracks, the clock, buses and master, MIDI out timing; blocks of 128 frames (MELODEE.md › Threads and data) | done | automatic | [ ] |
| **Render workers**: tracks in parallel on the spare cores (MELODEE.md › Threads and data) | mixer & effects | built behind `MELODEE_RENDER_THREADS=N` (and `melodee-render parallel|serial`), off: in the simulated Move two workers made the busy cycle slower (784-1516 µs against 579-849). Not on yet: it has to be measured on a Move first, which needs a deploy (Kerem's say-so) | [ ] |
| **Overload guard**: the busiest track gives up its oldest voice before a block runs late (MELODEE.md › Threads and data) | done | automatic | [ ] |
| **Pads at their frame**: a note played by hand sounds where it came in (MELODEE.md › Threads and data) | done | pads sound in the cycle they are read | [ ] |
| **Melodee Poly, PD and Chip**: Open MPC synths with factory presets and pad pressure (MELODEE.md › Synths) | schwung | Schwung's synths take their place | — |
| **Old PROPHET, CZ, SID tracks** open on Poly, PD and Chip (MELODEE.md › Synths) | schwung | open named and silent; pick a Schwung synth for the track | — |

## Plug-ins

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **VST3** bundles with `Contents/aarch64-linux/` (MELODEE.md › Plug-ins) | schwung | covered by Schwung's modules, the Move's plug-ins | — |
| **LV2** bundles for aarch64, presets as programs (MELODEE.md › Plug-ins, LV2) | schwung | covered by Schwung's modules, the Move's plug-ins | — |
| **CLAP** for aarch64 on Melodee's own host; presets found by discovery, cached (MELODEE.md › Plug-ins, CLAP) | schwung | covered by Schwung's modules, the Move's plug-ins | — |
| **VST2** `.so` and Open MPC plug-in folders for aarch64 (MELODEE.md › Plug-ins) | schwung | covered by Schwung's modules, the Move's plug-ins | — |
| **32-bit ARM plug-ins** through a bridge process (MELODEE.md › Plug-ins) | schwung | covered by Schwung's modules, the Move's plug-ins | — |
| **Picker**: folders, bundles of several, INSTRUMENT and EFFECT tags, the rest dimmed with the reason (MELODEE.md › Plug-ins) | done | Menu on a synth track: Schwung's modules | [ ] |
| **A plug-in instrument**: its parameters on the knobs, a step at a time, back to default (MELODEE.md › Plug-ins, An instrument) | done | a Schwung module's pages on knobs 1-8, Left / Right | [ ] |
| **Presets**: programs with names; CLAP preset files in folders (MELODEE.md › Plug-ins, Presets) | done | a Schwung module's presets on the jogwheel | [ ] |
| **Drum machine plug-ins**: voices named, the pads to Drums (MELODEE.md › Plug-ins, Drum machines) | done | Schwung's drum modules (9w9) from note 36: Down an octave | [ ] |
| **Plug-in effects** on track, bus, master and loop chains (MELODEE.md › Plug-ins, An effect) | done | Schwung's audio effect modules on track, bus, master (and the Looper's) chains, saved in their PLUG entries | [ ] |
| **Transport to plug-ins**: playing, position, tempo, time signature; TRANSPORT off (MELODEE.md › Plug-ins, Transport) | done | get_clock_status(), get_beat_position() and schwung_move_info(): playing, song beats, time signature, the tempo | [ ] |
| **Plugin Data** as the plug-ins' home folder (MELODEE.md › Plug-ins) | schwung | covered by Schwung's modules, the Move's plug-ins | — |
| **Neural Amp Modeler and TONE3000**: models and IRs from Plugin Data; capture size kept (MELODEE.md › Plug-ins) | schwung | covered by Schwung's modules, the Move's plug-ins | — |
| **Open MPC's rate bridge** for 48 and 96 kHz (MELODEE.md › Plug-ins) | n/a | the Move runs at 44.1 kHz, Open MPC's own rate | — |
| **Delay compensation**: plug-in and Limiter latencies lined up on every path, MIDI out too (MELODEE.md › Plug-ins) | schwung | covered by Schwung's modules, the Move's plug-ins | — |
| **Notes to plug-ins**: wheel, bend, poly aftertouch, CC 84 and CC 68 for slides and ties (MELODEE.md › Plug-ins, Notes) | done | Melodee's PLUGIN engine to a Schwung module: notes, pressure, CC | [ ] |
| **Idle plug-ins sleep** when silent (MELODEE.md › Threads and data) | schwung | covered by Schwung's modules, the Move's plug-ins | — |
| **Support in the binary**: GUI stubs, libsndfile, libsamplerate, FFTW, the armhf runtime (MELODEE.md › Plug-ins) | schwung | covered by Schwung's modules, the Move's plug-ins | — |
| **Saved with the project**: each plug-in's path and state (PLUG) (MELODEE.md › Plug-ins, Saving) | done | a Schwung module's state in its PLUG entry | [ ] |
| **Replace a plug-in**: asks; one undo brings the old one back with its state (MELODEE.md › Plug-ins) | done | loading another Schwung module over one asks (the question sheet); one undo brings the old one back with its values | [ ] |
| **Plug-in probe**: a command-line host to try a plug-in first (MELODEE.md › Plug-ins) | done | `--probe-modules` | [ ] |

## Files

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Projects** `.mldp` in tagged sections, written whole or not at all (MELODEE.md › Files on the card) | done | /data/UserData/Melodee/Projects | [ ] |
| **Everything under one folder** (MELODEE.md › Files on the card) | done | /data/UserData/Melodee; `--home` moves it | [ ] |
| **Folders named from outside** (`$MELODEE_*` on a host) (MELODEE.md › Files on the card) | done | `--home`, `--samples`, `--modules` | [ ] |
| **Settings file**: settings, MIDI and Input Config, the project to open at start, the last one (MELODEE.md › Files on the card) | done | Move.settings: Setup's settings, the metronome, the track played last, start= and last= (MIDI and Input Config: move-midi's, sampling's) | [ ] |
| **Samples folders**: Recorded, Edits, Banks, Looper, Expansions, Keygroups, Stems (MELODEE.md › Files on the card) | done | /data/UserData/Melodee/Samples, made at start; MELODEE among the browser's places | [ ] |
| **Cache**: decoded long samples and instrument files, 4 GB, aged out after 30 days (MELODEE.md › Files on the card) | done | /data/UserData/Melodee/Cache (Melodee's shell/sample_cache.h: tidied at start, 4 GB, 30 days) | [ ] |
| **Export folders**: Mixdowns, Stems, MIDI, MPC 3 (MELODEE.md › Files on the card; guide/song.md) | done | /data/UserData/Melodee/Mixdowns, Stems, MIDI, MPC 3 (Kerem, 2026-11-20) | [ ] |
| **SysEx folder** (MELODEE.md › Files on the card) | done | /data/UserData/Melodee/SysEx | [ ] |
| **Plugins folder**, read only (MELODEE.md › Files on the card) | schwung | covered by Schwung's modules, the Move's plug-ins | — |

## Remote and previews

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Status line** with Melodee's fields (MELODEE.md › Remote) | done | the Move's status line every 5 s; each area adds its fields | [ ] |
| **Remote presses** `melodee-press`: buttons, pads, knobs (MELODEE.md › Remote) | done | `melodee-move --ctl 'melodee-press shift+step1'` over SSH: the runtime's control socket plays them into the Move's input | [ ] |
| **`melodee-render`**: render workers off or on (MELODEE.md › Remote) | done | `melodee-move --ctl 'melodee-render serial|parallel'` | [ ] |
| **`melodee-looper`** (MELODEE.md › Remote) | done | `melodee-move --ctl 'melodee-looper status|on|off|rec N|length N 1-16|...'` over SSH: the runtime's control socket, LoopDesk::remote() | [ ] |
| **`melodee-midi`**: a message into the input; panic (MELODEE.md › Remote) | done | `melodee-move --ctl 'melodee-midi HEX...'` and `--ctl 'melodee-midi panic'`, the runtime's control socket, over SSH | [ ] |
| **`melodee-print`, `melodee-freeze`, `melodee-unfreeze`** (MELODEE.md › Print and freeze) | done | `melodee-move --ctl 'melodee-print 2 A1,A2'` (and freeze, unfreeze) over SSH: the runtime's control socket, Printer::command() | [ ] |
| **`melodee-autosample`** (MELODEE.md › Print and freeze) | done | `melodee-move --ctl 'melodee-autosample 2 36-84 step 3'` over SSH: the runtime's control socket, Studio::autosample_command (as Melodee takes its words) | [ ] |
| **`melodee-usb-input`** (MELODEE.md › Remote) | n/a | no USB audio | — |
| **`card-share`** (MELODEE.md › Remote) | n/a | no card | — |
| **Page previews** `--melodee-preview` (MELODEE.md › Previews) | done | `--melodee-previews`, `--ui-mockups` | [ ] |
| **Sound self-test** `--melodee-sound` (MELODEE.md › Previews) | done | `./build.sh` | [ ] |
| **Preview checks**: text under 11 px, the soft key and card rules (MELODEE.md › Previews) | n/a | made for the MPC's 320 x 240 pages and B1-B3 / K1-K3; the rail needs checks of its own | — |

## Web page

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **The page at melodee.local**, named by Melodee's own mDNS (MELODEE.md › Web page) | done | http://move.local:8080 from a computer on the USB-C cable (or Wi-Fi, with a code): the Move's own name (Ableton's mDNS); Melodee adds no second name. Shift + Capture shows the addresses | [ ] |
| **Port 80**: the stock web server stopped while Melodee runs and started again after (MELODEE.md › Web page) | n/a | port 80 stays Ableton's MoveWebService, which runs on while Ableton's app is stopped (its own init script); Schwung has 7700 and 7681; Melodee takes 8080, or 8081-8089 if busy (Kerem, 2026-11-20) | — |
| **Device tab**: the version, free space, the plug-ins (MELODEE.md › Web page) | done | the page's cards: Melodee's version, the storage free, the connection (USB, Wi-Fi paired); plug-ins are Schwung's modules, listed by Schwung's manager | [ ] |
| **Update** from a signed release zip (MELODEE.md › Web page) | schwung | no update of its own: Schwung installs and updates melodee-move as one of its tools (Kerem, 2026-11-20) | [ ] |
| **Plug-ins**: a zip uploaded and unzipped into Plugins; one removed (MELODEE.md › Web page) | schwung | Schwung's modules are the Move's plug-ins; its manager (port 7700) installs and removes them; there is no Melodee/Plugins folder (Kerem, 2026-11-20) | [ ] |
| **Files tab**: list, play, download, upload without overwriting, new folder, delete; protected folders (MELODEE.md › Web page) | done | Files: Melodee's folder (/data/UserData/Melodee): list, play, download, upload (never over a file: "Kick 2.wav"), rename, delete, new folders; nothing outside it, the folder itself refused | [ ] |
| **Quickstart and Manual tabs**, carried in the binary (MELODEE.md › Web page) | done | Guide: docs/GUIDE.md (the Move's controls), carried in the binary; one tab for both | [ ] |
| **Safety**: only IP or localhost hosts, an `X-Melodee` header on every POST, one request at a time (MELODEE.md › Web page) | done | a Host that is an IP, localhost or a .local name (else 421), an `X-Melodee` header on every POST (else 403), a Wi-Fi browser paired by a code; at most 8 connections at once instead of one request at a time, since the screen's stream stays open | [ ] |
| **The browser and project list look again** after a change (MELODEE.md › Web page) | done | automatic: an upload, rename, delete or new folder from the page and Load lists its folder again, Projects the projects (`melodee-files-changed`) | [ ] |

## Installing and updates

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Install and start** (the MPC: autorun on the card, `device.py install-sd`) (MELODEE.md › Installing) | done | Schwung's Tools menu; at power-on as a boot target of Schwung's boot selector (the user's choice: its power-on picker, or Schwung Manager's Boot page); or `tools/deploy-stock.sh` over SSH | [ ] |
| **Firmware check**: starts only on MPC firmware 1.3.0.12 (README › Requirements) | n/a | not an MPC | — |
| **Back to the stock app**: Exit's MPC APP (MELODEE.md › Project and system, Exit) | done | Shift + Back; Ableton's app comes back however the run ends | [ ] |
| **Recovery**: the stock app back after a crash (MELODEE.md › Threads and data) | done | `run-stock.sh`; started at power-on, a crash goes to Schwung; the 2 s boot window always reaches Schwung or stock | [ ] |
| **Power off** from the Exit sheet (MELODEE.md › Project and system, Exit) | n/a | the Move's own power button; Melodee saves on the way out | — |
| **Updates from the card**: a signed zip at the card's top (MELODEE.md › Updates from the card) | n/a | no card; updates come through the web page | — |
| **Update sheet**: Update to vX?, LATER / INSTALL, its steps, the previous version kept to fall back to (MELODEE.md › Updates from the card) | schwung | no update of its own: Schwung updates melodee-move (Kerem, 2026-11-20) | [ ] |
| **Release signing**: Ed25519 over `RELEASE.sha256` (MELODEE.md › Updates from the card) | schwung | no update of its own to sign: Schwung's installs carry melodee-move (Kerem, 2026-11-20) | [ ] |
| **Release package** (the MPC: the card's zip) (README › Install) | done | `dist/melodee-move`, the Schwung tool package | [ ] |

## Panel and lights

The MPC's other printed jobs (Shift + Pad 2-16) are rows in their own
sections, each on Shift + Step n.

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Soft keys B1-B3** (MELODEE.md › The panel) | done | Back (B1), Left / Right (pages), jogwheel push (the verb) | [ ] |
| **K1-K3 and Shift + K** (MELODEE.md › The panel) | done | knobs 1-8; Shift + knob finer | [ ] |
| **Wheel and Push**; Shift + Push goes back (MELODEE.md › The panel) | done | jogwheel and its push; Back | [ ] |
| **− / +**: octave, bank, list (MELODEE.md › The panel) | done | Up / Down | [ ] |
| **Main volume** (MELODEE.md › The panel) | done | master knob; kept in Move.settings across starts | [ ] |
| **Play and Stop** (MELODEE.md › The panel) | done | Play, a toggle | [ ] |
| **Sample Select**: the browser on a kit, the picker on a plug-in track (MELODEE.md › The panel) | done | Menu | [ ] |
| **Exit** (Power): save and back to the stock app (MELODEE.md › Project and system) | done | Shift + Back | [ ] |
| **Shift held**: the jobs lit white, toggles bright; toggles kept in the settings (MELODEE.md › Shift + pads) | done | Shift: the steps with a job lit, the metronome bright when on (move-controls); toggles kept in Move.settings | [ ] |
| **Button lights**: bright in use, faint where they act, dark where they would do nothing (MELODEE.md › Lights) | done | the Move's button lights | [ ] |
| **Track lights**: each track's colour, the selected one bright (MELODEE.md › Pad lights, Tracks) | done | Row1-4 | [ ] |
| **Bank lights** A-H (MELODEE.md › Lights) | n/a | no bank lights; the rail names the bank | — |
| **Level meters**: five lights a side, -24.1 to -0.1 dBFS; the input on Record (MELODEE.md › Lights) | done | no meter lights: the Volume knob touched (the Move's VU meter), marks at -24, -12, -6, -3, 0; the Mixer's strips; the input's is Sampling's | [ ] |
| **Kit pad lights**: the selected pad Full, a sample Dim, empty dark, muted Faint red, a flare as it sounds; five rungs that keep each colour's hue (MELODEE.md › Pad lights) | done | pad lights: dim in the pad's colour, bright while it sounds, the last pad bright in its colour and white while held or sounding (Kerem, 2026-11-21), muted dark grey (the Move's dim white), empty off (Kerem, 2026-11-20) | [ ] |
| **Key lights**: the key's roots in the track's colour, its other notes Dim, out of the key Faint (MELODEE.md › Pad lights) | done | pad lights as the Move's Keys & Scale: roots in the track's colour, the key light grey, the rest dark | [ ] |
| **Pad lights and Pad fade settings**; the Tap Tempo flash (MELODEE.md › Project and system, LIGHTS) | done | Setup > LIGHTS: pad lights (colour, white, off), pad fade (every pad fades to its new colour through the Move's RGB LED SysEx: up in a quarter of the time, down over the whole of it; at once under JACK), brightness; the Tap Tempo flash is the sequencer's tap tempo | [ ] |
| **Light show**: Pulse, Sweep, Chase, Ripple, Levels, in time with the tempo (MELODEE.md › Light show) | done | Setup > LIGHTS (Kerem: no key): Pulse, Sweep, Chase, Ripple, Levels in time with the tempo, on Play screens; a pad pressed flashes white | [ ] |

## Summary

| Status | Rows |
| --- | --- |
| `done` | 286 |
| `sequencer` | 0 |
| `sampling` | 0 |
| `kit depth` | 0 |
| `instruments` | 1 |
| `mixer & effects` | 1 |
| `song & export` | 0 |
| `looper` | 0 |
| `MIDI & sync & undo & projects` | 0 |
| `web page` | 0 |
| `schwung` | 20 |
| `n/a` | 34 |
| All rows | 342 |

# Ableton Move parity

The second half of parity: every key the Move prints keeps the meaning
Ableton's manual (https://www.ableton.com/en/move/manual/, its section in
brackets) gives it, and Melodee's own jobs move to keys the Move leaves
free, as melodee-sample's MELODEE_PARITY.md does for the MPC. The whole map,
every control and shortcut with what Melodee does on it, is
[MOVE_CONTROLS.md](MOVE_CONTROLS.md); its bindings table is the one place
the keys are handled. Below, the keys the core session (tracks, transport,
tempo) touches, and where each stands now.

| Move key | Ableton's meaning | Melodee on the Move | Status | ✓ |
| --- | --- | --- | --- | --- |
| **Play** | start / stop (20) | plays and stops the patterns | done | [ ] |
| **Shift + Play** | retrigger the playing clips from their start (17.1.5) | every playing pattern from its top (the Move's retrigger) | done | [ ] |
| **Rec** | record into the selected track's clip, after a count-in (14.1) | Seq Record, its count-in from stopped | done | [ ] |
| **Track button** | select the track (20) | selects one of the bank's four of sixteen | done | [ ] |
| **Track button held** (an unselected one) | look at that track without selecting it (20) | selects it on the press; held, the sixteen tracks on the screen, the steps picking (free on the Move) | done | [ ] |
| **Track button + Volume knob** | that track's volume, in dB (16.2) | the same (`VOL`) | done | [ ] |
| **Mute** | mute the selected track (16.4) | the same | done | [ ] |
| **Mute + track button** | mute that track (16.4) | the same; its button grey | done | [ ] |
| **Shift + Mute + track button** | solo that track, its button blue (16.3) | the same | done | [ ] |
| **Shift + track button** | the track's options (colour, MIDI channel) | selects it, its options: colour, MIDI IN, MIDI OUT (Kerem) | done | [ ] |
| **Copy / Delete + track button** | Control Live only (18.2) | free; Melodee copies and deletes with a track button held and the steps | done | [ ] |
| **Shift + Step 5** | Tempo: the wheel, Shift + wheel finer, push confirms (10.1) | the same | done | [ ] |
| **Shift + Step 6** | Metronome on / off (10.3) | the same; the wheel its level | done | [ ] |
| **Shift + Step 7** | Groove, 0 % to 100 % triplet sixteenths and past (10.2) | the same, as Melodee's swing 50-75 % | done | [ ] |
| **Shift + Step 3** | Workflow settings: count-in (13, 14.1.1) | Workflow: quantize, step grid, count-in, Rec Quantize, time signature, max length, metronome, BPM per pattern | done | [ ] |
| **Shift + Steps 1, 2, 8-11, 15, 16** | Set overview, Setup, 16 Pitches, Keys & Scale, full velocity, Repeat / Arp, double loop, quantize (20) | the same: Projects (the Set overview), Setup, 16 Pitches, Keys & Scale, Full Velocity, Repeat / Arp, Double Seq, Time Correct | done | [ ] |
| **Note / Session toggle** ("Menu", CC 50) | switch Note and Session; held, a peek (5, 17) | the same: Play and the patterns grid; held, Session peeks (Load is the wheel's push) | done | [ ] |
| **Loop** | Loop mode: the steps are bars (12.1) | the same: the pattern's Loop Mode (Chop is in Sample Options, Shift + push) | done | [ ] |
| **Volume knob** | the main volume (16.1) | the same | done | [ ] |
| **Wheel, its push, Shift + wheel** | choose, confirm, finer (20, 10.1) | the same on every screen | done | [ ] |
| **Back** | leave a menu or view (20) | the same | done | [ ] |
| **Undo, Shift + Undo** | undo, redo (20) | the same, every step named; held, the undo list | done | [ ] |
| **Capture** | the last notes played into a clip (14.3) | Melodee's Recall of notes; Shift + Capture clears it | done | [ ] |
| **Sample** ("Sampling") | Sampling mode (15.1) | Melodee's Sample Record (a pad records onto itself), Audio Recall on Shift + Sampling; an audio track's INPUT | done | [ ] |
