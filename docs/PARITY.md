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
  Delete + step deletes it, an empty step opens the new-track sheet.
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
| **Drum sources**: Card (opens the browser), Empty, Input (opens Record) (guide/tracks.md) | sampling | new-track sheet, second step | [ ] |
| **MIDI track ports** on the new-track sheet: Out jack, USB-C, All USB (guide/tracks.md) | n/a | one port on the Move: a device on USB-A and a computer on USB-C arrive together on cable 2, so there is nothing to pick (docs/MIDI.md) | — |
| **Delete a track**: sound, kit, patterns; its mix strip stays; asks (its undo: MIDI & sync & undo & projects) (MELODEE.md › The panel, Pad Bank) | done | track button held + Delete + step | [ ] |
| **Copy a track**: sound, kit, 128 patterns, motion; the target keeps its strip; a synth gets its own instance of the module (MELODEE.md › Across every page, Copy) | done | track button held + Copy + step, then a step | [ ] |
| **Move a track**: trades places; song clips, keys, lanes and sources follow it (MELODEE.md › Across every page, Move) | MIDI & sync & undo & projects | track button held + Shift + Copy + step, then a step | [ ] |
| **Stage**: each track's home pages, B1 source, B2 how it plays, B3 where it goes (MELODEE.md › Playing) | done | Shift + the jogwheel: a kit's nine pad pages (TRIM, LAYERS, TUNE, AMP ENV, PLAY, CLOUD, MIX, FILTER, KIT FILT, docs/UI.md "Kits"), a module's pages; the Play screen's knobs first | [ ] |
| **Synth presets**: walked, loaded a moment after the last turn (MELODEE.md › Playing, A synth) | done | jogwheel (Schwung modules) | [ ] |
| **Synth EDIT**: the instrument's own parameters and page map (MELODEE.md › Playing, Edit) | done | knobs 1-8, Left / Right; push: the module's menu | [ ] |
| **Macro knobs FILTER, ECHO, SPACE** on every plug-in track, saved, recorded as motion (MELODEE.md › Playing, A synth) | instruments | a page of knobs on module tracks | [ ] |
| **ARP**: Up, Down, Up-down, Random, Order; rate 1/4-1/32T; octaves; gate; hold (MELODEE.md › Playing) | instruments | ARP page, knobs 1-6 | [ ] |
| **CHORD**: a pad a chord of the scale; type, voicing, inversion, hold (MELODEE.md › Playing) | instruments | CHORD page, knobs 1-5 | [ ] |
| **MIX → SENDS → FX pages** of a track: level, pan, send A, meter; four sends and returns; the chain's slot in hand (MELODEE.md › Playing) | done | the Device View's track item: knobs 1-6 level, pan, sends A-D, its meter and CPU share; its effects the next items; the returns on the Mixer | [ ] |
| **Pads as notes** on a synth: rows of fourths, octave, pressure as poly aftertouch (MELODEE.md › Playing, Keys) | done | 32 pads; Up / Down the octave | [ ] |
| **Keys**: root, scale, layout (Scale, Chromatic, Chords, Prog, Drums), transpose ±12, roots lit (MELODEE.md › Playing, Keys) | instruments | Keys page knobs; pad lights | [ ] |
| **Project key and scale**, set from any track (MELODEE.md › Playing, Tempo) | instruments | tempo screen, knobs 4-5 | [ ] |
| **Prog**: MPC 3 `.progression` files, a chord a pad (48 at most), key, voicing (MELODEE.md › Playing, Prog) | instruments | pads; jogwheel walks the progressions | [ ] |
| **Chord builder**: held keys stored onto a pad, saved as Mine N (MELODEE.md › Playing, Prog) | instruments | Keys page: push stores | [ ] |
| **Drums layout**: a drum plug-in's or MIDI drum machine's pads from note 36, voices named (MELODEE.md › Playing, Drums) | instruments | 32 pads are notes 36-67; Up / Down the bank | [ ] |
| **Pads page**: the bank's pads each drawing its waveform (MELODEE.md › Playing, Pads) | n/a | 128 x 64 cannot show 32 waveforms; the pads light in their colours and the screen shows the last pad | — |
| **Sounds**: a MIDI track's saved sounds (`.mlds`), favourites (MELODEE.md › Playing, Sounds) | done | the wheel's push on a MIDI track's sound: its own and the saved `.mlds` (Melodee/Sounds), loaded as the wheel turns; Shift held: save, star, show starred or recent | [ ] |
| **16 Levels**: the last pad or note on every pad as Velocity, Filter, Tune, Scale or Zones; ends, octave; a MIDI keyboard plays the pad on every key (MELODEE.md › Playing, 16 Levels) | done | as the Move's 16 Pitches (Shift + Step 8): the last pad on the right 16 in the project's key (Melodee's Scale; Kerem, 2026-11-20), Plus / Minus the octave; the left 16 one bank. Velocity, Filter, Tune and Zones are not on the Move's Layout | [ ] |
| **Note Repeat**: 1/4-1/64 and triplets, swung, pressure as velocity, written under Seq Record (MELODEE.md › Playing, Note Repeat) | done | Shift + Step 11: on, its screen (style Repeat / arp Up, Down, Random; rate 1/4-1/64T; pressure as velocity); again off; repeats written under Rec | [ ] |
| **Tempo**: BPM, swing, metronome level, metronome on / off (MELODEE.md › Playing, Tempo) | done | Shift + Step 5 / 6 / 7: tempo, metronome, groove, as on the Move | [ ] |
| **Tap tempo, halve, double; metronome Rec** (clicks only while recording) (MELODEE.md › The panel; Playing, Tempo) | done | Tempo (Shift + Step 5): tap the steps, Left / Right halve, double; Workflow: Metronome Always / Rec only (Kerem, 2026-11-20) | [ ] |
| **Track speed**: ×½, ×¾, ×1, ×1½, ×2 against the tempo (MELODEE.md › Playing, Tracks) | done | SEQ pages (Shift + Loop), PATTERN: SPD, knob 4 | [ ] |
| **Keygroup track pages**: TRIM / ZONES, TUNE, AMP ENV, FILTER, FILT ENV, PLAY, ARP, CHORD (guide/instrument-and-midi-tracks.md) | instruments | Left / Right pages, knobs 1-8; Menu loads | [ ] |
| **A keygroup of one sample** played from the note found in it (guide/instrument-and-midi-tracks.md) | instruments | pads as keys | [ ] |
| **Audio track INPUT**: source, monitor, gain; armed by Seq Record, one pass of the pattern recorded (guide/audio-tracks.md) | sampling | Sample on an audio track; Rec arms; line in or mic | [ ] |
| **Audio track takes**: CLIP (level, start, end), FADES (in, out, reverse), WARP (Stretch, Repitch, BPM, semi); kept in `<project> Audio` (guide/audio-tracks.md) | sampling | Left / Right pages, knobs | [ ] |

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
| **Sequences and sequence chains**: pattern n of every track, a name and tempo of its own; several in one hold (SCNE) (MELODEE.md › Sequencing, Sequences) | done | Session: push, the column's sheet: knob 1 its tempo, knob 2 a ready name; pads of other columns chain sequences; push launches | [ ] |
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
| **Parameter locks**: hold a step, turn a knob (MELODEE.md › Sequencing, Step Edit) | sequencer | a step held + a knob (the Move's per-step automation): a kit pad's values and the mix's lanes. Not yet on a Schwung module's own parameters: they are not engine lanes, and need a module motion player | [ ] |
| **Lane length**: a kit pad's lane loops on its own, polymeter (LANE) (MELODEE.md › Sequencing, Step Edit) | done | SEQ pages' LANE: knob 1 | [ ] |
| **Euclid**: hits, steps 1-64, rotation on a lane (EUCL) (MELODEE.md › Sequencing, Step Edit) | done | SEQ pages' LANE: knobs 2-4, push lays it | [ ] |
| **Spare steps**: a step written past the end makes the pattern longer (MELODEE.md › Sequencing, Step Edit) | done | Right past the last bar, then a step | [ ] |
| **Roll**: notes at their ticks over the bars; the note in focus with length, velocity, chance, nudge (MELODEE.md › Sequencing, Roll) | done | SEQ pages' ROLL: the wheel the note, knobs 1-8 | [ ] |
| **Roll marks and jobs**: mark notes; move by Q or a row, mute, ratchet, velocities and ramp, pull to the grid, octave; split, legato, reverse, the arp as notes, loop the bars in view (MELODEE.md › Sequencing, Roll) | done | SEQ pages' ROLL: push, the jobs (marks, mute, pull to grid, legato, split, reverse, arp as notes, loop the bar, ramps, insert); knobs move by Q (MOV) or a row (KEY), octave, ratchet | [ ] |
| **Copy notes**: marked notes pasted at the cursor; a key's notes onto another; a step onto another (MELODEE.md › Across every page, Copy) | done | Roll: Copy the marked notes, Copy again pastes at the focus; MARK THE KEY then KEY moves a key's notes; Copy + a step, then a step | [ ] |
| **Erase notes**: a pad's notes while playing, a key's notes, the note in focus (MELODEE.md › Across every page, Erase) | done | Delete + pad (a kit); Roll: MARK THE KEY, Delete; Delete on Roll the note in focus | [ ] |
| **Motion**: knob moves recorded in each pattern, 8192 points, lanes reset at the pattern's start; a lane cleared (MELODEE.md › Sequencing, Motion) | sequencer | Rec + knob turns (14.2) on a kit's pages and the track's level; Delete + knob touch clears, Mute + touch off and on; SEQ pages' MOTION. Not yet a Schwung module's parameters (as Parameter locks) | [ ] |
| **Mix in Motion**: level, pan, sends, master chain and returns recorded (MELODEE.md › Sequencing) | mixer & effects | the track's level done (track button + Volume knob while recording); pan, sends, master chain and returns come with the mixer's pages: their knobs name their lanes (Screen::knob_lane) and Rec writes them | [ ] |
| **MOD**: eight modulators, an LFO (seven shapes, free or synced) or a follower, up to four lanes each (MODS) (MELODEE.md › Sequencing) | done | SEQ pages' MOD: the wheel the modulator, knobs kind, shape, rate, sync, depth, target, low, high; push the target slot | [ ] |
| **Macros**: eight, each one knob over up to eight lanes (MACR) (MELODEE.md › Sequencing, MOD) | done | SEQ pages' MACROS: a knob each; push: the lanes the last one moves | [ ] |
| **A pattern's feel**: STRENGTH, SWING, HUMANIZE, HUMAN VEL, GROOVE from another pattern (FEEL) (MELODEE.md › Sequencing) | done | SEQ pages' FEEL: knobs 1-6 | [ ] |
| **Time Correct**: heard live, no commit; ALL lays the feel on the other patterns (MELODEE.md › Sequencing) | done | SEQ pages' FEEL (heard live), push: ALL; Shift + Step 16 is the Move's Quantize (it rewrites the notes, Workflow's amount) | [ ] |
| **Half Seq, Double Seq, Half Speed, Double Speed** (MELODEE.md › Sequencing) | done | Shift + Step 15 doubles (the Move's); SEQ pages' PATTERN push: Half Seq, Double Seq, Half Speed, Double Speed | [ ] |
| **Recall**: every note played is kept; the last phrase into empty patterns, one undo (MELODEE.md › Sequencing, Recall) | done | Capture (14.3): every track's last phrase into its next empty pattern, playing; from stopped three tempos on the wheel; Shift + Capture clears | [ ] |
| **Song**: a lane per track, clips of patterns, markers, loop brace, cursor; play from the cursor (MELODEE.md › Sequencing, Song) | song & export | Shift + Step 12; jogwheel by the bar; knobs zoom, pattern, length | [ ] |
| **Song clip jobs**: place, duplicate, mute, own pattern, open notes, split, delete, repeat to a marker, loop its bars (MELODEE.md › Sequencing, Song) | song & export | push sheet; Copy, Delete, Loop | [ ] |
| **Markers**: add, name, jump (MELODEE.md › Sequencing, Song) | song & export | push sheet; Up / Down jump | [ ] |
| **Tempo lane**: tempo points by the bar, step or ramp, 64 points (TMPL) (MELODEE.md › Sequencing, Song) | song & export | knobs on Song | [ ] |
| **Record into the song**: launches written as clips, punches too (guide/song.md) | song & export | Rec while the song plays | [ ] |
| **Mixdown export**: 32-bit float, or 24 or 16-bit dithered; tails up to 10 s (MELODEE.md › Sequencing, Song) | song & export | Song: HERE > Export | [ ] |
| **Stems export**: `Mix.wav`, each track and each bus after its chain (MELODEE.md › Sequencing, Song) | song & export | Song: HERE > Export | [ ] |
| **MIDI file export**: format 1, 96 ticks, tempo and time signature (MELODEE.md › Sequencing, Song) | song & export | Song: HERE > Export | [ ] |
| **MPC 3 project export**: drum, keygroup, audio and MIDI tracks; plug-in tracks as their prints (guide/song.md) | song & export | Song: HERE > Export | [ ] |
| **Song into a new pattern**, laid end to end (MELODEE.md › Sequencing, Song) | song & export | Song: HERE > Export | [ ] |
| **A `.mid` file loaded** onto the selected track's pattern (MELODEE.md › Sequencing, Song) | song & export | Menu (load) | [ ] |

## Print and freeze

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Freeze**: each pattern printed; the track plays its prints; pads still live; level, pan, sends, mute live; a kit's bus sends printed too (MELODEE.md › Print and freeze) | song & export | Sample on a module track: the PRINT sheet | [ ] |
| **PRINT sheet**: patterns on the pads, ALL, TAP Post / Pre FX, TAIL Loop / Ring / Cut, room needed, CPU saved (MELODEE.md › Print and freeze) | song & export | pads; knobs 1-2; push freezes | [ ] |
| **Printing**: progress a pattern, the switch on a bar line, called off (MELODEE.md › Print and freeze) | song & export | Back calls it off | [ ] |
| **Freeze patterns from the pattern layer** (Seq held + Sample Record) (MELODEE.md › Print and freeze) | song & export | pattern layer + Sample | [ ] |
| **A frozen track shown**: a snowflake on the overview and pages; values a print holds dim (MELODEE.md › Print and freeze) | song & export | screen; step lights | [ ] |
| **Deep freeze**: the track's plug-ins unloaded too; thawing restores their state (MELODEE.md › Print and freeze) | song & export | PRINT sheet: Shift + push | [ ] |
| **Unfreeze and edit**: at once, prints kept; an edit asks first; unchanged patterns keep their prints (MELODEE.md › Print and freeze) | song & export | PRINT sheet; question sheet | [ ] |
| **Print host**: faster than real time on a spare core, lowest priority, into `<project> Prints` (MELODEE.md › Print and freeze) | song & export | automatic | [ ] |
| **Auto sample a plug-in** into an MPC keygroup program (`.xpm`, `.xty`) on a new keygroup track (MELODEE.md › Print and freeze, Auto sample) | instruments | Shift + Step 11 on a module track | [ ] |
| **Auto sample check**: drums, mono and glide, accent, velocity layers, takes, splits, sustain and loops, release fit (MELODEE.md › Print and freeze) | instruments | automatic | [ ] |
| **AUTO SAMPLE sheet**: KEYS (low, high, step), NOTE (layers, hold, tail), SOUND (tap, loop, level) (MELODEE.md › Print and freeze) | instruments | knobs 1-8 and a second page | [ ] |
| **Auto sample an external instrument** on a MIDI track, heard at the input (MELODEE.md › Print and freeze, External instruments) | instruments | Shift + Step 11 on a MIDI track; USB MIDI out, line in | [ ] |
| **Keygroup glide**: Mono, portamento, Legato, Mono Retrig (MELODEE.md › Print and freeze, Keygroup glide) | instruments | automatic | [ ] |
| **Delete from storage**: a file, folder, program or plug-in; what the project plays is refused (MELODEE.md › Print and freeze, Deleting) | sampling | Delete on the Load page | [ ] |

## Sampler

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **One kit of 32 pads** on Melodee's SamplerEngine, A01-B16 (MELODEE.md › Sampler) | done | pads | [ ] |
| **Kits of 128 pads** in banks (the MPC's A-H of 16; here four of 32) (MELODEE.md › Sampler) | done | the 32 pads two of eight banks of 16 (Kerem, 2026-11-20); Plus / Minus move 16; the rail's pad names its bank | [ ] |
| **No count of samples**: memory is the limit; a load that would leave too little is refused, "Memory full" (MELODEE.md › Sampler) | done | automatic: Melodee's shell/sample_memory.h, 256 MB left free; `MEMORY FULL` | [ ] |
| **Load samples**: WAV, AIFF, FLAC, Ogg, MP3, from places (My Samples, Recordings, Core Library, /data/UserData) (MELODEE.md › Sampler, Samples) | done | Menu; jogwheel, push; Back up | [ ] |
| **Long samples streamed** from storage, up to 6 hours; long MP3, FLAC, Ogg, AIFF decoded once into Cache (MELODEE.md › Sampler) | done | automatic: over 10 s (up to 6 hours), or what memory has no room for, read in place by Melodee's stream pager; long MP3, FLAC, Ogg, AIFF via Cache | [ ] |
| **PREVIEW**: each file plays as the cursor lands on it, streamed (MELODEE.md › Sampler, Samples) | sampling | Load page: Shift + push toggles | [ ] |
| **Load onto a chosen pad** (the MPC: Shift + pad onto the next empty one) (MELODEE.md › Sampler, Samples) | sampling | Load page: tap a pad | [ ] |
| **STARRED and RECENT**: star a file; the last 16 loaded (MELODEE.md › Sampler, Samples) | sampling | Load page: HERE > Star | [ ] |
| **SFZ instruments**: zones, round robin, layers, release samples, crossfades, keyswitches (MELODEE.md › Sampler) | instruments | Menu (load) | [ ] |
| **MPC keygroup programs** `.xpm`, and MPC 3 programs `.xty` (MELODEE.md › Sampler) | instruments | Menu (load) | [ ] |
| **EXS24 instruments** `.exs` (MELODEE.md › Sampler) | instruments | Menu (load) | [ ] |
| **SoundFonts** `.sf2`, presets listed as a folder (MELODEE.md › Sampler) | instruments | Menu (load) | [ ] |
| **DecentSampler presets** `.dspreset` (MELODEE.md › Sampler) | instruments | Menu (load) | [ ] |
| **An instrument on a kit pad**: its zones by key and velocity; ZONES page, zone tune and level trims (ZTRM) (MELODEE.md › Sampler, SFZ) | kit depth | not yet: an instrument reaches a pad by the instruments' loading of SFZ, programs and SoundFonts (`instruments` rows); ZONES (TRIM's place on such a pad, docs/ui/kit.png) is built once it can | [ ] |
| **MPC drum programs**: LOAD KIT sheet, the whole kit or banks, layers, pad colours (MELODEE.md › Sampler) | done | the browser lists them tagged KIT; push: LOAD KIT (whole kit, or a program bank into each kit bank, LOAD BANKS); pads of 2-4 layers take them as their own, more (an instrument) wait for the instruments' loading | [ ] |
| **MPC expansions** `.xpn`: installed into Expansions; program previews (MELODEE.md › Sampler) | sampling | Menu (load); push | [ ] |
| **Banks** `.mldb`: Save Bank, load into the bank in view (MELODEE.md › Sampler, Banks) | done | Shift held in the browser: Save Bank (the two banks in view); `.mldb` files listed tagged BANK, a push loads one into the bank of the pad in view | [ ] |
| **Pad sound knobs**: tune, level, pan, start, end, attack, decay, cutoff (MELODEE.md › Sampler, sample pages) | done | PLAY page, knobs 1-8; Shift finer | [ ] |
| **TRIM**: loop point, zoom at each handle, Loop Lock, Normalize (MELODEE.md › Sampler, Trim) | done | TRIM page: start, end, loop point, Loop Lock, gain; Shift + a touch: the handle close up; Normalize in Sample Options | [ ] |
| **LAYERS**: four samples a pad, Velocity, Cycle or Random; each layer's tune, level, velocities (MELODEE.md › Sampler, Layers) | done | LAYERS page: knob 1 the layer, its velocities, tune, fine, level, pan, how a hit picks; push loads the layer's sample; Sample Options removes it | [ ] |
| **Kit volume** (MELODEE.md › Sampler, Mix) | done | MIX page, knob 3 | [ ] |
| **Fine tune and Warp**: cents; Warp Off, 50-200 %, Seq from # Beats (MELODEE.md › Sampler, Tune) | done | TUNE page: tune, fine, Warp (Off, Seq, 50-200 %), # Beats; Shift + touch TUNE on Play: cents | [ ] |
| **Warp mode**: Time stretch or Pitch for the kit (MELODEE.md › Shift + pads) | done | TUNE page, Follow's mode (Tape is Pitch, Beats and Tones Time stretch): Shift + Step 15 is the Move's double loop | [ ] |
| **Amp envelope**: release on Note On, velocity sensitivity, decay from start or end (MELODEE.md › Sampler, Amp Env) | done | AMP ENV page: attack, decay, sustain, release, velocity sense, decay from | [ ] |
| **Play modes**: Mono / Poly, mute group 1-16, offset, Note On, reverse, loop, Pad Link chains (MELODEE.md › Sampler, Play) | done | PLAY page: One Shot / Note On, Mono / Poly, mute group, offset, Pad Link, loop, reverse | [ ] |
| **Loop seam crossfade** XFADE, 0-250 ms (MELODEE.md › Sampler, Play) | done | PLAY page, knob 8 | [ ] |
| **Filter type, resonance and envelope**: Classic, LPF2/4, HPF2/4, BPF2/4; up to seven octaves (MELODEE.md › Sampler, Filter) | done | FILTER page: type, cutoff, resonance, envelope amount, attack, decay, sustain, release | [ ] |
| **Follow**: the kit keeps the project's tempo, Beats, Tones or Tape; # Beats (MELODEE.md › Sampler) | done | TUNE page: Follow, its mode (Beats, Tones, Tape), Pitch; # Beats | [ ] |
| **Cloud**: a pad as a grain cloud; position, size, density, scan, spread (MELODEE.md › Playing, A kit) | done | CLOUD page: on, position, size, density, scan, spread | [ ] |
| **Kit filter**: one filter after every pad (MELODEE.md › Sampler) | done | KIT FILT page: type, cutoff, resonance | [ ] |
| **COLOUR**: a pad's light colour, the track's, one of sixteen, or the kit's own (MELODEE.md › Playing, A kit) | done | MIX page, knob 4: the track's, one of the sixteen track colours, or the pad's own; pad lights | [ ] |
| **Full Level**: every hit at 127 (MELODEE.md › Shift + pads) | kit depth | not here: Shift + Step 10 is the Move's Full Velocity, the `instruments` row in MOVE_CONTROLS.md; one flag in Studio::audio_event for every track, kits too | [ ] |
| **Pad sends** into buses A-D, and the kit's send (MELODEE.md › Playing, A kit) | done | a kit's track item, Shift + turn: the last pad's own sends on knobs 1-4; the kit's (the track's) on its first page | [ ] |
| **A pad's own chain** of four effects (MELODEE.md › Effects) | done | the Device View: after the pad, its own effects and their `+` (a dotted underline) | [ ] |
| **Chop**: Auto, Beats, Regions, Threshold; a slice's start and end (MELODEE.md › Sampler, Chop) | done | Loop; knobs 1-4; jogwheel or Left / Right the slice | [ ] |
| **Chop Manual**: pads tapped as it plays, cuts pulled onto hits (MELODEE.md › Sampler, Chop) | sampling | Chop: the type knob on Manual; pads | [ ] |
| **Slices 33-64** on a second bank (MELODEE.md › Sampler, Chop) | sampling | Up / Down in Chop | [ ] |
| **Chop close up**: the view follows the cut; a moved cut plays its slice; Shift + wheel jumps from hit to hit (MELODEE.md › Sampler, Chop) | sampling | jogwheel; Shift + jogwheel | [ ] |
| **Extract, Split, Merge**; a slice taken out; every cut out (MELODEE.md › Sampler, Chop) | sampling | Copy (Extract); Delete; Delete + Loop | [ ] |
| **CLEAN**: cut on the sample's drums stem (MELODEE.md › Sampler, Chop) | schwung | Schwung's separator writes the stems as files: Load the drums stem and Chop it | — |
| **To Pads**: Poly, Mono, Thru (MELODEE.md › Sampler, Chop) | done | Chop: push; jogwheel | [ ] |
| **To Pads KIT**: a pad a sound (MELODEE.md › Sampler, Chop) | sampling | To Pads sheet | [ ] |
| **To Pads PATTERN**: a note for each slice into the pattern (MELODEE.md › Sampler, Chop) | sequencer | Not yet on the sheet: To Pads is sampling's Chop (screens/chop.cpp); Sequencer::slices_to_pattern() lays the notes, tested, for its PATTERN choice | [ ] |
| **Slices kept with the sample** (in its WAV on the MPC; here in the project, its own `MVCH` section, so the Move's library is never written) (MELODEE.md › Sampler, Chop) | done | automatic | [ ] |
| **Zero Snap and 2 ms edge fades** on every cut (MELODEE.md › Sampler, Chop) | done | automatic | [ ] |
| **A sample's note** found as it loads: a pitch, or a chord's root (MELODEE.md › Sampler) | instruments | automatic | [ ] |
| **Trim Sample**: the region into a new file, the pad onto all of it (MELODEE.md › Sampler) | sampling | Shift + Step 13 | [ ] |
| **Save Sample** under a name (MELODEE.md › Sampler) | sampling | HERE > Save sample; pad keyboard | [ ] |
| **Input Config**: source, rec length (Free, Seq, 1-16 bars), threshold, gain, Post / Pre FX, monitor Off / Auto / On, Rec Input FX (MELODEE.md › Sampler) | sampling | Shift + Sample; knobs; the Move's mic or line in | [ ] |
| **USB audio in** (USB, USB L, USB R) (MELODEE.md › Sampler, Input Config) | n/a | no USB audio on the Move's stock firmware | — |
| **Record**: tap a pad to record onto it; threshold start; Seq length; other pads cut the take; written as recorded, up to 6 hours (MELODEE.md › Sampler, Record) | sampling | Sample (armed), then a pad | [ ] |
| **Sampling Melodee's own sound**: a track or Resample as the source, the pads playing it (MELODEE.md › Sampler, Record) | sampling | Input Config source | [ ] |
| **Audio Recall**: the input's last 25 s onto the next empty pad (MELODEE.md › Sampler) | sampling | Capture | [ ] |
| **Resample**: the pattern with every track onto a pad; on Song, the whole song (MELODEE.md › Sampler) | sampling | Shift + Step 11, then a pad | [ ] |
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
| **Mod wheel** from the panel (MELODEE.md › Fader) | instruments | a knob on module tracks, or pad pressure | [ ] |
| **Pitch bend** from the panel, back to the middle when let go (MELODEE.md › Fader) | instruments | a knob that springs back when let go (touch) | [ ] |
| **Fader takeover**: Pickup, Scaled, Instant (MELODEE.md › Fader) | n/a | relative knobs have nothing to pick up | — |
| **The fader's lane and light**: Seq Record writes it, Erase + fader clears it; its light shows its value (MELODEE.md › Fader) | n/a | no fader; knobs record motion (Sequencing) | — |

## MIDI and sync

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **MIDI Config**: port, in and out channels, sync, thru, program change, takeover rows, panic, reset (MELODEE.md › MIDI and sync) | done | Setup's MIDI and MIDI SYNC rows (Shift + Step 2; until move-projects' Setup is merged, Shift + Step 2 opens it): channels, pad in and out, sync in and out, Thru, program change, Remote, PANIC. Port: one; takeover: none, the knobs are endless encoders; reset: Settings (move-projects) | [ ] |
| **MIDI jacks** (the External port) (MELODEE.md › MIDI and sync) | n/a | the Move has no MIDI jacks; USB-C only | — |
| **USB MIDI**: a computer or USB devices on USB-C (MELODEE.md › MIDI and sync) | done | USB-A devices and a computer on USB-C (the Standalone Port), both on cable 2 (docs/MIDI.md); to check on the Move | [ ] |
| **MIDI in**: notes on the in channel play the selected track; a kit hears 36 as A01; pressure, mod wheel, pitch wheel, sustain pedal, CCs, All Notes Off (MELODEE.md › MIDI and sync; guide/midi-and-sync.md) | done | automatic, every page and track kind; a MIDI track echoes it | [ ] |
| **MIDI in recorded**: under Seq Record; keys write on Step Edit and Roll (guide/midi-and-sync.md) | sequencer | MIDI notes reach the engine as the pads' do (to the frame they came at); recording them is Seq Record's (move-seq) | [ ] |
| **Track channel, Track MIDI in, Pad MIDI in / out**: a controller a track (guide/midi-and-sync.md) | done | Shift + a track button: MIDI IN and MIDI OUT (Kerem); Pad MIDI in and out in MIDI Config; Track MIDI in's ports: one port | [ ] |
| **Remote CCs**: select a track, launch a pattern or a sequence (CC 102-104) (guide/midi-and-sync.md) | done | MIDI Config: Remote channel and CCs | [ ] |
| **Program change** launches that pattern (MELODEE.md › MIDI and sync) | done | MIDI Config: PROG CHANGE | [ ] |
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
| **Project page**: Save, Open, New (or KIT: a factory kit on track 1) (MELODEE.md › Project and system) | MIDI & sync & undo & projects | Shift + Step 16, or Places > Project | [ ] |
| **Project list**: folders, each project's tracks and tempo, sorting, the open one marked (MELODEE.md › Project and system) | MIDI & sync & undo & projects | jogwheel; push; Back | [ ] |
| **Project sheet**: Save, Save as new, Rename, Duplicate, Move to folder, New folder, Open at start, Delete (MELODEE.md › Project and system) | MIDI & sync & undo & projects | push sheet | [ ] |
| **Load kit (no patterns)** from another project (MELODEE.md › Project and system) | MIDI & sync & undo & projects | project sheet | [ ] |
| **A save frees unused sample slots** (MELODEE.md › Project and system) | MIDI & sync & undo & projects | automatic | [ ] |
| **UNSAVED** shown while there are changes (MELODEE.md › Project and system) | MIDI & sync & undo & projects | the rail | [ ] |
| **Loading**: the sound fades, the audio thread waits while the project builds; the Loading sheet counts files (MELODEE.md › Project and system) | MIDI & sync & undo & projects | screen | [ ] |
| **Card folders read ahead** (the MPC's exFAT driver) (MELODEE.md › Project and system) | n/a | a fix for the MPC's card driver; the Move's storage is internal | — |
| **MPC Sample projects** `.xpj` opened as a copy (MELODEE.md › Project and system) | MIDI & sync & undo & projects | the project list | [ ] |
| **MPC 3 projects** opened, keygroup and audio tracks too (guide/projects-and-files.md) | MIDI & sync & undo & projects | the project list | [ ] |
| **SD card access**: the card lent to a computer (MELODEE.md › Project and system) | n/a | no card; the web page's Files tab reaches the Move's storage | — |
| **Settings**: eight doors, a short list each; Reset Factory Settings (MELODEE.md › Project and system) | MIDI & sync & undo & projects | Shift + Step 8; jogwheel the row, a knob sets it | [ ] |
| **PADS settings**: pad response (soft, medium, hard, full level), pad pressure on / off, Double Seq (MELODEE.md › Project and system) | MIDI & sync & undo & projects | Settings | [ ] |
| **KNOBS settings**: takeover (MELODEE.md › Project and system) | n/a | relative knobs have nothing to pick up | — |
| **SCREEN settings**: palette, fonts, brightness, the place bar's place (MELODEE.md › Project and system; guide/settings.md) | n/a | a one-bit screen with one font (UI.md) | — |
| **Screen Mirror**: the MPC as a USB webcam of its screen (MELODEE.md › Project and system) | n/a | no USB video gadget on the Move's kernel; a screen view on the web page could stand in | — |
| **AUDIO settings**: sample rate 44.1, 48, 96 kHz; buffer 256-1024 frames (MELODEE.md › Project and system) | n/a | the Move runs 44.1 kHz in blocks of 128 frames, set by its XMOS link | — |
| **USB volume** (MELODEE.md › Project and system) | n/a | no USB audio | — |
| **SAMPLING settings**: Recall grab, gap, trim end; Take auto end, after, silence below (MELODEE.md › Project and system) | sampling | Settings | [ ] |
| **ABOUT**: each core's load, dropouts, storage room and stream misses, plug-ins' cost; versions, licence (MELODEE.md › Project and system) | MIDI & sync & undo & projects | Settings > About | [ ] |
| **Usage log**: what the hands do, no names, a file each start (MELODEE.md › Files on the card; guide/settings.md) | MIDI & sync & undo & projects | Settings | [ ] |
| **Name sheet**: pads type like phone keys (MELODEE.md › Project and system) | MIDI & sync & undo & projects | the pad keyboard module pages use (letters on pads, digits on steps) | [ ] |

## Across every page

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Screen grammar and header**: a glyph and two words, back on B1, the verb on B3, sheets as cards, where you are always shown (MELODEE.md › Across every page) | done | the Rail (UI.md): rail, subject, detail line, sheets | [ ] |
| **Questions before a loss**: one sheet, what goes, whether undo brings it back (MELODEE.md › Across every page) | MIDI & sync & undo & projects | push yes, Back no | [ ] |
| **Place bar**: the bars of the pattern playing, how far it has played, coloured by state (MELODEE.md › Across every page) | done | the steps' playhead (green), Loop Mode's bars, Session's progress | [ ] |
| **Mute mode**: pads mute kit pads or tracks, Shift + pad solos, Shift + Mute unmutes all; kit pad mutes saved (MELODEE.md › Across every page) | done | Mute held + pad (the Move's 16.5), Shift + Mute held + pad solos it, Shift + Mute alone unmutes all; a tap of Mute stays the Move's (the selected track), so no latch | [ ] |
| **Mute and solo tracks** from the overview (MELODEE.md › Pad lights, Tracks) | done | track button held + Mute (+ Shift) + step; the same on the sixteen tracks latched and on the Mixer | [ ] |
| **Erase a kit pad**, stopped (MELODEE.md › Across every page, Erase) | done | Delete + pad: its notes first, then its sound (Kerem, 2026-11-20: at once) | [ ] |
| **Close a gap**: an empty pad erased moves the pads after it down, notes and lanes too (MELODEE.md › Across every page, Erase) | done | Delete + an empty pad: the pads after it to the end of the next bank down one, notes too (Melodee's shell/pad_gap.h) | [ ] |
| **Erase + pot**: the value back to its start (MELODEE.md › Across every page, Erase) | MIDI & sync & undo & projects | Delete + knob touch | [ ] |
| **Erase tapped alone**: the page's selection (a note, step, pattern, track, loop, effect, slice, layer, project) (MELODEE.md › Across every page, Erase) | MIDI & sync & undo & projects | Delete tapped | [ ] |
| **Copy kit pads**: the first pad, then the target (MELODEE.md › Across every page, Copy) | done | Copy held + pad, then a pad; onto a pad with a sample it asks | [ ] |
| **Undo and redo**: the last 32 steps; a take, a control's turns, a press each one (MELODEE.md › Across every page, Undo) | MIDI & sync & undo & projects | Undo; Shift + Undo | [ ] |
| **Undo list**: the steps with their glyphs and age, back to any one (MELODEE.md › Across every page, Undo) | MIDI & sync & undo & projects | Undo held | [ ] |
| **Knob takeover**: Pickup, Scaled, Instant (MELODEE.md › Across every page) | n/a | relative knobs have nothing to pick up | — |
| **Pad expression**: a pad's pressure as each note's poly aftertouch (MELODEE.md › Across every page) | done | pad pressure (Schwung modules) | [ ] |
| **Text never under 11 px**, never cut (MELODEE.md › Across every page) | n/a | one 5 x 7 font at 128 x 64 (UI.md) | — |
| **The Menu**: HERE, the page's jobs with their shortcuts; GO, the 16 places as the pads (guide/basics.md) | MIDI & sync & undo & projects | jogwheel push held; places on the pads | [ ] |
| **Lessons**: First beat, Sequences, Mix a track, Shape a pad (guide/basics.md) | MIDI & sync & undo & projects | Places > Learn | [ ] |
| **Basic and Full**: Basic shows the stock pages plus tracks, plug-ins, the mixer and Song (guide/settings.md) | MIDI & sync & undo & projects | Settings | [ ] |

## Sound engines and synths

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Sampler engine** for kits (MELODEE.md › Sound engines) | done | pads | [ ] |
| **PLUGIN engine on Schwung's modules**: menus, presets, own pages in QuickJS, sources on their own thread (MELODEE.md › Sound engines; melodee-move README) | done | Row button, Menu loads; jogwheel; knobs | [ ] |
| **MIDI, keygroup and audio engines** (MELODEE.md › Sound engines) | done | new-track sheet | [ ] |
| **EngineHost**: 16 tracks, the clock, buses and master, MIDI out timing; blocks of 128 frames (MELODEE.md › Threads and data) | done | automatic | [ ] |
| **Render workers**: tracks in parallel on the spare cores (MELODEE.md › Threads and data) | mixer & effects | built behind `MELODEE_RENDER_THREADS=N`, off: in the simulated Move two workers made the busy cycle slower (784-1516 µs against 579-849); to measure on the Move before turning on | [ ] |
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
| **Replace a plug-in**: asks; one undo brings the old one back with its state (MELODEE.md › Plug-ins) | MIDI & sync & undo & projects | asks; its undo | [ ] |
| **Plug-in probe**: a command-line host to try a plug-in first (MELODEE.md › Plug-ins) | done | `--probe-modules` | [ ] |

## Files

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Projects** `.mldp` in tagged sections, written whole or not at all (MELODEE.md › Files on the card) | done | /data/UserData/Melodee/Projects | [ ] |
| **Everything under one folder** (MELODEE.md › Files on the card) | done | /data/UserData/Melodee; `--home` moves it | [ ] |
| **Folders named from outside** (`$MELODEE_*` on a host) (MELODEE.md › Files on the card) | done | `--home`, `--samples`, `--modules` | [ ] |
| **Settings file**: settings, MIDI and Input Config, the project to open at start, the last one (MELODEE.md › Files on the card) | MIDI & sync & undo & projects | automatic | [ ] |
| **Samples folders**: Recorded, Edits, Banks, Looper, Expansions, Keygroups, Stems (MELODEE.md › Files on the card) | sampling | /data/UserData/Melodee/Samples | [ ] |
| **Cache**: decoded long samples and instrument files, 4 GB, aged out after 30 days (MELODEE.md › Files on the card) | done | /data/UserData/Melodee/Cache (Melodee's shell/sample_cache.h: tidied at start, 4 GB, 30 days) | [ ] |
| **Export folders**: Mixdowns, Stems, MIDI, MPC 3 (MELODEE.md › Files on the card; guide/song.md) | song & export | /data/UserData/Melodee | [ ] |
| **SysEx folder** (MELODEE.md › Files on the card) | done | /data/UserData/Melodee/SysEx | [ ] |
| **Plugins folder**, read only (MELODEE.md › Files on the card) | schwung | covered by Schwung's modules, the Move's plug-ins | — |

## Remote and previews

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Status line** with Melodee's fields (MELODEE.md › Remote) | done | the Move's status line every 5 s; each area adds its fields | [ ] |
| **Remote presses** `melodee-press`: buttons, pads, knobs (MELODEE.md › Remote) | MIDI & sync & undo & projects | a command over SSH | [ ] |
| **`melodee-render`**: render workers off or on (MELODEE.md › Remote) | MIDI & sync & undo & projects | a command over SSH | [ ] |
| **`melodee-looper`** (MELODEE.md › Remote) | looper | a command over SSH | [ ] |
| **`melodee-midi`**: a message into the input; panic (MELODEE.md › Remote) | done | `melodee-midi HEX...` and `melodee-midi panic` in the package, over SSH | [ ] |
| **`melodee-print`, `melodee-freeze`, `melodee-unfreeze`** (MELODEE.md › Print and freeze) | song & export | a command over SSH | [ ] |
| **`melodee-autosample`** (MELODEE.md › Print and freeze) | instruments | a command over SSH | [ ] |
| **`melodee-usb-input`** (MELODEE.md › Remote) | n/a | no USB audio | — |
| **`card-share`** (MELODEE.md › Remote) | n/a | no card | — |
| **Page previews** `--melodee-preview` (MELODEE.md › Previews) | done | `--melodee-previews`, `--ui-mockups` | [ ] |
| **Sound self-test** `--melodee-sound` (MELODEE.md › Previews) | done | `./build.sh` | [ ] |
| **Preview checks**: text under 11 px, the soft key and card rules (MELODEE.md › Previews) | n/a | made for the MPC's 320 x 240 pages and B1-B3 / K1-K3; the rail needs checks of its own | — |

## Web page

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **The page at melodee.local**, named by Melodee's own mDNS (MELODEE.md › Web page) | web page | a computer on the Move's USB-C network | [ ] |
| **Port 80**: the stock web server stopped while Melodee runs and started again after (MELODEE.md › Web page) | web page | automatic | [ ] |
| **Device tab**: the version, free space, the plug-ins (MELODEE.md › Web page) | web page | browser | [ ] |
| **Update** from a signed release zip (MELODEE.md › Web page) | web page | browser; install confirmed on the Move | [ ] |
| **Plug-ins**: a zip uploaded and unzipped into Plugins; one removed (MELODEE.md › Web page) | web page | browser | [ ] |
| **Files tab**: list, play, download, upload without overwriting, new folder, delete; protected folders (MELODEE.md › Web page) | web page | browser; /data/UserData/Melodee and the sample places | [ ] |
| **Quickstart and Manual tabs**, carried in the binary (MELODEE.md › Web page) | web page | browser | [ ] |
| **Safety**: only IP or localhost hosts, an `X-Melodee` header on every POST, one request at a time (MELODEE.md › Web page) | web page | automatic | [ ] |
| **The browser and project list look again** after a change (MELODEE.md › Web page) | web page | automatic | [ ] |

## Installing and updates

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Install and start** (the MPC: autorun on the card, `device.py install-sd`) (MELODEE.md › Installing) | done | Schwung's Tools menu, or `tools/deploy-stock.sh` over SSH | [ ] |
| **Firmware check**: starts only on MPC firmware 1.3.0.12 (README › Requirements) | n/a | not an MPC | — |
| **Back to the stock app**: Exit's MPC APP (MELODEE.md › Project and system, Exit) | done | Shift + Back; Ableton's app comes back however the run ends | [ ] |
| **Recovery**: the stock app back after a crash (MELODEE.md › Threads and data) | done | `run-stock.sh`; a reboot is always stock | [ ] |
| **Power off** from the Exit sheet (MELODEE.md › Project and system, Exit) | n/a | the Move's own power button; Melodee saves on the way out | — |
| **Updates from the card**: a signed zip at the card's top (MELODEE.md › Updates from the card) | n/a | no card; updates come through the web page | — |
| **Update sheet**: Update to vX?, LATER / INSTALL, its steps, the previous version kept to fall back to (MELODEE.md › Updates from the card) | web page | push installs, Back for later | [ ] |
| **Release signing**: Ed25519 over `RELEASE.sha256` (MELODEE.md › Updates from the card) | web page | build tool | [ ] |
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
| **Main volume** (MELODEE.md › The panel) | done | master knob | [ ] |
| **Play and Stop** (MELODEE.md › The panel) | done | Play, a toggle | [ ] |
| **Sample Select**: the browser on a kit, the picker on a plug-in track (MELODEE.md › The panel) | done | Menu | [ ] |
| **Exit** (Power): save and back to the stock app (MELODEE.md › Project and system) | done | Shift + Back | [ ] |
| **Shift held**: the jobs lit white, toggles bright; toggles kept in the settings (MELODEE.md › Shift + pads) | MIDI & sync & undo & projects | Shift: the steps lit for their jobs | [ ] |
| **Button lights**: bright in use, faint where they act, dark where they would do nothing (MELODEE.md › Lights) | done | the Move's button lights | [ ] |
| **Track lights**: each track's colour, the selected one bright (MELODEE.md › Pad lights, Tracks) | done | Row1-4 | [ ] |
| **Bank lights** A-H (MELODEE.md › Lights) | n/a | no bank lights; the rail names the bank | — |
| **Level meters**: five lights a side, -24.1 to -0.1 dBFS; the input on Record (MELODEE.md › Lights) | done | no meter lights: the Volume knob touched (the Move's VU meter), marks at -24, -12, -6, -3, 0; the Mixer's strips; the input's is Sampling's | [ ] |
| **Kit pad lights**: the selected pad Full, a sample Dim, empty dark, muted Faint red, a flare as it sounds; five rungs that keep each colour's hue (MELODEE.md › Pad lights) | done | pad lights: dim in the pad's colour, bright while it sounds, the last pad white, muted dark grey (the Move's dim white), empty off (Kerem, 2026-11-20) | [ ] |
| **Key lights**: the key's roots in the track's colour, its other notes Dim, out of the key Faint (MELODEE.md › Pad lights) | instruments | pad lights | [ ] |
| **Pad lights and Pad fade settings**; the Tap Tempo flash (MELODEE.md › Project and system, LIGHTS) | MIDI & sync & undo & projects | Settings | [ ] |
| **Light show**: Pulse, Sweep, Chase, Ripple, Levels, in time with the tempo (MELODEE.md › Light show) | MIDI & sync & undo & projects | Sample + Mute; Up / Down the style | [ ] |

## Summary

| Status | Rows |
| --- | --- |
| `done` | 175 |
| `sequencer` | 4 |
| `sampling` | 22 |
| `kit depth` | 2 |
| `instruments` | 25 |
| `mixer & effects` | 2 |
| `song & export` | 21 |
| `looper` | 1 |
| `MIDI & sync & undo & projects` | 30 |
| `web page` | 11 |
| `schwung` | 16 |
| `n/a` | 26 |
| All rows | 335 |

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
| **Shift + Play** | retrigger the playing clips from their start (17.1.5) | nothing yet: the bindings table | MIDI & sync & undo & projects | [ ] |
| **Rec** | record into the selected track's clip, after a count-in (14.1) | Seq Record, its count-in from stopped | done | [ ] |
| **Track button** | select the track (20) | selects one of the bank's four of sixteen | done | [ ] |
| **Track button held** (an unselected one) | look at that track without selecting it (20) | selects it on the press; held, the sixteen, the steps pick (free on the Move) | MOVE_CONTROLS.md decides | [ ] |
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
| **Shift + Steps 1, 2, 8-11, 15, 16** | Set overview, Setup, 16 Pitches, Keys & Scale, full velocity, Repeat / Arp, double loop, quantize (20) | not yet | MOVE_CONTROLS.md decides | [ ] |
| **Note / Session toggle** ("Menu", CC 50) | switch Note and Session; held, a peek (5, 17) | Load (samples, modules) | MOVE_CONTROLS.md decides | [ ] |
| **Loop** | Loop mode: the steps are bars (12.1) | Chop | MOVE_CONTROLS.md decides | [ ] |
| **Volume knob** | the main volume (16.1) | the same | done | [ ] |
| **Wheel, its push, Shift + wheel** | choose, confirm, finer (20, 10.1) | the same on every screen | done | [ ] |
| **Back** | leave a menu or view (20) | the same | done | [ ] |
| **Undo, Shift + Undo** | undo, redo (20) | not yet | MIDI & sync & undo & projects | [ ] |
| **Capture** | the last notes played into a clip (14.3) | not yet | sampling | [ ] |
| **Sample** ("Sampling") | Sampling mode (15.1) | not yet | sampling | [ ] |
