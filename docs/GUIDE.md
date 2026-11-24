# Melodee on the Move

Melodee, the groovebox by Kerem Kiliç (Ellic Studio), on the Ableton Move:
its sampler and Chop on the 32 pads, sixteen tracks of kits, Schwung's synths,
audio and MIDI, a sequencer, a mixer with sends and buses, Song and a Looper.
Every Move key does what Ableton made it do; Melodee's own jobs take the keys
the Move leaves free. This guide is how to find them; docs/MOVE_CONTROLS.md
in the source has every control.

## Start and stop

- **From Schwung's Tools menu**: Tools > Melodee. Ableton's app stops while
  Melodee runs and comes back when it ends.
- **At power-on**, if you choose it: while the Move shows **Loading ...
  press Back to change**, press **Back**, turn the jogwheel to **Melodee**
  and press it. The Move starts in Melodee at every power-on from then on;
  choose **Schwung** the same way to change it back. Schwung Manager's
  **Boot** page (http://move.local:7700/boot) does the same from a browser.
- **Shift + Back**: back to Move, also when the Move started in Melodee.
  Save your Set before starting Melodee, as for any quit of Move.
- **Power button**: Melodee asks **SHUT DOWN?**, with the battery's charge
  beside it. Push the jogwheel to save and turn the Move off; **Back** keeps
  it on. Held for ten seconds, the button turns the Move off at once,
  unsaved.
- **Volume knob**: the volume, kept from one start to the next.

## The web page

Open **http://move.local:8080** on a computer joined to the Move.

- **By USB cable**: the page opens at once.
- **On Wi-Fi**: the page asks for a code. Press **Show a code on the Move**,
  then type the six digits the Move's screen shows. **Back** on the Move
  refuses, and that browser waits five minutes before it may ask again. The
  browser stays paired until Melodee stops.
- **Setup (Shift + Step 2) > WEB PAGE** shows the page's addresses: the
  USB one, the Wi-Fi one, and the port. Back closes it.

The page has three tabs. **Screen** mirrors the Move's screen live.
**Files** is Melodee's folder on the Move (`/data/UserData/Melodee`): play,
download, upload, rename and delete samples and projects. **Guide** is this.

## Where things are

Hold **Shift**: the steps with a job light. Shift + a step opens it.

- **Step 1** Projects (the Set Overview), **Step 2** Setup, **Step 3**
  Workflow (quantize, count-in, metronome, time signature).
- **Step 4** the **Mixer**; again for Knob FX, Pad FX and Flex Beat.
- **Step 5** Tempo (the steps tap it), **Step 6** the metronome, **Step 7**
  swing.
- **Step 8** **16 Pitches** on a kit.
- **Step 9** **Keys & Scale**: the key, the scale and how the pads lay out
  notes.
- **Step 10** Full Velocity, **Step 11** Note Repeat and the arpeggiator.
- **Step 12** the **Looper**, **Step 13** **Song**.
- **Step 14** the track's next empty pattern, **Step 15** doubles the
  pattern, **Step 16** quantizes it.
- **Shift + Loop**: Melodee's sequencer pages (Pattern, Roll, Motion, MOD,
  Macros, Feel, Follow, Lane, Euclid); **Shift + turn** the page.
- **Note/Session** switches the pads between playing and the patterns grid;
  **Shift + Note/Session** shows all sixteen tracks.

## Tracks

The four track buttons are the four tracks of the bank in view. **Hold a
track button and press a step** to pick any of the sixteen; an empty one
offers a new track: a **kit**, a **synth** (a Schwung module), a
**keygroup** (SFZ and other instruments), **MIDI** or **audio**. **Shift + a track button** is its options: colour, MIDI in and out.

The jogwheel walks the selected track's items, as the Move's Device View
does: the track, its sound (a kit's last pad, a synth's module), then its
effects and `+`. The eight knobs follow the item in focus. **Push the
jogwheel** to open that item's browser; **Shift + turn** for a kit pad's
pages (TRIM, LAYERS, TUNE, AMP ENV, PLAY, CLOUD, MIX, FILTER, KIT FILT).

## Kits

A kit has 128 pads in eight banks, **A** to **H**, 32 of them on the pads at
once.

- **Plus / Minus** move the pads 16 at a time through the banks; **Shift +
  Plus / Minus** four pads at a time.
- **Shift + a pad** picks it without playing it.
- **Hold a pad and turn the Volume knob** for its level.
- **Mute + a pad** mutes it, **Shift + Mute + a pad** solos it, **Copy + a
  pad, then another** copies its sound, **Delete + a pad** clears its notes
  (again: empties it).

## Loading samples and kits

Push the jogwheel with a kit pad in focus. The browser opens on its places:
STARRED and RECENT, **My Samples**, **Melodee** (the web page's uploads and
your saved kits), Recordings, the **Core Library** and all of the Move's
user data.

- **Turn** to choose; each sound plays as you land on it. **Push** opens a
  folder or loads the file onto the last pad. **Back** goes up.
- **Tap a pad** while browsing to pick the pad (and the bank) a file loads
  onto.
- **The Move's own drum kits** are in the Core Library under Track Presets
  > Drums (Acoustic, Electronic, Hybrid). Pushing one puts its 16 pads into
  the bank of the pad picked; the other banks stay.
- **A whole kit**: anything listed **KIT** loads whole onto the kit, all
  eight banks. That is a kit you saved (Melodee > Kits), any Melodee
  project (its kit), or an MPC drum program (.xpm, which asks for the whole
  kit or a bank at a time).
- **Shift + push** stars a file; **Delete** asks, then deletes it.

### Saving a kit

**Hold Shift in the browser** for its options:

- **SAVE KIT**: the whole kit, all eight banks with their effects, as
  `Melodee/Kits/<name>.mldp`. It lists as KIT; push it to load it back.
- **SAVE BANK A**, **SAVE BANK B**: the 16 pads of one bank in view, as
  `Melodee/Samples/Banks/<name>.mldb`, which loads into whichever bank you
  pick.
- **SAVE SAMPLE**: the last pad's sample as you trimmed it.

The Set Overview (Shift + Step 1) can also take a kit from another project:
pick the project, push the jogwheel, **LOAD KIT**.

## 16 Pitches

**Shift + Step 8** on a kit: the left 16 pads are one bank of the kit, the
right 16 play the last pad at 16 pitches up the project's key and scale.

- **Tap a left pad** to choose which sound the right 16 play.
- **Plus / Minus** move the pitches an octave.
- **Shift + Plus / Minus** move the left 16 a whole bank, **A** to **H**, so
  every pad of the kit can be reached.
- **To sequence pitches**: play a pitch on the right 16, then press steps;
  each step gets the pad at that pitch. Or **hold a step and press pitches**:
  each one goes on the step beside the others, pressed again it comes off.
  With Rec on, what you play is recorded at its pitch.

## Notes and keyboard layouts

On a synth, MIDI or keys track the pads play notes. **Shift + Step 9** opens
Keys & Scale:

- **MODE**: **IN-KEY** (only the scale's notes), **CHROMATIC** (every
  semitone), **PROG** (chords of a progression) or **DRUMS** (two 4x4 banks,
  for a drum module or a Move kit).
- **ROWS**: **OCTAVES** or **4THS**. **CHROMATIC with 4THS is an isomorphic
  grid**: each pad a semitone to the right, each row a fourth up, every
  chord and scale the same shape everywhere.
- **KEY** and **SCALE**: the project's, Ableton's 35 scales among them.
- **Plus / Minus** move an octave; **Shift + Plus / Minus** a note of the
  key.

Setup (Shift + Step 2) > PADS > LAYOUT sets whether the 32 pads read as the
Move's rows of eight or as two MPC 4x4 banks side by side.

## Steps and step locks

- **Press a step**: the last pad's note (or the notes you hold) goes on it.
  **Tap a step** with a note to clear it. Holding a step never clears it.
- **Hold a step** to edit it: **turn a knob** to lock that knob's value on
  the step (a step lock; the steps it covers light red); **the Volume
  knob** sets its velocity, **the jogwheel** its length, **Plus / Minus**
  transposes it, **Left / Right** nudges it, **another step** stretches it
  up to there.
- **Hold a step and push the jogwheel**: the knobs become TRIG (chance,
  condition, ratchet, ramps, accent, slide, tie); push again for the locks.
- **Delete + touch a knob** clears its locks; **Mute + touch** turns them
  off and on.
- **Copy + a step, then another** copies it.
- **Rec** records what you play (a bar's count-in from stopped);
  **Capture** keeps what you played without Rec.

## Loops and lengths

- **Hold Loop and turn the jogwheel**: the pattern's length in bars.
- **Hold Shift and Loop and turn the jogwheel**: the length a sixteenth at
  a time, so a pattern can be **shorter than a bar** (a half bar is 8
  sixteenths).
- **Tap Loop** for Loop Mode: the steps are bars. Press one to see it, **hold
  one and press another** to loop those bars, **press one twice** for a
  one-bar loop.
- **Left / Right** move through the pattern's bars.

## Time-stretch

- **A kit pad**: its **TUNE** page (Shift + turn to it) has **WARP**: **SEQ**
  fits the sample to the tempo, a percentage stretches it by that much, the
  pitch kept. **FOLLOW** on the same page fits every loop of the kit to the
  tempo by the tempo found in it.
- **An audio track**: **Shift + turn** to its **WARP** page: warp on or off,
  the take's BPM, and its pitch in semitones and cents.

## Audio tracks

An audio track plays a recording, a **take**, for each pattern.

- **Push the jogwheel** for the browser: **a sound file pushed there becomes
  the playing pattern's take**. Melodee finds its tempo, sets the pattern to
  its whole bars and turns WARP on, so it follows the project's tempo. Undo
  brings back the take before it.
- **Sampling** opens its INPUT (source, monitor, gain); **Rec** arms it and
  the next pass of the pattern is recorded.
- **Knobs 1-6** are the take's CLIP: level, start, end, fades, reverse.

## Sampling and Chop

- **Sampling**, then a pad: records onto that pad; the pad again, or
  Sampling, stops. **Shift + Sampling** puts the last 25 seconds of the input
  on the next empty pad.
- **Shift + push the jogwheel** on a kit pad: Sample Options, with **Chop**,
  Reverse, Trim, Normalize and Save.
- In Chop the pads play the slices; **the jogwheel** sets how many,
  **knobs 1 and 2** the slice's start and end, **knob 3** the chop type;
  **push** places the slices on pads.

## Mixer, sends and buses

**Shift + Step 4** opens the Mixer: eight tracks on the knobs, **Left /
Right** for 1-8, 9-16 and the submixes S1-S8. **Turn the jogwheel** to
change what the knobs set:

- **LEVEL** and **PAN**.
- **SEND A** to **SEND D**: how much of each track goes to the four FX
  buses. **Push the jogwheel on a SEND page** to open that bus's chain: its
  RETURN level first, then `+` to add effects (reverb, delay ...).
- **MAIN**: the returns and the master's compressor; push for the master's
  chain.
- **EQ** and **COMP** of the track in hand; push turns them off or on.
- **A track's OUTPUT** (the track item in the Device View) sends it to a
  submix, S1-S8, with its own chain.
- **The steps** select a track; **Mute + a step** mutes, **Shift + Mute + a
  step** solos.

A track's own effects are in its Device View: turn the jogwheel past its
sound to `+` and push.

## Song and the Looper

- **Shift + Step 13**, Song: the bank's four tracks as lanes, a row each,
  eight bars on the pads. Push on empty time to place a pattern; **Rec**
  writes the patterns you launch as clips. **Sampling** on Song exports a
  mixdown, stems, MIDI or an MPC 3 project.
- **Shift + Step 12**, the Looper: a pad row a loop, L1 the top row.

## Saving

The project saves itself a few seconds after each change and on the way out
(Setup's AUTOSAVE can turn that off). **Shift + Step 1** shows the projects;
push the jogwheel on the open one for Save, Save as new, Rename, Colour and
the rest. Samples are kept by path: one that has gone keeps its pads, silent,
until it is back.
