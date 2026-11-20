// SPDX-License-Identifier: AGPL-3.0-only
// The sequencer's screens and lights (docs/UI.md, "Sequencer"), as mockups:
// sequencer.png. The Move's keys keep the meaning Ableton's manual gives
// them (Step Sequencing 11, Loop Mode 12, Session 17, Recording 14, Capture
// 14.3, Repeat 11.6, Workflow 13): steps are a bar's sixteenths, Loop the
// bars, Note/Session the Patterns grid, Rec Seq Record with its count-in,
// Capture Melodee's Recall. Melodee's own (Roll, Motion, MOD, macros, feel,
// follow actions, variations, lanes and Euclid, sequences) sit on keys the
// Move leaves free, drawn here with the choices recommended to Kerem.
#include "ui/mockups.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "ui/seq_widgets.h"
#include "ui/track_style.h"
#include "ui/widgets.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace ui {
namespace mockup {
namespace {

using move::Button;

// The Move's lights as the panel shows them, with the buttons and the icons
// under the steps (a copy of mockups_controls.cpp's, kept apart so the
// wave-2 areas' sheets never collide).
struct Lit {
    std::array<uint8_t, 4> tracks {};
    std::array<uint8_t, move::pad_count> pads {};
    std::array<bool, move::pad_count> pulsing {};
    std::array<uint8_t, move::step_count> steps {};
    std::array<bool, move::step_count> step_pulsing {};
    std::array<bool, move::step_count> icons {};
    std::array<uint8_t, 8> knobs {};
    std::array<uint8_t, size_t(Button::Count)> buttons {};
    void button(Button b, uint8_t value) { buttons[size_t(b)] = value; }
};
struct LitPanel { const char* name; const char* caption; Lit lit; };

juce::Colour palette(uint8_t index) {
    if (!index) return juce::Colour(0xff121316);
    auto c = move::palette_rgb(index);
    return juce::Colour(c.r, c.g, c.b);
}
juce::Colour whiteness(uint8_t level) {
    if (!level) return juce::Colour(0xff121316);
    auto v = uint8_t(std::clamp(110 + level * 145 / 124, 0, 255));
    return juce::Colour(v, v, v);
}

juce::Image lit_image(const Lit& p, int w, int h) {
    juce::Image image(juce::Image::RGB, w, h, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    float sx = float(w) / 907.0f, sy = float(h) / 423.0f; // Ableton's figure: 907 x 423
    auto X = [&](float x) { return x * sx; };
    auto Y = [&](float y) { return y * sy; };
    g.setColour(juce::Colour(0xff2a2d33));
    g.fillRoundedRectangle(0, 0, float(w), float(h), 8);
    g.setColour(juce::Colour(0xff050505));
    g.fillRect(X(22), Y(25), X(98), Y(52));
    for (int knob = 0; knob < 8; ++knob) {
        float cx = X(205 + knob * 77.0f);
        g.setColour(juce::Colour(0xff6c7480));
        g.drawEllipse(cx - X(16), Y(43) - X(16), X(32), X(32), 1.5f);
        g.setColour(p.knobs[size_t(knob)] ? palette(p.knobs[size_t(knob)]) : juce::Colour(0xff6c7480));
        g.fillEllipse(cx - 3, Y(76) - 3, 6, 6);
    }
    g.setColour(juce::Colour(0xff6c7480));
    g.drawEllipse(X(866) - X(21), Y(48) - X(21), X(42), X(42), 1.5f);
    g.drawEllipse(X(69) - X(38), Y(156) - X(38), X(76), X(76), 1.5f);
    auto round = [&](float cx, float cy, float r, juce::Colour fill, const char* name) {
        g.setColour(fill);
        g.fillEllipse(X(cx) - X(r), Y(cy) - X(r), 2 * X(r), 2 * X(r));
        g.setColour(fill.getPerceivedBrightness() > 0.45f ? juce::Colours::black : juce::Colour(0xff8a96a3));
        move::draw_text(g, int(X(cx)) - move::text_width(name) / 2, int(Y(cy)) - 3, name);
    };
    auto lit = [&](Button b) { return move::buttons[size_t(b)].led == move::LedKind::Rgb ? palette(p.buttons[size_t(b)]) : whiteness(p.buttons[size_t(b)]); };
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
        g.setColour(palette(p.tracks[size_t(track)]));
        g.fillRoundedRectangle(X(133), Y(104 + track * 58.0f), X(22), Y(46), 4);
    }
    for (unsigned pad = 0; pad < move::pad_count; ++pad) {
        float x = X(173 + float(move::pad_column(pad)) * 76.8f), y = Y(103 + float(3 - move::pad_row(pad)) * 58.3f);
        juce::Rectangle<float> box(x, y, X(66), Y(47));
        g.setColour(palette(p.pads[pad]));
        g.fillRoundedRectangle(box, 3);
        if (p.pulsing[pad]) {
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(box.expanded(2.5f), 4, 2);
        }
    }
    for (unsigned step = 0; step < move::step_count; ++step) {
        float cx = X(188 + float(step) * 38.3f);
        g.setColour(palette(p.steps[step]));
        g.fillEllipse(cx - X(13), Y(371) - X(13), 2 * X(13), 2 * X(13));
        if (p.step_pulsing[step]) {
            g.setColour(juce::Colours::white);
            g.drawEllipse(cx - X(15), Y(371) - X(15), 2 * X(15), 2 * X(15), 1.5f);
        }
        if (p.icons[step]) {
            g.setColour(juce::Colours::white);
            g.fillRect(cx - X(6), Y(404), X(12), 2.0f);
        }
    }
    return image;
}

// ---------------------------------------------------------------- the example

// Track 5, a kit (BREAKS), is selected; pattern 3 plays, four bars of 16
// sixteenths; its kick on A01, snare A02, hat A03. Tracks 5-8 are the bank.
constexpr unsigned track = 4;
const auto five = track_colour(track);

// The pattern's bar 1 for the kick (A01) as steps: a hit on 1, 4, 7, 11.
constexpr bool kick[16] {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0};

std::array<uint8_t, 4> bank_lights(unsigned selected = track) {
    std::array<uint8_t, 4> lights {};
    const bool used[4] {true, true, true, false}; // tracks 5, 6, 7 used; 8 empty
    for (unsigned n = 0; n < 4; ++n) {
        unsigned t = 4 + n;
        lights[n] = !used[n] ? 0 : t == selected ? track_colour(t).bright : track_colour(t).dim;
    }
    return lights;
}
std::array<uint8_t, move::pad_count> kit_lights(int last) {
    std::array<uint8_t, move::pad_count> pads {};
    for (unsigned n = 0; n < 9; ++n) pads[n] = five.dim;
    if (last >= 0) pads[size_t(last)] = move::colour::white;
    return pads;
}
void white_buttons(Lit& s) {
    for (auto b : {Button::Back, Button::Menu, Button::Shift, Button::Loop, Button::Copy, Button::Delete, Button::Undo, Button::Mute,
                   Button::Left, Button::Right, Button::Up, Button::Down})
        s.button(b, move::white_dim);
}

std::vector<seq::RollNote> example_notes() {
    std::vector<seq::RollNote> notes;
    const struct { uint32_t step; uint8_t key; uint32_t length; } line[] {
        {0, 48, 4}, {4, 55, 2}, {6, 53, 2}, {8, 51, 6}, {14, 50, 2}, {16, 48, 4}, {20, 55, 2}, {22, 58, 2}, {24, 56, 4}, {28, 55, 4}};
    for (const auto& n : line) notes.push_back({n.step * 24, n.length * 24, n.key, n.step == 8 || n.step == 14});
    return notes;
}

} // namespace

std::vector<Screen> sequencer_screens() {
    using namespace seq;
    return {
        // Step Sequencing (manual 11): a step held shows what it holds.
        {"step-held", "STEP 4 HELD: ITS NOTE, VEL, LENGTH", [](juce::Graphics& g) {
            rail(g, "STEP", "4", "B1");
            step_card(g, content(0, 52), {"A01 KICK 909", 112, "1.00", "+.10", {"ACC", "1:2", "R3", "75%"}, false});
            detail(g, "PUSH: TRIG", "3 LCK");
        }},
        {"step-empty", "STEP 6 HELD, EMPTY: A PAD ADDS", [](juce::Graphics& g) {
            rail(g, "STEP", "6", "B1");
            step_card(g, content(0, 52), {"A01 KICK 909", 0, "", "", {}, true});
            detail(g, "WHEEL: LENGTH");
        }},
        {"step-lock", "STEP 4 HELD + KNOB 8: A LOCK (MOVE 14.2.4)", [](juce::Graphics& g) {
            rail(g, "LOCK", "4", "A01");
            big_parameter(g, content(4, 44), "CUTOFF ON STEP 4", "2.4 KHZ", 0.62f);
            detail(g, "DELETE+TOUCH: CLR");
        }},
        {"step-trig", "STEP 4 HELD, PUSH: MELODEE'S TRIG KNOBS", [](juce::Graphics& g) {
            rail(g, "TRIG", "4", "B1");
            title(g, "A01 KICK 909", "TRIG");
            value_cells(g, content(11, 40), {"CHN", "CND", "RTC", "RMP", "PIT", "ACC", "SLD", "TIE"},
                        {"75%", "1:2", "3", "-20", "+5", "ON", "-", "-"}, 1);
            detail(g, "PUSH: LOCKS");
        }},
        // Loop Mode (manual 12): the steps are bars.
        {"loop", "LOOP: THE BARS ON THE STEPS, BAR 2 SHOWN", [](juce::Graphics& g) {
            rail(g, "LOOP", "4", "BARS");
            title(g, "PATTERN 3", "1-16");
            Bars bars;
            bars.count = 6; bars.loop_from = 0; bars.loop_to = 4; bars.selected = 1; bars.playing = 2;
            bars.notes = {3, 2, 3, 1, 0, 1};
            bar_blocks(g, content(13, 38), bars);
            detail(g, "BAR 2 OF 6", "1-4");
        }},
        {"loop-length", "LOOP HELD + WHEEL: THE LENGTH", [](juce::Graphics& g) {
            rail(g, "LOOP", "4", "BARS");
            big_parameter(g, content(4, 44), "PATTERN 3 LENGTH", "4 BARS", 4 / 16.0f);
            detail(g, "SHIFT: 1/16S");
        }},
        // Session (manual 17): the Patterns grid, Note/Session.
        {"session", "NOTE/SESSION: PATTERNS 1-8 OF TRACKS 5-8", [](juce::Graphics& g) {
            rail(g, "PATT", "3", "1-8");
            std::array<GridRow, 4> rows {};
            rows[0] = {"5", {Cell::Notes, Cell::Notes, Cell::Playing, Cell::Notes, Cell::Queued, Cell::Empty, Cell::Empty, Cell::Empty}};
            rows[1] = {"6", {Cell::Playing, Cell::Notes, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty}};
            rows[2] = {"7", {Cell::Notes, Cell::Notes, Cell::Stopping, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty}};
            rows[3] = {"", {}};
            pattern_grid(g, content(1, 50), rows, 0, 0, 2);
            detail(g, "5 KIT BREAKS", "P3");
        }},
        {"session-chain", "HOLD PAD 1, TAP 3 AND 4: A CHAIN", [](juce::Graphics& g) {
            rail(g, "CHN", "5", "TRK");
            move::draw_text(g, content_x + 2, 4, "1>3>4", 3);
            move::draw_text(g, content_x + 2, 30, "EACH PLAYS ONCE,");
            move::draw_text(g, content_x + 2, 40, "THEN ROUND AGAIN");
            detail(g, "A TAP ENDS IT");
        }},
        {"session-copy", "COPY + PAD 3, THEN PAD 4 (HAS NOTES)", [](juce::Graphics& g) {
            rail(g, "COPY", "3", "> 4");
            move::draw_text(g, content_x + 2, 4, "PATTERN 3 TO 4");
            move::draw_text(g, content_x + 2, 18, "4 HAS 12 NOTES:");
            move::draw_text(g, content_x + 2, 28, "REPLACE THEM?");
            detail(g, "PUSH YES", "BACK NO");
        }},
        {"scene", "SESSION, PUSH: SEQUENCE 3 (A COLUMN)", [](juce::Graphics& g) {
            rail(g, "SCNE", "3", "");
            title(g, "SEQUENCE 3", "VERSE");
            value_cells(g, content(11, 40), {"BPM", "", "", "", "", "", "", ""}, {"98.0", "", "", "", "", "", "", ""});
            move::draw_text(g, content_x + 2, 33, "PATTERN 3 OF EACH");
            move::draw_text(g, content_x + 2, 42, "TRACK, NEXT BAR");
            detail(g, "PUSH: LAUNCH");
        }},
        // Recording (manual 14): Rec, the count-in, a free pattern growing.
        {"count-in", "REC, STOPPED: THE COUNT-IN, BEAT 3", [](juce::Graphics& g) {
            rail(g, "REC", "", "CNT");
            g.setColour(juce::Colours::black);
            dot(g, 12, 32, 6);
            g.setColour(juce::Colours::white);
            count_in(g, content(0, 52), 3, 4);
            detail(g, "PATTERN 3", "1 BAR");
        }},
        {"rec-free", "REC ON AN EMPTY PATTERN: FREE, IT GROWS", [](juce::Graphics& g) {
            rail(g, "REC", "2", "BARS");
            title(g, "PATTERN 4", "FREE");
            Bars bars;
            bars.count = 3; bars.loop_from = 0; bars.loop_to = 3; bars.selected = -1; bars.playing = 1;
            bars.notes = {3, 2, 0};
            bar_blocks(g, content(13, 38), bars);
            detail(g, "REC: ENDS BAR", "2.3");
        }},
        // Capture (manual 14.3): Melodee's Recall.
        {"capture", "CAPTURE, PLAYING: 2 BARS INTO PATTERN 4", [](juce::Graphics& g) {
            rail(g, "CAP", "2", "BARS");
            roll(g, content(1, 50), example_notes(), 0, 32 * 24, 48, 59, -1, -1, 16 * 24);
            detail(g, "TO PATTERN 4", "18");
        }},
        {"capture-tempo", "CAPTURE, STOPPED: THE WHEEL PICKS A TEMPO", [](juce::Graphics& g) {
            rail(g, "CAP", "96", "BPM");
            list(g, content(0, 50), {"96.0 BPM  2 BAR", "192.0 BPM 4 BAR", "48.0 BPM  1 BAR"}, 0, 1);
            detail(g, "PUSH: OK");
        }},
        // The Move's settings keys.
        {"workflow", "SHIFT + STEP 3: WORKFLOW (THE MOVE'S, 13)", [](juce::Graphics& g) {
            rail(g, "WORK", "", "SEQ");
            list(g, content(0, height), {"QUANTIZE    100%", "STEP GRID   1/16", "COUNT-IN      ON", "REC QUANTIZE  ON",
                                         "TIME SIG     4/4", "MAX LENGTH  FREE", "METRONOME ALWAYS", "BPM/PATTERN  OFF"}, 2, 1);
        }},
        {"repeat", "SHIFT + STEP 11: REPEAT (THE MOVE'S, 11.6)", [](juce::Graphics& g) {
            rail(g, "RPT", "ON", "");
            move::draw_text(g, content_x + 4, 2, "1/16", 3);
            list(g, content(28, 22), {"STYLE     REPEAT", "RATE        1/16"}, 1, 1);
            detail(g, "PRESSURE: VEL");
        }},
        {"tempo-tap", "SHIFT + STEP 5: TAP THE STEPS; < HALF >", [](juce::Graphics& g) {
            rail(g, "TMPO", "", "4/4");
            move::draw_text(g, content_x + 4, 4, "96.5", 3);
            move::draw_text(g, content_x + 4, 28, "BPM  TAP STEPS");
            move::draw_text(g, content_x + 4, 40, "< HALF  DOUBLE >");
            detail(g, "PUSH: OK", "3.2");
        }},
        // Melodee's own pages: Shift + Loop (free on the Move), Shift + wheel the page.
        {"pattern", "SHIFT + LOOP: SEQ PAGES, 1 PATTERN", [](juce::Graphics& g) {
            rail(g, "SEQ", "3", "1/9");
            title(g, "PATTERN 3", "PAT");
            value_cells(g, content(11, 40), {"LEN", "Q", "SIG", "SPD", "BPM", "FRE", "", ""},
                        {"4BR", "1/16", "4/4", "X1", "--", "OFF", "", ""});
            detail(g, "PUSH: HALF, DBL..");
        }},
        {"pattern-jobs", "PATTERN, PUSH: HALF/DOUBLE, VARIATIONS", [](juce::Graphics& g) {
            rail(g, "SEQ", "3", "1/9");
            list(g, content(0, 50), {"HALF SEQ", "DOUBLE SEQ", "HALF SPEED", "DOUBLE SPEED", "THINNER", "BUSIER", "HUMANISED", "REVERSED", "OCTAVE UP", "OCTAVE DOWN"}, 4, 1);
            detail(g, "PUSH: TO P7", "50%");
        }},
        {"entry", "2 ENTRY: PADS WRITE AT THE CARET", [](juce::Graphics& g) {
            rail(g, "ENTR", "5", "2/9");
            title(g, "STEP 5", "BAR 1");
            std::vector<bool> hits {1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
            step_row(g, content(12, 7).reduced(1, 0), hits, 16);
            g.fillRect(content_x + 1 + 4 * 6, 20, 5, 2);
            value_cells(g, content(22, 40), {"LEN", "VEL", "CHN", "", "", "", "", ""}, {"1", "HIT", "100%", "", "", "", "", ""});
            detail(g, "PUSH:REST X:BACK");
        }},
        {"roll", "3 ROLL: THE NOTES; WHEEL FOCUS, KNOBS", [](juce::Graphics& g) {
            rail(g, "ROLL", "3", "3/9");
            roll(g, content(1, 50), example_notes(), 0, 16 * 24, 48, 59, 3, 5 * 24, 16 * 24);
            detail(g, "D#2 V96 1/16+", "B1");
        }},
        {"roll-knobs", "ROLL, KNOB TOUCHED: THE NOTE IN FOCUS", [](juce::Graphics& g) {
            rail(g, "ROLL", "3", "3/9");
            title(g, "D#2 AT 1.3.1", "2 MRK");
            value_cells(g, content(11, 40), {"LEN", "VEL", "CHN", "NDG", "RTC", "OCT", "MOV", "KEY"},
                        {"6/16", "96", "100%", "0", "1", "0", "Q", "D#2"}, 1);
            detail(g, "PUSH: JOBS");
        }},
        {"motion", "4 MOTION: THE PATTERN'S LANES", [](juce::Graphics& g) {
            rail(g, "MOTN", "3", "4/9");
            list(g, content(0, 50), {"CUTOFF    24 PTS", "RESONANCE  6 PTS", "LEVEL     12 PTS", "STEP LOCKS     3"}, 0, 1);
            detail(g, "PUSH:OFF X:CLEAR");
        }},
        {"mod", "5 MOD: EIGHT MODULATORS, LFO 1", [](juce::Graphics& g) {
            rail(g, "MOD", "1", "5/9");
            lfo_shape(g, content(1, 9).reduced(2, 0), 0);
            value_cells(g, content(11, 40), {"KND", "SHP", "RAT", "SYN", "DPT", "TGT", "LOW", "HI"},
                        {"LFO", "SINE", "1/4", "ON", "40", "CUT", "0", "127"});
            detail(g, "WHEEL: MOD 1-8");
        }},
        {"macros", "6 MACROS: EIGHT, A KNOB EACH", [](juce::Graphics& g) {
            rail(g, "MACR", "1", "6/9");
            title(g, "MACROS", "8");
            value_cells(g, content(11, 40), {"DRK", "WID", "DRV", "SPC", "M5", "M6", "M7", "M8"},
                        {"64", "20", "0", "88", "-", "-", "-", "-"});
            detail(g, "PUSH: LANES");
        }},
        {"feel", "7 FEEL: TIME CORRECT, HEARD LIVE", [](juce::Graphics& g) {
            rail(g, "FEEL", "3", "7/9");
            title(g, "PATTERN 3", "LIVE");
            value_cells(g, content(11, 40), {"STR", "SWG", "HUM", "HVL", "GRV", "AMT", "", ""},
                        {"60", "56", "10", "20", "P2", "100", "", ""});
            detail(g, "PUSH: TO ALL");
        }},
        {"follow", "8 FOLLOW: WHAT COMES AFTER PATTERN 3", [](juce::Graphics& g) {
            rail(g, "FOLW", "3", "8/9");
            title(g, "PATTERN 3", "NEXT");
            value_cells(g, content(11, 40), {"DO", "AFT", "CHN", "OR", "BY", "", "", ""},
                        {"NEXT", "2", "75%", "ANY", "BARS", "", "", ""});
            detail(g, "PUSH: COLUMN");
        }},
        {"lane", "9 LANE: A01'S OWN LENGTH AND EUCLID", [](juce::Graphics& g) {
            rail(g, "LANE", "A01", "9/9");
            std::vector<bool> hits {1, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0};
            step_row(g, content(2, 7).reduced(1, 0), hits, 8);
            value_cells(g, content(11, 40), {"LAN", "HIT", "STP", "ROT", "", "", "", ""}, {"8", "5", "8", "0", "", "", "", ""});
            detail(g, "PUSH: LAY E(5,8)");
        }},
    };
}

std::vector<LitPanel> sequencer_lights() {
    using namespace move::colour;
    using seq::Cell;
    std::vector<LitPanel> panels;
    {   // Steps (manual 9.5): white notes, the track's colour dim where empty, the playhead green, past the loop grey.
        Lit s;
        s.tracks = bank_lights();
        s.pads = kit_lights(0);
        for (unsigned n = 0; n < 16; ++n) s.steps[n] = kick[n] ? white : five.dim;
        s.steps[8] = green;
        s.button(Button::Play, green);
        s.button(Button::Rec, move::white_dim);
        white_buttons(s);
        panels.push_back({"lights-steps", "PLAY: A01'S STEPS, THE PLAYHEAD GREEN", s});
    }
    {   // A step held: its pad white, the automation's range red (14.2.4).
        Lit s;
        s.tracks = bank_lights();
        s.pads = kit_lights(-1);
        s.pads[0] = white;
        for (unsigned n = 0; n < 16; ++n) s.steps[n] = kick[n] ? white : five.dim;
        for (unsigned n = 3; n < 6; ++n) s.steps[n] = red;
        s.knobs[7] = red;
        s.button(Button::Play, green);
        white_buttons(s);
        panels.push_back({"lights-step-held", "STEP 4 HELD, KNOB 8 LOCKED: RED 4-6", s});
    }
    {   // Loop Mode (12.1): the selected bar white, the bars in the loop in the track's colour, the rest dim.
        Lit s;
        s.tracks = bank_lights();
        s.pads = kit_lights(0);
        for (unsigned n = 0; n < 6; ++n) s.steps[n] = n < 4 ? five.bright : dark_grey;
        s.steps[1] = white;
        s.step_pulsing[2] = true;
        s.button(Button::Loop, move::white_bright);
        s.button(Button::Play, green);
        white_buttons(s);
        s.button(Button::Loop, move::white_bright);
        panels.push_back({"lights-loop", "LOOP: BAR 2 WHITE, LOOP 1-4, 3 PLAYING", s});
    }
    {   // Session (17.1): four tracks a row, eight patterns; playing in colour, queued pulsing green.
        Lit s;
        s.tracks = bank_lights();
        const Cell rows[4][8] {
            {Cell::Notes, Cell::Notes, Cell::Playing, Cell::Notes, Cell::Queued, Cell::Empty, Cell::Empty, Cell::Empty},
            {Cell::Playing, Cell::Notes, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty},
            {Cell::Notes, Cell::Notes, Cell::Stopping, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty},
            {Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty, Cell::Empty}};
        for (unsigned r = 0; r < 4; ++r)
            for (unsigned c = 0; c < 8; ++c) {
                unsigned pad = (3 - r) * 8 + c; // the top row is the bank's first track
                auto colour = track_colour(4 + r);
                switch (rows[r][c]) {
                case Cell::Empty: break;
                case Cell::Notes: s.pads[pad] = colour.dim; break;
                case Cell::Playing: s.pads[pad] = colour.bright; break;
                case Cell::Queued: s.pads[pad] = green; s.pulsing[pad] = true; break;
                case Cell::Stopping: s.pads[pad] = colour.bright; s.pulsing[pad] = true; break;
                }
            }
        s.pads[(3 - 0) * 8 + 2] = white;
        s.pulsing[(3 - 0) * 8 + 2] = true;
        s.button(Button::Play, green);
        s.button(Button::Menu, move::white_bright);
        white_buttons(s);
        s.button(Button::Menu, move::white_bright);
        panels.push_back({"lights-session", "SESSION: P3 SELECTED, P5 QUEUED (GREEN)", s});
    }
    {   // Recording (14.1): the count-in flashes Rec; the steps count the beats.
        Lit s;
        s.tracks = bank_lights();
        s.pads = kit_lights(0);
        for (unsigned beat = 0; beat < 4; ++beat) s.steps[beat * 4] = beat == 2 ? white : light_grey;
        s.button(Button::Rec, red);
        s.button(Button::Play, green);
        white_buttons(s);
        panels.push_back({"lights-count-in", "REC: COUNT-IN BEAT 3, REC FLASHING RED", s});
    }
    {   // Shift held: the steps with a Shift job; the sequencer's 3, 11, 14, 15, 16 lit too, Repeat bright while on.
        Lit s;
        s.tracks = bank_lights();
        s.pads = kit_lights(-1);
        for (unsigned n : {2u, 4u, 5u, 6u, 13u, 14u, 15u}) { s.steps[n] = light_grey; s.icons[n] = true; }
        s.steps[10] = white;
        s.icons[10] = true;
        s.button(Button::Shift, move::white_bright);
        s.button(Button::Play, green);
        white_buttons(s);
        s.button(Button::Shift, move::white_bright);
        panels.push_back({"lights-shift", "SHIFT: 3, 11 (REPEAT ON), 14, 15, 16", s});
    }
    {   // Capture (14.3): lit while there is something to keep.
        Lit s;
        s.tracks = bank_lights();
        s.pads = kit_lights(1);
        for (unsigned n = 0; n < 16; ++n) s.steps[n] = five.dim;
        s.button(Button::Capture, move::white_bright);
        s.button(Button::Play, dark_grey);
        white_buttons(s);
        panels.push_back({"lights-capture", "CAPTURE LIT: NOTES PLAYED TO KEEP", s});
    }
    {   // Fill (Melodee): Loop held while playing; Loop bright, FILL steps' notes pulse.
        Lit s;
        s.tracks = bank_lights();
        s.pads = kit_lights(0);
        for (unsigned n = 0; n < 16; ++n) s.steps[n] = kick[n] ? white : five.dim;
        s.steps[12] = white;
        s.step_pulsing[12] = true;
        s.steps[13] = green;
        s.button(Button::Play, green);
        white_buttons(s);
        s.button(Button::Loop, move::white_bright);
        panels.push_back({"lights-fill", "LOOP HELD, PLAYING: FILL; STEP 13 FILL", s});
    }
    return panels;
}

bool write_sequencer_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = sequencer_screens();
    auto panels = sequencer_lights();
    auto each = folder.getChildFile("sequencer");
    each.createDirectory();
    bool ok = true;
    constexpr int scale = 4, gap = 24, caption = 30, top = 92;
    int screen_w = width * scale, screen_h = height * scale;
    for (const auto& screen : screens)
        ok = write_png(screen_image(screen.draw, scale), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = write_png(lit_image(panel.lit, screen_w, screen_h), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    int rows = int(screens.size() + 1) / 2, panel_rows = int(panels.size() + 1) / 2;
    int panels_top = top + rows * (screen_h + caption + gap);
    juce::Image image(juce::Image::RGB, gap + 2 * (screen_w + gap), panels_top + panel_rows * (screen_h + caption + gap), true);
    {
        juce::Graphics g(image);
        g.fillAll(juce::Colour(0xff1b1d22));
        g.setColour(juce::Colour(0xffd8f0ff));
        move::draw_text(g, gap, 20, "MELODEE ON MOVE: SEQUENCER", 4);
        g.setColour(juce::Colour(0xff8a96a3));
        move::draw_text(g, gap, 60, "STEPS, LOOP, SESSION, REC, CAPTURE AS THE MOVE HAS THEM. DOCS/UI.MD", 2);
        auto place = [&](size_t n, int from) { return juce::Point<int>(gap + int(n % 2) * (screen_w + gap), from + int(n / 2) * (screen_h + caption + gap)); };
        for (size_t n = 0; n < screens.size(); ++n) {
            auto at = place(n, top);
            g.drawImageAt(screen_image(screens[n].draw, scale), at.x, at.y);
            g.setColour(juce::Colour(0xff8a96a3));
            move::draw_text(g, at.x, at.y + screen_h + 8, screens[n].caption, 2);
        }
        for (size_t n = 0; n < panels.size(); ++n) {
            auto at = place(n, panels_top);
            g.drawImageAt(lit_image(panels[n].lit, screen_w, screen_h), at.x, at.y);
            g.setColour(juce::Colour(0xff8a96a3));
            move::draw_text(g, at.x, at.y + screen_h + 8, panels[n].caption, 2);
        }
    }
    auto file = folder.getChildFile("sequencer.png");
    ok = write_png(image, file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace mockup
} // namespace ui
