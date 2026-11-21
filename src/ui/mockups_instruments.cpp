// SPDX-License-Identifier: AGPL-3.0-only
// Melodee's instruments on the Move, as mockups: instruments.png. Keygroup
// tracks (an SFZ, an MPC keygroup program, an EXS, a SoundFont preset, a
// DecentSampler preset, or one sample played from the note found in it) with
// their pages; the Move's Keys & Scale (Shift + Step 9) with Melodee's own
// layouts (Chords, Prog, Drums) and the chord builder; the ARP and CHORD
// pages of every note track; the auto sampler. A module track's knobs are
// the module's own (Kerem, 2026-11-20: no FILTER / ECHO / SPACE on the
// Move). The Move's keys
// keep Ableton's meaning (docs/MOVE_CONTROLS.md): Shift + Step 9 is Keys &
// Scale, 10 Full Velocity, 11 Repeat and Arp; Plus and Minus the octave,
// Shift + Plus / Minus a scale degree (Control Live's); the wheel walks the
// Device View, its push opens the item's browser, Shift + turn the pages.
#include "ui/mockups.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/lights.h"
#include "ui/track_style.h"
#include "ui/widgets.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace ui {
namespace {

using move::Button;
using Draw = std::function<void(juce::Graphics&)>;
struct Screen { const char* name; const char* caption; Draw draw; };
const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// The Move's lights, laid out as Ableton's controls figure has them (as
// mockups_controls.cpp draws them): pads, steps, track buttons, buttons.
struct Panel {
    std::array<uint8_t, 4> tracks {};
    std::array<uint8_t, move::pad_count> pads {};   // bottom left first
    std::array<bool, move::pad_count> pulsing {};   // ringed: pulsing
    std::array<uint8_t, move::step_count> steps {};
    std::array<bool, move::step_count> icons {};
    std::array<uint8_t, size_t(Button::Count)> buttons {};
    void button(Button b, uint8_t value) { buttons[size_t(b)] = value; }
};
struct Lights { const char* name; const char* caption; Panel panel; };

// ---------------------------------------------------------------- the example

// Track 3 is a keygroup (a grand piano, an SFZ of 88 zones), track 2 a
// Schwung synth (OB-Xd), track 4 a MIDI track to a drum machine.
constexpr unsigned keys_track = 2, synth_track = 1, drums_track = 3;
const uint8_t keys_colour = track_colour(keys_track).bright, keys_dim = track_colour(keys_track).dim;

// C major's notes; Ableton's names (60 is C3).
constexpr int major[7] {0, 2, 4, 5, 7, 9, 11};
bool in_c_major(int note) { int pc = ((note % 12) + 12) % 12; return std::find(std::begin(major), std::end(major), pc) != std::end(major); }

// The Move's pads in Keys & Scale's layouts (its manual, 9.1), from C1 (36):
// In-Key in octaves (a row an octave, a column the next note of the scale),
// In-Key in 4ths (up a row three notes of the scale: a fourth), Chromatic
// (right a semitone, up a fourth).
enum class Layout { InKeyOctaves, InKey4ths, Chromatic };
int pad_note(Layout layout, unsigned pad) {
    int row = int(move::pad_row(pad)), column = int(move::pad_column(pad));
    switch (layout) {
    case Layout::InKeyOctaves: { int degree = column; return 36 + 12 * row + 12 * (degree / 7) + major[degree % 7]; }
    case Layout::InKey4ths: { int degree = column + 3 * row; return 36 + 12 * (degree / 7) + major[degree % 7]; }
    case Layout::Chromatic: default: return 36 + column + 5 * row;
    }
}
// Roots in the track's colour, the scale's other notes light grey, the rest dark; held pads white.
std::array<uint8_t, move::pad_count> key_pads(Layout layout, std::initializer_list<unsigned> held = {}) {
    std::array<uint8_t, move::pad_count> pads {};
    for (unsigned pad = 0; pad < move::pad_count; ++pad) {
        int note = pad_note(layout, pad);
        pads[pad] = note % 12 == 0 ? keys_colour : in_c_major(note) ? move::colour::light_grey : 0;
    }
    for (unsigned pad : held) pads[pad] = move::colour::white;
    return pads;
}
// The bank of tracks 1-4, track `chosen` bright.
std::array<uint8_t, 4> bank(unsigned chosen) {
    std::array<uint8_t, 4> lights {};
    for (unsigned t = 0; t < 4; ++t) lights[t] = t == chosen ? track_colour(t).bright : track_colour(t).dim;
    return lights;
}
// The buttons a note track's Play lights: the octave both ways, Back where it leaves.
void note_buttons(Panel& p) {
    p.button(Button::Up, move::white_dim);
    p.button(Button::Down, move::white_dim);
    p.button(Button::Shift, move::white_dim);
}

// ---------------------------------------------------------------- drawing

// The Device View's items along the top of the subject (as controls.png).
void items(juce::Graphics& g, const std::array<const char*, 4>& names, int selected) {
    int cell = content_width / 4;
    for (int n = 0; n < 4; ++n) {
        int x = content_x + n * cell;
        g.setColour(on);
        if (n == selected) g.fillRect(x, 0, cell - 1, header_height);
        else g.fillRect(x, header_height - 1, cell - 1, 1);
        g.setColour(n == selected ? off : on);
        move::draw_text_centred(g, x, 1, cell - 1, names[size_t(n)]);
    }
    g.setColour(on);
}
// A page's name under the items, and what it is on at the right.
void page_title(juce::Graphics& g, std::string_view name, std::string_view right = {}) {
    move::draw_text(g, content_x + 1, 11, name);
    if (!right.empty()) move::draw_text(g, width - move::text_width(right), 11, right);
}
// The eight knobs of a page under its title.
void page_cells(juce::Graphics& g, const std::array<std::string, 8>& names, const std::array<float, 8>& values, int touched = -1) {
    knob_cells(g, content(20, 32), names, values, touched);
}

// A page of five knobs or fewer, in one row: each name and a bar.
void cell_row(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<std::string>& names, const std::vector<float>& values) {
    int cell = area.getWidth() / int(names.size());
    for (size_t n = 0; n < names.size(); ++n) {
        int x = area.getX() + int(n) * cell;
        g.setColour(on);
        move::draw_text(g, x + 1, area.getY(), std::string_view(names[n]).substr(0, size_t(std::max(1, (cell - 2) / move::glyph_advance))));
        int bar_y = area.getBottom() - 4, bar = cell - 4;
        g.fillRect(x + 1, bar_y + 3, bar, 1);
        g.fillRect(x + 1, bar_y, std::max(1, int(std::lround(values[n] * float(bar)))), 3);
    }
}

// An instrument's zones over the keyboard (MIDI 21-108, the piano's keys),
// a tick a zone's lowest key, the zone picked filled; the key last played
// marked under the strip.
void zone_map(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<std::pair<int, int>>& zones, int picked, int played) {
    auto x_of = [&](int key) { return area.getX() + (std::clamp(key, 21, 109) - 21) * area.getWidth() / 88; };
    g.setColour(on);
    g.drawRect(area);
    for (size_t n = 0; n < zones.size(); ++n) {
        int from = x_of(zones[n].first), to = x_of(zones[n].second + 1);
        if (int(n) == picked) g.fillRect(from, area.getY(), std::max(1, to - from), area.getHeight());
        else g.fillRect(from, area.getY() + area.getHeight() / 2, 1, area.getHeight() / 2);
    }
    // the C's under it, and the key played
    for (int c = 24; c <= 108; c += 12) g.fillRect(x_of(c), area.getBottom() + 1, 1, 2);
    int p = x_of(played);
    g.fillRect(p - 1, area.getBottom() + 3, 3, 2);
}

// An envelope's shape: attack, decay, sustain, release, each 0..1.
void envelope(juce::Graphics& g, juce::Rectangle<int> area, float a, float d, float s, float r) {
    int w = area.getWidth(), h = area.getHeight();
    int x0 = area.getX(), y0 = area.getBottom() - 1;
    int xa = x0 + int(a * float(w) * 0.25f), xd = xa + int(d * float(w) * 0.25f), xs = x0 + int(float(w) * 0.72f), xr = xs + int(r * float(w) * 0.28f);
    int ys = y0 - int(s * float(h - 1));
    juce::Path path;
    path.startNewSubPath(float(x0), float(y0));
    path.lineTo(float(xa), float(area.getY()));
    path.lineTo(float(xd), float(ys));
    path.lineTo(float(xs), float(ys));
    path.lineTo(float(std::min(xr, area.getRight() - 1)), float(y0));
    g.setColour(on);
    g.strokePath(path, juce::PathStrokeType(1.0f));
    g.fillRect(x0, y0, w, 1);
}

// A short bass note's waveform: a hit, then a decaying tone.
std::vector<float> bass_peaks() {
    std::vector<float> peaks(128);
    for (int column = 0; column < 128; ++column)
        peaks[size_t(column)] = std::min(1.0f, 0.08f + 0.9f * std::exp(-column / 40.0f) * (0.8f + 0.2f * std::sin(column * 0.9f)));
    return peaks;
}

// The auto sampler's keys: a mark over each root, the done ones filled; the
// split's line where the upper sound starts.
void auto_keys(juce::Graphics& g, juce::Rectangle<int> area, int low, int high, int step, int done, int split = -1) {
    auto x_of = [&](int key) { return area.getX() + (key - 24) * area.getWidth() / 85; };
    g.setColour(on);
    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);
    for (int c = 24; c <= 108; c += 12) g.fillRect(x_of(c), area.getBottom() - 3, 1, 2);
    int n = 0;
    for (int key = low; key <= high; key += step, ++n) {
        int x = x_of(key);
        if (n < done) g.fillRect(x - 1, area.getY(), 3, area.getHeight() - 4);
        else g.drawRect(x - 1, area.getY(), 3, area.getHeight() - 4);
    }
    if (split > 0) {
        int x = x_of(split);
        for (int y = area.getY() - 2; y < area.getBottom(); y += 2) g.fillRect(x, y, 1, 1);
    }
}

// ---------------------------------------------------------------- screens

std::vector<Screen> instruments_screens() {
    auto bass = bass_peaks();
    std::vector<std::pair<int, int>> piano_zones;
    for (int low = 21; low <= 105; low += 3) piano_zones.push_back({low, low + 2});
    return {
        // A keygroup track: its items, its pages on Shift + turn.
        {"keys-zones", "KEYGROUP 3: ZONES, KNOB 1 PICKS ONE", [=](juce::Graphics& g) {
            rail(g, "KEYS", "3", "1/6");
            items(g, {"KEYS", "PIAN", "FX1", "FX2"}, 1);
            page_title(g, "ZONES", "Z13/29");
            zone_map(g, content(21, 13).reduced(1, 0), piano_zones, 12, 60);
            move::draw_text(g, content_x + 1, 41, "C3-D3 +0 0DB");
            detail(g, "GRAND C3.WAV", "C3");
        }},
        {"keys-trim", "KEYGROUP OF ONE SAMPLE: ITS NOTE, E1", [=](juce::Graphics& g) {
            rail(g, "KEYS", "3", "1/6");
            items(g, {"KEYS", "BASS", "FX1", "FX2"}, 1);
            page_title(g, "TRIM", "LOOP");
            auto area = content(20, 30);
            waveform(g, area, bass);
            cuts(g, area, {2, 70, 96}, 0);
            detail(g, "BASS E.WAV", "E1");
        }},
        {"keys-play", "SHIFT + TURN: PLAY (TUNE, LEVEL, VOICES)", [](juce::Graphics& g) {
            rail(g, "KEYS", "3", "2/6");
            items(g, {"KEYS", "PIAN", "FX1", "FX2"}, 1);
            page_title(g, "PLAY", "C3");
            page_cells(g, {"TUNE", "FINE", "LEVEL", "PAN", "VEL", "MONO", "MODE", "GLIDE"}, {0.5f, 0.5f, 0.78f, 0.5f, 1.0f, 0.0f, 1.0f, 0.1f});
            detail(g, "SFZ 88 ZONES", "POLY");
        }},
        {"keys-amp", "AMP ENV: THE SHAPE, KNOBS 1-4", [](juce::Graphics& g) {
            rail(g, "KEYS", "3", "3/6");
            items(g, {"KEYS", "PIAN", "FX1", "FX2"}, 1);
            page_title(g, "AMP ENV", "A D S R");
            envelope(g, content(21, 28).reduced(3, 0), 0.05f, 0.6f, 0.45f, 0.5f);
            detail(g, "ATT 2MS", "REL 0.9S");
        }},
        {"keys-filter", "FILTER WITH ITS ENVELOPE: 8 KNOBS", [](juce::Graphics& g) {
            rail(g, "KEYS", "3", "4/6");
            items(g, {"KEYS", "PIAN", "FX1", "FX2"}, 1);
            page_title(g, "FILTER", "LP 24");
            page_cells(g, {"TYPE", "CUT", "RESO", "AMT", "ATT", "DEC", "SUS", "REL"}, {0.3f, 0.62f, 0.2f, 0.4f, 0.0f, 0.5f, 0.0f, 0.2f});
            detail(g, "CUTOFF 62", "");
        }},
        {"keys-touched", "A KNOB TOUCHED: AMP ENV ATTACK", [](juce::Graphics& g) {
            rail(g, "KNOB", "1", "KEYS");
            big_parameter(g, content(4, 44), "ATTACK", "12 MS", 0.12f);
        }},
        // ARP and CHORD: the last pages of every note track (keygroup, synth, MIDI).
        {"arp", "ARP PAGE (ALSO SHIFT + STEP 11, AS THE MOVE)", [](juce::Graphics& g) {
            rail(g, "KEYS", "3", "5/6");
            items(g, {"KEYS", "PIAN", "FX1", "FX2"}, 1);
            page_title(g, "ARP", "ON");
            page_cells(g, {"ON", "MODE", "RATE", "OCT", "GATE", "HOLD", "", ""}, {1.0f, 0.5f, 0.45f, 0.33f, 0.6f, 0.0f, 0, 0});
            detail(g, "UP-DOWN 1/16", "x2");
        }},
        {"chord", "CHORD: A PAD A CHORD OF THE SCALE", [](juce::Graphics& g) {
            rail(g, "KEYS", "3", "6/6");
            items(g, {"KEYS", "PIAN", "FX1", "FX2"}, 1);
            page_title(g, "CHORD", "AM7");
            cell_row(g, content(20, 14), {"ON", "TYPE", "VOICE", "INV", "HOLD"}, {1.0f, 0.2f, 0.0f, 0.0f, 0.0f});
            move::draw_text_centred(g, content_x, 37, content_width, "A2 C3 E3 G3", 1); // the chord's notes, lowest first
            detail(g, "DIA 7 CLOSE", "ROOT");
        }},
        // Loading an instrument: the wheel's push on the keygroup's sound.
        {"load-instrument", "PUSH ON THE SOUND: INSTRUMENTS AND SAMPLES", [](juce::Graphics& g) {
            rail(g, "LOAD", "3", "INS");
            list(g, content(0, 50), {"GRAND.SFZ    SFZ", "STRINGS.XPM  MPC", "RHODES.EXS   EXS", "GM.SF2         >", "CELLO    DECENT", "BASS E.WAV   WAV"}, 1, 1);
            detail(g, "PUSH: LOAD");
        }},
        {"load-sf2", "A SOUNDFONT OPENS AS A FOLDER OF PRESETS", [](juce::Graphics& g) {
            rail(g, "LOAD", "3", "SF2");
            list(g, content(0, 50), {"000 GRAND PIANO", "001 BRIGHT PIANO", "004 E.PIANO 1", "019 CHURCH ORGAN", "032 ACOUSTIC BS"}, 2, 1);
            detail(g, "GM.SF2", "3/128");
        }},
        {"loading", "STREAMED: ZONES PLAY AS THEIR FILES LAND", [=](juce::Graphics& g) {
            rail(g, "KEYS", "3", "1/6");
            items(g, {"KEYS", "STRG", "FX1", "FX2"}, 1);
            page_title(g, "ZONES", "40/88");
            zone_map(g, content(21, 13).reduced(1, 0), piano_zones, -1, 48);
            g.drawRect(content_x + 1, 41, content_width - 4, 5);
            g.fillRect(content_x + 2, 42, (content_width - 6) * 40 / 88, 3);
            detail(g, "LOADING", "MPC");
        }},
        // Keys & Scale: the Move's Shift + Step 9, with Melodee's own layouts.
        {"keys-scale", "SHIFT + STEP 9: KEYS & SCALE (THE MOVE'S)", [](juce::Graphics& g) {
            rail(g, "SCAL", "C", "MAJ");
            list(g, content(0, 50), {"MODE      IN-KEY", "ROWS     OCTAVES", "KEY            C", "SCALE      MAJOR", "TRANSPOSE      0"}, 0, 1);
            detail(g, "PUSH: CHANGE");
        }},
        {"keys-scale-edit", "PUSH: THE VALUE, THE WHEEL TURNS IT", [](juce::Graphics& g) {
            rail(g, "SCAL", "C", "DOR");
            list(g, content(0, 50), {"MODE      IN-KEY", "ROWS     OCTAVES", "KEY            C", "SCALE   [DORIAN]", "TRANSPOSE      0"}, 3, 1);
            detail(g, "PUSH: OK", "4/13");
        }},
        {"keys-scale-prog", "MODE PROG: THE PROGRESSION, ITS VOICING", [](juce::Graphics& g) {
            rail(g, "SCAL", "A", "MIN");
            list(g, content(0, 50), {"MODE        PROG", "PROG  NEO SOUL >", "VOICING    CLOSE", "KEY SHIFT     +2", "STORE CHORD    >"}, 1, 1);
            detail(g, "12 CHORDS", "A MIN");
        }},
        {"prog-list", "PUSH ON PROG: LOADED AS THE WHEEL TURNS", [](juce::Graphics& g) {
            rail(g, "LOAD", "2", "PROG");
            list(g, content(0, 50), {"LOFI DREAMS", "NEO SOUL 1", "MINE 1", "MINE 2", "TRAP DARK 3"}, 1, 1);
            detail(g, "MELODEE", "PUSH: OK");
        }},
        {"store-chord", "STORE CHORD: KEYS HELD, THEN A PAD", [](juce::Graphics& g) {
            rail(g, "PROG", "", "STOR");
            move::draw_text_centred(g, content_x, 4, content_width, "CMAJ7", 3);
            move::draw_text_centred(g, content_x, 30, content_width, "PRESS A PAD", 1);
            detail(g, "MINE 1", "BACK: NO");
        }},
        {"play-prog", "PLAY, MODE PROG: THE CHORD PLAYED", [](juce::Graphics& g) {
            rail(g, "OBXD", "2", "PROG");
            items(g, {"OBXD", "012", "FX1", "FX2"}, 0);
            move::draw_text_centred(g, content_x, 14, content_width, "F#M9", 3);
            move::draw_text_centred(g, content_x, 38, content_width, "NEO SOUL 1  5/12", 1);
            detail(g, "VOICING CLOSE", "+2");
        }},
        {"play-drums", "MODE DRUMS: NOTES FROM 36, VOICES NAMED", [](juce::Graphics& g) {
            rail(g, "MIDI", "4", "DRUM");
            items(g, {"MIDI", "TR-8", "FX1", "FX2"}, 0);
            move::draw_text(g, content_x + 2, 13, "38", 3);
            move::draw_text(g, content_x + 42, 14, "SNARE");
            move::draw_text(g, content_x + 42, 24, "PAD A03");
            detail(g, "36-67", "+/-: 16");
        }},
        // The auto sampler: a module (or an instrument outside) into an MPC keygroup program.
        {"auto-check", "AUTO SAMPLE: THE CHECK FIRST", [](juce::Graphics& g) {
            rail(g, "AUTO", "2", "CHK");
            list(g, content(0, 50), {"CHECKING...", "MONO, LEGATO", "GLIDE 0.12S", "ACCENT AT 101", "LOOPS"}, 0, 1);
            detail(g, "OB-XD", "BACK: NO");
        }},
        {"auto-sheet", "KNOBS 1-8; SHIFT + TURN: LEVEL, INPUT", [](juce::Graphics& g) {
            rail(g, "AUTO", "2", "1/2");
            move::draw_text(g, content_x + 1, 1, "MONO");
            move::draw_text(g, width - move::text_width("21 SMP"), 1, "21 SMP");
            auto_keys(g, content(10, 9).reduced(1, 0), 36, 96, 3, 0);
            knob_cells(g, content(21, 32), {"LOW", "HIGH", "STEP", "LAYER", "HOLD", "TAIL", "TAP", "LOOP"}, {0.28f, 0.75f, 0.1f, 0.0f, 0.0f, 0.4f, 0.0f, 0.0f});
            detail(g, "3 MIN", "40 MB");
        }},
        {"auto-sheet-2", "PAGE 2: LEVEL; ON A MIDI TRACK, THE INPUT", [](juce::Graphics& g) {
            rail(g, "AUTO", "4", "2/2");
            move::draw_text(g, content_x + 1, 1, "LEVEL  AS PLAYED");
            move::draw_text(g, content_x + 1, 11, "INPUT  LINE IN");
            move::draw_text(g, content_x + 1, 25, "IN");
            meter(g, juce::Rectangle<int>(content_x + 16, 25, content_width - 20, 12), 0.55f, 0.52f, 0.94f);
            detail(g, "TR-8 > LINE IN");
        }},
        {"auto-sampling", "SAMPLING: KEYS DONE FILLED; BACK STOPS", [](juce::Graphics& g) {
            rail(g, "AUTO", "2", "9/21");
            move::draw_text(g, content_x + 1, 1, "C4 V127");
            move::draw_text(g, width - move::text_width("1:40"), 1, "1:40");
            auto_keys(g, content(10, 9).reduced(1, 0), 36, 96, 3, 8);
            std::vector<float> note(102);
            for (size_t n = 0; n < note.size(); ++n) note[n] = n < 70 ? 0.7f + 0.1f * std::sin(float(n)) : 0.7f * std::exp(-(float(n) - 70) / 10.0f);
            waveform(g, content(22, 30), note);
            detail(g, "TAIL", "BACK: STOP");
        }},
        {"auto-done", "DONE: A NEW KEYGROUP TRACK, ITS PATTERNS", [](juce::Graphics& g) {
            rail(g, "AUTO", "5", "DONE");
            move::draw_text(g, content_x + 2, 4, "BRASS LEAD", 1);
            move::draw_text(g, content_x + 2, 16, "21 SAMPLES", 1);
            move::draw_text(g, content_x + 2, 26, "ON TRACK 5", 1);
            move::draw_text(g, content_x + 2, 36, "KEYGROUPS/", 1);
            detail(g, "PUSH: GO TO IT");
        }},
    };
}

std::vector<Lights> instruments_lights() {
    using namespace move::colour;
    std::vector<Lights> panels;
    {   // In-Key, octaves: a row an octave; every pad in the key, roots in the track's colour.
        Panel p;
        p.tracks = bank(keys_track);
        p.pads = key_pads(Layout::InKeyOctaves, {9});
        note_buttons(p);
        panels.push_back({"lights-in-key", "IN-KEY, OCTAVES (THE MOVE'S DEFAULT): C MAJOR", p});
    }
    {   // In-Key, 4ths.
        Panel p;
        p.tracks = bank(keys_track);
        p.pads = key_pads(Layout::InKey4ths);
        note_buttons(p);
        panels.push_back({"lights-in-key-4ths", "IN-KEY, 4THS: UP A ROW IS A FOURTH", p});
    }
    {   // Chromatic: roots coloured, the scale grey, the rest dark.
        Panel p;
        p.tracks = bank(keys_track);
        p.pads = key_pads(Layout::Chromatic, {14, 18});
        note_buttons(p);
        panels.push_back({"lights-chromatic", "CHROMATIC: ROOTS, SCALE GREY, REST DARK", p});
    }
    {   // Shift held on a note track: the Move's icons; Full Velocity's bright while on.
        Panel p;
        p.tracks = bank(keys_track);
        p.pads = key_pads(Layout::InKeyOctaves);
        for (unsigned s : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u, 10u, 11u, 12u, 13u, 14u, 15u}) { p.steps[s] = light_grey; p.icons[s] = true; }
        p.steps[9] = white; p.icons[9] = true; // Full Velocity on
        p.steps[7] = 0;                        // 16 Pitches: a kit's
        p.button(Button::Shift, move::white_bright);
        panels.push_back({"lights-shift", "SHIFT: STEP 9 KEYS, 10 FULL VEL (ON), 11 ARP", p});
    }
    {   // Prog: a chord a pad, the file's root chord bright, pads with no chord dark.
        Panel p;
        p.tracks = bank(synth_track);
        auto colour = track_colour(synth_track);
        for (unsigned pad = 0; pad < 12; ++pad) p.pads[pad] = pad == 0 ? colour.bright : colour.dim;
        p.pads[4] = white;
        note_buttons(p);
        panels.push_back({"lights-prog", "PROG: 12 CHORDS, THE ROOT CHORD BRIGHT", p});
    }
    {   // Storing a chord: the keys held white, the empty pads pulsing for the chord to go on.
        Panel p;
        p.tracks = bank(synth_track);
        auto colour = track_colour(synth_track);
        for (unsigned pad = 0; pad < move::pad_count; ++pad) {
            if (pad < 12) p.pads[pad] = colour.dim;
            else p.pulsing[pad] = true;
        }
        p.button(Button::Back, move::white_dim);
        panels.push_back({"lights-store", "STORE: THE EMPTY PADS PULSE; PRESS ONE", p});
    }
    {   // Drums: the drum machine's voices from 36, the last hit in the track's colour.
        Panel p;
        p.tracks = bank(drums_track);
        auto colour = track_colour(drums_track);
        for (unsigned pad = 0; pad < 16; ++pad) p.pads[pad] = colour.dim;
        p.pads[2] = colour.bright;
        note_buttons(p);
        panels.push_back({"lights-drums", "DRUMS: 16 VOICES FROM 36; +/- 16 MORE", p});
    }
    {   // Auto sampling: the steps fill as the keys are done; Back lit to stop.
        Panel p;
        p.tracks = bank(synth_track);
        for (unsigned s = 0; s < 16; ++s) p.steps[s] = s < 6 ? white : s == 6 ? red : dark_grey;
        p.button(Button::Back, move::white_dim);
        panels.push_back({"lights-auto", "AUTO SAMPLE: THE STEPS ARE ITS PROGRESS", p});
    }
    return panels;
}

// The Move as Ableton's controls figure lays it out (mockups_controls.cpp's
// drawing): the wheel top left, the track buttons left of the pads, the
// steps under them, the buttons right of the pads.
juce::Image panel_image(const Panel& p, int w, int h) {
    juce::Image image(juce::Image::RGB, w, h, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    float sx = float(w) / 907.0f, sy = float(h) / 423.0f;
    auto X = [&](float x) { return x * sx; };
    auto Y = [&](float y) { return y * sy; };
    auto rgb = [](uint8_t index) {
        if (!index) return juce::Colour(0xff121316);
        auto c = move::palette_rgb(index);
        return juce::Colour(c.r, c.g, c.b);
    };
    auto white = [](uint8_t level) {
        if (!level) return juce::Colour(0xff121316);
        auto v = uint8_t(std::clamp(110 + level * 145 / 124, 0, 255));
        return juce::Colour(v, v, v);
    };
    g.setColour(juce::Colour(0xff2a2d33));
    g.fillRoundedRectangle(0, 0, float(w), float(h), 8);
    g.setColour(juce::Colour(0xff050505));
    g.fillRect(X(22), Y(25), X(98), Y(52));
    g.setColour(juce::Colour(0xff6c7480));
    for (int knob = 0; knob < 8; ++knob) {
        float cx = X(205 + knob * 77.0f);
        g.drawEllipse(cx - X(16), Y(43) - X(16), X(32), X(32), 1.5f);
    }
    g.drawEllipse(X(866) - X(21), Y(48) - X(21), X(42), X(42), 1.5f);
    g.drawEllipse(X(69) - X(38), Y(156) - X(38), X(76), X(76), 1.5f);
    auto round = [&](float cx, float cy, float r, juce::Colour fill, const char* name) {
        g.setColour(fill);
        g.fillEllipse(X(cx) - X(r), Y(cy) - X(r), 2 * X(r), 2 * X(r));
        g.setColour(fill.getPerceivedBrightness() > 0.45f ? juce::Colours::black : juce::Colour(0xff8a96a3));
        move::draw_text(g, int(X(cx)) - move::text_width(name) / 2, int(Y(cy)) - 3, name);
    };
    auto lit = [&](Button b) { return move::buttons[size_t(b)].led == move::LedKind::Rgb ? rgb(p.buttons[size_t(b)]) : white(p.buttons[size_t(b)]); };
    round(42, 243, 19, lit(Button::Back), "<");
    round(95, 243, 19, lit(Button::Menu), "NS");
    round(43, 371, 20, lit(Button::Play), "PL");
    round(95, 371, 20, lit(Button::Rec), "RE");
    round(815, 126, 20, lit(Button::Capture), "CA");
    round(867, 126, 20, lit(Button::Sample), "SA");
    round(815, 185, 20, lit(Button::Loop), "LP");
    round(867, 185, 20, lit(Button::Mute), "M");
    round(815, 243, 20, lit(Button::Delete), "X");
    round(867, 243, 20, lit(Button::Copy), "CP");
    round(815, 301, 20, lit(Button::Undo), "UN");
    round(867, 301, 20, lit(Button::Shift), "SH");
    round(806, 370, 13, lit(Button::Left), "<");
    round(873, 370, 13, lit(Button::Right), ">");
    round(840, 352, 13, lit(Button::Up), "+");
    round(840, 388, 13, lit(Button::Down), "-");
    for (int track = 0; track < 4; ++track) {
        g.setColour(rgb(p.tracks[size_t(track)]));
        g.fillRoundedRectangle(X(133), Y(104 + track * 58.0f), X(22), Y(46), 4);
    }
    for (unsigned pad = 0; pad < move::pad_count; ++pad) {
        float x = X(173 + float(move::pad_column(pad)) * 76.8f), y = Y(103 + float(3 - move::pad_row(pad)) * 58.3f);
        juce::Rectangle<float> box(x, y, X(66), Y(47));
        g.setColour(rgb(p.pads[pad]));
        g.fillRoundedRectangle(box, 3);
        if (p.pulsing[pad]) {
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(box.expanded(2.5f), 4, 2);
        }
    }
    for (unsigned step = 0; step < move::step_count; ++step) {
        float cx = X(188 + float(step) * 38.3f);
        g.setColour(rgb(p.steps[step]));
        g.fillEllipse(cx - X(13), Y(371) - X(13), 2 * X(13), 2 * X(13));
        if (p.icons[step]) {
            g.setColour(juce::Colours::white);
            g.fillRect(cx - X(6), Y(404), X(12), 2.0f);
        }
    }
    return image;
}

juce::Image sheet(const std::vector<Screen>& screens, const std::vector<Lights>& panels) {
    constexpr int scale = 4, gap = 24, caption = 30, top = 92;
    int screen_w = width * scale, screen_h = height * scale;
    int rows = int(screens.size() + 1) / 2, panel_rows = int(panels.size() + 1) / 2;
    int panels_top = top + rows * (screen_h + caption + gap);
    juce::Image image(juce::Image::RGB, gap + 2 * (screen_w + gap), panels_top + panel_rows * (screen_h + caption + gap), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    g.setColour(juce::Colour(0xffd8f0ff));
    move::draw_text(g, gap, 20, "MELODEE ON MOVE: INSTRUMENTS", 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, "KEYGROUPS, KEYS & SCALE, ARP, CHORD, AUTO SAMPLE. DOCS/UI.MD", 2);
    auto place = [&](size_t n, int from) { return juce::Point<int>(gap + int(n % 2) * (screen_w + gap), from + int(n / 2) * (screen_h + caption + gap)); };
    for (size_t n = 0; n < screens.size(); ++n) {
        auto at = place(n, top);
        g.drawImageAt(mockup::screen_image(screens[n].draw, scale), at.x, at.y);
        g.setColour(juce::Colour(0xff8a96a3));
        move::draw_text(g, at.x, at.y + screen_h + 8, screens[n].caption, 2);
    }
    for (size_t n = 0; n < panels.size(); ++n) {
        auto at = place(n, panels_top);
        g.drawImageAt(panel_image(panels[n].panel, screen_w, screen_h), at.x, at.y);
        g.setColour(juce::Colour(0xff8a96a3));
        move::draw_text(g, at.x, at.y + screen_h + 8, panels[n].caption, 2);
    }
    return image;
}

} // namespace

bool write_instruments_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = instruments_screens();
    auto panels = instruments_lights();
    auto each = folder.getChildFile("instruments");
    each.createDirectory();
    bool ok = true;
    for (const auto& screen : screens)
        ok = mockup::write_png(mockup::screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = mockup::write_png(panel_image(panel.panel, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    auto file = folder.getChildFile("instruments.png");
    ok = mockup::write_png(sheet(screens, panels), file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace ui
