# Melodee on the Move: every control

Kerem's rule (2026-11-20): **every Ableton Move key and key shortcut does
what Ableton made it do; Melodee's own jobs take the controls left free.**
It is the rule melodee-sample follows for the MPC Sample's printed labels
(its `docs/MELODEE_PARITY.md`), applied to the Move. This file is the map:
one row a control or shortcut, what it does on the Move, what it does in
Melodee, and where it stands. `src/app/bindings.*` holds the same map as a
table, and `build/controls-tests` checks one against the other (below).

Source: Ableton's Move manual (<https://www.ableton.com/en/move/manual/>,
chapters 2 and 5-17, its Control Reference and its controls figure,
`MoveControls.png`, which names each Shift job and hold) and the Move
release notes
(<https://www.ableton.com/en/release-notes/move-1/>), read on 2026-11-20.
The newest firmware Ableton lists is **Move 2.1.5** (2 November 2026);
release-note changes to controls (Shift + turn for banks since 1.5, Shift +
track button since 1.3, Shift + Step 14 since 1.3, time signatures in 2.1)
are folded in. Where only Control Live Mode gives a control a job, the row
says so: Melodee follows it when the standalone Move leaves the control
free.

## Names

The Move's names are used here; the code's names (`src/move/controls.h`,
after Schwung) differ for some buttons:

| The Move | `move::Button` / input | MIDI |
| --- | --- | --- |
| Note/Session toggle | `Menu` | CC 50 |
| Plus, Minus | `Up`, `Down` | CC 55, 54 |
| Sampling | `Sample` | CC 118 |
| Track buttons 1-4 (top first) | `Row1`-`Row4` | CC 43-40 |
| Wheel: turn, push, touch | `MainKnob`, `MainPush`, `Touch` 9 | CC 14, CC 3, note 9 |
| Volume knob, its touch | `MasterKnob`, `Touch` 8 | CC 79, note 8 |
| Knobs 1-8, their touch | `Knob` 0-7, `Touch` 0-7 | CC 71-78, notes 0-7 |
| Record | `Rec` | CC 86 |

## How the map reads

- **Screens.** *Play* is the Move's Note Mode: the selected track's Device
  View. *Loop* is Loop Mode, *Session* the Patterns grid (Session Mode),
  *Overview* the projects (the Set Overview). *Browser* is a list opened by
  the wheel's push, *list* any menu or sheet the wheel walks (Setup, a
  module's menu, Sample Options), *Chop*, *To Pads*, *Sampling*, *Typing*
  *Canvas* (a module's own page, entered), *Tracks* (the sixteen
  tracks, latched), *Mixer* (Shift + Step 4), *Knob FX*, *Pad FX* and
  *Flex Beat* (Shift + Step 4 again) are Melodee's. A row with no
  screen holds wherever no row for that screen says otherwise; Typing
  takes every control but the Volume knob and Shift + Back.
- **Tap and hold.** A button with a hold job (Note/Session, Undo, the
  track buttons) does its tap job when let go within 0.4 s with nothing
  else used; held 0.4 s it does its hold job; used as a modifier (another
  control pressed while it is down) it does neither. A button with no hold
  job acts on its press (Back too: a press goes up a level at once, and
  held on it goes all the way out). *Long* (long Plus, long Left) is a
  press held 0.4 s; a *double press* is two within 0.3 s.
- **Pads never wait.** A pad sounds from `App::audio_event()` in the cycle
  it was read, whatever the table says. The table decides only what the
  MIDI thread does with it and whether it should sound at all: while a
  modifier that gives pads another job is held (Shift on a kit, Copy,
  Delete, Mute, a step), the MIDI thread sets one atomic the audio thread
  reads, and the pad stays quiet.
- **Status.** *done*: works so in the app today. *next*: in the table,
  not yet done by the app. *core*: part of core's tracks and transport. *wave 2: area*: the area that builds
  it (sequencer, sampling, kit depth, instruments, mixer & effects, song &
  export, looper, MIDI/sync/undo/projects). *free*: no job on the Move and
  none given in Melodee yet. *none*: no Melodee counterpart, and why.

## Decisions (Kerem, 2026-11-20)

| Control | Decision |
| --- | --- |
| Loop / Chop | **Loop is Loop Mode** (a pattern's bars on the steps). Chop takes the Move's own door to slicing: **Shift + push on the pad's sample** opens Sample Options, Chop where the Move has Slice, beside Reverse (and Melodee's Trim, Normalize, Save). Shift + Loop stayed free, then took Melodee's SEQ pages (below). |
| Note/Session / Load | **Note/Session switches Play and the Patterns grid.** Load is the Device View's: on Play the wheel walks the track's items (the track, its sound: a kit's last pad or a module's preset, its effects), the **push opens that item's browser** (track types, kits and Schwung modules; samples onto the pad; the module's presets, loaded as the wheel turns; Schwung effects). A module's menu moves to **Shift + push**, the pad and module pages to **Shift + turn** (the Move's parameter banks). |
| Track buttons / 16 tracks | **The track buttons are the four tracks of the bank in view.** Holding a track button and pressing step 1-16 picks any track (core's mockup A); **Shift + Note/Session** opens the 16-track overview (as Control Live's Session Overview); in Session, Plus and Minus move the bank. Shift + track button stays the Move's track options. |
| Capture / Audio Recall | **Capture is Melodee's Recall** (the notes played, into a pattern), Shift + Capture clears it. **Shift + Sampling is Audio Recall**: the last 25 s of the input onto the next empty pad, as Shift + Sample Record on the MPC. |
| Kit pads | **32 kit pads** (two banks of 16) as today; **Plus and Minus move 16 pads** through the kit's 8 banks (as Control Live's Drum mode). **Shift + Step 8** turns the right 16 into **16 Pitches** of the last pad (Melodee's 16 Levels, Tune), the left 16 staying the kit; Plus and Minus are then the pitches' octave. |
| Shift + Steps 4, 12, 13 | No Move job (no icon): **4 the Mixer** (16 tracks, their effects), **12 the Looper**, **13 Song**. |
| Shift held | **Nothing on the screen**, as on the stock Move: the steps with a Shift job light, Melodee's 4, 12 and 13 too (Kerem dropped the on-screen Shift card from the mockups). |
| Pads | **Two layouts, the user's choice**: **Rows**, the Move's eight a row (1-8 the bottom row, up to 25-32), or **Banks**, two MPC 4x4 banks side by side (1-16 the left bank from its bottom left, 17-32 the right). Every pad job follows it: kit pads and banks, Chop's slices, 16 Pitches, notes, typing, the lights. Notes in Banks run **up the scale in pad order** (reading (a): 1-16 the left bank, 17-32 on up the right). The setting is **global, in Setup > PADS > LAYOUT**, kept in `Move.settings`; **no shortcut**; typing follows the banks. 16 Pitches is the left 4x4 kit, right 4x4 pitches in both layouts (docs/UI.md, "Pads"; docs/ui/pads.png). |
| Looper (Shift + Step 12) | **A pad row a loop** (layout A of `docs/ui/looper.png`): L1 the top row beside Track 1; its four left pads record / overdub, play / stop on the boundary (Shift: at once), undo / redo, held to clear; **the right 16 keep playing the selected track**. On the Looper **the track buttons are L1-L4** (select, Mute and Shift + Mute with one, held with the Volume knob its level, as the Move's tracks). **Rec records the selected loop** (Ableton: Rec records the selected track); the wheel picks the loop, **its push opens the loop's sheet** (MORE), Shift + the wheel turns to MIX. The screens as drawn (Kerem, 2026-11-20). |
| Undo, quit | **Undo held 0.4 s opens the undo list** (as Melodee's Shift + − hold on the MPC). **Shift + Back goes back to Ableton's Move app**, as today, and Setup (Shift + Step 2) lists it too. |
| Pad pages (kit depth) | **Nine pad pages on Shift + turn**, after the Play screen's own eight knobs (`docs/ui/kit.png`): TRIM, LAYERS (ZONES on an instrument's pad), TUNE (with Warp, Follow and the kit's warp mode), AMP ENV, PLAY, CLOUD, MIX (with kit volume and COLOUR), FILTER (with its envelope), KIT FILT; Back returns to Play. |
| 16 Pitches (kit depth) | **In key**: the right 16 play the last pad up the project's key and scale from its root (Melodee's 16 Levels Scale, the Move's In-Key), the sample counting as its own note; Plus and Minus its octave. |
| Delete + pad (kit depth) | **At once, nothing asked**, as Melodee and the Move do: the pad's notes in the pattern go first; a pad with none is emptied; an empty pad closes the gap, the pads after it moving down one with their notes. Copy onto a pad that holds a sample asks first, as Copy a track does. |
| Kit pad lights (kit depth) | **Dim, with a flare**: a pad with a sample dim in its colour, bright while it sounds; the last pad white; a muted pad dark grey (the Move's dim white); an empty pad off. |
| Effects in the Device View | **The wheel walks the track's effects as devices** (the Move's two a Track Preset): the track, its sound, a kit pad's own four, the track's six, then `+`; the knobs follow the item in focus; the push opens the effect browser (Melodee's families and Schwung's audio effects), **Shift + push the effect's options** (bypass, move, copy, paste, remove), as Sample Options on a pad. |
| Effect shortcuts | The manual gives none; Control Live deactivates a device with Mute + push. **Mute held + push bypasses the effect in focus, Delete held + push removes it, Copy held + push copies it** (and, Copy still held, a push on another slot pastes it). |
| Knob FX, Pad FX, Flex Beat | **Shift + Step 4 again** (on the Mixer) opens Knob FX, again Pad FX, again Flex Beat, then the Mixer once more. |
| Mixer | **Shift + Step 4**: knobs 1-8 are eight tracks of the page (the wheel: levels, pans, sends A-D, MAIN), **Left and Right 1-8 or 9-16**; the 16 steps are the tracks: a step selects, **Mute held + step mutes, Shift + Mute held + step solos** (as with the track buttons); the push opens the page's chain (the track's, a bus's, the master's with its compressor). **Touching the Volume knob shows the main meter**; **Shift + Mute alone lets every mute go** (Melodee's Unmute everything). |
| Step TRIG (sequencer) | Step held + knob stays the Move's per-step automation (locks). **Step held + wheel push turns the knobs to Melodee's TRIG** for the step's notes (chance, condition, ratchet, ramp, pitch ramp, accent, slide, tie); push again, back to the locks. |
| Melodee's sequencer pages | **Shift + Loop (a tap) opens Melodee's SEQ pages**: Pattern (its push: Half and Double Seq and Speed, the variations), Step entry, Roll, Motion, MOD, Macros, Feel, Follow, Lane and Euclid; Shift + turn the page. Held with the wheel, Shift + Loop stays the Move's fine length. Chop stays on Shift + push. |
| Fill | **Loop held 0.4 s while playing is Fill** until it is let go (FILL conditions play). Loop tapped stays Loop Mode; Loop held + wheel the length. |
| Tap tempo, metronome on Rec | **On Tempo (Shift + Step 5) the steps tap the tempo, Left / Right halve and double it**; **Workflow (Shift + Step 3) has Metronome: Always / Rec only** (Melodee's metronome Rec). The Move has neither. |
| Track options: MIDI | **Shift + a track button shows MIDI IN and MIDI OUT**, as the Move (1.5) does: MIDI IN is AUTO or a channel 1-16 that plays the track whichever is selected (Melodee's track channel); MIDI OUT is AUTO or the channel its notes go out on, kept in a section of the Move's own in the project (the MPC keeps one channel); a MIDI track's MIDI OUT is its DEVICE channel. |
| MIDI Panic | **No Move key**: MIDI Config's PANIC row (push) and `melodee-midi panic`, as Melodee's P4 kept it. Stopping already ends the notes sent. |
| A MIDI track's slots | **Shift + a knob on a CC page is what that slot sends** (the Move's second parameter): touched, the address shows large; turned, it changes (CC, 14-bit CC, NRPN, bend, pressure). |
| Schwung modules' MIDI out | **A module's `midi_send_external` goes out on the Move's MIDI port** with Melodee's own MIDI. |
| Set Overview (Shift + Step 1) | **The Move's way** (Kerem, mockups `docs/ui/projects.png`): the projects of a folder on the 32 pads, each keeping its pad as a Set keeps its slot. **A pad picks** (pulsing); **Back or a track button opens it**, as the Move enters Note Mode with the Set picked; an empty pad, then Back or a track button, makes a new project there. **The wheel's push opens the picked one's options** (Melodee's project sheet: Save, Save as new, Rename, Colour, Duplicate, Move to folder, New folder, Open at start; on another: Open, Load kit, Delete), Shift + a pad too (the Move's Set options). |
| Typing | **The Move's own** (its Wi-Fi password): the wheel walks a strip of characters and its push types the one in the box; the strip ends with SPACE, DEL and OK, and OK finishes. The pads still type letters at once and the steps digits; Back deletes, held cancels; Shift is upper case. Every text: names and a module's page. |
| The Menu | **No Menu on the Move**: Melodee's HERE, GO and its lessons are left out; Shift + the steps are the places. |
| Light show | **Setup > Lights only** (LIGHT SHOW, SHOW STYLE): no key. |

Followed from the rule without a choice: Left and Right become the
pattern's bars, so pages move to Shift + turn; in Typing, Back deletes a
character as on the Move, and holding Back cancels; in Chop the wheel sets
the amount, as the Move's wheel sets the number of slices. Delete + a knob's
touch deletes its motion, as on the Move; where the knob has none, it does
Melodee's Erase + pot (the value back to its start). In the overview, Delete
alone asks to delete the project picked (Melodee's Erase tapped), Plus and
Minus page through the folder's 32-pad pages and the wheel walks the pads,
none of which the Move uses there; a track held with Shift + Copy, a step,
then another moves a track (Melodee's Move).

## The map

### Pads

A pad's job comes from its number in the layout chosen in Setup > PADS >
LAYOUT (Rows or Banks, the decision above; `src/app/pad_layout.h`): "pad
n", "the right 16" and "slice n" below are in that layout's numbering. The
bindings table matches pads by where they lie (16 Pitches' right 4x4), the
screens and the audio thread turn the pad into its number; `audio_event()`
does it once for every note a pad plays or writes (Seq Record, Capture,
Repeat, the arp go through the track and key it works out). The Move's
grids keep where the pads lie in both layouts (the decision above):
Session (a row a track of the bank, a column a pattern) and the Overview.

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Pad (Play, kit) | Plays the Drum Rack's pad (the left 16), by velocity | Plays the kit pad and makes it the last pad; 32 pads, two banks of 16 | done |
| Pad (Play, synth) | Plays notes, In-Key or Chromatic, in octaves or 4ths (Shift + Step 9) | Plays the module's notes in rows of fourths, C2 bottom left; the other layouts with Keys & Scales | done |
| Pad, right 16 (16 Pitches) | The selected drum pad's sample at 16 pitches of the key and scale | The last pad up the project's key from its own note (Melodee's 16 Levels Scale), from the audio thread in the cycle it was read | done |
| Pad pressure | Polyphonic aftertouch (Drift, Wavetable) | Polyphonic aftertouch to the Schwung module; a kit pad's pressure waits for its job | done |
| Shift + Pad (Play, kit) | No job (Control Live's Drum mode: selects a pad without playing it) | Makes the pad the last pad without sounding it (the MPC's Sample held + pad) | done |
| Pad (Browser) | Picks the drum pad the browser loads onto; the browser stays where it is | Plays the pad and makes it the one a sample loads onto | done |
| Pad (Chop) | In Slicing Mode, picks the slice the third knob moves | Plays the slice and selects it, 32 at once | done |
| Pad (Typing) | (Text is typed with the wheel) | A key: letters, space, backspace | done |
| Pad (Sampling) | Records into that pad (pulsing red); the pad again, any pad or Sampling stops; held, records until let go; another pad while recording carries on onto it from there | Melodee's Record onto the pad; a pad hit while recording starts the next part there | wave 2: sampling |
| Pad (Session) | Launches the clip (a row a track, a column a scene); an empty slot is selected for a new clip; sliding down a column plays the scene | Launches the track's pattern; a column is a scene | done |
| Pad held + Pad (Session) | No job | Chains the held pad's pattern with the next ones of its row: each plays once, then round again (Melodee's CHAN) | done |
| Shift + Pad (Session) | With an audio clip copied: pastes it bounced into an audio clip slot | Pastes the copied pattern printed to audio | wave 2: song & export |
| Pad (Overview) | Selects a Set; Play previews it | Picks a project (pulsing), a folder or an empty pad; Back or a track button opens it | done |
| Shift + Pad (Overview) | The Set's options: colour, Ableton Cloud | The project's options: colour, and Melodee's project sheet (save as new, rename, duplicate, move, load kit...) | done |
| Pad held + Volume knob | The sample's gain | The pad's level | done |
| Pad held + Volume knob (Overview) | The Set's volume, all four tracks at once | The open project's volume, all its tracks at once | done |
| Copy held + Pad (Play, kit) | No job (Control Live's Drum mode: copies the pad's devices onto the next pad pressed) | Copies the pad's sound onto the next pad pressed (its sample and values, chain, mute); onto a pad that holds a sample it asks first | done |
| Copy held + Pad (Session) | Copies the clip, then pastes it on the next pad pressed (asks before replacing) | Copies the pattern | done |
| Copy held + Pad (Overview) | Copies the Set onto the next pad (asks before replacing) | The same for the project | done |
| Delete held + Pad (Play, kit) | Deletes the pad's notes in the clip | Deletes its notes; a pad with none is emptied (as Control Live's Drum mode and the MPC's Erase + pad); an empty pad closes the gap, the pads after it down one with their notes (Melodee's Close a gap) | done |
| Delete held + Pad (Session) | Deletes the clip at once | Deletes the pattern | done |
| Delete held + Pad (Overview) | Deletes the Set, the pad again to confirm | The same: the question, the pad again (or push) deletes it | done |
| Delete held + Pad (Chop) | No job (Control Live's Slicing mode: deletes the slice) | Takes the slice's cut out | wave 2: sampling |
| Mute held + Pad (Play, kit) | Mutes or unmutes the drum pad (dim white) | Mutes or unmutes the kit pad, saved with the kit (dim white) | done |
| Shift + Mute held + Pad (Play, kit) | No job (Shift + Mute + a track button solos a track) | Solos the kit pad: every other pad with a sound muted; the same pad again, the kit back | done |
| Pad (Knob FX, kit) | (No Knob FX on the Move) | Knob FX on or off for that kit pad (lit white while on); quiet | done |
| Pad (Pad FX) | (No Pad FX on the Move) | A punch while held, pressure its depth; up to four at once, a fifth takes the oldest's place | done |
| Delete held + Pad (Pad FX) | (As above) | Takes that punch's kept punches out of the pattern | done |
| Pad (Flex Beat) | (No Flex Beat on the Move) | Pads 1-16: pad 1 Empty, 2-16 the fifteen time effects over the master | done |
| Step held + Pad | Adds or removes that note on the step | The same on the pattern | done |
| Step held + Pad (Loop) | The note on every step of the bar | The same | done |
| Shift + Step 16 held + Pad | Quantizes that drum pad's notes | Time Correct on that pad | done |

### Steps

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Step (Play) | Puts the last pad's note (or the held pads') on the step; a step with notes is cleared | Step Edit, the same | done |
| Step held + Volume knob | The notes' velocity | The same | done |
| Step held + Wheel turn | The notes' length, a tenth of a step a detent | The same | done |
| Step held + Step | The note's length up to that step | The same | done |
| Step held + Plus or Minus | Transposes the notes a semitone | The same | done |
| Step held + long Plus or Minus | Transposes the notes an octave | The same | done |
| Step held + Left or Right | Nudges the notes a tenth of a step | The same | done |
| Shift + Step held + Left or Right | Nudges a hundredth of a step | The same | done |
| Step held + long Left or Right | Nudges a whole step | The same | done |
| Step held + Knob | Per-step automation, the LEDs after it red for its range | Per-step motion | done |
| Step held + Wheel push | No job | The knobs turn to Melodee's TRIG for the step's notes (chance, condition, ratchet, ramps, accent, slide, tie); again, back to the locks | done |
| Step (Loop) | Selects the bar (its steps on the step buttons) | The pattern's bar | done |
| Step held + Step (Loop) | The loop from the first bar to the second (or both pressed at once) | The pattern's loop | done |
| Step double press (Loop) | A one-bar loop | The same | done |
| Step held + Wheel turn (Loop) | The bar's note lengths | The same | done |
| Step held + Volume knob (Loop) | The bar's velocities | The same | done |
| Step held + Plus or Minus (Loop) | Transposes the bar | The same | done |
| Step held + Left or Right (Loop) | Nudges the bar | The same | done |
| Copy held + Step | Copies the step (hold the first and press the last for a range), then pastes where the next step is pressed | The same | done |
| Copy held + Step (Loop) | Copies the bar | The same | done |
| Delete held + Step (Loop) | Clears the bar's notes | The same | done |
| Step (Chop) | No job | Plays the sample from that sixteenth (the steps are the sample in 16 parts, lit where a cut falls) | wave 2: sampling |
| Step (Typing) | (Text is typed with the wheel) | A digit, steps 1-10 | done |
| Track button held + Step | No job | Picks track 1-16; an empty one opens the new-track sheet | done |
| Track button held + Mute held + Step | No job | Mutes or unmutes track 1-16 | done |
| Track button held + Shift + Mute held + Step | No job | Solos or unsolos track 1-16 | done |
| Step (Mixer) | (No Mixer on the Move) | Selects that track | done |
| Mute held + Step (Mixer) | (As above) | Mutes or unmutes that track | done |
| Shift + Mute held + Step (Mixer) | (As above) | Solos or unsolos that track | done |
| Step (Knob FX) | (No Knob FX on the Move) | Knob FX on or off for that track | done |

### Shift + steps

The Move prints an icon under the steps that have a Shift job and lights it
while Shift is held.

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Shift + Step 1 | The Set Overview | Projects, 32 to a page, each keeping its pad | done |
| Shift + Step 2 | Setup: battery, Wi-Fi, update, Cloud, Control Live, Link, MIDI and MIDI Sync, USB-C audio, Move Manager, brightness | Setup: Melodee's settings (pads, lights and brightness, Basic or Full, the usage log, About), MIDI and sync, the web page, Back to Move | done |
| Shift + Step 3 | Workflow Settings: quantize amount, step grid, count-in, autoload, time signature, maximum recording length | The same for Melodee's patterns | done |
| Shift + Step 4 | No job | The Mixer: 16 tracks, their effects; on the Mixer, Knob FX, then Pad FX, then Flex Beat | done |
| Shift + Step 5 | Tempo (the wheel; Shift for fine; push confirms) | The Tempo screen (today on Shift + Play) | done |
| Shift + Step 6 | Metronome on or off | The same (today a push on the Tempo screen) | done |
| Shift + Step 7 | Groove: triplet sixteenth swing, 0-100 % and beyond | Swing (today knob 2 on the Tempo screen) | done |
| Shift + Step 8 | Layout: 16 Pitches on or off (drums) | 16 Pitches on the right 16 pads, the left 16 one bank of the kit as the MPC lays it | done |
| Shift + Step 9 | Keys & Scales: In-Key or Chromatic, octaves or 4ths, key, scale | The project's key and scale, a synth's layout | wave 2: instruments |
| Shift + Step 10 | Full Velocity on or off | Full Level | wave 2: instruments |
| Shift + Step 11 | Repeat and the Arpeggiator (rate; Repeat, Up, Down, Random) | Note Repeat and Arp | done |
| Shift + Step 12 | No job | The Looper; on it, back to Play | done |
| Shift + Step 13 | No job | Song | wave 2: song & export |
| Shift + Step 14 | A new clip for the selected track, selected | The track's next empty pattern | done |
| Shift + Step 15 | Doubles the loop with its notes and automation | Double Seq | done |
| Shift + Step 16 | Quantizes the clip by Workflow's amount | Time Correct | done |

### Track buttons

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Track button | Selects the track (and Note Mode, from the Overview or Session) | Selects that track of the bank in view, as it is let go (today on the press) | done |
| Track button held | Shows that track while held, without selecting it | The sixteen tracks on the screen, the steps picking (core's design A; it selects the track on the press today) | done |
| Track button held + Volume knob | The track's volume, in dB on the screen | The track's level | done |
| Shift + Track button | The track's options: colour, MIDI In and Out channel | Selects it and shows its options: its colour, MIDI IN and MIDI OUT (docs/UI.md, MIDI and sync) | done |
| Mute held + Track button | Mutes or unmutes the track (its LED dims) | The same | done |
| Shift + Mute held + Track button | Solos or unsolos the track (its LED blue) | The same | done |
| Track button held + Copy held + Step | No job (Control Live: Copy + a track button duplicates the track) | Copies that track onto the next step's, asking before replacing | done |
| Track button held + Delete held + Step | No job (Control Live: Delete + a track button deletes the track) | Deletes that track, asked first | done |
| Step (Tracks) | (No Melodee screen of tracks on the Move) | In the sixteen tracks latched by Shift + Note/Session: picks that track | done |
| Copy held + Step (Tracks) | (As above) | Copies that track onto the next step's | done |
| Delete held + Step (Tracks) | (As above) | Deletes that track, asked first | done |
| Mute held + Step (Tracks) | (As above) | Mutes or unmutes that track | done |
| Shift + Mute held + Step (Tracks) | (As above) | Solos or unsolos that track | done |
| Track button held + Shift + Copy held + Step | No job | Moves that track: then a step, where it goes; a track there trades places (Melodee's Move) | done |
| Shift + Copy held + Step (Tracks) | (As above) | The same | done |

### Knobs

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Knob | The item in focus's parameters; while recording, written as automation (its LED red) | The screen's eight: a pad page, a module's page; motion with the sequencer | done |
| Knob touch | Shows the parameter | The knob large | done |
| Shift + Knob | Fine (Transpose detunes, Sample Start zooms) | Fine | done |
| Shift + Knob touch | The knob's second parameter (Length, Velocity > Vol, LFO Rate) | On a kit, the knob's fine view: TUNE's cents, START and END close up (the Move's Sample Start zoom), a page's value in full; Shift + turn moves it finely | done |
| Delete held + Knob touch | Deletes the parameter's automation | Deletes its motion | done |
| Mute held + Knob touch | Turns the parameter's automation off or on | The same for its motion | done |
| Knob (Chop) | Slicing Mode: knob 1 and 2 the region's start and end, knob 3 the pressed slice's start | Knob 1 and 2 the selected slice's start and end, 3 the chop type, 4 the amount | done |
| Knob (Session) | The Set's two main effects (the wheel picks one) | The main effects (the compressor and the master chain) | wave 2: mixer & effects |

### Volume knob

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Volume knob | The main output, a meter on the screen | Main volume | done |
| Volume touch | Shows the output level | The main meter, left and right, in dB | done |

### The wheel

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Wheel turn (Play) | Device View: walks the Track Preset, its instrument, sample and effects | Walks the track, its sound (a kit's last pad, a module's preset) and its effects | done |
| Wheel push (Play) | Opens the browser for the item in focus | Opens its browser: track types, kits and Schwung modules; samples onto the pad; the module's presets; Schwung's effects | done |
| Shift + Wheel push (Play, kit) | Sample Options for the sample in focus: Slice, Reverse | Sample Options: Chop, Reverse, Trim, Normalize, Save | done |
| Shift + Wheel push (Play, synth) | No job (in the browser, Shift opens Options: Save Preset) | The module's menu: its levels, presets and own pages | done |
| Shift + Wheel turn (Play) | The parameter banks of the instrument or sample in focus | The pad pages, a module's pages | done |
| Wheel touch (Play) | Highlights the sample (audio track) | Names the item in focus | done |
| Mute held + Wheel push (Play) | No job (Control Live: deactivates the device) | Bypasses the effect in focus, or brings it back | done |
| Delete held + Wheel push (Play) | No job | Removes the effect in focus; the rest close up | done |
| Copy held + Wheel push (Play) | No job | Copies the effect in focus; Copy still held, a push on another slot pastes it | done |
| Copy held + Wheel turn (Play) | No job | Walks the items as without Copy, to the slot to paste on | done |
| Wheel turn (Mixer) | (No Mixer on the Move) | The page: levels, pans, sends A-D, MAIN | done |
| Wheel push (Mixer) | (As above) | The page's chain: the selected track's, the bus's, the master's | done |
| Wheel turn (Knob FX) | (No Knob FX on the Move) | Walks the MPC's 28 and Melodee's 15 | done |
| Wheel push (Knob FX) | (As above) | Loads the one turned to | done |
| Shift + Wheel push (Knob FX) | (As above) | Every track (or the bank's kit pads) on, or all off | done |
| Mute held + Wheel push (Knob FX) | (As above) | Bypasses Knob FX, or brings it back | done |
| Wheel turn (Pad FX) | (No Pad FX on the Move) | The other bank of punches | done |
| Wheel push (Pad FX) | (As above) | KEEP: the take laid into the pattern playing | done |
| Shift + Wheel push (Pad FX) | (As above) | Its options: latch, master or track, quantize, snap, tail | done |
| Wheel turn (Browser) | Moves through the list, loading each preset (Autoload) | Moves through the list; a module's presets load as it turns | done |
| Wheel push (Browser) | Opens a folder; loads (Autoload off); leaves with the preset chosen | Opens a folder, loads onto the pad or the track | done |
| Shift held (Browser) | Options: Save Preset (Shift held) | Options: Save Sample, Save Bank (the two banks in view, as `.mldb`); a module's preset waits for the instruments | done |
| Wheel turn (list) | Moves through a menu; turns a value | The same | done |
| Shift + Wheel turn (list) | Fine (tempo, groove) | Fine | done |
| Wheel push (list) | Opens an entry, switches it, confirms | The same | done |
| Wheel long push | Confirms a factory reset | Melodee never resets the Move | none |
| Wheel turn (Chop) | Slicing Mode: the number of slices | The chop's amount: slices, beat division, threshold | done |
| Wheel push (Chop) | Confirms the slices onto a new Drum Rack | To Pads: Poly, Mono, Thru, Kit | done |
| Wheel turn (To Pads) | (Slicing Mode places at once) | Picks Poly, Mono, Thru or Kit | done |
| Wheel push (To Pads) | (Slicing Mode places at once) | Places the slices on the first empty pads | done |
| Wheel turn (Sampling) | Picks gain, monitoring or source | Melodee's input: source, monitoring, threshold | wave 2: sampling |
| Wheel push (Sampling) | Switches the source (Mic, Line in, Line in mono, Resampling, USB-C) or the setting | The same | wave 2: sampling |
| Wheel touch (Sampling) | Shows the input source | The same | wave 2: sampling |
| Wheel turn (Canvas) | No job | The module's own page | done |
| Wheel push (Canvas) | No job | Enters the page, then the page's push | done |
| Wheel push (Typing) | Types the character, or Enter | Done | done |
| Wheel turn (Session) | No job | Moves the playhead by the pattern's Q (Melodee's Shift + wheel on Patterns); stopped, Play continues from it | done |
| Wheel push (Session) | No job (a scene launches by sliding down a column) | Launches the selected pattern's column on every track (Melodee's sequence), with its tempo | done |
| Loop held + Wheel turn | The loop's length in bars | The pattern's length | done |
| Shift + Loop held + Wheel turn | The loop's length, fine | The same | done |
| Wheel turn (Typing) | Moves through the characters | The same: the strip of characters, SPACE, DEL and OK at its end | done |
| Wheel turn (Overview) | No job | Picks the next or previous pad's project | done |
| Wheel push (Overview) | No job | The picked project's options (as Shift + its pad); on a folder, goes in | done |

### Transport

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Play | Starts or stops; stops a recording too | The same | done |
| Shift + Play | Restarts every playing clip | Restarts every pattern from its top | done |
| Play (Overview) | Previews the selected Set | Opens the project picked and plays it, the overview staying (the open one: Play) | done |
| Rec | Records into the selected track (count-in first); again stops, playback goes on | Melodee's Seq Record: a bar's count-in from stopped (Workflow), at once while playing, overdub, Rec Quantize; a free pattern grows a bar at a time | done |
| Rec (Sampling) | On an audio track: records into the clip | Into an audio track's take | wave 2: sampling |

### Capture and Sampling

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Capture | Keeps the notes (and knob moves) played without recording, in a clip; stopped, sets the tempo from them (the wheel picks one of three) | Melodee's Recall | done |
| Shift + Capture | Clears what Capture would keep | The same | done |
| Sampling | Sampling Mode: press the pad to record into; again or Back leaves; on an audio track, arms it | Melodee's Record | wave 2: sampling |
| Shift + Sampling | No job | Audio Recall: the last 25 s of the input onto the next empty pad | wave 2: sampling |

### Loop, Mute, Undo, Copy, Delete

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Loop | Loop Mode: the clip's bars on the steps | The pattern's Loop Mode | done |
| Shift + Loop | No job | Melodee's SEQ pages (Pattern, Roll, Motion, MOD, Macros, Feel, Follow, Vary, Lane); Shift + turn the page | done |
| Loop held | No job (held with the wheel: the length) | Fill while playing: FILL conditions play until it is let go | done |
| Mute | Mutes or unmutes the selected track | The same | done |
| Shift + Mute | No job (Shift + Mute + a track button solos) | Let go alone: every mute and solo let go, the tracks' and the kit pads' | done |
| Undo | Undoes (a preset just loaded too) | Melodee's undo: the last step, named on the screen (`UNDO TUNE A05`) | done |
| Shift + Undo | Redoes | The same | done |
| Undo held | No job | The undo list: the steps with their track and age, back or forward to any one | done |
| Copy | Duplicates the selected clip into the next slot; again clears the clipboard | Duplicates the pattern | done |
| Shift + Copy | No job | None given | free |
| Delete | Deletes the selected track's clip | Deletes the pattern | done |
| Delete (Typing) | (Back deletes on the Move) | A backspace | done |
| Shift + Delete | No job | None given | free |
| Delete (Overview) | No job (Delete + a pad deletes) | Asks to delete the project picked (Melodee's Erase tapped) | done |

### Note/Session, Back, Shift

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Note/Session | Switches Note and Session Mode; from the Overview, Session | Switches Play and the Patterns grid (today it opens Load, which moves to the wheel's push) | done |
| Note/Session held | Shows the other mode while held | The same | done |
| Shift + Note/Session | No job (Control Live: the Session Overview) | The sixteen tracks (today a track button held) | done |
| Back | Leaves a menu, the browser or Sampling Mode; up a folder; from the Overview, the mode before | The same | done |
| Back held | Leaves the menu or view altogether | Back to Play from any depth | done |
| Back (Chop) | Confirms and leaves Slicing Mode, the slices on the pads | Leaves Chop keeping the chop; the pads change only by To Pads | done |
| Back (Canvas) | No job | The page's own Back, then out of it | done |
| Back (Typing) | Deletes a character | The same | done |
| Back held (Typing) | Leaves the text entry | Cancels the typing | done |
| Shift + Back | No job | Back to Ableton's Move app (the runtime takes it) | done |
| Shift held | Lights the icons under the steps with a Shift job; fine turns | The steps with a Shift job light, 4, 12 and 13 too; the screen stays as it is | done |
| Shift double press | Locks Shift on until pressed again (Shift + Play then does not retrigger) | The same | done |
| Shift held (Typing) | Uppercase while held | The same | done |

### Plus and Minus, Left and Right

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Plus or Minus (Play, synth) | The octave | The same | done |
| Plus or Minus (Play, kit) | No job without 16 Pitches (Control Live's Drum mode: the next or previous 16 pads) | The next or previous 16 pads of the kit's 8 banks | done |
| Plus or Minus (16 Pitches) | The pitches' octave | The same | done |
| Shift + Plus or Minus | No job (Control Live: a scale degree; four pads) | A synth a scale degree, a kit four pads | wave 2: instruments |
| Plus or Minus (Session) | No job (Control Live: the session ring a scene) | The bank of four tracks | done |
| Plus or Minus (Chop) | No job | Slices 33-64 | wave 2: sampling |
| Left or Right (Play) | The clip's previous or next bar (or page of steps) | The pattern's bar (today the page, which moves to Shift + turn) | done |
| Shift + Left or Right | No job (Control Live: 16 bars) | 16 bars | done |
| Left or Right (Session) | No job (Control Live: the session ring a track) | The patterns' page, eight at a time | done |
| Left or Right (Chop) | No job | The slice before or after | done |
| Left or Right (Mixer) | (No Mixer on the Move) | Tracks 1-8 or 9-16 | done |
| Plus or Minus (Pad FX) | (No Pad FX on the Move) | The other bank of punches | done |
| Plus or Minus (Overview) | No job | The folder's next or previous 32 pads | done |

### Outside the surface

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Power button | On; off after a push of the wheel; held 10 s, off at once | The Move's own: it never reaches the app | none |
| Control Live (Setup) | Makes the Move a control surface for Live | Melodee plays itself; Ableton's app does this | none |

### The Looper

Shift + Step 12 (docs/UI.md, "Looper"; `docs/ui/looper.png`). The Move has
no looper: its controls keep their meaning for the four loop tracks L1-L4
(a track button selects one, Mute mutes one, Rec records into the
selected one), and a pad row is a loop, L1 the top row beside Track 1.
What has no row here holds as on Play (Play, Shift + Play, the knobs and
their touch, Plus and Minus, Back, a track button held and a step).

| Control | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Pad, left 16 (Looper) | (No Looper on the Move) | A loop's row, L1 on top: record, close, overdub and play again (as Rec); play or stop on the boundary, again cancels; undo then redo the last overdub; held 0.6 s, clear. By the pad map: Rows L1 pads 25-28 to L4 1-4; Banks the left bank, L1 13-16 to L4 1-4 | done |
| Shift + Pad, left 16 (Looper) | (As above) | The play / stop pad at once; the rest as without Shift | done |
| Pad, right 16 (Looper, kit) | (As above) | The selected kit's bank of 16 (the last pad's), A01 bottom left (Banks: the right bank, 17-32); the pad played is the last pad | done |
| Pad, right 16 (Looper, synth) | (As above) | 16 notes up from C at the track's octave | done |
| Track button (Looper) | Selects the track | Selects the loop track, L1 the top button | done |
| Track button held + Volume knob (Looper) | The track's volume, in dB | The loop track's level, in dB | done |
| Mute (Looper) | Mutes the selected track | Mutes the selected loop | done |
| Mute held + Track button (Looper) | Mutes that track | Mutes that loop | done |
| Shift + Mute held + Track button (Looper) | Solos that track | Solos that loop | done |
| Rec (Looper) | Records into the selected track | Records the selected loop: record, close, overdub and play again; one at another tempo is fitted first; stopped, Play starts it | done |
| Wheel turn (Looper) | (Moves through a list) | Picks the loop; on the loop's sheet, its item | done |
| Wheel push (Looper) | (Opens, confirms) | The loop's sheet (MORE); on it, its item; the Looper off, turns it on | done |
| Shift + Wheel turn (Looper) | The parameter banks | LOOPS and MIX | done |
| Shift + Wheel push (Looper) | Options for the item in focus | Nothing yet: the loop's sheet is the push | free |
| Step (Looper) | (Step Edit) | Nothing: the steps show the selected loop's place | free |

## Lights

Not checked by the tests: they follow from the screens.

| Light | On the Move | In Melodee | Status |
| --- | --- | --- | --- |
| Pads, a kit | The Drum Rack's pads in their colours; a muted pad dim white | The two banks in view: each pad dim in its colour (COLOUR, or the track's), bright while it sounds; the last one white; muted dark grey; empty off | done |
| Pads, a synth | Roots in the track's colour, the scale's notes light grey, the rest dark | Every C in the track's colour, the other naturals dim; scales with Keys & Scales | done |
| Pads, 16 Pitches | The right 16: roots in the track's colour, the scale light grey | The same | done |
| Pads, Session | Clips in the track's colour; the selected one pulsing white; one waiting to start pulsing green, one about to stop pulsing in its colour | The same for patterns | done |
| Pads, Overview | Sets in their colours, the selected one pulsing; an empty slot dark, white when selected | The same for projects; folders grey | done |
| Pads, Browser | All pulse while browsing presets; the target pad pulses while browsing samples | The target pad blinks while browsing samples; presets wait for the instruments | next |
| Pads, Sampling | The pads that can take a sample pink; the one recording pulsing red | The same | wave 2: sampling |
| Steps, Play | White with notes; the track's colour dim where empty; grey dim past the loop; the playhead green; brighter under a long note; red over a step's automation | The same | done |
| Steps, Loop | The selected bar white; bars in the loop the track's colour; the rest dim | The same | done |
| Steps, Shift held | The icons under the steps with a job | Those steps white dim; one that is on (metronome, 16 Pitches, Full Velocity, Repeat) bright | next |
| Steps, Chop | No job | The sixteenths where a cut falls; the selected slice's white | done |
| Step icons | Setup's flashes while open; groove's and the metronome's stay lit while on | The same, if the icon LEDs answer as Schwung lists them (CC 16-31, LED only): to check on the device | next |
| Track buttons | Each track's colour; while playing, its level (white when loud); a solo blue; a mute dim | The same for the bank in view | core |
| Play | Lit while playing | The same | core |
| Rec | Dim when it can record, flashing in the count-in, solid red recording, dark when it cannot | The same | done |
| Sampling | Pulses red in Sampling Mode | The same | wave 2: sampling |
| Capture | Lit while there is something to keep, dark once cleared | The same | done |
| Steps, Mixer | No Mixer | The sixteen tracks: each its colour, the selected one white, a muted one grey, a soloed one blue, an empty one dark | done |
| Pads, Knob FX | No Knob FX | A kit's pads with Knob FX white, the rest dim; the steps the tracks with it bright | done |
| Pads, Pad FX | No Pad FX | The MPC's sixteen orange, Melodee's cyan, a sounding one white; the steps where the kept punches fall | done |
| Pads, Flex Beat | No Flex Beat | Pads 1-16 violet, pad 1 (Empty) grey, the one playing white | done |
| Looper, pads | (No Looper on the Move) | A loop's row: record red (blinking armed), orange overdubbing, its colour dim otherwise; play / stop its colour, white on the bar, dim stopped, blinking with a change waiting; undo white, redo grey; clear dim red, red while held | done |
| Looper, track buttons | (Tracks) | L1-L4 in their colours (mint, yellow, purple, sky), the selected one bright, an empty one dark; muted grey, soloed blue | done |
| Looper, steps | (The playhead) | The selected loop across its length, its place white; a take running, the bar's sixteenths red | done |
| Looper, Rec | (Recording) | Red while a loop records or overdubs, blinking while one is armed | done |
| Knob LEDs | Red with automation; with Mute held, red active, white off, dark none; green for a macro; all pulse while browsing effects | Lit for the knobs the screen uses; motion's colours with the sequencer | done |
| White buttons | Lit where they do something | The same | done |

## How the app runs the map

Every control goes through the table. `MelodeeApp::surface()` hands each
input to a `bindings::Gestures`, looks each use up with `bindings::find()`
(the screen on top's `screen_kind()`, the track's kind) and does its job:
the app's own (the track buttons, transport, the Volume knob, mute and
solo, typing), else the job of the screen on top (`Screen::job()`), else
that of the track's Play screen under it, else the sequencer's
(`src/app/sequencer.*`: the steps, bars and patterns, Rec, Capture), else
it opens the screen whose `Trigger` names the job (Tempo, Metronome,
Groove, Workflow, Repeat, Loop Mode, Session, the SEQ pages). Screens never read
buttons; `Screen::input()` only shows what is held. Holds come due in
`lights()`. The audio thread keeps a pad quiet while `pad_sounds()` says it
has another job (`Studio::quiet_pads()`).

What changed for the hands, from the app before the rule:

| Before | Now |
| --- | --- |
| Loop: Chop | Shift + push on the pad's sample: Sample Options, Chop |
| Note/Session (`Menu`): Load | The wheel's push on the Device View's pad (samples) or module (modules) |
| A synth's push: its menu | Shift + push |
| The wheel on a synth: its presets | The wheel to the preset, push, then the wheel (loaded as it turns) |
| Left / Right: a module's pages, a kit's pad | Shift + the wheel: the pages; Left / Right wait for the bars (wave 2) |
| Shift + Play: Tempo | Shift + Step 5; Shift + Play restarts the playing patterns |
| A track button: selects at once | Selects as it is let go; held, the sixteen |
| Typing: Back or Menu cancels | Back deletes a character; held, it cancels |

`docs/PARITY.md`'s Move controls column holds proposals from before the
rule (Shift + Step n as the MPC's Shift + Pad n, Menu to load, Shift +
Capture for Recall, Left / Right for pages): this map wins where they
differ.

## Undo steps

Undo needs nothing from a job but its name. The studio keeps a copy of the
project as the last step left it (`src/app/history.*`): Melodee's
`Project` (its bytes, the parts that changed), the sample and chop slots,
the patterns, the Move's own part (`MoveExtras`: colours) and each Schwung
module's values set through `Studio::synth_set()`. The app closes a step
(`Studio::close_step()`) after every press's job, and after a turn's: the
same knob or wheel turning on is the same step, until another control is
used or it rests two seconds (Melodee's rule); the service thread closes
its own after a sample or a module is loaded. Whatever changed since the
last close is the step.

A screen marks a step by changing the project as it does today (and
calling `Studio::changed()` for the autosave). It may:

- name it, for the screen and the undo list: `studio.name_step("CLEAR
  P3", track)` in its `job()`; else a knob's turns are its name and owner
  (`TUNE A05`, from `Screen::knob()` and `knob_owner()`), the rest the
  job's name spelled out (`DUPLICATE PATTERN`);
- say a change is no step (performance, as Melodee leaves out mutes,
  solos, the octave, the pad played, the pattern launched):
  `studio.not_a_step()`, or add the job to `no_step()` in
  `melodee_app.cpp`; the next close takes the change in without a step, and
  no undo touches it later;
- hold a step open past its press (a take): `studio.hold_step("TAKE",
  track)`, then `studio.release_step()`.

An undo puts the changed parts back and tells the engine: a `TrackLoad`
for each track it touched, the tempo and swing by their commands, and
anything else of the project (a bus, the master, the song) by rebuilding
the engine with the audio thread parked (`Studio::rebuild()`, the
transport going on from the top). A module comes back with its values, or
the module replaced is loaded again as it was. A project opened or made
forgets the steps, as in Melodee.

## The tests

`build/controls-tests` (`tests/controls_tests.cpp`, run by `./build.sh`)
reads the tables under "The map" above and, for every row not marked
*none*: finds the bindings table's entry carrying the same Control text and
status, and feeds that entry's input (the screen, the track, the held
modifiers, the control, the gesture) through `bindings::resolve()`,
expecting the entry's job back. Every entry in the table must have its row
here, so the doc and the code cannot drift. The gesture tracker (tap, hold,
long, double, used as a modifier) has tests of its own.

## Mockups

`build/melodee-move --ui-mockups DIR` draws the screens this map moves or
adds (`src/ui/mockups_controls.cpp`): `docs/ui/controls.png`; the
sequencer's (`src/ui/mockups_sequencer.cpp`): `docs/ui/sequencer.png`.
