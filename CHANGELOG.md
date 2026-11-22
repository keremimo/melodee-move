# Changelog

Melodee for Ableton Move as Schwung's module catalog installs it
(`melodee-move-module.tar.gz`). A version is `module/module.json`'s;
`tools/release.sh` builds it and takes its release notes from here.

## 0.4.0 (2026-11-22)

- Melodee's engine from ellic-core b410da7, as Melodee 0.6.5 on the MPC
  Sample has it.
- Sequences keep a pattern, or off, for each track. A pattern launched in
  Session is kept in the sequence launched last. The column's sheet (the
  wheel's push in Session) shows what the sequence keeps for the four tracks
  in view: push launches it, Capture keeps what every track plays now, Delete
  clears it. A sequence that keeps nothing launches its column, as before.
  The playing pattern's pad pressed again turns its track off from the next
  bar. Program change and the Remote CCs launch sequences the same way.
- A sequence has a colour: knob 3 on the column's sheet. The steps under
  Session's eight columns light in their sequences' colours.
- Jam capture: the bars played outside Song are logged. On Song, Capture
  lays them after the song's end as clips, with markers where a sequence
  took over and tempo steps where the tempo changed. Song's detail line
  counts them (JAM 12), and Capture lights while there is a jam.
- Chop's pads record slice hits while Rec runs: the chopped pad's note with
  its slice, the kit left as it is. A change that renumbers the slices they
  play asks first. MPC Sample projects open their Chop pad hits as slice
  hits, and the MPC 3 export puts slice hits on free pads.
- An MPC Sample project's song opens as Song: a clip a step, markers named
  after its sequences, the tempo lane where the sequences' tempos change.
  Its sequences keep their names.
- Setup's AUTOSAVE can be turned off. Then only SAVE writes the project,
  REVERT on the open project's options goes back to its file, and opening
  another project, a new one or leaving keeps it as it was saved.
- MIDI Config's SCALE KEYS: on an In-Key track, a keyboard's white keys play
  the scale and its black keys nothing.
- Fixed: an audio track's MONITOR is OFF, AUTO or ON. A new track showed
  OFF, and its input was not heard while armed.
- Fixed: a track named in its options keeps the name when its sound or
  module changes.
- Fixed: keygroup programs whose zones have RootNote 0 (MPC-V exports) play
  at each WAV's own root.

## 0.3.0 (2026-11-22)

- A Schwung module's pages are Schwung's own screens. From the push into a
  module (a synth's sound, or a Schwung effect) until Back, Melodee runs the
  screen code of the Schwung on the Move, on a screen that draws exactly as
  Schwung's: header, page bar, knob cells, footers, Sections, the enum lists,
  the keyboard (with pad typing and text preview as set in Schwung), file
  browsers, a module's drawn pages (Schimera's mouth and lyrics, Webstream's
  Browse), the loading screen, My Presets, the Module page (Module Help, Add
  to List, Swap Module, Remove Module, and Melodee's Auto Sample) and the knob
  lights. Schimera's Lyrics opens as in Schwung: hold knob 3 and push the
  wheel.
- My Presets shares Schwung's own preset files: a preset saved in Melodee is
  in Schwung's My Presets, and the other way round.
- A module's pages start over each time you go in, as in Schwung, so pages a
  setting shows or hides are there: Schimera's Vowel and Mouth pages, with
  its mouth, in Vowel mode however it was chosen.
- The Device View is laid out as Schwung's chain editor. ARP and CHORD are
  items before the module, like Schwung's MIDI effects: push shows the page,
  Mute + push turns it on or off. On a module, push goes into its pages,
  Shift + push swaps it, Delete + push removes it, Mute + push bypasses it;
  Shift + the wheel moves a Schwung effect along the chain.
- A module's whole state now reaches it in full: loading a preset of a large
  module no longer cuts it short.
- Typing works with Shift held or locked: a pad types a capital.
- With Shift locked (a double press), Back works again.
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
