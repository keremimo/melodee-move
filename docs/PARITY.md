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
| **MIDI track ports** on the new-track sheet: Out jack, USB-C, All USB (guide/tracks.md) | MIDI & sync & undo & projects | new-track sheet; USB only | [ ] |
| **Delete a track**: sound, kit, patterns; its mix strip stays; asks (its undo: MIDI & sync & undo & projects) (MELODEE.md › The panel, Pad Bank) | done | track button held + Delete + step | [ ] |
| **Copy a track**: sound, kit, 128 patterns, motion; the target keeps its strip; a synth gets its own instance of the module (MELODEE.md › Across every page, Copy) | done | track button held + Copy + step, then a step | [ ] |
| **Move a track**: trades places; song clips, keys, lanes and sources follow it (MELODEE.md › Across every page, Move) | MIDI & sync & undo & projects | track button held + Shift + Copy + step, then a step | [ ] |
| **Stage**: each track's home pages, B1 source, B2 how it plays, B3 where it goes (MELODEE.md › Playing) | kit depth | the track's Play screen (done: a kit's PLAY knobs, a module's pages); the rest as pad pages, Left / Right | [ ] |
| **Synth presets**: walked, loaded a moment after the last turn (MELODEE.md › Playing, A synth) | done | jogwheel (Schwung modules) | [ ] |
| **Synth EDIT**: the instrument's own parameters and page map (MELODEE.md › Playing, Edit) | done | knobs 1-8, Left / Right; push: the module's menu | [ ] |
| **Macro knobs FILTER, ECHO, SPACE** on every plug-in track, saved, recorded as motion (MELODEE.md › Playing, A synth) | instruments | a page of knobs on module tracks | [ ] |
| **ARP**: Up, Down, Up-down, Random, Order; rate 1/4-1/32T; octaves; gate; hold (MELODEE.md › Playing) | instruments | ARP page, knobs 1-6 | [ ] |
| **CHORD**: a pad a chord of the scale; type, voicing, inversion, hold (MELODEE.md › Playing) | instruments | CHORD page, knobs 1-5 | [ ] |
| **MIX → SENDS → FX pages** of a track: level, pan, send A, meter; four sends and returns; the chain's slot in hand (MELODEE.md › Playing) | mixer & effects | the track's last pages; knobs 1-8 | [ ] |
| **Pads as notes** on a synth: rows of fourths, octave, pressure as poly aftertouch (MELODEE.md › Playing, Keys) | done | 32 pads; Up / Down the octave | [ ] |
| **Keys**: root, scale, layout (Scale, Chromatic, Chords, Prog, Drums), transpose ±12, roots lit (MELODEE.md › Playing, Keys) | instruments | Keys page knobs; pad lights | [ ] |
| **Project key and scale**, set from any track (MELODEE.md › Playing, Tempo) | instruments | tempo screen, knobs 4-5 | [ ] |
| **Prog**: MPC 3 `.progression` files, a chord a pad (48 at most), key, voicing (MELODEE.md › Playing, Prog) | instruments | pads; jogwheel walks the progressions | [ ] |
| **Chord builder**: held keys stored onto a pad, saved as Mine N (MELODEE.md › Playing, Prog) | instruments | Keys page: push stores | [ ] |
| **Drums layout**: a drum plug-in's or MIDI drum machine's pads from note 36, voices named (MELODEE.md › Playing, Drums) | instruments | 32 pads are notes 36-67; Up / Down the bank | [ ] |
| **Pads page**: the bank's pads each drawing its waveform (MELODEE.md › Playing, Pads) | n/a | 128 x 64 cannot show 32 waveforms; the pads light in their colours and the screen shows the last pad | — |
| **Sounds**: a MIDI track's saved sounds (`.mlds`), favourites (MELODEE.md › Playing, Sounds) | MIDI & sync & undo & projects | Menu (load) on a MIDI track | [ ] |
| **16 Levels**: the last pad or note on every pad as Velocity, Filter, Tune, Scale or Zones; ends, octave; a MIDI keyboard plays the pad on every key (MELODEE.md › Playing, 16 Levels) | kit depth | Places > 16 Levels; the levels over the pads; knobs type and ends | [ ] |
| **Note Repeat**: 1/4-1/64 and triplets, swung, pressure as velocity, written under Seq Record (MELODEE.md › Playing, Note Repeat) | sequencer | Shift + Step 9 (the MPC's Fader page); the rate to settle | [ ] |
| **Tempo**: BPM, swing, metronome level, metronome on / off (MELODEE.md › Playing, Tempo) | done | Shift + Step 5 / 6 / 7: tempo, metronome, groove, as on the Move | [ ] |
| **Tap tempo, halve, double; metronome Rec** (clicks only while recording) (MELODEE.md › The panel; Playing, Tempo) | sequencer | tempo screen: tap a step; Left / Right halve, double; push cycles Off / On / Rec | [ ] |
| **Track speed**: ×½, ×¾, ×1, ×1½, ×2 against the tempo (MELODEE.md › Playing, Tracks) | sequencer | a knob on the Mixer or Patterns page | [ ] |
| **Keygroup track pages**: TRIM / ZONES, TUNE, AMP ENV, FILTER, FILT ENV, PLAY, ARP, CHORD (guide/instrument-and-midi-tracks.md) | instruments | Left / Right pages, knobs 1-8; Menu loads | [ ] |
| **A keygroup of one sample** played from the note found in it (guide/instrument-and-midi-tracks.md) | instruments | pads as keys | [ ] |
| **Audio track INPUT**: source, monitor, gain; armed by Seq Record, one pass of the pattern recorded (guide/audio-tracks.md) | sampling | Sample on an audio track; Rec arms; line in or mic | [ ] |
| **Audio track takes**: CLIP (level, start, end), FADES (in, out, reverse), WARP (Stretch, Repitch, BPM, semi); kept in `<project> Audio` (guide/audio-tracks.md) | sampling | Left / Right pages, knobs | [ ] |

## Looper

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Looper on / off**: asks first; 45 MB a loop (MELODEE.md › Looper) | looper | Shift + Loop, or Places > Looper | [ ] |
| **Four loops L1-L4** on loop tracks of their own, each with a mix strip and a chain of six (MELODEE.md › Looper) | looper | Row1-4 pick the loop while the Looper shows | [ ] |
| **Loop states**: ARM, REC, DUB, PLAY, STOP, FIT; record → close → overdub ⇄ play (MELODEE.md › Looper) | looper | jogwheel push | [ ] |
| **Loop settings**: length (Free, 1-16 bars), level, feedback, Quantize (Bar, Beat, Off), After take (Play, Overdub, Stop) (MELODEE.md › Looper) | looper | knobs 1-6 | [ ] |
| **Loop pads**: record / overdub, play / stop on the boundary, undo, redo, held to clear (MELODEE.md › Looper, Pads) | looper | a row of pads a loop | [ ] |
| **Loop sources**: Input, a track after its chain, another loop, Mix; each written behind the play head by its delay (MELODEE.md › Looper, Sources) | looper | knob (SOURCE); line in or mic; measure the Move's input delay | [ ] |
| **Looper MIX**: level, pan, send, bus; mute and solo a loop (MELODEE.md › Looper) | looper | knobs; Mute + a loop's row | [ ] |
| **MORE sheet**: Play / Stop on the boundary or at once, Clear (asks) (MELODEE.md › Looper) | looper | push sheet | [ ] |
| **Commit to pad**: the loop onto the kit's next empty pad, with a note at bar 1 or not (MELODEE.md › Looper, Kept) | looper | push sheet | [ ] |
| **Transport**: Play lines the loops up with bar 1, armed takes start on the downbeat after the count-in; Stop closes takes (MELODEE.md › Looper) | looper | Play | [ ] |
| **Tempo follow**: a WSOLA stretch keeps the pitch; Fit renders the loop at the tempo now (MELODEE.md › Looper, Tempo) | looper | automatic; push fits | [ ] |
| **Loops kept with the project** as `<project> Loops/Loop N.wav` (MELODEE.md › Looper, Kept) | looper | automatic | [ ] |

## Sequencing

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Patterns as note lists**: tick (96 a quarter), length, key, velocity, chance, accent, slide, tie, ratchet (MELODEE.md › Sequencing) | sequencer | steps; Places > Roll | [ ] |
| **128 patterns a track** in banks (MELODEE.md › Sequencing) | sequencer | pattern layer: 32 a bank, Up / Down | [ ] |
| **Pattern length, Q, time signature**: 1-128 bars, Q 1/4-1/64 with triplets, 1/4-15/16 (MELODEE.md › Sequencing, Patterns) | sequencer | Patterns page knobs | [ ] |
| **Swing** 50-75 % (MELODEE.md › Sequencing) | done | Shift + Step 7: the Move's groove | [ ] |
| **Free length**: an empty pattern grows a bar at a time as it is recorded (MELODEE.md › Sequencing) | sequencer | Rec | [ ] |
| **Launch a pattern**: at the next bar, at once when stopped; an empty one as long as the one playing (MELODEE.md › Sequencing, Seq held) | sequencer | pattern layer: a pad | [ ] |
| **Pattern chains**: up to eight patterns, each played once, round again (CHAN) (MELODEE.md › Sequencing, Seq held) | sequencer | pattern layer: several pads in one hold | [ ] |
| **Copy and clear patterns**, across banks; clearing asks (MELODEE.md › Sequencing, Seq held) | sequencer | pattern layer: Copy + pad, then a pad; Delete + pad | [ ] |
| **Patterns page**: cells as the pads sit, NEXT, follow arrows, the other tracks' dots, progress (MELODEE.md › Sequencing, Patterns) | sequencer | Places > Sequence | [ ] |
| **Playhead by Q and Continue** from it (Shift + wheel, Shift + Play) (MELODEE.md › Sequencing, Patterns) | sequencer | jogwheel on Patterns; Continue to settle (Play is a toggle) | [ ] |
| **Variations** into the next empty pattern: Thinner, Busier, Humanised, Reversed, Octave up and down (VARY) (MELODEE.md › Sequencing, Patterns) | sequencer | Patterns: HERE > Vary; knob 1 the amount | [ ] |
| **Sequences and sequence chains**: pattern n of every track, a name and tempo of its own; several in one hold (SCNE) (MELODEE.md › Sequencing, Sequences) | sequencer | Places > Sequence: Sequences; a pad launches | [ ] |
| **Follow actions**: Next, Previous, Any, Again, Stop; after 1-64 bars or plays; chance; OR (FOLW) (MELODEE.md › Sequencing) | sequencer | Patterns' FOLLOW page, knobs 1-4 | [ ] |
| **BPM per pattern** (MELODEE.md › Sequencing, Patterns) | sequencer | tempo screen: a setting | [ ] |
| **Seq Record**: armed, then Play; at once while playing; overdub; notes at the tick heard; a take is one undo (MELODEE.md › Sequencing) | sequencer | Rec | [ ] |
| **Count-In**: a bar of clicks before recording (MELODEE.md › Shift + pads) | sequencer | Shift + Step 4 | [ ] |
| **Rec Quantize**: notes land on the pattern's Q (MELODEE.md › Sequencing, Seq Record) | sequencer | Shift + Step 10 | [ ] |
| **Fill**: FILL conditions play while held (MELODEE.md › The panel, Play) | sequencer | to settle: a button held while playing | [ ] |
| **Chance and trig conditions**: ALWAYS, n:m, FILL, FIRST, PRE, NEI and their NOTs (COND) (MELODEE.md › Sequencing) | sequencer | step held: knobs | [ ] |
| **Ratchets**: 1-8 hits, velocity ramp, pitch ramp on a kit pad (RTCH) (MELODEE.md › Sequencing) | sequencer | step held: TRIG knobs | [ ] |
| **Accent, Slide, Tie**: about 4 dB louder; a glide of 60 ms; legato; CC 84 and CC 68 to synths (MELODEE.md › Sequencing, Roll) | sequencer | step held: knobs; Roll's note sheet | [ ] |
| **Step Edit, STEPS**: the 16 steps of a bar for one pad or note, lit by velocity, the playhead; Tie and Insert (MELODEE.md › Sequencing, Step Edit) | sequencer | the 16 steps; Left / Right the bar; a pad picks the lane; HERE: Tie, Insert | [ ] |
| **Step Edit, PLAY**: pads write at the caret; REST, ERASE; length, velocity, chance (MELODEE.md › Sequencing, Step Edit) | sequencer | Places > Step Edit; jogwheel the caret | [ ] |
| **Parameter locks**: hold a step, turn a knob (MELODEE.md › Sequencing, Step Edit) | sequencer | step held + knobs 1-8 | [ ] |
| **Lane length**: a kit pad's lane loops on its own, polymeter (LANE) (MELODEE.md › Sequencing, Step Edit) | sequencer | a knob on the steps page | [ ] |
| **Euclid**: hits, steps 1-64, rotation on a lane (EUCL) (MELODEE.md › Sequencing, Step Edit) | sequencer | EUCLID knobs on the steps page | [ ] |
| **Spare steps**: a step written past the end makes the pattern longer (MELODEE.md › Sequencing, Step Edit) | sequencer | Right past the last bar, then a step | [ ] |
| **Roll**: notes at their ticks over the bars; the note in focus with length, velocity, chance, nudge (MELODEE.md › Sequencing, Roll) | sequencer | Places > Roll; jogwheel; knobs | [ ] |
| **Roll marks and jobs**: mark notes; move by Q or a row, mute, ratchet, velocities and ramp, pull to the grid, octave; split, legato, reverse, the arp as notes, loop the bars in view (MELODEE.md › Sequencing, Roll) | sequencer | Shift + jogwheel marks; knobs; HERE | [ ] |
| **Copy notes**: marked notes pasted at the cursor; a key's notes onto another; a step onto another (MELODEE.md › Across every page, Copy) | sequencer | Copy | [ ] |
| **Erase notes**: a pad's notes while playing, a key's notes, the note in focus (MELODEE.md › Across every page, Erase) | sequencer | Delete + pad; Delete | [ ] |
| **Motion**: knob moves recorded in each pattern, 8192 points, lanes reset at the pattern's start; a lane cleared (MELODEE.md › Sequencing, Motion) | sequencer | Rec + knob turns; Places > Motion; Delete + knob | [ ] |
| **Mix in Motion**: level, pan, sends, master chain and returns recorded (MELODEE.md › Sequencing) | sequencer | Rec + mixer knobs | [ ] |
| **MOD**: eight modulators, an LFO (seven shapes, free or synced) or a follower, up to four lanes each (MODS) (MELODEE.md › Sequencing) | sequencer | Places > Motion, MOD; knobs | [ ] |
| **Macros**: eight, each one knob over up to eight lanes (MACR) (MELODEE.md › Sequencing, MOD) | sequencer | MOD page, MACRO; knobs | [ ] |
| **A pattern's feel**: STRENGTH, SWING, HUMANIZE, HUMAN VEL, GROOVE from another pattern (FEEL) (MELODEE.md › Sequencing) | sequencer | Time Correct page knobs | [ ] |
| **Time Correct**: heard live, no commit; ALL lays the feel on the other patterns (MELODEE.md › Sequencing) | sequencer | Shift + Step 14 | [ ] |
| **Half Seq, Double Seq, Half Speed, Double Speed** (MELODEE.md › Sequencing) | sequencer | Shift + Step 2, 3, 6, 7 | [ ] |
| **Recall**: every note played is kept; the last phrase into empty patterns, one undo (MELODEE.md › Sequencing, Recall) | sequencer | Shift + Capture | [ ] |
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
| **Kits of 128 pads** in banks (the MPC's A-H of 16; here four of 32) (MELODEE.md › Sampler) | kit depth | Up / Down the bank; the rail names it | [ ] |
| **No count of samples**: memory is the limit; a load that would leave too little is refused, "Memory full" (MELODEE.md › Sampler) | kit depth | automatic | [ ] |
| **Load samples**: WAV, AIFF, FLAC, Ogg, MP3, from places (My Samples, Recordings, Core Library, /data/UserData) (MELODEE.md › Sampler, Samples) | done | Menu; jogwheel, push; Back up | [ ] |
| **Long samples streamed** from storage, up to 6 hours; long MP3, FLAC, Ogg, AIFF decoded once into Cache (MELODEE.md › Sampler) | kit depth | automatic (now 60 s at most) | [ ] |
| **PREVIEW**: each file plays as the cursor lands on it, streamed (MELODEE.md › Sampler, Samples) | sampling | Load page: Shift + push toggles | [ ] |
| **Load onto a chosen pad** (the MPC: Shift + pad onto the next empty one) (MELODEE.md › Sampler, Samples) | sampling | Load page: tap a pad | [ ] |
| **STARRED and RECENT**: star a file; the last 16 loaded (MELODEE.md › Sampler, Samples) | sampling | Load page: HERE > Star | [ ] |
| **SFZ instruments**: zones, round robin, layers, release samples, crossfades, keyswitches (MELODEE.md › Sampler) | instruments | Menu (load) | [ ] |
| **MPC keygroup programs** `.xpm`, and MPC 3 programs `.xty` (MELODEE.md › Sampler) | instruments | Menu (load) | [ ] |
| **EXS24 instruments** `.exs` (MELODEE.md › Sampler) | instruments | Menu (load) | [ ] |
| **SoundFonts** `.sf2`, presets listed as a folder (MELODEE.md › Sampler) | instruments | Menu (load) | [ ] |
| **DecentSampler presets** `.dspreset` (MELODEE.md › Sampler) | instruments | Menu (load) | [ ] |
| **An instrument on a kit pad**: its zones by key and velocity; ZONES page, zone tune and level trims (ZTRM) (MELODEE.md › Sampler, SFZ) | kit depth | pad pages; knobs | [ ] |
| **MPC drum programs**: LOAD KIT sheet, the whole kit or banks, layers, pad colours (MELODEE.md › Sampler) | kit depth | Menu (load); push sheet | [ ] |
| **MPC expansions** `.xpn`: installed into Expansions; program previews (MELODEE.md › Sampler) | sampling | Menu (load); push | [ ] |
| **Banks** `.mldb`: Save Bank, load into the bank in view (MELODEE.md › Sampler, Banks) | kit depth | Load page: HERE > Save bank | [ ] |
| **Pad sound knobs**: tune, level, pan, start, end, attack, decay, cutoff (MELODEE.md › Sampler, sample pages) | done | PLAY page, knobs 1-8; Shift finer | [ ] |
| **TRIM**: loop point, zoom at each handle, Loop Lock, Normalize (MELODEE.md › Sampler, Trim) | kit depth | TIME page; Shift + knob zooms | [ ] |
| **LAYERS**: four samples a pad, Velocity, Cycle or Random; each layer's tune, level, velocities (MELODEE.md › Sampler, Layers) | kit depth | LAYERS page; knobs | [ ] |
| **Kit volume** (MELODEE.md › Sampler, Mix) | kit depth | a knob on a pad page | [ ] |
| **Fine tune and Warp**: cents; Warp Off, 50-200 %, Seq from # Beats (MELODEE.md › Sampler, Tune) | kit depth | TIME page | [ ] |
| **Warp mode**: Time stretch or Pitch for the kit (MELODEE.md › Shift + pads) | kit depth | Shift + Step 15 | [ ] |
| **Amp envelope**: release on Note On, velocity sensitivity, decay from start or end (MELODEE.md › Sampler, Amp Env) | kit depth | SHAPE page | [ ] |
| **Play modes**: Mono / Poly, mute group 1-16, offset, Note On, reverse, loop, Pad Link chains (MELODEE.md › Sampler, Play) | kit depth | SHAPE and TIME pages | [ ] |
| **Loop seam crossfade** XFADE, 0-250 ms (MELODEE.md › Sampler, Play) | kit depth | TIME page knob | [ ] |
| **Filter type, resonance and envelope**: Classic, LPF2/4, HPF2/4, BPF2/4; up to seven octaves (MELODEE.md › Sampler, Filter) | kit depth | FILTER page | [ ] |
| **Follow**: the kit keeps the project's tempo, Beats, Tones or Tape; # Beats (MELODEE.md › Sampler) | kit depth | TIME page | [ ] |
| **Cloud**: a pad as a grain cloud; position, size, density, scan, spread (MELODEE.md › Playing, A kit) | kit depth | CLOUD page | [ ] |
| **Kit filter**: one filter after every pad (MELODEE.md › Sampler) | kit depth | FILTER page | [ ] |
| **COLOUR**: a pad's light colour, the track's, one of sixteen, or the kit's own (MELODEE.md › Playing, A kit) | kit depth | a knob; pad lights | [ ] |
| **Full Level**: every hit at 127 (MELODEE.md › Shift + pads) | kit depth | Shift + Step 1 | [ ] |
| **Pad sends** into buses A-D, and the kit's send (MELODEE.md › Playing, A kit) | mixer & effects | SENDS page; pads lit by their send | [ ] |
| **A pad's own chain** of four effects (MELODEE.md › Effects) | mixer & effects | Places > Effects on a pad | [ ] |
| **Chop**: Auto, Beats, Regions, Threshold; a slice's start and end (MELODEE.md › Sampler, Chop) | done | Loop; knobs 1-4; jogwheel or Left / Right the slice | [ ] |
| **Chop Manual**: pads tapped as it plays, cuts pulled onto hits (MELODEE.md › Sampler, Chop) | sampling | Chop: the type knob on Manual; pads | [ ] |
| **Slices 33-64** on a second bank (MELODEE.md › Sampler, Chop) | sampling | Up / Down in Chop | [ ] |
| **Chop close up**: the view follows the cut; a moved cut plays its slice; Shift + wheel jumps from hit to hit (MELODEE.md › Sampler, Chop) | sampling | jogwheel; Shift + jogwheel | [ ] |
| **Extract, Split, Merge**; a slice taken out; every cut out (MELODEE.md › Sampler, Chop) | sampling | Copy (Extract); Delete; Delete + Loop | [ ] |
| **CLEAN**: cut on the sample's drums stem (MELODEE.md › Sampler, Chop) | schwung | Schwung's separator writes the stems as files: Load the drums stem and Chop it | — |
| **To Pads**: Poly, Mono, Thru (MELODEE.md › Sampler, Chop) | done | Chop: push; jogwheel | [ ] |
| **To Pads KIT**: a pad a sound (MELODEE.md › Sampler, Chop) | sampling | To Pads sheet | [ ] |
| **To Pads PATTERN**: a note for each slice into the pattern (MELODEE.md › Sampler, Chop) | sequencer | To Pads sheet | [ ] |
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
| **Chains**: six slots on each track, four buses A-D with six slots and a return, the master's six before the compressor (MELODEE.md › Effects) | mixer & effects | Places > Effects; jogwheel the slot; knobs | [ ] |
| **41 built-in effects**: reverbs, delays, modulation, dirt, filters and EQ, dynamics, Spring, Ensemble, Multi-chorus, Auto-wah, Formant, Width (MELODEE.md › Effects) | mixer & effects | the effect picker | [ ] |
| **Schwung's audio FX modules** in chains (melodee-move README › Next) | mixer & effects | Menu (load): FX | [ ] |
| **FX Select**: Track → Sends → Bus → Master; the slot strip; the effect sheet (change, presets, bypass, move, remove); copy an effect (MELODEE.md › Effects) | mixer & effects | Places > Effects; push sheet; Copy | [ ] |
| **Mixer**: the 16 tracks' levels, pans, meters, CPU share, the strip in hand (MELODEE.md › Playing, Tracks) | mixer & effects | Places > Mixer; knobs 1-8 eight tracks | [ ] |
| **Sends page**: every track's send into one bus, its return (MELODEE.md › Effects) | mixer & effects | Places > Sends | [ ] |
| **Keyed Compressor and Gate**: KEY from a track or a bus (FXKY) (MELODEE.md › Effects) | mixer & effects | the effect's last page | [ ] |
| **SYNC on the delays**: 1/32-1/1, triplets and dotted, following the tempo (MELODEE.md › Effects) | mixer & effects | the effect's knob | [ ] |
| **Knob FX**: the MPC's 28 and Melodee's 15; settings A and B and the move between them; per kit pad or per track (MELODEE.md › Effects) | mixer & effects | Places > Knob FX; knobs 1-8; A to B on a knob (no fader) | [ ] |
| **Pad FX**: 47 punches (the MPC's 16, Melodee 1 and 2), up to four held, pressure as depth, LATCH, master or track, QUANTIZE (MELODEE.md › Effects) | mixer & effects | Places > Pad FX; punches on the 32 pads | [ ] |
| **Pad FX KEEP**: punches laid into the pattern and looping with it (KEPT); taken out again (MELODEE.md › Effects) | mixer & effects | Pad FX: push keeps; Delete + pad | [ ] |
| **Flex Beat**: a time effect over the master for a bar, 15 effects, One Shot / Loop, mix, quantize (MELODEE.md › Effects) | mixer & effects | Places > Flex Beat | [ ] |
| **Master Compressor**: attack, release, amount, In Boost, Color, Bypass (MELODEE.md › Effects) | mixer & effects | Shift + Step 5 | [ ] |
| **Compressor Sidechain**: one track or one kit pad keys it (MELODEE.md › Effects) | mixer & effects | Compressor page: a pad picks | [ ] |

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
| **MIDI Config**: port, in and out channels, sync, thru, program change, takeover rows, panic, reset (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | Settings > MIDI (Shift + Step 8) | [ ] |
| **MIDI jacks** (the External port) (MELODEE.md › MIDI and sync) | n/a | the Move has no MIDI jacks; USB-C only | — |
| **USB MIDI**: a computer or USB devices on USB-C (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | USB-C, if the device page passes it (to check) | [ ] |
| **MIDI in**: notes on the in channel play the selected track; a kit hears 36 as A01; pressure, mod wheel, pitch wheel, sustain pedal, CCs, All Notes Off (MELODEE.md › MIDI and sync; guide/midi-and-sync.md) | MIDI & sync & undo & projects | automatic | [ ] |
| **MIDI in recorded**: under Seq Record; keys write on Step Edit and Roll (guide/midi-and-sync.md) | MIDI & sync & undo & projects | Rec | [ ] |
| **Track channel, Track MIDI in, Pad MIDI in / out**: a controller a track (guide/midi-and-sync.md) | MIDI & sync & undo & projects | Settings > MIDI | [ ] |
| **Remote CCs**: select a track, launch a pattern or a sequence (CC 102-104) (guide/midi-and-sync.md) | MIDI & sync & undo & projects | Settings > MIDI | [ ] |
| **Program change** launches that pattern (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | Settings > MIDI | [ ] |
| **MIDI out**: pads, patterns, the arp, each at the frame it is heard (MELODEE.md › MIDI and sync, Out) | MIDI & sync & undo & projects | automatic | [ ] |
| **MIDI clock in and out**: Start, Continue, Stop, Song Position, tempo (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | Settings > MIDI | [ ] |
| **MTC in and out**: in at 24, 25, 29.97 df, 30 fps; out at 25 (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | Settings > MIDI | [ ] |
| **MIDI Thru** (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | Settings > MIDI | [ ] |
| **MIDI Panic and all notes off** (MELODEE.md › MIDI and sync; The panel, Stop) | MIDI & sync & undo & projects | HERE > Panic; Settings > MIDI | [ ] |
| **CV / sync out**, base and division (MELODEE.md › MIDI and sync) | n/a | no CV or sync jacks on the Move | — |
| **MIDI track DEVICE**: port and channel, USB devices by name, OFFLINE, DELAY ±50 ms (MELODEE.md › MIDI and sync, MIDI tracks) | MIDI & sync & undo & projects | knobs | [ ] |
| **MIDI track PROGRAM**: bank MSB, LSB, program; a pattern's own program (MELODEE.md › Playing, A MIDI track) | MIDI & sync & undo & projects | knobs | [ ] |
| **MIDI track CC and SLOTS**: 24 controls (CC, 14-bit CC, bend, pressure, NRPN), recorded as motion (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | eight knobs a page, three pages | [ ] |
| **MIDI track EXPRESS**: a bend lane; pressure out as Poly, Channel or Off (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | knobs | [ ] |
| **MIDI track PADS**: Keys or Drums with General MIDI names (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | knobs; pads | [ ] |
| **MIDI in echoed** out on a MIDI track's port and channel, without loops (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | automatic | [ ] |
| **MIDI track AUDIO**: the instrument heard back through the track's strip and chain (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | knobs; line in or mic | [ ] |
| **SysEx**: a `.syx` sent, ON LOAD (MELODEE.md › MIDI and sync) | MIDI & sync & undo & projects | knobs; push sends | [ ] |

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
| **Place bar**: the bars of the pattern playing, how far it has played, coloured by state (MELODEE.md › Across every page) | sequencer | step lights (the playhead); a bar on the rail | [ ] |
| **Mute mode**: pads mute kit pads or tracks, Shift + pad solos, Shift + Mute unmutes all; kit pad mutes saved (MELODEE.md › Across every page) | mixer & effects | Mute (a tap latches, a hold lasts) + pad; Shift + Mute | [ ] |
| **Mute and solo tracks** from the overview (MELODEE.md › Pad lights, Tracks) | mixer & effects | track button held + Mute + step | [ ] |
| **Erase a kit pad**, stopped (MELODEE.md › Across every page, Erase) | kit depth | Delete + pad | [ ] |
| **Close a gap**: an empty pad erased moves the pads after it down, notes and lanes too (MELODEE.md › Across every page, Erase) | kit depth | Delete + an empty pad | [ ] |
| **Erase + pot**: the value back to its start (MELODEE.md › Across every page, Erase) | MIDI & sync & undo & projects | Delete + knob touch | [ ] |
| **Erase tapped alone**: the page's selection (a note, step, pattern, track, loop, effect, slice, layer, project) (MELODEE.md › Across every page, Erase) | MIDI & sync & undo & projects | Delete tapped | [ ] |
| **Copy kit pads**: the first pad, then the target (MELODEE.md › Across every page, Copy) | kit depth | Copy + pad, then a pad | [ ] |
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
| **Render workers**: tracks in parallel on the spare cores (MELODEE.md › Threads and data) | mixer & effects | `EngineHost::set_render_threads()`; measure on the Move first (the audio thread renders alone now) | [ ] |
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
| **Plug-in effects** on track, bus, master and loop chains (MELODEE.md › Plug-ins, An effect) | mixer & effects | Schwung's audio effect modules on track, bus and master chains | [ ] |
| **Transport to plug-ins**: playing, position, tempo, time signature; TRANSPORT off (MELODEE.md › Plug-ins, Transport) | sequencer | `schwung_move_info()` and `get_bpm()`: the tempo now; playing and the place to come | [ ] |
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
| **Cache**: decoded long samples and instrument files, 4 GB, aged out after 30 days (MELODEE.md › Files on the card) | kit depth | /data/UserData/Melodee/Cache | [ ] |
| **Export folders**: Mixdowns, Stems, MIDI, MPC 3 (MELODEE.md › Files on the card; guide/song.md) | song & export | /data/UserData/Melodee | [ ] |
| **SysEx folder** (MELODEE.md › Files on the card) | MIDI & sync & undo & projects | /data/UserData/Melodee/SysEx | [ ] |
| **Plugins folder**, read only (MELODEE.md › Files on the card) | schwung | covered by Schwung's modules, the Move's plug-ins | — |

## Remote and previews

| Feature | Status | Move controls | ✓ |
| --- | --- | --- | --- |
| **Status line** with Melodee's fields (MELODEE.md › Remote) | done | the Move's status line every 5 s; each area adds its fields | [ ] |
| **Remote presses** `melodee-press`: buttons, pads, knobs (MELODEE.md › Remote) | MIDI & sync & undo & projects | a command over SSH | [ ] |
| **`melodee-render`**: render workers off or on (MELODEE.md › Remote) | MIDI & sync & undo & projects | a command over SSH | [ ] |
| **`melodee-looper`** (MELODEE.md › Remote) | looper | a command over SSH | [ ] |
| **`melodee-midi`**: a message into the input; panic (MELODEE.md › Remote) | MIDI & sync & undo & projects | a command over SSH | [ ] |
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
| **Level meters**: five lights a side, -24.1 to -0.1 dBFS; the input on Record (MELODEE.md › Lights) | mixer & effects | no meter lights: a meter on the screen | [ ] |
| **Kit pad lights**: the selected pad Full, a sample Dim, empty dark, muted Faint red, a flare as it sounds; five rungs that keep each colour's hue (MELODEE.md › Pad lights) | kit depth | pad lights | [ ] |
| **Key lights**: the key's roots in the track's colour, its other notes Dim, out of the key Faint (MELODEE.md › Pad lights) | instruments | pad lights | [ ] |
| **Pad lights and Pad fade settings**; the Tap Tempo flash (MELODEE.md › Project and system, LIGHTS) | MIDI & sync & undo & projects | Settings | [ ] |
| **Light show**: Pulse, Sweep, Chase, Ripple, Levels, in time with the tempo (MELODEE.md › Light show) | MIDI & sync & undo & projects | Sample + Mute; Up / Down the style | [ ] |

## Summary

| Status | Rows |
| --- | --- |
| `done` | 55 |
| `sequencer` | 44 |
| `sampling` | 22 |
| `kit depth` | 27 |
| `instruments` | 25 |
| `mixer & effects` | 22 |
| `song & export` | 21 |
| `looper` | 13 |
| `MIDI & sync & undo & projects` | 54 |
| `web page` | 11 |
| `schwung` | 16 |
| `n/a` | 25 |
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
| **Rec** | record into the selected track's clip, after a count-in (14.1) | Seq Record | sequencer | [ ] |
| **Track button** | select the track (20) | selects one of the bank's four of sixteen | done | [ ] |
| **Track button held** (an unselected one) | look at that track without selecting it (20) | selects it on the press; held, the sixteen, the steps pick (free on the Move) | MOVE_CONTROLS.md decides | [ ] |
| **Track button + Volume knob** | that track's volume, in dB (16.2) | the same (`VOL`) | done | [ ] |
| **Mute** | mute the selected track (16.4) | the same | done | [ ] |
| **Mute + track button** | mute that track (16.4) | the same; its button grey | done | [ ] |
| **Shift + Mute + track button** | solo that track, its button blue (16.3) | the same | done | [ ] |
| **Shift + track button** | the track's options (colour, MIDI channel) | nothing yet | MIDI & sync & undo & projects | [ ] |
| **Copy / Delete + track button** | Control Live only (18.2) | free; Melodee copies and deletes with a track button held and the steps | done | [ ] |
| **Shift + Step 5** | Tempo: the wheel, Shift + wheel finer, push confirms (10.1) | the same | done | [ ] |
| **Shift + Step 6** | Metronome on / off (10.3) | the same; the wheel its level | done | [ ] |
| **Shift + Step 7** | Groove, 0 % to 100 % triplet sixteenths and past (10.2) | the same, as Melodee's swing 50-75 % | done | [ ] |
| **Shift + Step 3** | Workflow settings: count-in (13, 14.1.1) | with Rec | sequencer | [ ] |
| **Shift + Steps 1, 2, 8-11, 15, 16** | Set overview, Setup, 16 Pitches, Keys & Scale, full velocity, Repeat / Arp, double loop, quantize (20) | not yet | MOVE_CONTROLS.md decides | [ ] |
| **Note / Session toggle** ("Menu", CC 50) | switch Note and Session; held, a peek (5, 17) | Load (samples, modules) | MOVE_CONTROLS.md decides | [ ] |
| **Loop** | Loop mode: the steps are bars (12.1) | Chop | MOVE_CONTROLS.md decides | [ ] |
| **Volume knob** | the main volume (16.1) | the same | done | [ ] |
| **Wheel, its push, Shift + wheel** | choose, confirm, finer (20, 10.1) | the same on every screen | done | [ ] |
| **Back** | leave a menu or view (20) | the same | done | [ ] |
| **Undo, Shift + Undo** | undo, redo (20) | not yet | MIDI & sync & undo & projects | [ ] |
| **Capture** | the last notes played into a clip (14.3) | not yet | sampling | [ ] |
| **Sample** ("Sampling") | Sampling mode (15.1) | not yet | sampling | [ ] |
