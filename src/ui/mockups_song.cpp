// SPDX-License-Identifier: AGPL-3.0-only
// Song, exports, print and freeze (docs/UI.md, "Song"; docs/PARITY.md's
// `song & export` rows), as mockups: song.png. Song is Shift + Step 13 (the
// Move prints no icon there, docs/MOVE_CONTROLS.md's decision): a lane per
// track of the bank in view, clips of patterns, markers, the loop brace, the
// cursor. The pads are the song too, as the Move's Session lays clips out (a
// row a track, the track buttons' four), a column a bar; the steps are
// sixteen bars, a step held and another the loop, as in Loop Mode. Exports
// and the PRINT sheet open from keys the Move leaves free; the lights show
// where. The light panels draw the whole Move as Ableton's controls figure
// lays it out.
#include "ui/mockups.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/lights.h"
#include "ui/song_view.h"
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
const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// The Move's lights, laid out as on the panel.
struct Panel {
    std::array<uint8_t, 4> tracks {};                       // Track 1 (top) first
    std::array<uint8_t, move::pad_count> pads {};            // bottom left first
    std::array<bool, move::pad_count> pulsing {};
    std::array<uint8_t, move::step_count> steps {};
    std::array<uint8_t, 8> knobs {};                         // the knobs' LEDs, white
    std::array<uint8_t, size_t(Button::Count)> buttons {};
    void button(Button b, uint8_t value) { buttons[size_t(b)] = value; }
};
struct Lights { const char* name; const char* caption; Panel panel; };

uint8_t ice() { return move::nearest_colour({150, 215, 255}); }

// ---------------------------------------------------------------- example data

// The example project: tracks 5-8 in view (BREAKS on a kit, VOX on audio,
// PAD on Surge, an empty one), the cursor on bar 9 of a 16-bar song with
// markers INTRO, VERSE and CHORUS and the loop on bars 9-12.
SongView example_song() {
    SongView v;
    v.first = 0; v.zoom = 16; v.cursor = 8; v.lane = 0;
    v.clips = {{0, 0, 4, "A1"}, {0, 4, 4, "A2"}, {0, 8, 4, "A2"}, {0, 12, 4, "A3"},
               {1, 4, 8, "B1"}, {2, 0, 8, "A1"}, {2, 8, 8, "A1", true}};
    v.markers = {{0, "INTRO"}, {4, "VERSE"}, {12, "CHORUS"}};
    v.loop_from = 8; v.loop_to = 12;
    v.lane_used[3] = false;
    return v;
}

void song_rail(juce::Graphics& g, const SongView& v) { rail(g, "SONG", std::to_string(v.cursor + 1), "T5"); }

// The sixteen tracks as a grid, a row a bank, the selected one inverted, a
// frozen one with its snowflake in place of its kind.
void track_grid(juce::Graphics& g, juce::Rectangle<int> area, unsigned chosen, unsigned frozen) {
    static const char* tags[16] {"KIT", "OBXD", "KEYS", "MIDI", "KIT", "AUD", "SURG", "", "KIT", "", "", "", "", "", "", ""};
    int cell_w = area.getWidth() / 4, cell_h = area.getHeight() / 4;
    for (unsigned t = 0; t < 16; ++t) {
        int x = area.getX() + int(t % 4) * cell_w, y = area.getY() + int(t / 4) * cell_h;
        bool used = tags[t][0] != 0;
        g.setColour(on);
        if (t == chosen) g.fillRect(x, y, cell_w - 1, cell_h - 1);
        else if (used) g.drawRect(x, y, cell_w - 1, cell_h - 1);
        g.setColour(t == chosen ? off : on);
        if (t == frozen) snowflake(g, x + (cell_w - 8) / 2, y + (cell_h - 8) / 2);
        else move::draw_text_centred(g, x, y + (cell_h - 7) / 2, cell_w - 1, used ? tags[t] : std::to_string(t + 1));
        g.setColour(on);
    }
}

// The PRINT sheet's facts and its action, which the wheel picks.
void print_sheet(juce::Graphics& g, const char* action, const char* detail_left) {
    rail(g, "PRNT", "2", "OBXD");
    move::draw_text(g, content_x + 2, 1, "TAP   POST FX");
    move::draw_text(g, content_x + 2, 10, "TAIL  LOOP");
    move::draw_text(g, content_x + 2, 19, "ROOM  9.1 MB");
    move::draw_text(g, content_x + 2, 28, "CPU   14% NOW");
    g.fillRect(content_x, 39, content_width, 11);
    g.setColour(off);
    move::draw_text_centred(g, content_x, 41, content_width, std::string("< ") + action + " >");
    g.setColour(on);
    detail(g, detail_left, "4/6");
}

// ---------------------------------------------------------------- screens

std::vector<Screen> song_screens() {
    return {
        {"song", "SHIFT + STEP 13: SONG, CURSOR ON A CLIP", [](juce::Graphics& g) {
            auto v = example_song();
            song_rail(g, v);
            song_view(g, v);
            detail(g, "A2 4 BARS", "0:16");
        }},
        {"song-empty", "CURSOR ON EMPTY TIME: PUSH PLACES", [](juce::Graphics& g) {
            auto v = example_song();
            v.lane = 1; v.cursor = 13;
            rail(g, "SONG", "14", "T6");
            song_view(g, v);
            detail(g, "PLACE B1 X4", "PUSH");
        }},
        {"song-playing", "PLAY: THE SONG FROM THE CURSOR; REC", [](juce::Graphics& g) {
            auto v = example_song();
            v.playhead = 10.4f; v.recording = true;
            song_rail(g, v);
            song_view(g, v);
            detail(g, "BAR 11.2", "0:21");
        }},
        {"song-zoom", "KNOB 3 ZOOM: 64 BARS, TEMPO POINTS", [](juce::Graphics& g) {
            auto v = example_song();
            v.zoom = 64;
            v.clips.push_back({0, 16, 16, "A4"});
            v.clips.push_back({1, 20, 12, "B2"});
            v.clips.push_back({2, 16, 24, "A5"});
            v.markers.push_back({32, "OUTRO"});
            v.tempo_points = {16, 32};
            song_rail(g, v);
            song_view(g, v);
            detail(g, "A2 4 BARS", "128.0");
        }},
        {"song-knob-pattern", "KNOB 1: THE CLIP'S PATTERN", [](juce::Graphics& g) {
            rail(g, "KNOB", "1", "SONG");
            big_parameter(g, content(4, 44), "PATTERN", "A2", 0.08f);
            detail(g, "KNOB 2: BARS 4");
        }},
        {"song-knob-tempo", "KNOB 5: TEMPO FROM BAR 9 (KNOB 6 SHAPE)", [](juce::Graphics& g) {
            rail(g, "KNOB", "5", "SONG");
            big_parameter(g, content(4, 44), "BAR 9 TEMPO", "128.0", 0.37f);
            detail(g, "UNDER 30: NONE");
        }},
        {"clip-sheet", "PUSH ON A CLIP: ITS JOBS", [](juce::Graphics& g) {
            rail(g, "CLIP", "A2", "T5");
            list(g, content(0, 52), {"OPEN NOTES", "DUPLICATE", "SPLIT AT CURSOR", "OWN PATTERN", "REPEAT TO MARK",
                                     "LOOP ITS BARS", "MUTE", "DELETE"}, 2, 1);
            detail(g, "PUSH: DO", "BACK");
        }},
        {"marker", "SHIFT + PUSH: A MARKER (AGAIN: NEXT NAME)", [](juce::Graphics& g) {
            auto v = example_song();
            v.cursor = 12;
            song_rail(g, v);
            rail(g, "SONG", "13", "T5");
            song_view(g, v);
            detail(g, "MARKER CHORUS");
        }},
        {"export", "SAMPLING ON SONG: EXPORT", [](juce::Graphics& g) {
            rail(g, "EXPT", "", "SONG");
            list(g, content(0, 52), {"MIXDOWN 32 FLOAT", "MIXDOWN 24 BIT", "MIXDOWN 16 BIT", "STEMS", "MIDI FILE", "MPC 3 PROJECT",
                                     "SONG TO PATTERN"}, 0, 1);
            detail(g, "AS MOVE 3", "3:14");
        }},
        {"exporting", "MIXDOWN: THE SONG PLAYS ONCE THROUGH", [](juce::Graphics& g) {
            rail(g, "EXPT", "37", "%");
            move::draw_text(g, content_x + 2, 1, "MIXDOWN 32 FLOAT");
            move::draw_text(g, content_x + 2, 12, "MOVE 3.WAV");
            progress(g, {content_x + 2, 26, content_width - 4, 9}, 0.37f);
            move::draw_text(g, content_x + 2, 40, "1:12 OF 3:14");
            detail(g, "BACK: STOP");
        }},
        {"exported", "DONE: WHERE IT WENT", [](juce::Graphics& g) {
            rail(g, "EXPT", "OK", "");
            move::draw_text(g, content_x + 2, 1, "MIXDOWNS/");
            move::draw_text(g, content_x + 2, 12, "MOVE 3.WAV", 1);
            move::draw_text(g, content_x + 2, 26, "3:24  70.1 MB");
            move::draw_text(g, content_x + 2, 37, "TAIL 10.0 S");
            detail(g, "PUSH: OK");
        }},
        {"load-mid", "A .MID IN THE BROWSER: NOTES ONTO A PATTERN", [](juce::Graphics& g) {
            rail(g, "LOAD", "5", "MID");
            list(g, content(0, 52), {"..", "BREAK 94.WAV", "CHORDS.MID", "SONG 2.MID", "VOX TAKE.WAV"}, 2, 1);
            detail(g, "PUSH: ONTO A1");
        }},
        {"print", "TRACK 2 HELD + SAMPLING: PRINT", [](juce::Graphics& g) { print_sheet(g, "FREEZE", "PATTERNS"); }},
        {"print-deep", "THE WHEEL: DEEP FREEZE, ALL, UNFREEZE", [](juce::Graphics& g) { print_sheet(g, "DEEP FREEZE", "MODULE OFF"); }},
        {"printing", "PRINTING: BACK CALLS IT OFF", [](juce::Graphics& g) {
            rail(g, "PRNT", "2", "3/4");
            move::draw_text(g, content_x + 2, 1, "PRINTING");
            move::draw_text(g, content_x + 2, 11, "A3", 3);
            progress(g, {content_x + 44, 13, content_width - 46, 9}, 0.6f);
            move::draw_text(g, content_x + 44, 25, "X12 REAL");
            move::draw_text(g, content_x + 2, 40, "THEN ON A BAR");
            detail(g, "BACK: CALL OFF", "4S");
        }},
        {"frozen", "A FROZEN TRACK: SNOWFLAKE, PADS LIVE", [](juce::Graphics& g) {
            rail(g, "OBXD", "2", "2/5");
            g.setColour(off);
            snowflake(g, 8, 43);
            g.setColour(on);
            move::draw_text(g, content_x + 2, 1, "FILTER", 2);
            knob_cells(g, content(18, 30), {"CUTOFF", "RESO", "ENVAMT", "KEYTRK", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"},
                       {0.62f, 0.3f, 0.75f, 0.5f, 0.05f, 0.45f, 0.8f, 0.35f});
            detail(g, "FROZEN", "PADS LIVE");
        }},
        {"unfreeze", "A KNOB ON A FROZEN TRACK: ASKS FIRST", [](juce::Graphics& g) {
            rail(g, "FRZN", "2", "OBXD");
            move::draw_text_centred(g, content_x, 6, content_width, "UNFREEZE", 2);
            move::draw_text_centred(g, content_x, 26, content_width, "TRACK 2?", 2);
            detail(g, "PUSH YES", "BACK NO");
        }},
        {"tracks-frozen", "TRACK BUTTON HELD: FROZEN TRACK 2", [](juce::Graphics& g) {
            rail(g, "TRKS", "2", "/16");
            track_grid(g, content(0, 52), 1, 1);
            detail(g, "2 OBXD FROZEN");
        }},
    };
}

// ---------------------------------------------------------------- lights

// Tracks 5-8 in view, track 5 selected: the bank's colours.
Panel song_panel() {
    Panel p;
    for (unsigned n = 0; n < 4; ++n) p.tracks[n] = n == 3 ? move::colour::off : n == 0 ? track_colour(4 + n).bright : track_colour(4 + n).dim;
    // The pads: bars 9-16 of the four lanes, top row lane 1. A clip's first
    // bar bright, the rest dim, a muted clip grey, the cursor white.
    auto v = example_song();
    for (const auto& clip : v.clips)
        for (int bar = clip.bar; bar < clip.bar + clip.bars; ++bar) {
            if (bar < 8 || bar >= 16) continue;
            unsigned pad = unsigned(3 - clip.lane) * 8 + unsigned(bar - 8);
            auto colour = track_colour(4 + unsigned(clip.lane));
            p.pads[pad] = clip.muted ? move::colour::dark_grey : bar == clip.bar ? colour.bright : colour.dim;
        }
    p.pads[3 * 8 + 0] = move::colour::white;
    // The steps: bars 1-16; the loop's bars bright, the lane's clips dim, the cursor white.
    for (int bar = 0; bar < 16; ++bar) p.steps[size_t(bar)] = track_colour(4).dim;
    for (int bar = v.loop_from; bar < v.loop_to; ++bar) p.steps[size_t(bar)] = track_colour(4).bright;
    p.steps[size_t(v.cursor)] = move::colour::white;
    for (int k : {0, 1, 2, 4, 5}) p.knobs[size_t(k)] = move::white_dim;
    for (auto b : {Button::Back, Button::Menu, Button::Loop, Button::Mute, Button::Delete, Button::Copy, Button::Undo,
                   Button::Left, Button::Right, Button::Up, Button::Down, Button::Shift})
        p.button(b, move::white_dim);
    p.button(Button::Play, move::colour::dark_grey);
    p.button(Button::Rec, move::colour::dark_grey);
    p.button(Button::Sample, move::colour::dark_grey);
    return p;
}

// Track 2 (OB-Xd, orange) selected, its patterns on the pads.
Panel print_panel() {
    Panel p;
    p.tracks = {track_colour(0).dim, track_colour(1).bright, track_colour(2).dim, track_colour(3).dim};
    auto colour = track_colour(1);
    // Pattern n of the bank on pad n, bottom left first, as a kit's pads.
    for (unsigned pattern : {0u, 1u, 2u, 3u, 8u, 9u}) p.pads[pattern] = colour.dim; // A1-A4, A9, A10 have notes
    for (unsigned pattern : {0u, 1u, 2u, 3u}) p.pads[pattern] = ice();             // four to print
    p.pulsing[1] = true;                                                            // A2 plays
    p.knobs[0] = p.knobs[1] = move::white_dim;
    for (auto b : {Button::Back, Button::Up, Button::Down, Button::Shift}) p.button(b, move::white_dim);
    p.button(Button::Sample, move::colour::red);
    p.button(Button::Play, move::colour::green);
    return p;
}

std::vector<Lights> song_lights() {
    std::vector<Lights> panels;
    panels.push_back({"lights-song", "SONG: PADS BARS 9-16, STEPS BARS 1-16", song_panel()});
    {   // Playing and recording: the playhead's bar green on the steps and pads.
        Panel p = song_panel();
        p.steps[10] = move::colour::green;
        for (unsigned row = 0; row < 4; ++row)
            if (!p.pads[row * 8 + 2]) p.pads[row * 8 + 2] = move::colour::very_dark_green;
        p.button(Button::Play, move::colour::green);
        p.button(Button::Rec, move::colour::red);
        panels.push_back({"lights-song-playing", "PLAYING, REC: LAUNCHES BECOME CLIPS", p});
    }
    {   // Step 9 held, then step 12: the loop on bars 9-12.
        Panel p = song_panel();
        for (int bar = 0; bar < 16; ++bar) p.steps[size_t(bar)] = track_colour(4).dim;
        for (int bar = 8; bar < 12; ++bar) p.steps[size_t(bar)] = move::colour::white;
        panels.push_back({"lights-song-loop", "STEP 9 HELD + STEP 12: THE LOOP", p});
    }
    {   // Copy held: the clip pad pressed white, the pads that take it lit.
        Panel p = song_panel();
        p.button(Button::Copy, move::white_bright);
        p.pulsing[3 * 8 + 0] = true;
        panels.push_back({"lights-song-copy", "COPY HELD + A CLIP'S PAD, THEN A PAD", p});
    }
    {   // Exporting: Sampling red while the song plays into the file.
        Panel p = song_panel();
        p.button(Button::Sample, move::colour::red);
        p.button(Button::Play, move::colour::green);
        p.steps[4] = move::colour::green;
        for (auto& light : p.knobs) light = 0;
        for (auto b : {Button::Loop, Button::Delete, Button::Copy, Button::Undo, Button::Left, Button::Right, Button::Up, Button::Down})
            p.button(b, 0);
        panels.push_back({"lights-export", "EXPORTING: SAMPLING RED, BACK STOPS", p});
    }
    panels.push_back({"lights-print", "PRINT: PADS THE PATTERNS, ICE TO PRINT", print_panel()});
    {   // Printing: done ones ice, the one printing pulsing, the rest dim.
        Panel p = print_panel();
        p.pulsing = {};
        p.pads[2] = ice();
        p.pulsing[2] = true;
        p.pads[3] = track_colour(1).dim;
        panels.push_back({"lights-printing", "PRINTING A3: DONE ICE, A3 PULSING", p});
    }
    {   // The sixteen tracks held: a frozen track's step ice.
        Panel p = song_panel();
        p.pads = {};
        p.pulsing = {};
        static const bool used[16] {true, true, true, true, true, true, true, false, true};
        for (unsigned t = 0; t < 16; ++t) p.steps[t] = used[t] ? track_colour(t).dim : move::colour::dark_grey;
        p.steps[1] = ice();
        p.steps[4] = move::colour::white;
        p.tracks = {track_colour(4).bright, track_colour(5).dim, track_colour(6).dim, move::colour::off};
        p.knobs = {};
        for (auto b : {Button::Loop, Button::Undo, Button::Left, Button::Right, Button::Up, Button::Down}) p.button(b, 0);
        p.button(Button::Sample, move::colour::dark_grey);
        panels.push_back({"lights-tracks-frozen", "TRACK HELD: TRACK 2 FROZEN, ICE", p});
    }
    return panels;
}

// The Move as Ableton's controls figure lays it out (907 x 423 of its
// pixels), scaled to `w` x `h`.
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
    for (int knob = 0; knob < 8; ++knob) { // the knobs, their LEDs under them
        float cx = X(205 + knob * 77.0f);
        g.setColour(juce::Colour(0xff6c7480));
        g.drawEllipse(cx - X(16), Y(43) - X(16), X(32), X(32), 1.5f);
        g.setColour(white(p.knobs[size_t(knob)]));
        g.fillEllipse(cx - 2.5f, Y(76) - 2.5f, 5, 5);
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

bool write_png(const juce::Image& image, const juce::File& file) {
    file.deleteFile();
    juce::FileOutputStream output(file);
    return output.openedOk() && juce::PNGImageFormat().writeImageToStream(image, output);
}

juce::Image screen_image(const Draw& draw, int scale) {
    juce::Image image(juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g(image);
        g.fillAll(off);
        g.setColour(on);
        draw(g);
    }
    return move::frame_image(move::pack(image), scale);
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
    move::draw_text(g, gap, 20, "MELODEE ON MOVE: SONG", 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, "SONG, EXPORTS, PRINT AND FREEZE. DOCS/UI.MD", 2);
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

bool write_song_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = song_screens();
    auto panels = song_lights();
    auto each = folder.getChildFile("song");
    each.createDirectory();
    bool ok = true;
    for (const auto& screen : screens)
        ok = write_png(screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = write_png(panel_image(panel.panel, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    auto file = folder.getChildFile("song.png");
    ok = write_png(sheet(screens, panels), file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace ui
