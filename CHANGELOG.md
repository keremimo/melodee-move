# Changelog

Melodee for Ableton Move as Schwung's module catalog installs it
(`melodee-move-module.tar.gz`). A version is `module/module.json`'s;
`tools/release.sh` builds it and takes its release notes from here.

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
