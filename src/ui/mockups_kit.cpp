// SPDX-License-Identifier: AGPL-3.0-only
// Kit depth on the Move, as mockups: kit.png. Melodee's kits of 128 pads in
// eight banks, Plus and Minus moving the 32 pads 16 at a time; the pad
// pages (Melodee's Stage) on Shift + turn, as the Move's parameter banks;
// 16 Pitches on Shift + Step 8; copying and emptying pads; Sample Options
// (Chop, Reverse, Trim, Normalize, Save); long samples read from storage;
// "Memory full". docs/UI.md, "Kits", says what each does; the controls are
// docs/MOVE_CONTROLS.md's.
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

// ---------------------------------------------------------------- example data

// Track 1's kit: A01-A09 hold a break's sounds, A05 the last pad, its
// sample a two-bar break at 94 BPM; B01-B04 more, C01-C06 a vocal kit.
std::vector<float> break_peaks(int columns) {
    struct Hit { int at; float level, decay; };
    static const Hit hits[] {{0, .95f, 10}, {64, .95f, 10}, {32, .75f, 7}, {96, .75f, 7}, {56, .45f, 3}, {88, .45f, 3},
                             {120, .5f, 4}, {8, .35f, 2}, {16, .35f, 2}, {24, .35f, 2}, {40, .35f, 2}, {48, .35f, 2},
                             {72, .35f, 2}, {80, .35f, 2}, {104, .35f, 2}, {112, .35f, 2}};
    std::vector<float> peaks(size_t(columns), 0.04f);
    for (int column = 0; column < columns; ++column)
        for (const auto& hit : hits) {
            int at = hit.at * columns / 128;
            if (column >= at) peaks[size_t(column)] += hit.level * std::exp(-(column - at) / hit.decay);
        }
    for (auto& peak : peaks) peak = std::min(peak, 1.0f);
    return peaks;
}
std::vector<float> long_peaks(int columns) { // a three-minute take: verses and a chorus
    std::vector<float> peaks(static_cast<size_t>(columns), 0.0f);
    for (int column = 0; column < columns; ++column) {
        float swell = 0.45f + 0.25f * std::sin(column * 0.11f) + (column > 40 && column < 70 ? 0.25f : 0.0f);
        peaks[size_t(column)] = std::clamp(swell + 0.1f * std::sin(column * 1.7f), 0.05f, 1.0f);
    }
    return peaks;
}

// The Device View's items along the top: the track, its sound, two effects.
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
// A message over the detail line, as the app says things (Studio::say).
void message(juce::Graphics& g, std::string_view text) {
    sheet(g, {content_x, detail_y - 2, content_width, 10});
    move::draw_text_centred(g, content_x, detail_y, content_width, text);
}
// A pad page: its title large, the eight knobs as cells.
void page(juce::Graphics& g, const char* pad, const char* number, const char* title, const std::array<std::string, 8>& names,
          const std::array<float, 8>& values) {
    rail(g, "PAD", pad, number);
    move::draw_text(g, content_x + 2, 1, title, 2);
    knob_cells(g, content(18, 46), names, values);
}
// A flag on a waveform: a line down, a tab at its top with a letter.
void flag(juce::Graphics& g, juce::Rectangle<int> area, int column, const char* letter) {
    int x = area.getX() + column;
    g.setColour(on);
    for (int y = area.getY(); y < area.getBottom(); y += 2) g.fillRect(x, y, 1, 1);
    g.fillRect(x, area.getY(), 7, 9);
    g.setColour(off);
    move::draw_text(g, x + 1, area.getY() + 1, letter);
    g.setColour(on);
}

// ---------------------------------------------------------------- screens

std::vector<Screen> kit_screens() {
    return {
        {"play-banks", "PLUS: THE 32 PADS ARE BANKS C-D", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "C05");
            items(g, {"KIT", "C05", "FX1", "FX2"}, 1);
            waveform(g, content(header_height + 1, subject_height - header_height - 1), break_peaks(content_width));
            message(g, "PADS C01-D16");
        }},
        {"page-trim", "SHIFT + TURN: PAGE 1, TRIM", [](juce::Graphics& g) {
            rail(g, "PAD", "A05", "1/9");
            move::draw_text(g, content_x + 2, 1, "TRIM");
            move::draw_text(g, width - move::text_width("LOCK"), 1, "LOCK");
            auto area = content(10, 40);
            waveform(g, area, break_peaks(content_width));
            flag(g, area, 8, "S");
            flag(g, area, 88, "E");
            detail(g, "1.71S", "OF 2.04S");
        }},
        {"page-layers", "PAGE 2, LAYERS: FOUR SAMPLES A PAD", [](juce::Graphics& g) {
            rail(g, "PAD", "A05", "2/9");
            move::draw_text(g, content_x + 2, 1, "LAYERS");
            move::draw_text(g, width - move::text_width("VEL"), 1, "VEL");
            static const char* names[] {"BREAK 94", "KICK HARD", "SNARE RIM", ""};
            static const int low[] {1, 90, 1, 0}, high[] {127, 127, 60, 0};
            for (int layer = 0; layer < 4; ++layer) {
                int y = 11 + layer * 10;
                move::draw_text(g, content_x + 2, y + 1, std::to_string(layer + 1));
                move::draw_text(g, content_x + 10, y + 1, *names[layer] ? names[layer] : "--");
                if (*names[layer]) { // its velocities across 1-127
                    int x0 = 98 + low[layer] * 28 / 127, x1 = 98 + high[layer] * 28 / 127;
                    g.fillRect(98, y + 7, 28, 1);
                    g.fillRect(x0, y + 4, std::max(1, x1 - x0), 3);
                }
                if (layer == 1) g.drawRect(content_x, y - 1, content_width, 10);
            }
            detail(g, "VEL 90-127", "-3 DB");
        }},
        {"page-tune", "PAGE 3, TUNE: WARP, FOLLOW", [](juce::Graphics& g) {
            page(g, "A05", "3/9", "TUNE", {"TUNE", "FINE", "WRP", "BTS", "FLW", "HOW", "PIT", ""},
                 {0.5f, 0.5f, 0.1f, 0.12f, 1, 0, 0, 0});
        }},
        {"page-amp", "PAGE 4, AMP ENV", [](juce::Graphics& g) {
            page(g, "A05", "4/9", "AMP ENV", {"ATT", "DEC", "SUS", "REL", "VEL", "FRM", "", ""},
                 {0.02f, 0.7f, 1, 0.2f, 1, 0, 0, 0});
        }},
        {"page-play", "PAGE 5, PLAY: MODES, GROUP, LINK", [](juce::Graphics& g) {
            page(g, "A05", "5/9", "PLAY", {"PLY", "MON", "GRP", "DLY", "LNK", "LP", "REV", "XFD"},
                 {0, 1, 0.12f, 0, 0, 0, 0, 0});
        }},
        {"page-cloud", "PAGE 6, CLOUD: THE PAD AS GRAINS", [](juce::Graphics& g) {
            rail(g, "PAD", "A05", "6/9");
            move::draw_text(g, content_x + 2, 1, "CLOUD");
            move::draw_text(g, width - move::text_width("ON"), 1, "ON");
            auto area = content(10, 40);
            waveform(g, area, break_peaks(content_width), 34, 50);
            g.fillRect(content_x + 42, area.getY(), 1, area.getHeight());
            detail(g, "0.78S", "24/S");
        }},
        {"page-mix", "PAGE 7, MIX: LEVEL, KIT, COLOUR", [](juce::Graphics& g) {
            page(g, "A05", "7/9", "MIX", {"LVL", "PAN", "KIT", "COL", "", "", "", ""}, {0.79f, 0.5f, 0.79f, 0.25f, 0, 0, 0, 0});
        }},
        {"page-filter", "PAGE 8, FILTER AND ITS ENVELOPE", [](juce::Graphics& g) {
            page(g, "A05", "8/9", "FILTER", {"TYP", "CUT", "RES", "ENV", "ATT", "DEC", "SUS", "REL"},
                 {0.43f, 0.62f, 0.3f, 0.5f, 0, 0.5f, 0, 0.2f});
        }},
        {"page-kit-filter", "PAGE 9, KIT FILT: AFTER EVERY PAD", [](juce::Graphics& g) {
            page(g, "A05", "9/9", "KIT FILT", {"TYP", "CUT", "RES", "", "", "", "", ""}, {0.28f, 0.8f, 0.1f, 0, 0, 0, 0, 0});
        }},
        {"knob-touched", "PAGE 3, KNOB 3 TOUCHED", [](juce::Graphics& g) {
            rail(g, "KNOB", "3", "A05");
            big_parameter(g, content(4, 44), "WARP", "SEQ", 0.1f);
            detail(g, "8 BEATS");
        }},
        {"touch-start", "KNOB 4 TOUCHED: START OVER ITS PLACE, X8", [](juce::Graphics& g) {
            rail(g, "KNOB", "4", "A05");
            place_value(g, "0.24S");
            std::vector<float> peaks(size_t(content_width), 0.04f);
            for (int column = 54; column < content_width; ++column) peaks[size_t(column)] = 0.95f * std::exp(-(column - 54) / 18.0f) + 0.06f;
            for (int column = 0; column < 22; ++column) peaks[size_t(column)] = 0.5f * std::exp(-column / 10.0f) + 0.04f;
            close_up(g, place_area(), peaks, content_width / 2, content_width);
            detail(g, "START", "X8");
        }},
        {"shift-touch", "SHIFT + KNOB 1 ON TRIM: START CLOSE UP", [](juce::Graphics& g) {
            rail(g, "KNOB", "1", "STRT");
            place_value(g, "0.24S");
            std::vector<float> peaks(size_t(content_width), 0.03f);
            for (int column = 54; column < content_width; ++column) peaks[size_t(column)] = 0.95f * std::exp(-(column - 54) / 30.0f) + 0.05f;
            close_up(g, place_area(), peaks, content_width / 2, content_width);
            detail(g, "START", "X32");
        }},
        {"touch-end", "KNOB 5 TOUCHED ON A SHORT REGION: ITS START IN VIEW", [](juce::Graphics& g) {
            rail(g, "KNOB", "5", "A05");
            place_value(g, "0.31S");
            std::vector<float> peaks(size_t(content_width), 0.04f);
            for (int column = 12; column < content_width; ++column) peaks[size_t(column)] = 0.9f * std::exp(-(column - 12) / 34.0f) + 0.05f;
            for (int column = 0; column < 4; ++column) peaks[size_t(column)] = -1;
            close_up(g, place_area(), peaks, 12, content_width / 2 - 1, {12});
            detail(g, "END", "X8");
        }},
        {"zones", "PAGE 1 ON AN INSTRUMENT PAD: ZONES", [](juce::Graphics& g) {
            rail(g, "PAD", "A07", "ZONE");
            move::draw_text(g, content_x + 2, 1, "ZONES");
            move::draw_text(g, width - move::text_width("12 SFZ"), 1, "12 SFZ");
            for (int key = 0; key < 25; ++key) { // two octaves of keys, a tile a zone over them
                int x = content_x + 1 + key * 4;
                g.drawRect(x, 34, 4, 10);
            }
            for (int zone = 0; zone < 6; ++zone) {
                int x = content_x + 1 + zone * 17;
                if (zone == 2) g.fillRect(x, 12, 16, 20);
                else g.drawRect(x, 12 + (zone % 2) * 8, 16, 12);
            }
            detail(g, "ZONE 3/12", "C3-D#3");
        }},
        {"pitches", "SHIFT + STEP 8: 16 PITCHES OF A05", [](juce::Graphics& g) {
            rail(g, "PTCH", "1", "A05");
            items(g, {"KIT", "A05", "FX1", "FX2"}, 1);
            waveform(g, content(header_height + 1, subject_height - header_height - 1), break_peaks(content_width));
            detail(g, "C MAJOR", "OCT 0");
        }},
        {"copy-pad", "COPY HELD + A05: THEN THE PAD TO FILL", [](juce::Graphics& g) {
            rail(g, "COPY", "A05", "PAD");
            move::draw_text_centred(g, content_x, 6, content_width, "A05", 2);
            move::draw_text_centred(g, content_x, 26, content_width, "ONTO A PAD");
            detail(g, "BREAK 94");
        }},
        {"copy-replace", "ONTO B02, WHICH HOLDS A SAMPLE", [](juce::Graphics& g) {
            rail(g, "COPY", "A05", "B02");
            move::draw_text_centred(g, content_x, 6, content_width, "REPLACE", 2);
            move::draw_text_centred(g, content_x, 26, content_width, "B02 SNARE RIM?");
            detail(g, "PUSH YES", "BACK NO");
        }},
        {"delete-pad", "DELETE + A07 (NO NOTES): EMPTIED", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "A07");
            items(g, {"KIT", "A07", "FX1", "FX2"}, 1);
            move::draw_text_centred(g, content_x, 22, content_width, "EMPTY", 2);
            message(g, "A07 EMPTIED");
        }},
        {"delete-gap", "DELETE + EMPTY A07: THE GAP CLOSES", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "A07");
            items(g, {"KIT", "A07", "FX1", "FX2"}, 1);
            waveform(g, content(header_height + 1, subject_height - header_height - 1), break_peaks(content_width));
            message(g, "A08-H16 DOWN ONE");
        }},
        {"sample-options", "SHIFT + PUSH: SAMPLE OPTIONS", [](juce::Graphics& g) {
            rail(g, "OPT", "A05", "SMP");
            list(g, content(0, height), {"CHOP >", "REVERSE      OFF", "TRIM SAMPLE", "NORMALIZE", "SAVE SAMPLE"}, 1, 1);
        }},
        {"sample-options-layer", "ON LAYERS, LAYER 2 IN HAND", [](juce::Graphics& g) {
            rail(g, "OPT", "A05", "LYR2");
            list(g, content(0, height), {"REVERSE      OFF", "NORMALIZE", "SAVE SAMPLE", "REMOVE LAYER"}, 3, 1);
        }},
        {"save-sample", "SAVE SAMPLE: ITS NAME, THEN PUSH", [](juce::Graphics& g) {
            move::draw_text(g, 1, 1, "SAVE SAMPLE");
            g.fillRect(0, 10, width, 1);
            move::draw_text(g, 1, 24, "BREAK 94 TRIM_");
            move::draw_text(g, 1, 46, "PADS: LETTERS");
            move::draw_text(g, 1, 56, "PUSH: OK  BACK: DEL");
        }},
        {"browser-options", "SHIFT HELD IN THE BROWSER: OPTIONS", [](juce::Graphics& g) {
            rail(g, "OPT", "A05", "LOAD");
            list(g, content(0, height), {"SAVE SAMPLE", "SAVE BANK A", "SAVE BANK B"}, 1, 1);
        }},
        {"load-kit", "PUSH ON AN MPC DRUM PROGRAM: LOAD KIT", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "LOAD");
            list(g, content(0, height), {"WHOLE KIT", "INTO A  PROG A", "INTO B  PROG B", "INTO C  --", "LOAD BANKS"}, 1, 1);
        }},
        {"browser-bank", "BANK FILES IN THE BROWSER", [](juce::Graphics& g) {
            rail(g, "LOAD", "A05", "SMP");
            list(g, content(0, height), {"BANKS >", "909 KIT     BANK", "DUSTY HATS  BANK", "BREAK 94.WAV", "LIVE TAKE.MP3"}, 1, 1);
        }},
        {"long-sample", "A LONG SAMPLE PLAYS FROM STORAGE", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "A08");
            items(g, {"KIT", "A08", "FX1", "FX2"}, 1);
            waveform(g, content(header_height + 1, subject_height - header_height - 1), long_peaks(content_width));
            detail(g, "LIVE TAKE", "3:12");
        }},
        {"long-decoding", "A LONG MP3 THE FIRST TIME: TO CACHE", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "A08");
            items(g, {"KIT", "A08", "FX1", "FX2"}, 1);
            move::draw_text_centred(g, content_x, 16, content_width, "READING", 2);
            g.drawRect(content_x + 6, 36, content_width - 12, 5);
            g.fillRect(content_x + 7, 37, (content_width - 14) * 40 / 100, 3);
            detail(g, "LIVE TAKE.MP3", "40%");
        }},
        {"memory-full", "A LOAD MEMORY HAS NO ROOM FOR", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "A09");
            items(g, {"KIT", "A09", "FX1", "FX2"}, 1);
            move::draw_text_centred(g, content_x, 14, content_width, "MEMORY", 2);
            move::draw_text_centred(g, content_x, 32, content_width, "FULL", 2);
            message(g, "MEMORY FULL");
        }},
    };
}

// ---------------------------------------------------------------- lights

// The Move's lights, laid out as on the panel (as Ableton's controls figure).
struct Panel {
    std::array<uint8_t, 4> tracks {};
    std::array<uint8_t, move::pad_count> pads {};   // bottom left first
    std::array<bool, move::pad_count> ringed {};    // pulsing, or sounding
    std::array<uint8_t, move::step_count> steps {};
    std::array<uint8_t, size_t(Button::Count)> buttons {};
    void button(Button b, uint8_t value) { buttons[size_t(b)] = value; }
};
struct Lights { const char* name; const char* caption; Panel panel; };

const uint8_t azure = ui::track_colour(0).bright, azure_dim = ui::track_colour(0).dim;
const uint8_t orange_dim = ui::track_colour(1).dim, violet_dim = ui::track_colour(2).dim, yellow_dim = ui::track_colour(4).dim;
const uint8_t muted = 124; // dark grey: the Move's dim white for a muted drum pad

// Track 1's kit in view A-B: A01-A09 (the break, dim in the track's colour,
// A03 coloured orange by COLOUR), B01-B04; A05 the last pad, white; A02
// sounding (bright, a flare); A06 muted.
Panel kit_panel() {
    Panel p;
    p.tracks = {azure, ui::track_colour(1).dim, ui::track_colour(2).dim, 0};
    for (unsigned n = 0; n < 9; ++n) p.pads[n] = azure_dim;
    for (unsigned n = 16; n < 20; ++n) p.pads[n] = violet_dim;
    p.pads[2] = orange_dim;
    p.pads[1] = azure;
    p.ringed[1] = true;
    p.pads[5] = muted;
    p.pads[4] = move::colour::white;
    for (auto b : {Button::Back, Button::Menu, Button::Capture, Button::Loop, Button::Mute, Button::Delete, Button::Copy,
                   Button::Undo, Button::Left, Button::Right, Button::Up, Button::Shift})
        p.button(b, move::white_dim);
    p.button(Button::Play, move::colour::dark_grey);
    p.button(Button::Rec, move::colour::dark_grey);
    p.button(Button::Sample, move::colour::dark_grey);
    return p; // Minus dark: bank A is the first
}

std::vector<Lights> kit_lights() {
    std::vector<Lights> panels;
    panels.push_back({"lights-kit", "KIT A-B: DIM IN COLOUR, A05 WHITE, A06 MUTED", kit_panel()});
    {   // Plus: banks C-D, C01-C06 a vocal kit in yellow; both Plus and Minus lit.
        Panel p = kit_panel();
        p.pads = {};
        p.ringed = {};
        for (unsigned n = 0; n < 6; ++n) p.pads[n] = yellow_dim;
        p.pads[4] = move::colour::white;
        p.button(Button::Down, move::white_dim);
        panels.push_back({"lights-banks", "PLUS: BANKS C-D, MINUS LIT TO GO BACK", p});
    }
    {   // 16 Pitches: the left 16 bank A as the MPC lays it, the right 16 A05's pitches in C major.
        Panel p = kit_panel();
        p.pads = {};
        p.ringed = {};
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column) {
                unsigned pad = row * 8 + column, mpc = row * 4 + column;
                p.pads[pad] = mpc < 9 ? (mpc == 4 ? move::colour::white : azure_dim) : 0;
            }
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 4; column < 8; ++column) {
                unsigned degree = row * 4 + (column - 4);
                p.pads[row * 8 + column] = degree % 7 == 0 ? azure : move::colour::light_grey;
            }
        p.button(Button::Down, move::white_dim);
        panels.push_back({"lights-pitches", "16 PITCHES: BANK A LEFT, +/- THE OCTAVE", p});
    }
    {   // Copy held, A05 pressed: A05 pulses until the pad to fill.
        Panel p = kit_panel();
        p.ringed = {};
        p.ringed[4] = true;
        p.button(Button::Copy, move::white_bright);
        panels.push_back({"lights-copy", "COPY HELD + A05: A05 PULSES", p});
    }
    {   // Shift held on a kit with 16 Pitches on: Step 8 bright, the rest of the Shift jobs dim.
        Panel p = kit_panel();
        for (unsigned n = 0; n < move::step_count; ++n) p.steps[n] = move::colour::light_grey;
        p.steps[7] = move::colour::white;
        p.button(Button::Shift, move::white_bright);
        panels.push_back({"lights-shift", "SHIFT HELD: STEP 8 WHITE, 16 PITCHES ON", p});
    }
    {   // Browsing samples onto A05: it pulses.
        Panel p = kit_panel();
        p.ringed = {};
        p.ringed[4] = true;
        panels.push_back({"lights-browser", "LOADING ONTO A05: IT PULSES", p});
    }
    return panels;
}

juce::Image panel_image(const Panel& p, int w, int h) {
    juce::Image image(juce::Image::RGB, w, h, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    float sx = float(w) / 907.0f, sy = float(h) / 423.0f; // Ableton's figure: 907 x 423
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
        if (p.ringed[pad]) {
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(box.expanded(2.5f), 4, 2);
        }
    }
    for (unsigned step = 0; step < move::step_count; ++step) {
        float cx = X(188 + float(step) * 38.3f);
        g.setColour(rgb(p.steps[step]));
        g.fillEllipse(cx - X(13), Y(371) - X(13), 2 * X(13), 2 * X(13));
    }
    return image;
}

juce::Image kit_sheet(const std::vector<Screen>& screens, const std::vector<Lights>& panels) {
    constexpr int scale = 4, gap = 24, caption = 30, top = 92;
    int screen_w = width * scale, screen_h = height * scale;
    int rows = int(screens.size() + 1) / 2, panel_rows = int(panels.size() + 1) / 2;
    int panels_top = top + rows * (screen_h + caption + gap);
    juce::Image image(juce::Image::RGB, gap + 2 * (screen_w + gap), panels_top + panel_rows * (screen_h + caption + gap), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    g.setColour(juce::Colour(0xffd8f0ff));
    move::draw_text(g, gap, 20, "MELODEE ON MOVE: KITS", 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, "128 PADS, PAD PAGES, 16 PITCHES, SAMPLE OPTIONS. DOCS/UI.MD", 2);
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

bool write_kit_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = kit_screens();
    auto panels = kit_lights();
    auto each = folder.getChildFile("kit");
    each.createDirectory();
    bool ok = true;
    for (const auto& screen : screens)
        ok = mockup::write_png(mockup::screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = mockup::write_png(panel_image(panel.panel, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    auto file = folder.getChildFile("kit.png");
    ok = mockup::write_png(kit_sheet(screens, panels), file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace ui
