// SPDX-License-Identifier: AGPL-3.0-only
// Melodee's sampling on the Move, as mockups: sampling.png. The Move's
// Sampling button is Melodee's Sample Record (a pad records onto itself, the
// pad again or Sampling stops, held it records until let go, another pad
// carries the take on onto that pad), the wheel walks the input's rows as
// the Move's walks gain, monitoring and source, Shift + Sampling is Audio
// Recall (Kerem, 2026-11-20). Then what goes with it: the drum sources on
// the new-track sheet, Trim and Save from Sample Options, the browser's
// starred and recent files, its preview and Delete, an MPC expansion
// installed, Chop's manual take, close up, second bank and options, To Pads
// KIT, and an audio track's input and takes. Built on the approved sheets:
// controls.png (Sampling, Audio Recall) and rail.png (Record armed, taking).
#include "ui/mockups.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/lights.h"
#include "ui/widgets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace ui {
namespace {

using move::Button;
using mockup::Draw;
using mockup::Screen;
const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// ---------------------------------------------------------------- example data

// A sung phrase taken on the line input: long swells.
std::vector<float> phrase(int columns, float until = 1.0f) {
    std::vector<float> peaks(size_t(columns), 0.02f);
    struct Swell { float at, level, decay; };
    for (const Swell& s : {Swell {.02f, .6f, .22f}, Swell {.24f, .8f, .19f}, Swell {.46f, .5f, .12f}, Swell {.58f, .7f, .22f}, Swell {.84f, .55f, .1f}})
        for (int column = int(s.at * float(columns)); column < columns; ++column)
            peaks[size_t(column)] += s.level * std::exp(-(float(column) / float(columns) - s.at) / s.decay);
    for (int column = 0; column < columns; ++column) {
        auto& peak = peaks[size_t(column)];
        peak = float(column) >= until * float(columns) ? 0.0f : std::min(peak, 1.0f);
    }
    return peaks;
}
// A two-bar break, as rail.png's A05.
std::vector<float> breakbeat(int columns) {
    struct Hit { float at, level, decay; };
    static const Hit hits[] {{0, .95f, .08f}, {.5f, .95f, .08f}, {.25f, .75f, .055f}, {.75f, .75f, .055f}, {.44f, .45f, .025f},
                             {.69f, .45f, .025f}, {.94f, .5f, .03f}, {.06f, .35f, .015f}, {.125f, .35f, .015f}, {.19f, .35f, .015f},
                             {.31f, .35f, .015f}, {.375f, .35f, .015f}, {.56f, .35f, .015f}, {.625f, .35f, .015f}, {.81f, .35f, .015f},
                             {.875f, .35f, .015f}};
    std::vector<float> peaks(size_t(columns), 0.04f);
    for (int column = 0; column < columns; ++column)
        for (const auto& hit : hits) {
            float x = float(column) / float(columns);
            if (x >= hit.at) peaks[size_t(column)] += hit.level * std::exp(-(x - hit.at) / hit.decay);
        }
    for (auto& peak : peaks) peak = std::min(peak, 1.0f);
    return peaks;
}

// The record light on the rail, as rail.png's Record.
void record_light(juce::Graphics& g) {
    g.setColour(off);
    dot(g, 12, 30, 5);
    g.setColour(on);
}
// A row of a settings list: the name left, the value right, 16 characters.
std::string row(const char* name, const char* value) {
    char text[24];
    std::snprintf(text, sizeof text, "%-*s%s", 16 - int(std::strlen(value)), name, value);
    return text;
}
// The input's level, left over right, thin, along the subject's foot; the
// threshold a dotted tick.
void level(juce::Graphics& g, int y, float left, float right, float threshold = -1) { meter(g, {content_x + 1, y, content_width - 4, 10}, left, right, threshold); }
// Small marks under a waveform: the hits a cut can jump to.
void hit_marks(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<int>& columns) {
    for (int column : columns) g.fillRect(area.getX() + column, area.getBottom() + 1, 1, 2);
}
// A chip: a pad's name inverted, over a waveform.
void chip(juce::Graphics& g, int x, int y, const char* text) {
    int w = move::text_width(text) + 2;
    g.setColour(on);
    g.fillRect(x, y, w, 9);
    g.setColour(off);
    move::draw_text(g, x + 1, y + 1, text);
    g.setColour(on);
}

// ---------------------------------------------------------------- screens

const std::vector<std::string> input_rows {row("SOURCE", "LINE"), row("GAIN", "+6 DB"), row("MONITOR", "AUTO"), row("START", "AT ONCE"),
                                           row("LENGTH", "FREE")};

std::vector<Screen> sampling_screens() {
    return {
        {"sampling", "SAMPLING: PRESS A PAD", [](juce::Graphics& g) {
            rail(g, "SMPL", "", "LINE");
            record_light(g);
            move::draw_text_centred(g, content_x, 2, content_width, "PRESS", 2);
            move::draw_text_centred(g, content_x, 20, content_width, "A PAD", 2);
            level(g, 38, 0.55f, 0.48f);
            detail(g, "LINE +6 DB", "FREE");
        }},
        {"sampling-input", "THE WHEEL: A ROW, PUSH: ITS NEXT VALUE", [](juce::Graphics& g) {
            rail(g, "SMPL", "", "LINE");
            record_light(g);
            list(g, content(0, 36), input_rows, 0, 1);
            level(g, 40, 0.55f, 0.48f);
            detail(g, "", "");
        }},
        {"sampling-gain", "GAIN: PUSH, THEN THE WHEEL; KNOB 2 TOO", [](juce::Graphics& g) {
            rail(g, "SMPL", "", "LINE");
            record_light(g);
            auto rows = input_rows;
            rows[1] = row("GAIN", "[+6 DB]");
            list(g, content(0, 36), rows, 1, 1);
            level(g, 40, 0.55f, 0.48f);
        }},
        {"sampling-sources", "SOURCES: LINE OR MIC BY THE JACK", [](juce::Graphics& g) {
            rail(g, "SMPL", "", "MIC");
            record_light(g);
            list(g, content(0, 36), {row("MIC", "NO CABLE"), row("LINE", ""), row("LINE L", ""), row("LINE R", ""), row("RESAMPLE", ""),
                                      row("TRACK 2", "OB-XD")}, 0, 1);
            level(g, 40, 0.3f, 0.3f);
        }},
        {"sampling-armed", "A PAD, START AT -24 DB: WAITING", [](juce::Graphics& g) {
            rail(g, "REC", "", "MIC");
            record_light(g);
            level(g, 6, 0.12f, 0.1f, 0.42f);
            move::draw_text(g, content_x + 2, 26, "4 BARS ONTO A06");
            move::draw_text(g, content_x + 2, 38, "THE PAD: AT ONCE");
            detail(g, "WAITS -24 DB");
        }},
        {"sampling-taking", "TAKING: THE PAD AGAIN OR SAMPLING STOPS", [](juce::Graphics& g) {
            rail(g, "REC", "", "A06");
            record_light(g);
            move::draw_text(g, content_x + 2, 1, "0:03.2", 2);
            waveform(g, content(20, 30), phrase(content_width, 0.45f));
            detail(g, "4 BARS", "BAR 2");
        }},
        {"sampling-cut", "ANOTHER PAD: THE TAKE GOES ON ONTO A07", [](juce::Graphics& g) {
            rail(g, "REC", "", "A07");
            record_light(g);
            move::draw_text(g, content_x + 2, 1, "0:05.8", 2);
            auto area = content(20, 30);
            waveform(g, area, phrase(content_width, 0.72f));
            cuts(g, area, {44}, 0);
            chip(g, content_x + 1, 21, "A06");
            chip(g, content_x + 46, 21, "A07");
            detail(g, "FREE", "A06 > A07");
        }},
        {"sampling-done", "DONE: TAKE 3 ON A06 AND A07", [](juce::Graphics& g) {
            rail(g, "SMPL", "", "LINE");
            record_light(g);
            auto area = content(0, 36);
            waveform(g, area, phrase(content_width, 0.72f));
            cuts(g, area, {44}, -1);
            level(g, 40, 0.05f, 0.05f);
            detail(g, "TAKE 3", "7.2S");
        }},
        {"sampling-track", "TRACK 2 AS SOURCE: THE PADS PLAY IT", [](juce::Graphics& g) {
            rail(g, "REC", "", "TRK2");
            record_light(g);
            move::draw_text_centred(g, content_x, 2, content_width, "PLAY", 2);
            move::draw_text_centred(g, content_x, 20, content_width, "OB-XD", 2);
            level(g, 38, 0.0f, 0.0f);
            detail(g, "ONTO A06", "1ST NOTE");
        }},
        {"sampling-resample", "RESAMPLE, LENGTH SEQ: A PAD PLAYS ONE PASS", [](juce::Graphics& g) {
            rail(g, "SMPL", "", "RSMP");
            record_light(g);
            move::draw_text_centred(g, content_x, 2, content_width, "PRESS", 2);
            move::draw_text_centred(g, content_x, 20, content_width, "A PAD", 2);
            level(g, 38, 0.0f, 0.0f);
            detail(g, "PATTERN 1", "2 BARS");
        }},
        {"sampling-settings", "SHIFT + TURN: SAMPLING SETTINGS", [](juce::Graphics& g) {
            rail(g, "SMPL", "2/2", "SET");
            list(g, content(0, height), {row("RECALL", "PHRASE"), row("RECALL GAP", "1 S"), row("TRIM END", "ON"), row("AUTO END", "SYNTHS"),
                                         row("END AFTER", "1.5 S"), row("SILENCE", "-60 DB")}, 0, 1);
        }},
        {"audio-recall", "SHIFT + SAMPLING: AUDIO RECALL", [](juce::Graphics& g) {
            rail(g, "RCL", "25", "SEC");
            waveform(g, content(0, subject_height), phrase(content_width));
            detail(g, "NEXT EMPTY: A07");
        }},
        {"new-track-kit", "NEW TRACK, KIT: WHERE ITS FIRST SOUND IS", [](juce::Graphics& g) {
            rail(g, "NEW", "3", "KIT");
            list(g, content(0, 50), {"LIBRARY >", "EMPTY", "INPUT >"}, 2, 1);
            detail(g, "PUSH: MAKE");
        }},
        {"sample-options", "SAMPLE OPTIONS: TRIM, SAVE SAMPLE", [](juce::Graphics& g) {
            rail(g, "OPT", "A05", "SMP");
            list(g, content(0, height), {"CHOP >", "REVERSE", "TRIM", "NORMALIZE", "SAVE SAMPLE >"}, 2, 1);
        }},
        {"save-sample", "SAVE SAMPLE: ITS NAME ON THE PADS", [](juce::Graphics& g) {
            move::draw_text(g, 1, 1, "SAVE SAMPLE AS");
            g.fillRect(0, 10, width, 1);
            move::draw_text(g, 1, 24, "BREAK 94 CHOP_");
            move::draw_text(g, 1, 46, "PADS: LETTERS");
            move::draw_text(g, 1, 56, "PUSH: OK  BACK: DEL");
        }},
        {"load-places", "BROWSER: STARRED AND RECENT FIRST", [](juce::Graphics& g) {
            rail(g, "LOAD", "A05", "FROM");
            list(g, content(0, height), {row("STARRED", "3 >"), row("RECENT", "16 >"), "MY SAMPLES >", "RECORDINGS >", "MELODEE >",
                                         "CORE LIBRARY >"}, 0, 1);
        }},
        {"load-preview", "EACH FILE PLAYS AS THE WHEEL LANDS", [](juce::Graphics& g) {
            rail(g, "LOAD", "A05", "SMP");
            list(g, content(0, 50), {"AMEN 136.WAV", "*BREAK 94.WAV", "CLAP 2.WAV", "HAT OPEN.WAV", "KICK 808.WAV"}, 1, 1);
            g.fillRect(content_x + 1, 51, 60, 2); // the preview's head through the file
            detail(g, "PREVIEW", "1.2/2.0S");
        }},
        {"delete-file", "DELETE IN THE BROWSER: ASKS FIRST", [](juce::Graphics& g) {
            rail(g, "DEL", "", "FILE");
            move::draw_text_centred(g, content_x, 4, content_width, "DELETE", 2);
            move::draw_text_centred(g, content_x, 26, content_width, "TAKE 2.WAV");
            move::draw_text_centred(g, content_x, 36, content_width, "FROM RECORDED");
            detail(g, "PUSH YES", "BACK NO");
        }},
        {"delete-refused", "WHAT THE PROJECT PLAYS IS KEPT", [](juce::Graphics& g) {
            rail(g, "DEL", "", "FILE");
            move::draw_text_centred(g, content_x, 4, content_width, "IN USE", 2);
            move::draw_text_centred(g, content_x, 26, content_width, "BREAK 94.WAV");
            move::draw_text_centred(g, content_x, 36, content_width, "PLAYS ON A05");
            detail(g, "NOT DELETED");
        }},
        {"expansion", "PUSH ON AN .XPN: INSTALLED, THEN OPENED", [](juce::Graphics& g) {
            rail(g, "XPN", "42", "%");
            move::draw_text(g, content_x + 2, 2, "INSTALLING");
            move::draw_text(g, content_x + 2, 14, "VINTAGE DRUMS");
            g.drawRect(content_x + 2, 30, content_width - 6, 6);
            g.fillRect(content_x + 3, 31, (content_width - 8) * 42 / 100, 4);
            detail(g, "FILE 126/300");
        }},
        {"chop-manual", "CHOP MANUAL: TAP THE RED PAD TO CUT", [](juce::Graphics& g) {
            rail(g, "TAKE", "05", "TAP");
            auto area = content(0, subject_height);
            waveform(g, area, breakbeat(content_width));
            cuts(g, area, {13, 25, 38, 51}, 3);
            playhead(g, area, 60);
            detail(g, "NEXT: A06", "MANUAL");
        }},
        {"chop-close", "KNOB 1 TURNING: CLOSE UP ON THE CUT", [](juce::Graphics& g) {
            rail(g, "SLC", "07", "STRT");
            auto area = content(0, subject_height - 4);
            auto peaks = breakbeat(content_width * 8);
            std::vector<float> zoomed(peaks.begin() + 300, peaks.begin() + 300 + content_width);
            waveform(g, area, zoomed);
            cuts(g, area, {50}, 0);
            hit_marks(g, area, {22, 50, 81});
            detail(g, "+0.38S", "X8");
        }},
        {"chop-bank", "PLUS: SLICES 33-64 ON THE PADS", [](juce::Graphics& g) {
            rail(g, "SLC", "35", "/40");
            auto area = content(0, subject_height);
            waveform(g, area, breakbeat(content_width), 34 * content_width / 40, 35 * content_width / 40 - 1);
            std::vector<int> columns;
            for (int slice = 1; slice < 40; ++slice) columns.push_back(slice * content_width / 40);
            cuts(g, area, columns, 33);
            detail(g, "PADS 33-40", "AUTO");
        }},
        {"chop-options", "SHIFT + PUSH IN CHOP: SLICE OPTIONS", [](juce::Graphics& g) {
            rail(g, "OPT", "07", "SLC");
            list(g, content(0, height), {"EXTRACT", "SPLIT", "MERGE", "REMOVE ALL CUTS"}, 0, 1);
        }},
        {"to-pads-kit", "TO PADS: KIT, A PAD A SOUND", [](juce::Graphics& g) {
            rail(g, "SLC", "12", "PADS");
            move::draw_text(g, content_x + 1, 1, "12 SLICES TO PADS");
            list(g, content(11, 36), {"POLY", "MONO", "THRU", "KIT", "PATTERN OFF"}, 3, 1);
            detail(g, "FROM A10", "PUSH");
        }},
        {"to-pads-pattern", "TO PADS: PATTERN, A NOTE A SLICE TOO", [](juce::Graphics& g) {
            rail(g, "SLC", "16", "PADS");
            move::draw_text(g, content_x + 1, 1, "16 SLICES TO PADS");
            list(g, content(11, 36), {"POLY", "MONO", "THRU", "KIT", "PATTERN ON"}, 4, 1);
            detail(g, "A NOTE A SLICE", "FLIP");
        }},
        {"audio-input", "AUDIO TRACK: SAMPLING OPENS ITS INPUT", [](juce::Graphics& g) {
            rail(g, "AUD", "3", "IN");
            list(g, content(0, 36), {row("SOURCE", "LINE"), row("MONITOR", "ON"), row("GAIN", "+0 DB")}, 0, 1);
            level(g, 38, 0.4f, 0.36f);
            detail(g, "REC: ARM", "PAT 1");
        }},
        {"audio-armed", "REC ARMS: PLAY RECORDS ONE PASS", [](juce::Graphics& g) {
            rail(g, "AUD", "3", "ARM");
            move::draw_text_centred(g, content_x, 2, content_width, "ARMED", 2);
            move::draw_text_centred(g, content_x, 22, content_width, "PATTERN 1, 4 BARS");
            level(g, 36, 0.4f, 0.36f);
            detail(g, "PLAY: RECORD");
        }},
        {"audio-taking", "RECORDING PATTERN 1, BAR 2 OF 4", [](juce::Graphics& g) {
            rail(g, "REC", "", "TRK3");
            record_light(g);
            auto area = content(10, 40);
            g.drawRect(area);
            for (int bar = 1; bar < 4; ++bar) g.fillRect(area.getX() + bar * area.getWidth() / 4, area.getY(), 1, 3);
            waveform(g, area.reduced(1), phrase(area.getWidth() - 2, 0.4f));
            for (int bar = 0; bar < 4; ++bar) move::draw_text(g, area.getX() + 2 + bar * area.getWidth() / 4, 1, std::to_string(bar + 1));
            detail(g, "PAT 1", "BAR 2/4");
        }},
        {"audio-clip", "AUDIO TRACK: CLIP, KNOBS 1-6", [](juce::Graphics& g) {
            rail(g, "AUD", "3", "1/2");
            move::draw_text(g, content_x + 2, 1, "CLIP", 2);
            knob_cells(g, content(18, 34), {"LVL", "STA", "END", "IN", "OUT", "REV", "", ""},
                       {0.8f, 0.0f, 1.0f, 0.1f, 0.25f, 0.0f, 0, 0});
            detail(g, "TAKE 4", "4 BARS");
        }},
        {"audio-warp", "SHIFT + TURN: WARP, KNOBS 1-4", [](juce::Graphics& g) {
            rail(g, "AUD", "3", "2/2");
            move::draw_text(g, content_x + 2, 1, "WARP", 2);
            knob_cells(g, content(18, 34), {"WRP", "BPM", "ST", "CT", "", "", "", ""}, {0.5f, 0.45f, 0.5f, 0.5f, 0, 0, 0, 0});
            detail(g, "STRETCH", "94.0");
        }},
    };
}

// ---------------------------------------------------------------- lights

// The Move's lights as Ableton's controls figure lays it out (as
// controls.png draws them): the buttons, the track buttons, the pads, the
// steps; a ring round a pad or button that pulses or blinks.
struct Panel {
    std::array<uint8_t, 4> tracks {};
    std::array<uint8_t, move::pad_count> pads {};
    std::array<bool, move::pad_count> pulsing {};
    std::array<uint8_t, move::step_count> steps {};
    std::array<uint8_t, size_t(Button::Count)> buttons {};
    std::array<bool, size_t(Button::Count)> button_pulsing {};
    void button(Button b, uint8_t value, bool pulse = false) { buttons[size_t(b)] = value; button_pulsing[size_t(b)] = pulse; }
};
struct Lights { const char* name; const char* caption; Panel panel; };

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
    auto round = [&](Button b, float cx, float cy, float r, const char* name) {
        bool coloured = move::buttons[size_t(b)].led == move::LedKind::Rgb;
        auto fill = coloured ? rgb(p.buttons[size_t(b)]) : white(p.buttons[size_t(b)]);
        g.setColour(fill);
        g.fillEllipse(X(cx) - X(r), Y(cy) - X(r), 2 * X(r), 2 * X(r));
        if (p.button_pulsing[size_t(b)]) {
            g.setColour(juce::Colours::white);
            g.drawEllipse(X(cx) - X(r) - 2.5f, Y(cy) - X(r) - 2.5f, 2 * X(r) + 5, 2 * X(r) + 5, 2);
        }
        g.setColour(fill.getPerceivedBrightness() > 0.45f ? juce::Colours::black : juce::Colour(0xff8a96a3));
        move::draw_text(g, int(X(cx)) - move::text_width(name) / 2, int(Y(cy)) - 3, name);
    };
    round(Button::Back, 42, 243, 19, "<");
    round(Button::Menu, 95, 243, 19, "NS");
    round(Button::Play, 43, 371, 20, "PL");
    round(Button::Rec, 95, 371, 20, "RE");
    round(Button::Capture, 815, 126, 20, "CA");
    round(Button::Sample, 867, 126, 20, "SA");
    round(Button::Loop, 815, 185, 20, "LP");
    round(Button::Mute, 867, 185, 20, "M");
    round(Button::Delete, 815, 243, 20, "X");
    round(Button::Copy, 867, 243, 20, "CP");
    round(Button::Undo, 815, 301, 20, "UN");
    round(Button::Shift, 867, 301, 20, "SH");
    round(Button::Left, 806, 370, 13, "<");
    round(Button::Right, 873, 370, 13, ">");
    round(Button::Up, 840, 352, 13, "+");
    round(Button::Down, 840, 388, 13, "-");
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

uint8_t pink() { return move::nearest_colour({255, 120, 190}); }

// The example kit: track 1, A01-A05 hold samples, A05 the last pad.
Panel kit() {
    Panel p;
    p.tracks = {move::colour::azure, move::colour::deep_magenta, move::colour::orange, move::colour::dark_grey};
    for (unsigned n = 0; n < 5; ++n) p.pads[n] = move::colour::azure;
    p.pads[4] = move::colour::white;
    p.button(Button::Shift, move::white_dim);
    p.button(Button::Play, move::colour::dark_grey);
    p.button(Button::Rec, move::colour::dark_grey);
    p.button(Button::Sample, move::colour::dark_grey);
    return p;
}

std::vector<Lights> sampling_lights() {
    std::vector<Lights> panels;
    {   // Sampling: every pad can take a sample, the empty ones pink; the used ones keep their colour (recorded over).
        Panel p = kit();
        for (unsigned n = 5; n < move::pad_count; ++n) p.pads[n] = pink();
        p.button(Button::Sample, move::colour::red, true);
        p.button(Button::Back, move::white_dim);
        panels.push_back({"lights-sampling", "SAMPLING: EMPTY PADS PINK, IT PULSES RED", p});
    }
    {   // Taking onto A06: it pulses red; any other pad carries the take on onto itself.
        Panel p = kit();
        for (unsigned n = 6; n < move::pad_count; ++n) p.pads[n] = pink();
        p.pads[5] = move::colour::red;
        p.pulsing[5] = true;
        p.button(Button::Sample, move::colour::red, true);
        p.button(Button::Back, move::white_dim);
        panels.push_back({"lights-taking", "TAKING ONTO A06: PULSING RED", p});
    }
    {   // Carried on onto A07: A06 holds its part, solid red, A07 pulses.
        Panel p = kit();
        for (unsigned n = 7; n < move::pad_count; ++n) p.pads[n] = pink();
        p.pads[5] = move::colour::red;
        p.pads[6] = move::colour::red;
        p.pulsing[6] = true;
        p.button(Button::Sample, move::colour::red, true);
        panels.push_back({"lights-cut", "A07 PRESSED: A06 KEPT, A07 TAKING", p});
    }
    {   // A track as the source: the pads play OB-XD (track 2's notes); the take's pad is on the screen.
        Panel p = kit();
        p.pads = {};
        for (unsigned pad = 0; pad < move::pad_count; ++pad) {
            int note = 48 + int(move::pad_column(pad)) + 5 * int(move::pad_row(pad));
            int in_octave = note % 12;
            bool natural = in_octave != 1 && in_octave != 3 && in_octave != 6 && in_octave != 8 && in_octave != 10;
            p.pads[pad] = in_octave == 0 ? move::colour::deep_magenta : natural ? move::colour::dark_grey : move::colour::off;
        }
        p.button(Button::Sample, move::colour::red, true);
        panels.push_back({"lights-track-source", "TRACK 2 AS SOURCE: PADS ARE ITS NOTES", p});
    }
    {   // Chop Manual: five cuts made, A06 blinks red: it cuts next.
        Panel p = kit();
        p.pads = {};
        for (unsigned n = 0; n < 5; ++n) p.pads[n] = n % 2 ? move::colour::brown_yellow : move::colour::orange;
        p.pads[5] = move::colour::red;
        p.pulsing[5] = true;
        p.button(Button::Back, move::white_dim);
        panels.push_back({"lights-chop-manual", "CHOP MANUAL: A06 RED, CUTS NEXT", p});
    }
    {   // Chop, the second bank: slices 33-48 on the pads, 35 selected; Plus and Minus lit.
        Panel p = kit();
        p.pads = {};
        for (unsigned n = 0; n < 8; ++n) p.pads[n] = n % 2 ? move::colour::brown_yellow : move::colour::orange;
        p.pads[2] = move::colour::white;
        for (unsigned step = 0; step < move::step_count; ++step) p.steps[step] = move::colour::orange;
        p.button(Button::Up, move::white_dim);
        p.button(Button::Down, move::white_bright);
        p.button(Button::Left, move::white_dim);
        p.button(Button::Right, move::white_dim);
        panels.push_back({"lights-chop-bank", "CHOP, PLUS: SLICES 33-40, 35 WHITE", p});
    }
    {   // An audio track armed: Rec pulsing red, Play waiting.
        Panel p = kit();
        p.pads = {};
        p.tracks[2] = move::colour::orange;
        p.button(Button::Rec, move::colour::red, true);
        p.button(Button::Play, move::colour::dark_grey);
        p.button(Button::Sample, move::colour::light_grey);
        p.button(Button::Back, move::white_dim);
        panels.push_back({"lights-audio-armed", "AUDIO TRACK 3 ARMED: REC PULSES", p});
    }
    return panels;
}

// ---------------------------------------------------------------- the sheet

juce::Image sheet(const std::vector<Screen>& screens, const std::vector<Lights>& panels) {
    constexpr int scale = 4, gap = 24, caption = 30, top = 92;
    int screen_w = width * scale, screen_h = height * scale;
    int rows = int(screens.size() + 1) / 2, panel_rows = int(panels.size() + 1) / 2;
    int panels_top = top + rows * (screen_h + caption + gap);
    juce::Image image(juce::Image::RGB, gap + 2 * (screen_w + gap), panels_top + panel_rows * (screen_h + caption + gap), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    g.setColour(juce::Colour(0xffd8f0ff));
    move::draw_text(g, gap, 20, "MELODEE ON MOVE: SAMPLING", 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, "SAMPLE RECORD ON SAMPLING, AUDIO RECALL, TAKES. DOCS/UI.MD", 2);
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

bool write_sampling_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = sampling_screens();
    auto panels = sampling_lights();
    auto each = folder.getChildFile("sampling");
    each.createDirectory();
    bool ok = true;
    for (const auto& screen : screens)
        ok = mockup::write_png(mockup::screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = mockup::write_png(panel_image(panel.panel, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    auto file = folder.getChildFile("sampling.png");
    ok = mockup::write_png(sheet(screens, panels), file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace ui
