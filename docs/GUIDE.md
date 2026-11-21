# Melodee on the Move

Melodee, the groovebox by Kerem Kiliç (Ellic Studio), on the Ableton Move:
its sampler and Chop on the 32 pads, and Schwung's synths on a second
track. This guide is the Move's controls for what is built so far; the web
page carries it too.

## Start and stop

- **From Schwung's Tools menu**: Tools > Melodee. Ableton's app stops while
  Melodee runs and comes back when it ends.
- **Shift + Back**: back to Move. Save your Set before starting Melodee, as
  for any quit of Move.
- **Master knob**: the volume.

## The web page

Open **http://move.local:8080** on a computer joined to the Move.

- **By USB cable**: the page opens at once.
- **On Wi-Fi**: the page asks for a code. Press **Show a code on the Move**,
  then type the six digits the Move's screen shows. **Back** on the Move
  refuses, and that browser waits five minutes before it may ask again. The
  browser stays paired until Melodee stops.
- **Shift + Capture** shows the page's addresses: the USB one, the Wi-Fi
  one, and the port. Back closes it.

The page has three tabs. **Screen** mirrors the Move's screen live.
**Files** is Melodee's folder on the Move (`/data/UserData/Melodee`): play,
download, upload, rename and delete samples and projects. **Guide** is this.

## The kit track

**Row 1** is the kit: 32 pads, each a sample.

- **Pads** play. The screen shows the last pad's sample.
- **Knobs 1-8**: tune, level, pan, start, end, attack, decay, cutoff. Hold
  **Shift** for finer steps. Touch a knob to see its value large.

## Load

**Menu** loads a sound file onto the last pad: WAV, AIFF, FLAC, Ogg or MP3.

- It opens on the places: My Samples, Melodee (the web page's uploads),
  Recordings, Ableton's Core Library, and all of the Move's user data.
- **Jogwheel**: turn to choose; push to open a folder or load a file.
- **Back** goes up a folder, then back to the places.

## Chop

**Loop** chops the last pad's sample with Melodee's Auto, Beats, Regions or
Threshold.

- The pads play the slices; the steps show where the cuts fall.
- **Jogwheel** or **Left / Right** picks a slice. **Knobs 1 and 2** move its
  start and end; **knob 3** is the chop type, **knob 4** the amount.
- Push the jogwheel for **To Pads**: Poly, Mono or Thru, onto the first
  empty pads.
- **Back** or **Loop** leaves Chop without changing the pads.

## The synth track

**Row 2** is a synth: one of Schwung's sound generators.

- **Menu** lists them; push the jogwheel to load one.
- **Pads** are notes in rows of fourths, C2 at the bottom left. **Up /
  Down** move an octave. Pressure is aftertouch.
- **Knobs 1-8** are the module's page; **Left / Right** its pages; the
  **jogwheel** turns its presets.
- **Push the jogwheel** for the module's menu: turn to a row, push to edit a
  value or open a level or list. **Back** goes up, then closes the menu.
- A module's own page (Radio Garden, Webstream) is one of its pages: push to
  enter it, **Back** to leave. When it asks for text, the pads are a
  keyboard and the steps digits; push is done, Back cancels.

## Saving

The kit and the synth track are saved three seconds after the last change,
and on the way out, as `Projects/Move.mldp` in Melodee's folder. The next
start opens it. Samples are kept by path: one that has gone keeps its pads,
silent, until it is back.
