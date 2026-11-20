// SPDX-License-Identifier: AGPL-3.0-only
// The Looper on the Move (docs/UI.md, "Looper"; melodee-sample's
// docs/LOOPER.md), as mockups: looper.png. Shift + Step 12 opens it (Kerem,
// 2026-11-20: no Move job there). Four loops, L1-L4, each a loop track of
// its own: on the screen four rows, L1 on top, as the track buttons and the
// pad rows sit beside them. The track buttons pick a loop (the Move's track
// select, here for the loop tracks), Mute and Shift + Mute with one mute and
// solo it, a track button held with the Volume knob is its level, as on the
// Move. Each pad row is its loop: record / overdub, play / stop on the
// boundary, undo / redo, held to clear; the right 16 pads keep playing the
// selected track. Rec records the selected loop, as Rec records the
// selected track on the Move; the wheel picks the loop, its push opens the
// loop's sheet (MORE); Shift + the wheel turns to MIX. Knobs 1-8 are the
// loop's: source, length, level, feedback, pan, send, quantize, after take.
// Layout B, Melodee's own grid (a column a loop on the left 16), is drawn
// beside it for Kerem to choose.
#include "ui/mockups.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/lights.h"
#include "ui/looper_view.h"
#include "ui/track_style.h"
#include "ui/widgets.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>

namespace ui {
namespace {

using Draw = std::function<void(juce::Graphics&)>;
struct Screen { const char* name; const char* caption; Draw draw; };
using move::Button;
namespace colour = move::colour;
const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// ---------------------------------------------------------------- example

// A drum loop, a bass line and a sung phrase, as peaks.
std::vector<float> drum_peaks() {
    std::vector<float> peaks(64, 0.05f);
    for (int beat = 0; beat < 16; ++beat) {
        float level = beat % 4 == 0 ? 0.95f : beat % 2 ? 0.4f : 0.7f;
        for (int k = 0; k < 4; ++k) peaks[size_t(beat * 4 + k)] = std::max(0.05f, level * std::exp(-k / 1.2f));
    }
    return peaks;
}
std::vector<float> bass_peaks() {
    std::vector<float> peaks(64);
    for (int c = 0; c < 64; ++c) peaks[size_t(c)] = 0.35f + 0.3f * float(std::abs(std::sin(c * 0.39))) * (c % 8 < 6 ? 1.0f : 0.3f);
    return peaks;
}
std::vector<float> voice_peaks() {
    std::vector<float> peaks(64, 0.03f);
    for (int c = 0; c < 64; ++c) {
        float swell = float(std::exp(-std::pow((c - 12) / 8.0, 2)) + 0.8 * std::exp(-std::pow((c - 40) / 10.0, 2)));
        peaks[size_t(c)] = std::min(1.0f, 0.03f + swell);
    }
    return peaks;
}

// The example: playing at 120 BPM, bar 3. L1 a 4-bar drum loop playing,
// L2 recording its first take of 4 bars from Track 2 (selected), L3 a
// 2-bar bass overdubbing, L4 empty (a free take fills the strip as it grows).
std::array<looper::Row, 4> example_rows() {
    std::array<looper::Row, 4> rows;
    rows[0] = {"PLAY", false, drum_peaks(), 1, 0.56f, "4B", false, false};
    rows[1] = {"REC", true, voice_peaks(), 0.3f, 0.3f, "4B", true, false};
    rows[2] = {"DUB", true, bass_peaks(), 1, 0.12f, "2B", false, false};
    rows[3] = {};
    rows[3].length = "1B";
    return rows;
}

void loops_screen(juce::Graphics& g, const std::array<looper::Row, 4>& rows, std::string_view big, std::string_view bottom,
                  std::string_view left, std::string_view right = {}) {
    rail(g, "LOOP", big, bottom);
    looper::rows(g, content(0, subject_height), rows);
    detail(g, left, right);
}

// ---------------------------------------------------------------- screens

std::vector<Screen> looper_screens() {
    return {
        {"off", "SHIFT + STEP 12, LOOPER OFF: IT ASKS", [](juce::Graphics& g) {
            rail(g, "LOOP", "", "OFF");
            move::draw_text(g, content_x + 2, 2, "LOOPER", 2);
            move::draw_text(g, content_x + 2, 22, "4 LOOPS, L1-L4");
            move::draw_text(g, content_x + 2, 32, "45 MB EACH");
            detail(g, "PUSH: TURN ON");
        }},
        {"memory", "TURNED ON: MEMORY MADE AS IT PLAYS ON", [](juce::Graphics& g) {
            std::array<looper::Row, 4> rows {};
            for (auto& row : rows) row.length = "1B";
            rows[0].selected = true;
            loops_screen(g, rows, "L1", "BAR", "MAKING MEMORY", "2/4");
        }},
        {"loops", "LOOPS: L1 PLAYS, L2 RECORDS, L3 DUBS", [](juce::Graphics& g) {
            loops_screen(g, example_rows(), "L2", "BAR", "TRACK 2", "3.2");
        }},
        {"armed", "STOPPED: L1 ARMED, PLAY STARTS IT", [](juce::Graphics& g) {
            std::array<looper::Row, 4> rows {};
            rows[0] = {"ARM", false, {}, 1, -1, "4B", true, false};
            rows[1].length = rows[2].length = rows[3].length = "1B";
            loops_screen(g, rows, "L1", "BAR", "PLAY: COUNT-IN");
        }},
        {"tempo", "140 BPM: L1 TAKEN AT 120, L3 FITTING", [](juce::Graphics& g) {
            auto rows = example_rows();
            rows[0].length = "@120";
            rows[0].selected = true;
            rows[1] = {"STOP", false, voice_peaks(), 1, -1, "2B", false, false};
            rows[2] = {"FIT", false, bass_peaks(), 1, 0.5f, "@120", false, false};
            loops_screen(g, rows, "L1", "BAR", "REC: FIT, DUB", "x2");
        }},
        {"knob-source", "KNOB 1 TOUCHED: L2'S SOURCE", [](juce::Graphics& g) {
            rail(g, "KNOB", "1", "L2");
            big_parameter(g, content(4, 44), "L2 SOURCE", "TRACK 2", 2.0f / 22.0f);
        }},
        {"knob-length", "KNOB 2: LENGTH, BEFORE A TAKE", [](juce::Graphics& g) {
            rail(g, "KNOB", "2", "L4");
            big_parameter(g, content(4, 44), "L4 LENGTH", "4 BARS", 3.0f / 5.0f);
        }},
        {"knob-after", "KNOB 8: AFTER TAKE, ALL FOUR", [](juce::Graphics& g) {
            rail(g, "KNOB", "8", "ALL");
            big_parameter(g, content(4, 44), "AFTER TAKE", "OVERDUB", 0.5f);
        }},
        {"mix", "SHIFT + WHEEL: MIX, L3 MUTED, L1 SOLO", [](juce::Graphics& g) {
            rail(g, "MIX", "L2", "2/2");
            std::array<looper::Strip, 4> strips {{{0.79f, 0.6f, 0, false, true, false},
                                                  {0.65f, 0.3f, -0.4f, false, false, true},
                                                  {0.55f, 0.0f, 0.3f, true, false, false},
                                                  {0.79f, 0.0f, 0, false, false, false}}};
            looper::strips(g, content(0, subject_height), strips);
            detail(g, "-3.2 DB", "SEND 20");
        }},
        {"more", "PUSH: L1'S SHEET, THE WHEEL WALKS IT", [](juce::Graphics& g) {
            rail(g, "MORE", "L1", "PLAY");
            list(g, content(0, height), {"STOP NEXT BAR", "STOP NOW", "COMMIT TO A10", "COMMIT + NOTE", "SEND TO BUS A", "CLEAR L1",
                                         "LOOPER OFF", "IN DELAY 128"}, 0, 1);
        }},
        {"more-commit", "MORE: COMMIT, THE KIT'S NEXT EMPTY PAD", [](juce::Graphics& g) {
            rail(g, "MORE", "L1", "PLAY");
            list(g, content(0, height), {"STOP NEXT BAR", "STOP NOW", "COMMIT TO A10", "COMMIT + NOTE", "SEND TO BUS A", "CLEAR L1",
                                         "LOOPER OFF", "IN DELAY 128"}, 2, 1);
            sheet(g, {content_x, detail_y - 2, content_width, 10});
            move::draw_text_centred(g, content_x, detail_y, content_width, "L1 TO A10");
        }},
        {"delay", "MORE, LAST: CABLE OUT TO IN, MEASURED", [](juce::Graphics& g) {
            rail(g, "MORE", "L1", "PLAY");
            list(g, content(0, height), {"STOP NEXT BAR", "STOP NOW", "COMMIT TO A10", "COMMIT + NOTE", "SEND TO BUS A", "CLEAR L1",
                                         "LOOPER OFF", "MEASURING 2/4"}, 7, 1);
        }},
        {"clear", "MORE: CLEAR L1, OR ITS CLEAR PAD HELD", [](juce::Graphics& g) {
            rail(g, "CLR", "L1", "PLAY");
            looper::question(g, "CLEAR L1?", "4 BARS, 3 LAYERS", "UNDO BRINGS NONE");
        }},
        {"off-ask", "MORE: LOOPER OFF, WITH LOOPS", [](juce::Graphics& g) {
            rail(g, "LOOP", "", "OFF");
            looper::question(g, "TURN IT OFF?", "3 LOOPS ARE LOST", "THEIR FILES TOO");
        }},
        {"refused", "A KNOB WHILE IT RECORDS: SAID ONCE", [](juce::Graphics& g) {
            loops_screen(g, example_rows(), "L2", "BAR", "TRACK 2", "3.2");
            sheet(g, {content_x, detail_y - 2, content_width, 10});
            move::draw_text_centred(g, content_x, detail_y, content_width, "BETWEEN TAKES");
        }},
    };
}

// ---------------------------------------------------------------- lights

// The Move's lights as the controls sheet draws them (mockups_controls.cpp).
struct Panel {
    std::array<uint8_t, 4> tracks {};                       // palette; Track 1 (top) first
    std::array<uint8_t, move::pad_count> pads {};            // palette; bottom left first
    std::array<bool, move::pad_count> pulsing {};            // ringed: pulsing or blinking
    std::array<uint8_t, move::step_count> steps {};          // palette
    std::array<uint8_t, size_t(Button::Count)> buttons {};   // palette (RGB) or brightness (white)
    void button(Button b, uint8_t value) { buttons[size_t(b)] = value; }
};
struct Lights { const char* name; const char* caption; Panel panel; };

uint8_t dim_red() { return move::nearest_colour({90, 0, 0}); }

// A loop's row of pads, layout A: L1 the top row; columns 1-4 its jobs.
unsigned row_pad(unsigned loop, unsigned job) { return (3 - loop) * move::pad_columns + job; }

// The example's lights on layout A: L1 playing, L2 recording (selected), L3
// overdubbing, L4 empty; the right 16 the selected kit's first 16 pads.
Panel looper_panel() {
    Panel p;
    using looper::loop_colour;
    p.tracks = {loop_colour(0).dim, loop_colour(1).bright, loop_colour(2).dim, colour::dark_grey};
    // L1 playing: its record pad dim, play bright, undo there, clear dim red.
    p.pads[row_pad(0, 0)] = loop_colour(0).dim;
    p.pads[row_pad(0, 1)] = loop_colour(0).bright;
    p.pads[row_pad(0, 2)] = colour::light_grey;
    p.pads[row_pad(0, 3)] = dim_red();
    // L2 recording its first take: record red.
    p.pads[row_pad(1, 0)] = colour::red;
    p.pads[row_pad(1, 1)] = loop_colour(1).dim;
    p.pads[row_pad(1, 3)] = dim_red();
    // L3 overdubbing: record orange, play bright, undo comes when the pass ends.
    p.pads[row_pad(2, 0)] = colour::orange;
    p.pads[row_pad(2, 1)] = loop_colour(2).bright;
    p.pads[row_pad(2, 2)] = colour::dark_grey;
    p.pads[row_pad(2, 3)] = dim_red();
    // L4 empty: only its record pad, dim.
    p.pads[row_pad(3, 0)] = loop_colour(3).dim;
    // The right 16: the kit's A01-A16, nine with samples, A05 the last.
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 4; column < 8; ++column) {
            unsigned kit = row * 4 + (column - 4);
            p.pads[row * 8 + column] = kit == 4 ? colour::white : kit < 9 ? colour::azure : colour::off;
        }
    // The steps: L2's take so far across its bar, red.
    for (unsigned n = 0; n < 5; ++n) p.steps[n] = dim_red();
    p.steps[5] = colour::red;
    for (auto b : {Button::Back, Button::Mute, Button::Shift, Button::Up, Button::Down}) p.button(b, move::white_dim);
    p.button(Button::Rec, colour::red);
    p.button(Button::Play, colour::green);
    return p;
}

std::vector<Lights> looper_lights() {
    using looper::loop_colour;
    std::vector<Lights> panels;
    panels.push_back({"lights-a", "A: A ROW A LOOP; RIGHT 16 THE KIT", looper_panel()});
    {   // Stopped, L1 armed: its record pad blinks red; Rec blinks; Play starts it after the count-in.
        Panel p;
        p.tracks = {loop_colour(0).bright, colour::dark_grey, colour::dark_grey, colour::dark_grey};
        p.pads[row_pad(0, 0)] = colour::red;
        p.pulsing[row_pad(0, 0)] = true;
        for (unsigned loop = 1; loop < 4; ++loop) p.pads[row_pad(loop, 0)] = loop_colour(loop).dim;
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 4; column < 8; ++column) {
                unsigned kit = row * 4 + (column - 4);
                p.pads[row * 8 + column] = kit < 9 ? colour::azure : colour::off;
            }
        for (auto b : {Button::Back, Button::Mute, Button::Shift, Button::Up, Button::Down}) p.button(b, move::white_dim);
        p.button(Button::Rec, colour::red);
        p.button(Button::Play, colour::dark_grey);
        panels.push_back({"lights-armed", "STOPPED, L1 ARMED: ITS PAD BLINKS", p});
    }
    {   // L1's clear pad held: filling to red; let go early, nothing.
        Panel p = looper_panel();
        p.pads[row_pad(0, 3)] = colour::red;
        p.pulsing[row_pad(0, 3)] = true;
        panels.push_back({"lights-clear", "L1'S CLEAR PAD HELD 0.6 S: CLEARED", p});
    }
    {   // Mute held: L3 muted (grey), L1 soloed (blue), as the Move shows tracks.
        Panel p = looper_panel();
        p.tracks = {colour::blue, loop_colour(1).bright, colour::dark_grey, colour::dark_grey};
        p.button(Button::Mute, move::white_bright);
        panels.push_back({"lights-mute", "MUTE + ROW 3: L3 MUTED; SHIFT: L1 SOLO", p});
    }
    {   // On the Looper the right 16 play the selected track: here a synth, chromatic from C.
        Panel p = looper_panel();
        auto two = track_colour(1);
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 4; column < 8; ++column) {
                unsigned note = row * 4 + (column - 4);
                unsigned in_octave = note % 12;
                bool natural = in_octave != 1 && in_octave != 3 && in_octave != 6 && in_octave != 8 && in_octave != 10;
                p.pads[row * 8 + column] = in_octave == 0 ? two.bright : natural ? colour::dark_grey : colour::off;
            }
        p.pads[1 * 8 + 6] = colour::white;
        panels.push_back({"lights-synth", "RIGHT 16 ON A SYNTH: 16 NOTES FROM C", p});
    }
    {   // Layout B, Melodee's own grid: a column a loop on the left 16 (top row record).
        Panel p = looper_panel();
        for (unsigned n = 0; n < 16; ++n) p.pads[(n / 4) * 8 + n % 4] = 0;
        auto at = [](unsigned loop, unsigned job) { return (3 - job) * 8 + loop; }; // job 0 the top row
        p.pads[at(0, 0)] = loop_colour(0).dim; p.pads[at(0, 1)] = loop_colour(0).bright; p.pads[at(0, 2)] = colour::light_grey; p.pads[at(0, 3)] = dim_red();
        p.pads[at(1, 0)] = colour::red; p.pads[at(1, 1)] = loop_colour(1).dim; p.pads[at(1, 3)] = dim_red();
        p.pads[at(2, 0)] = colour::orange; p.pads[at(2, 1)] = loop_colour(2).bright; p.pads[at(2, 2)] = colour::dark_grey; p.pads[at(2, 3)] = dim_red();
        p.pads[at(3, 0)] = loop_colour(3).dim;
        panels.push_back({"lights-b", "B (MELODEE'S GRID): A COLUMN A LOOP", p});
    }
    return panels;
}

// The Move as Ableton's controls figure lays it out (mockups_controls.cpp
// draws the same).
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
        g.fillEllipse(cx - 1.5f, Y(76) - 1.5f, 3, 3);
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
    }
    return image;
}

juce::Image sheet(const std::vector<Screen>& screens, const std::vector<Lights>& panels) {
    using mockup::screen_image;
    constexpr int scale = 4, gap = 24, caption = 30, top = 92;
    int screen_w = width * scale, screen_h = height * scale;
    int rows = int(screens.size() + 1) / 2, panel_rows = int(panels.size() + 1) / 2;
    int panels_top = top + rows * (screen_h + caption + gap);
    juce::Image image(juce::Image::RGB, gap + 2 * (screen_w + gap), panels_top + panel_rows * (screen_h + caption + gap), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    g.setColour(juce::Colour(0xffd8f0ff));
    move::draw_text(g, gap, 20, "MELODEE ON MOVE: LOOPER", 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, "SHIFT + STEP 12. 4 LOOPS ON TRACKS L1-L4. DOCS/UI.MD", 2);
    auto place = [&](size_t n, int from) { return juce::Point<int>(gap + int(n % 2) * (screen_w + gap), from + int(n / 2) * (screen_h + caption + gap)); };
    for (size_t n = 0; n < screens.size(); ++n) {
        auto at = place(n, top);
        g.drawImageAt(screen_image(screens[n].draw, scale), at.x, at.y);
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

bool write_looper_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = looper_screens();
    auto panels = looper_lights();
    auto each = folder.getChildFile("looper");
    each.createDirectory();
    bool ok = true;
    for (const auto& screen : screens)
        ok = mockup::write_png(mockup::screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = mockup::write_png(panel_image(panel.panel, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    auto file = folder.getChildFile("looper.png");
    ok = mockup::write_png(sheet(screens, panels), file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace ui
