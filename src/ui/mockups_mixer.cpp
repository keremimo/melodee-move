// SPDX-License-Identifier: AGPL-3.0-only
// The mixer and effects (docs/UI.md, "Mixer and effects"), as mockups:
// mixer.png. Melodee's mix and its 41 effects on the Move's own grammar
// (its manual, chapters 7 and 16): on Play the wheel walks the track's
// items, now its effects too (six a track, a kit pad's own four after the
// pad), the knobs follow the item in focus, the wheel's push opens the
// item's browser and Shift + push its options, as Sample Options. The Move
// has two effects a Track Preset and two main effects a Set; Melodee has six
// a track, four buses and six on the master before its compressor. Shift +
// Step 4 (no Move job) opens the Mixer: Melodee's MIX hub, its pages along
// the top the wheel walks: the sixteen tracks' levels, pans and sends, the
// main out, then the strip of the track in hand (EQ, COMP); Left / Right
// past 9-16 the eight submixes (melodee-sample docs/MIXER_PLAN.md, step 8).
// Shift + Step 4 again: Knob FX, again Pad FX, again Flex Beat (Kerem,
// 2026-11-20).
#include "ui/mockups.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/lights.h"
#include "ui/mix_widgets.h"
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
using mix::Item;
using mix::Strip;
const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// ---------------------------------------------------------------- the example project

// As tracks.png: sixteen tracks, nine in use, track 5 selected.
struct Example { const char* tag; const char* name; int level; int pan; int send_a; float meter; bool muted, soloed; };
constexpr Example example[16] {
    {"KIT", "DRUMS", 100, 0, 10, .82f, false, false},  {"OBXD", "LEAD", 88, -12, 35, .55f, false, false},
    {"KEYS", "GRAND", 76, 18, 50, .40f, false, false}, {"MIDI", "TB-03", 100, 0, 0, 0, false, false},
    {"KIT", "BREAKS", 92, 0, 25, .70f, false, false},  {"AUD", "VOX", 70, 6, 60, .30f, true, false},
    {"SURG", "PAD", 60, -30, 80, .45f, false, false},  {"", "", 100, 0, 0, 0, false, false},
    {"KIT", "FOLEY", 54, 40, 0, .20f, false, false},   {"", "", 100, 0, 0, 0, false, false},
    {"", "", 100, 0, 0, 0, false, false},              {"", "", 100, 0, 0, 0, false, false},
    {"", "", 100, 0, 0, 0, false, false},              {"", "", 100, 0, 0, 0, false, false},
    {"", "", 100, 0, 0, 0, false, false},              {"", "", 100, 0, 0, 0, false, false},
};
constexpr unsigned selected = 4; // track 5

std::vector<Strip> track_strips(unsigned first, int which) { // which: 0 level, 1 pan, 2 send A
    std::vector<Strip> out;
    for (unsigned t = first; t < first + 8; ++t) {
        const Example& e = example[t];
        Strip s;
        s.label = std::to_string(t + 1);
        s.used = e.tag[0] != 0;
        s.muted = e.muted;
        s.soloed = e.soloed;
        s.meter = e.meter;
        if (which == 1) { s.bipolar = true; s.value = e.pan / 50.0f; }
        else s.value = which == 0 ? e.level / 127.0f : e.send_a / 100.0f;
        out.push_back(s);
    }
    return out;
}

// The hub's pages along the top: the wheel walks them.
std::vector<Item> hub() { return {{"VOL"}, {"PAN"}, {"SNDA"}, {"SNDB"}, {"SNDC"}, {"SNDD"}, {"MAIN"}, {"EQ"}, {"COMP"}}; }

// The submixes: drums (1, 5, 9) into S1, the lead and the pad (2, 7) into S2.
std::vector<Strip> sub_strips() {
    const float level[8] {0.78f, 0.7f, 100 / 127.0f, 100 / 127.0f, 100 / 127.0f, 100 / 127.0f, 100 / 127.0f, 100 / 127.0f};
    const float heard[8] {0.6f, 0.5f, 0, 0, 0, 0, 0, 0};
    std::vector<Strip> out;
    for (int n = 0; n < 8; ++n) {
        Strip s;
        s.label = std::to_string(n + 1); // the rail says S1-8
        s.value = level[n];
        s.meter = heard[n];
        s.used = n < 2; // no track plays into it: a dot
        out.push_back(s);
    }
    return out;
}

// The kit's Device View: the kit, its pad A05, A05's own effect and the
// plus that adds its next, the track's two and the plus that adds the next.
std::vector<Item> kit_items() { return {{"KIT"}, {"A05"}, {"CRSH", true}, {"+", true}, {"ECHO"}, {"ROOM"}, {"+"}}; }

std::vector<float> pump(int columns) { // a compressor ducking under four kicks
    std::vector<float> out(size_t(columns), 0.0f);
    for (int c = 0; c < columns; ++c) {
        float into = float(c % (columns / 4)) / float(columns / 4);
        out[size_t(c)] = into < 0.04f ? into * 20 : std::exp(-into * 4.5f);
    }
    return out;
}

// Eight knob cells under a title line, as a module's page is drawn.
// No detail line under them: the cells take the screen's foot, as a
// module's page does.
void cells(juce::Graphics& g, const char* title, const char* right, const std::array<std::string, 8>& names, const std::array<float, 8>& values) {
    move::draw_text(g, content_x + 1, 11, title);
    if (right[0]) move::draw_text(g, width - move::text_width(right) - 1, 11, right);
    knob_cells(g, content(20, height - 20), names, values);
}

// ---------------------------------------------------------------- screens

std::vector<Screen> mixer_screens() {
    return {
        // The Device View, with the effects in it.
        {"play-fx", "PLAY: WHEEL ON AN EFFECT, KNOBS ITS SIX", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "FX1");
            mix::item_row(g, kit_items(), 4);
            cells(g, "ECHO", "TIME", {"TIME", "FEEDBACK", "MIX", "TONE", "SYNC", "", "", ""}, {0.6f, 0.4f, 0.3f, 0.6f, 0.7f, 0, 0, 0});
        }},
        {"play-add", "PAST THE LAST EFFECT: THE PLUS", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "FX3");
            mix::item_row(g, kit_items(), 6);
            move::draw_text_centred(g, content_x, 16, content_width, "ADD AN", 2);
            move::draw_text_centred(g, content_x, 32, content_width, "EFFECT", 2);
            detail(g, "2/6", "PUSH: BROWSE");
        }},
        {"play-mix", "WHEEL ON THE TRACK: ITS MIX ON THE KNOBS", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "MIX");
            mix::item_row(g, kit_items(), 0);
            move::draw_text(g, content_x + 1, 11, "DRUMS");
            move::draw_text(g, width - 6 - move::text_width(">S1"), 11, ">S1"); // where it plays: submix 1
            knob_cells(g, {content_x, 20, content_width - 6, 34}, {"VOL", "PAN", ">A", ">B", ">C", ">D", "OUT", "WIDTH"},
                       {100 / 127.0f, 0.5f, 0.1f, 0, 0, 0.3f, 1 / 8.0f, 0.5f});
            g.fillRect(width - 4, 14, 1, 1);
            g.fillRect(width - 4, 20, 2, 34); // its meter, left and right
            g.fillRect(width - 1, 23, 1, 31);
            detail(g, "0.0DB", "CPU 4%");
        }},
        {"play-pad-fx", "A05'S OWN EFFECT, BEFORE THE TRACK'S", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "A05");
            mix::item_row(g, kit_items(), 2);
            cells(g, "CRUSH", "A05 1/4", {"BITS", "RATE", "MIX", "", "", "", "", ""}, {0.4f, 0.3f, 1, 0, 0, 0, 0, 0});
        }},
        {"fx-families", "PUSH: THE BROWSER, BY FAMILY", [](juce::Graphics& g) {
            rail(g, "LOAD", "1", "FX1");
            list(g, content(0, height), {"SPACE >", "TIME >", "MOTION >", "DIRT >", "TONE >", "DYNAMICS >", "SCHWUNG >", "NONE"}, 1, 1);
        }},
        {"fx-time", "TIME: EACH LOADS AS THE WHEEL TURNS", [](juce::Graphics& g) {
            rail(g, "LOAD", "1", "FX1");
            list(g, content(0, 50), {"ECHO", "TAPE ECHO", "PING PONG", "LOOPER", "DIFF DELAY", "SAMPLE DELAY"}, 1, 1);
            detail(g, "", "PUSH: OK");
        }},
        {"fx-schwung", "SCHWUNG'S AUDIO FX: PUSH LOADS", [](juce::Graphics& g) {
            rail(g, "LOAD", "1", "FX1");
            list(g, content(0, 50), {"CLOUDSEED", "MVERB", "PSX VERB", "TAPESCAM", "TAPEDELAY", "DUCKER"}, 0, 1);
            detail(g, "", "PUSH: LOAD");
        }},
        {"fx-options", "SHIFT + PUSH ON ECHO: ITS OPTIONS", [](juce::Graphics& g) {
            rail(g, "OPT", "1", "ECHO");
            list(g, content(0, height), {"BYPASS       OFF", "MOVE LEFT", "MOVE RIGHT", "COPY", "PASTE", "REMOVE"}, 0, 1);
        }},
        {"fx-key", "COMPRESSOR, KNOB 7 ITS KEY: TRACK 1", [](juce::Graphics& g) {
            rail(g, "OBXD", "2", "FX1");
            mix::item_row(g, {{"OBXD"}, {"012"}, {"COMP"}, {"+"}}, 2);
            cells(g, "COMPRESSOR", "KEY 1", {"THRESH", "RATIO", "MAKEUP", "ATTACK", "RELEASE", "MIX", "KEY", ""},
                  {0.55f, 0.2f, 0.25f, 0.2f, 0.45f, 1, 0.1f, 0});
        }},
        {"fx-module", "A SCHWUNG FX: ITS PAGES ON SHIFT + TURN", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "FX2");
            mix::item_row(g, {{"KIT"}, {"A05"}, {"ECHO"}, {"CLDS"}, {"+"}}, 3);
            cells(g, "CLOUDSEED", "1/3", {"MIX", "SIZE", "DECAY", "PREDLY", "DIFFUSE", "MOD", "LOCUT", "HICUT"},
                  {0.35f, 0.7f, 0.6f, 0.1f, 0.8f, 0.3f, 0.2f, 0.7f});
        }},
        // The Mixer, Shift + Step 4.
        {"mixer-vol", "SHIFT + STEP 4: THE MIXER, LEVELS", [](juce::Graphics& g) {
            rail(g, "MIX", "5", "1-8");
            mix::item_row(g, hub(), 0);
            mix::strips(g, content(10, 44), track_strips(0, 0), int(selected));
            detail(g, "BREAKS", "-1.4DB");
        }},
        {"mixer-pan", "THE WHEEL: PANS. LEFT/RIGHT: 9-16", [](juce::Graphics& g) {
            rail(g, "MIX", "5", "1-8");
            mix::item_row(g, hub(), 1);
            mix::strips(g, content(10, 44), track_strips(0, 1), int(selected));
            detail(g, "BREAKS", "C");
        }},
        {"mixer-send", "SEND A: EVERY TRACK INTO BUS A, ITS RETURN", [](juce::Graphics& g) {
            rail(g, "MIX", "5", "1-8");
            mix::item_row(g, hub(), 2);
            auto list = track_strips(0, 2);
            Strip back;
            back.label = "A";
            back.value = 0.7f;
            back.meter = 0.35f;
            list.push_back(back);
            mix::strips(g, content(10, 44), list, int(selected));
            detail(g, "RTN 70", "PUSH: FX");
        }},
        {"mixer-main", "MAIN: THE RETURNS, COMPRESSOR, OUT", [](juce::Graphics& g) {
            rail(g, "MIX", "", "MAIN");
            mix::item_row(g, hub(), 6);
            std::vector<Strip> list;
            const float returns[4] {0.7f, 0.55f, 0.7f, 0.7f}, heard[4] {0.35f, 0.2f, 0, 0};
            for (int b = 0; b < 4; ++b) {
                Strip s;
                s.label = std::string(1, char('A' + b));
                s.value = returns[b];
                s.meter = heard[b];
                list.push_back(s);
            }
            mix::strips(g, {content_x, 10, 60, 44}, list, -1);
            move::draw_text(g, 88, 47, "GR");
            g.drawRect(89, 18, 7, 27);
            g.fillRect(89, 18, 7, 9); // what the compressor takes off, from the top
            g.fillRect(105, 21, 5, 24); // the main out, left and right
            g.fillRect(112, 24, 5, 21);
            move::draw_text(g, 105, 47, "L R");
            detail(g, "-4.5DB", "PUSH: FX");
        }},
        {"mixer-touch", "A KNOB TOUCHED: THAT TRACK, LARGE", [](juce::Graphics& g) {
            rail(g, "KNOB", "3", "VOL");
            big_parameter(g, content(4, 44), "3 GRAND LEVEL", "-4.2DB", 76 / 127.0f);
            move::draw_text_centred(g, content_x, detail_y, content_width, "SHIFT: FINER");
        }},
        {"bus-chain", "PUSH ON SEND A: BUS A, ITS CHAIN", [](juce::Graphics& g) {
            rail(g, "BUS", "A", "RTN");
            mix::item_row(g, {{"A"}, {"ROOM"}, {"ECHO"}, {"+"}}, 0);
            big_parameter(g, content(12, 34), "RETURN", "70", 0.7f);
            detail(g, "FROM 1 2 3 5 6 7");
        }},
        {"main-chain", "PUSH ON MAIN: SIX EFFECTS, THEN COMP", [](juce::Graphics& g) {
            rail(g, "MAIN", "", "COMP");
            mix::item_row(g, {{"DRIV"}, {"EQ"}, {"+"}, {"COMP"}}, 3);
            mix::reduction(g, content(11, 40), pump(content_width), 4, 70);
            detail(g, "KEY 1", "-4.5DB");
        }},
        // The strip of the track in hand, after MAIN on the wheel; a step picks the track.
        {"mixer-eq", "EQ: TRACK 5'S NINE BANDS, KNOBS 1-8", [](juce::Graphics& g) {
            rail(g, "MIX", "5", "EQ");
            mix::item_row(g, hub(), 7);
            mix::eq_curve(g, content(11, 41), {4, 6, 2, -5, -3, 0, 2, 0, -4}, true, 1);
            detail(g, "125 HZ", "+6.0DB");
        }},
        {"mixer-comp", "COMP: ITS CURVE AND WHAT IT TAKES OFF", [](juce::Graphics& g) {
            rail(g, "MIX", "5", "COMP");
            mix::item_row(g, hub(), 8);
            auto taken = pump(64);
            for (auto& t : taken) t *= 6; // dB
            mix::comp_picture(g, content(12, 38), 1 - 18 / 48.0f, 4, true, taken);
            detail(g, "BREAKS", "-4.5DB");
        }},
        {"mixer-subs", "RIGHT PAST 9-16: THE SUBMIXES", [](juce::Graphics& g) {
            rail(g, "MIX", "S2", "S1-8");
            mix::item_row(g, hub(), 0);
            mix::strips(g, content(10, 44), sub_strips(), 1);
            detail(g, "SUBMIX 2", "-2.8DB");
        }},
        {"sub-chain", "PUSH: THE SUBMIX'S CHAIN, LEVEL FIRST", [](juce::Graphics& g) {
            rail(g, "SUB", "2", "LVL");
            mix::item_row(g, {{"S2"}, {"DRIV"}, {"+"}}, 0);
            big_parameter(g, content(12, 34), "LEVEL", "-2.8DB", 0.7f);
            detail(g, "FROM 2 7");
        }},
        // Melodee's performance effects: Shift + Step 4 again, and again, and again.
        {"knob-fx", "SHIFT + STEP 4 AGAIN: KNOB FX", [](juce::Graphics& g) {
            rail(g, "KFX", "A", "ON");
            move::draw_text(g, content_x + 1, 1, "LP FILTER");
            move::draw_text(g, width - move::text_width("EDIT A") - 1, 1, "EDIT A");
            g.fillRect(content_x, 9, content_width, 1);
            knob_cells(g, content(12, height - 12), {"CUTOFF", "RESO", "DRIVE", "", "", "", "A/B", "A>B"}, {0.3f, 0.6f, 0.2f, 0, 0, 0, 0, 0.45f});
        }},
        {"pad-fx", "AGAIN: PAD FX, PRESSURE IS DEPTH", [](juce::Graphics& g) {
            rail(g, "PFX", "1", "MST");
            move::draw_text(g, content_x + 1, 1, "LOOP 8 + PHONE");
            mix::punch_lanes(g, content(12, 40), {{4, 7, true}, {9, 13, false}, {12, 15, false}}, 12);
            detail(g, "Q1/16", "PUSH: KEEP");
        }},
        {"flex", "AGAIN: FLEX BEAT, A PAD PICKS", [](juce::Graphics& g) {
            rail(g, "FLEX", "7", "");
            move::draw_text_centred(g, content_x, 15, content_width, "BACKSPIN", 2);
            move::draw_text_centred(g, content_x, 34, content_width, "ONE SHOT  MIX 90");
            detail(g, "Q ON", "PADS 1-16");
        }},
        {"meter", "VOLUME TOUCHED: THE MAIN METER", [](juce::Graphics& g) {
            rail(g, "OUT", "", "");
            move::draw_text_centred(g, content_x, 2, content_width, "-6.2DB", 2);
            const int marks[] {-24, -12, -6, -3, 0};
            for (int side = 0; side < 2; ++side) {
                int y = 22 + side * 9, w = content_width - 4;
                g.drawRect(content_x + 2, y, w, 7);
                g.fillRect(content_x + 3, y + 1, (w - 2) * (side ? 68 : 72) / 100, 5);
            }
            for (int db : marks) {
                int x = content_x + 2 + (content_width - 5) * (db + 30) / 30;
                g.fillRect(x, 40, 1, 2);
            }
            move::draw_text(g, content_x + 1, 44, "-24  -12 -6 -3 0");
            detail(g, "VOLUME 80");
        }},
    };
}

// ---------------------------------------------------------------- lights

// The Move's lights, as mockups_controls.cpp draws them, from Ableton's
// controls figure (the manual's MoveControls.png).
struct Panel {
    std::array<uint8_t, 4> tracks {};
    std::array<uint8_t, move::pad_count> pads {};
    std::array<bool, move::pad_count> pulsing {};
    std::array<uint8_t, move::step_count> steps {};
    std::array<uint8_t, size_t(Button::Count)> buttons {};
    void button(Button b, uint8_t value) { buttons[size_t(b)] = value; }
};
struct Lights { const char* name; const char* caption; Panel panel; };

uint8_t dim_white() { return move::nearest_colour({96, 96, 96}); }

Panel base_panel() {
    Panel p;
    for (unsigned n = 0; n < 4; ++n) {
        unsigned t = 4 + n; // the bank of track 5
        p.tracks[n] = !example[t].tag[0] ? 0 : t == selected ? track_colour(t).bright : track_colour(t).dim;
    }
    for (unsigned n = 0; n < 9; ++n) p.pads[n] = track_colour(selected).bright;
    p.pads[4] = move::colour::white;
    for (auto b : {Button::Back, Button::Mute, Button::Undo, Button::Left, Button::Right}) p.button(b, move::white_dim);
    p.button(Button::Shift, move::white_dim);
    p.button(Button::Play, move::colour::green);
    return p;
}
// The sixteen tracks on the steps: each its colour, the selected one white,
// a muted one grey, a soloed one blue, an empty one dark.
void track_steps(Panel& p) {
    for (unsigned t = 0; t < 16; ++t)
        p.steps[t] = !example[t].tag[0] ? 0 : t == selected ? move::colour::white : example[t].muted ? move::colour::dark_grey
                   : example[t].soloed ? move::colour::blue : track_colour(t).bright;
}

std::vector<Lights> mixer_lights() {
    std::vector<Lights> panels;
    {
        Panel p = base_panel();
        track_steps(p);
        panels.push_back({"lights-mixer", "MIXER: STEPS THE 16 TRACKS, 6 MUTED", p});
    }
    {
        Panel p = base_panel();
        track_steps(p);
        p.steps[2] = move::colour::blue;
        p.button(Button::Mute, move::white_bright);
        p.button(Button::Shift, move::white_bright);
        panels.push_back({"lights-mixer-solo", "SHIFT + MUTE + STEP 3: SOLO, BLUE", p});
    }
    {
        Panel p = base_panel();
        for (unsigned t = 0; t < 16; ++t)
            p.steps[t] = !example[t].tag[0] ? 0 : (t == 0 || t == 4) ? track_colour(t).bright : track_colour(t).dim;
        p.pads = {};
        for (unsigned n = 0; n < 9; ++n) p.pads[n] = n < 4 ? move::colour::white : track_colour(selected).dim;
        panels.push_back({"lights-knob-fx", "KNOB FX: ON TRACKS 1, 5; PADS A01-A04", p});
    }
    {
        Panel p = base_panel();
        p.pads = {};
        for (unsigned pad = 0; pad < 32; ++pad) p.pads[pad] = pad < 16 ? move::colour::orange : move::colour::cyan;
        p.pads[1] = move::colour::white;  // held: Loop 8
        p.pads[10] = move::colour::white; // held: Phone
        p.pulsing[10] = true;             // latched
        for (unsigned s = 4; s < 7; ++s) p.steps[s] = move::colour::orange; // a kept punch's sixteenths
        p.steps[12] = move::colour::green;                                  // the playhead
        p.button(Button::Up, move::white_dim);
        p.button(Button::Down, move::white_dim);
        panels.push_back({"lights-pad-fx", "PAD FX: 32 PUNCHES, 2 HELD, KEPT ON STEPS", p});
    }
    {
        Panel p = base_panel();
        p.pads = {};
        for (unsigned pad = 0; pad < 16; ++pad) p.pads[pad] = pad == 0 ? move::colour::dark_grey : move::colour::violet;
        p.pads[6] = move::colour::white;
        panels.push_back({"lights-flex", "FLEX BEAT: PAD 1 EMPTY, 7 PLAYING", p});
    }
    {
        Panel p = base_panel();
        p.pads[2] = dim_white();
        p.button(Button::Mute, move::white_bright);
        panels.push_back({"lights-mute-pad", "MUTE HELD + A03: A03 MUTED, DIM WHITE", p});
    }
    return panels;
}

juce::Image panel_image(const Panel& p, int w, int h) {
    juce::Image image(juce::Image::RGB, w, h, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    float sx = float(w) / 907.0f, sy = float(h) / 423.0f; // the figure's body, 907 x 423
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
    round(867, 185, 20, lit(Button::Mute), "MU");
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
    constexpr int scale = 4, gap = 24, caption = 30, top = 92;
    int screen_w = width * scale, screen_h = height * scale;
    int rows = int(screens.size() + 1) / 2, panel_rows = int(panels.size() + 1) / 2;
    int panels_top = top + rows * (screen_h + caption + gap);
    juce::Image image(juce::Image::RGB, gap + 2 * (screen_w + gap), panels_top + panel_rows * (screen_h + caption + gap), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    g.setColour(juce::Colour(0xffd8f0ff));
    move::draw_text(g, gap, 20, "MELODEE ON MOVE: MIXER AND EFFECTS", 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, "EFFECTS IN THE DEVICE VIEW; SHIFT + STEP 4 THE MIXER, EQ, COMP, SUBMIXES. DOCS/UI.MD", 2);
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

bool write_mixer_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = mixer_screens();
    auto panels = mixer_lights();
    auto each = folder.getChildFile("mixer");
    each.createDirectory();
    bool ok = true;
    for (const auto& screen : screens)
        ok = mockup::write_png(mockup::screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = mockup::write_png(panel_image(panel.panel, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    auto file = folder.getChildFile("mixer.png");
    ok = mockup::write_png(sheet(screens, panels), file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace ui
