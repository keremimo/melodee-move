# Melodee on the Move: MIDI and sync

What MIDI the Move offers Melodee while Ableton's app is stopped and
melodee-move holds `/dev/ablspi0.0`, and how Melodee's MIDI and sync
(melodee-sample's `docs/MELODEE.md` › MIDI and sync, `docs/MIDI_TRACKS.md`,
`guide/midi-and-sync.md`) run on it. The screens are in
[UI.md](UI.md) › MIDI and sync; the keys in
[MOVE_CONTROLS.md](MOVE_CONTROLS.md).

## What the Move has (found 2026-11-20)

Sources: the code here, Schwung's sources and docs (`docs/SPI_PROTOCOL.md`,
`docs/SYSEX.md`, `src/schwung_shim.c`, `src/host/plugin_api_v1.h`, MIT),
Ableton's Move manual (1.2.2 Connections, 2.1.7 MIDI, 4.1.3) and release
notes (1.2.0, 1.5.0, 1.7.0), and the probe reports in `.local/probe/`.

- **One external MIDI stream: USB-MIDI cable 2 of the device page**, in and
  out. The Move's XMOS, not the CM4's Linux, owns the USB ports and puts
  their MIDI into the page, so whoever holds the device has it.
  - **USB-A**: the XMOS hosts class-compliant MIDI devices, full speed only
    (12 Mbit/s), up to 500 mA (manual 1.2.2, 4.1.3; release 1.7.0).
    Schwung's SPI_PROTOCOL.md: cable 2 is "devices on Move's USB-A port".
  - **USB-C**: the Move is a USB MIDI device to a computer, the port
    "Ableton Move Standalone Port" (release 1.7.0). Schwung measured it
    (docs/SYSEX.md, 2026-08-31): it lands on cable 2 too, merged with USB-A;
    the other three USB-C ports (Live, User, External) put nothing in the page.
  - So a device on USB-A and a computer arrive as **one port**, and nothing
    tells them apart; what goes out on cable 2 reaches the computer
    (Schwung's SysEx test), and USB-A (Schwung's API says so).
  - Linux sees no MIDI device: no ALSA (`/dev/snd` is absent), and the CM4's
    own USB gadget is NCM networking only (`g1/ncm.usb0`), with no
    `usb_f_midi`. None is needed: the XMOS does USB MIDI.
- **Schwung's modules** hear external MIDI through Schwung's shim inside
  Ableton's app: cable 2 goes to a module's `on_midi(..., SOURCE_EXTERNAL)`,
  and a module sends with `host->midi_send_external` (USB-MIDI packets onto
  cable 2). Here a module plays the notes Melodee gives its track, and its
  `midi_send_external` goes out on the port (Kerem, 2026-11-20).
- **No DIN or TRS MIDI**: the manual lists power, a 3.5 mm out, a 3.5 mm in,
  USB-A and USB-C; Schwung measured that cable 1 carries nothing. No
  Bluetooth MIDI. Ableton Link is Ableton's app's, over Wi-Fi.
- **Limits**: 20 MIDI-out slots an exchange (every 2.9 ms), shared with the
  lights (cable 0); 31 in. The XMOS cuts an incoming SysEx burst past about
  381 bytes (paced it is whole); outgoing SysEx must be paced, and nothing
  else on cable 2 may land inside one.

## How Melodee uses it

`src/app/midi.*` does for the one port what melodee-sample's
`shell/midi_io.cpp` does for the MPC's jacks and USB. Melodee's engine host
decides what goes out and when (`engine/midi_out.h`); its route and MTC
reader (`shell/midi_route.cpp`, `shell/mtc_in.cpp`) are compiled unchanged.

- **Out**, on the audio thread: after each `render()` the engine host's
  events go into a queue by the frame they belong to; those heard in the
  block just rendered go out with the exchange that carries that block's
  audio (`App::midi_out()`, before the lights, 16 of the 20 slots at most).
  What is played by hand (a pad, a key, MIDI in, a knob) goes with the next
  exchange. The device takes MIDI once an exchange, so the finest timing is
  a block (2.9 ms), not a frame. MIDI clock, Start, Stop and Continue go
  first; MTC's quarter frames and full frame, the notes, CCs and programs
  in order. Nothing on that path allocates, locks, waits or touches files.
- **SysEx out**: the service thread reads the `.syx` (64 KB at most) and
  hands it over; the audio thread sends it from F0 to F7 in pieces of three
  bytes, four pieces an exchange (4.1 KB/s, a little past DIN's 3.1), the
  clocks between them and everything else waiting behind it.
- **MIDI Config** opens from Setup's MIDI and MIDI SYNC rows (Shift + Step
  2); a track's MIDI IN and MIDI OUT are rows of its options (Shift + a
  track button), both screens move-projects'.
- **In**, on the MIDI thread: notes on MIDI Config's in channel play the
  selected track (a kit hears 36 as A01), a track's own channel (its MIDI IN)
  plays it whichever is selected, each at the frame it came in at; pressure,
  the mod wheel, the pitch wheel, the sustain pedal and other CCs, All Notes
  Off. With MIDI Config's SCALE KEYS on, an In-Key track (Melodee's Scale
  and Chords layouts) plays the scale on the white keys: C4 the root nearest
  it, a note of the scale a white key, the black keys nothing (melodee-sample's
  Scale on white keys, `src/app/scale_keys.h`). A MIDI track echoes what plays it on its own channel, unless that is
  the channel it came on (a keyboard and its own synth: Local Off), and a CC
  that one of its slots sends turns that slot. Remote CCs (102, 103, 104 on
  the Remote channel) select a track, launch a pattern or a sequence;
  program change launches that pattern on every track; Thru sends on what no
  MIDI track echoed. MIDI clock in (Start, Continue from Song Position,
  Stop, the tempo from the pulses) and MTC in (24, 25, 29.97 df, 30 fps)
  lead the transport while Sync In says so (an MTC cue while stopped
  leaves the playhead where it is). A SysEx coming in is read only for
  MTC's full frame.
- **Panic**: every note Melodee holds ends; All Sound Off, All Notes Off and
  the bend centred on all sixteen channels. MIDI Config's PANIC row and
  `melodee-midi panic`; no Move key (Kerem, 2026-11-20).
- **`melodee-midi`** (over SSH, through the runtime's control socket):
  `melodee-move --ctl 'melodee-midi 90 3c 64'` puts a message into the
  input as if it came in on the port; `--ctl 'melodee-midi panic'` is MIDI
  Config's Panic (`MelodeeApp::command()`, under the studio's lock).
- **Files**: MIDI Config in `Move.settings`, under Melodee's own keys
  (`midi_in_channel`, `midi_sync_out`, ..., and `scale_keys`), written with Setup's settings; each track's MIDI OUT in the
  project's `MVMO` section (a section of the Move's own, which Melodee's
  reader skips, read again as each project opens; MIDI IN is Melodee's
  track channel, `MIDC`); a MIDI track's
  drum map and SysEx file in Melodee's `MIDT`; SysEx files in
  `/data/UserData/Melodee/SysEx`; sounds in `/data/UserData/Melodee/Sounds`
  (`.mlds`, starred ones listed in `Starred.txt`).

## Not on the Move

- The MIDI jacks and the CV / sync out: there are none.
- Picking a port (MIDI Config's port; a MIDI track's External, USB-C, All
  USB; a USB device by name and OFFLINE; Track MIDI in's ports): one port.
- A MIDI track's AUDIO from USB or the mic as its own choice: the track
  takes the Move's input, L, R or both, which is the line in or the
  microphone as the Move routes it (Sampling's input, the same input).
- The knobs' takeover rows (Pickup, Scaled, Instant): the Move's knobs are
  endless encoders, with nothing to pick up.

## Status line

`melodee_midi_in`, `melodee_midi_out`, `melodee_midi_dropped`,
`melodee_midi_clock_in`, `melodee_midi_ext` (MIDI clock leads),
`melodee_midi_ext_tempo`, `melodee_midi_mtc_in`, `melodee_midi_mtc_leads`,
`melodee_midi_echo` (echoed / held back by the loop guard),
`melodee_midi_thru`, `melodee_midi_sysex` (files / bytes sent),
`melodee_midi_sysex_in_dropped`, `melodee_midi_panics`,
`melodee_midi_module` (a module's messages sent),
`melodee_midi_superseded`; on the screens `midi_page`, `midi_channel`,
`midi_row`, `midi_in_channel`, `midi_out_channel`. The runtime's own
`midi_in` counts every message from the device, the controls' too.

## To check on the Move

Each needs a Move; none was run on one yet.

1. A class-compliant keyboard on USB-A, Ableton's app stopped: its notes
   play the selected track (`midi_in` counts).
2. A computer on USB-C: "Ableton Move Standalone Port" in its MIDI setup;
   notes from it play the track; a MIDI track's notes reach it.
3. Out reaching USB-A and USB-C at once; whether Ableton's Setup › MIDI
   (In / Out) still applies with its app stopped.
4. MIDI clock out against a DAW or a drum machine: steady, and in time
   with the Move's own sound (if MIDI leads the audio by the XMOS's output
   buffering, a MIDI track's DELAY or a lead setting can take it up).
5. Clock in and MTC in from a DAW: following, the tempo heard.
6. A SysEx dump (a synth's patch) sent whole; Panic silencing a held note.
7. A Schwung module that sends MIDI (an arp or sequencer) reaching the port.
