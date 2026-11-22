# Changelog

Melodee for Ableton Move as Schwung's module catalog installs it
(`melodee-move-module.tar.gz`). A version is `module/module.json`'s;
`tools/release.sh` builds it and takes its release notes from here.

## Unreleased

- A module's pages are now Schwung's own screens, exactly as Schwung shows
  them: from the push into a module until Back, Melodee runs Schwung's own
  screen code on a screen that draws as Schwung's does. Header, page bar,
  knob cells, footers, Sections, the enum lists, the keyboard (with pad
  typing and text preview as set in Schwung), file browsers, a module's
  drawn pages, My Presets and the Module page are Schwung's, pixel for pixel,
  and so are the knob lights. Schimera's Lyrics now opens as in Schwung:
  hold knob 3 and push the wheel (turning it no longer types).
- Typing works with Shift held or locked: a pad types a capital, the steps,
  the wheel and its push type as without it.
- A module's pages start over each time you go in, as under Schwung, so
  pages that depend on a setting changed elsewhere show up: Schimera's Vowel
  and Mouth pages, with its mouth, appear in Vowel mode whichever way it was
  chosen. Loading one of My Presets does the same and lands on My Presets.
- A module's pages now behave exactly as under Schwung, in Melodee's own
  look. On a synth track the push on the module goes into its pages; there
  the wheel turns pages, Shift + the wheel jumps a section, the push with no
  knob held opens the Sections list, Back steps out one layer at a time and
  finally back to the Device View. A page the module draws (Schimera's mouth,
  Webstream's Browse) shows in place. Schwung's effects work the same way.
- ARP and CHORD are now items before the module in the Device View, like
  Schwung's MIDI effects; push shows the page, Mute + push turns it on or off.
- Shift + push on a module swaps it, Delete + push removes it, Mute + push
  bypasses it. Shift + the wheel moves a Schwung effect along the chain.
- My Presets on a module's pages saves and loads its whole state, shared with
  Schwung's own presets. The Module page has the module's help, Swap, Remove
  and Auto Sample.
- With Shift locked (a double press), Back works again.
- A module's text, such as Schimera's Lyrics, opens the keyboard when its knob
  is turned (knob 3 on Line and Play), as well as with its knob held and the
  wheel pushed. Sections and the other lists no longer draw over a page the
  module draws.
- A Schwung module's pages follow what the module declares, as under
  Schwung: its pages shown or hidden as it asks (Schimera's Vowel pages in
  Vowel mode), its presets page and lists, and a text parameter opens the
  keyboard: touch its knob and push the wheel (Schimera's Lyrics).
- Pad lights fade. A pad going to a new colour (a press, a sound, a page
  changing) now gets there smoothly through the Move's true RGB instead of
  jumping between the palette's few shades: up in a quarter of Setup's PAD
  FADE time, so a press still lights at once, and down over the whole of it.
  PAD FADE was saved but did nothing before; its choices are now 150, 250
  (the default), 400 and 600 ms.
- On a kit, the last pad pressed now flashes white while held or sounding
  and fades back to its own colour, bright, instead of staying white.
- Touching the wheel no longer flashes a message (a module's bare preset
  number like 001, the item in focus, the input source): the screen
  already shows them.

## 0.2.1 (2026-11-21)

- A long sample streamed from the card no longer takes memory for the whole
  file when it loads. Memory is now locked as it is first touched, so a
  5-minute WAV that used to make 102 MB resident on load makes none, and
  playing it touches about 8 MB. The audio thread's stack, the render
  workers' stacks and Schwung's modules are still read in and locked before
  they play.
- `melodee-move --ctl 'melodee-press wheel-push'` presses the jogwheel's
  button; it was read as a turn of nothing.
- Melodee's engine and model now come from
  [ellic-core](https://github.com/keremimo/ellic-core), the engine shared
  with Melodee on the MPC Sample (MIT), compiled unchanged. The package
  carries its licence in `licenses/ellic-core-LICENSE.txt`.

## 0.2.0 (2026-11-21)

What Schwung asked for before listing Melodee in its catalog.

- The screen reader: with Schwung's screen reader on, Melodee speaks
  through Schwung's own speech (its `schwung-tts`, the user's voice, speed
  and volume): at the start "Melodee", the view, the track and how to quit
  (Shift and Back); then a new view with its track, a new track, a touched
  or turned knob's name and value, and the message line, each once things
  have been still for 300 ms. With it off, Melodee says nothing.
- Realtime from Schwung's Tools menu and at power-on: Melodee's own
  privileged helper (`src/boot/heal.c`), installed by Schwung's
  `schwung-heal` as `bin/heal`, gives its audio thread SCHED_FIFO 70, Move's
  own audio priority, and nothing else. The log says `realtime=yes`.
- Help: a Screen Reader topic.

## 0.1.0 (2026-11-21)

The first release: Melodee, the groovebox by Kerem Kiliç (Ellic Studio), on
the Ableton Move as a Schwung tool. Move stops while Melodee runs and comes
back when it ends.

- Sixteen tracks on Melodee's own engine and project (`.mldp`): kits of 32
  pads (128 in eight banks) on Melodee's sampler, synth tracks playing
  Schwung's sound generators, keygroup tracks (SFZ, MPC programs,
  SoundFonts, DecentSampler), MIDI and audio tracks.
- Chop (Auto, Beats, Regions, Threshold, Manual) and To Pads; Sampling and
  Audio Recall from the mic or the line in.
- The sequencer on the Move's own keys: steps and locks, Melodee's TRIG,
  Loop Mode, the patterns grid, Seq Record, Recall, Note Repeat and Arp,
  Keys & Scale.
- The Looper's four loops, the Mixer with Melodee's 41 effects and
  Schwung's audio effects, Song and its exports (mixdown, stems, MIDI, an
  MPC 3 project, an Ableton Live Set), print and freeze.
- Pads in the Move's rows of eight or two MPC 4x4 banks.
- A web page at http://move.local:8080 while Melodee runs: the screen,
  Melodee's files, the guide. A browser on Wi-Fi asks for a code the Move
  shows.
- Starts from Schwung's Tools menu, or at power-on if chosen in Schwung's
  boot picker (Back while "Loading" shows); Shift + Back goes back to Move.
  Projects and samples stay in `/data/UserData/Melodee` when Melodee is
  removed.

Known limits:

- Started by Schwung, Melodee runs as the `ableton` user, which may not
  take realtime priority (`realtime=no` in `melodee-move.log`).
- Ableton's app stops while Melodee runs: save your Set first.
- Tested on AbletonOS 4.1 with Schwung 1.9.4.
