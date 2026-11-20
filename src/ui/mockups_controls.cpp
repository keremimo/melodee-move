// SPDX-License-Identifier: AGPL-3.0-only
// The screens and lights docs/MOVE_CONTROLS.md moves or adds, as mockups:
// controls.png. The Move's keys keep Ableton's meaning, so Melodee's own
// jobs reach new screens by new doors: the Device View on Play (the wheel
// walks the track's items, the push opens one's browser), Shift + push for
// Sample Options and a module's menu, Shift + turn for pages, Shift +
// Sampling for Audio Recall, the Shift card for Shift + Steps 4, 12 and 13.
// The light panels draw the whole Move as Ableton's controls figure lays it
// out (the manual's MoveControls.png).
#include "ui/mockups.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/lights.h"
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

// The Move's lights: what Lights would send, laid out as on the panel.
struct Panel {
    std::array<uint8_t, 4> tracks {};                // palette; Track 1 (top) first
    std::array<uint8_t, move::pad_count> pads {};     // palette; bottom left first
    std::array<bool, move::pad_count> pulsing {};     // ringed: pulsing or blinking
    std::array<uint8_t, move::step_count> steps {};   // palette
    std::array<bool, move::step_count> icons {};      // the icons under the steps, lit
    std::array<uint8_t, size_t(Button::Count)> buttons {}; // palette (RGB buttons) or brightness (white)
    void button(Button b, uint8_t value) { buttons[size_t(b)] = value; }
};
struct Lights { const char* name; const char* caption; Panel panel; };

// ---------------------------------------------------------------- example data

// The example project of rail.png: track 1's kit, its pad A05 a two-bar
// break at 94 BPM; track 2 OB-Xd, a Schwung synth, on its preset 12.
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
std::vector<float> phrase_peaks(int columns) { // a sung phrase: long swells
    std::vector<float> peaks(size_t(columns), 0.02f);
    struct Swell { int at; float level, decay; };
    for (const Swell& s : {Swell {2, .6f, 30}, Swell {30, .8f, 24}, Swell {58, .5f, 16}, Swell {72, .7f, 28}})
        for (int column = s.at; column < columns; ++column) peaks[size_t(column)] += s.level * std::exp(-(column - s.at) / s.decay);
    for (auto& peak : peaks) peak = std::min(peak, 1.0f);
    return peaks;
}

// The Device View's items along the top of the subject: the track, its
// sound, its two effects; the one the wheel is on inverted.
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

// ---------------------------------------------------------------- screens

std::vector<Screen> controls_screens() {
    return {
        {"shift-card", "SHIFT HELD ALONE: THE SHIFT CARD", [](juce::Graphics& g) {
            rail(g, "SHFT", "", "STEP");
            // Steps 1-16 in reading order; Melodee's own (no icon on the Move) inverted.
            static const char* jobs[16] {"SET", "STP", "FLW", "MIX", "BPM", "CLK", "SWG", "LAY",
                                         "SCL", "VEL", "RPT", "LPR", "SNG", "NEW", "X2", "QNT"};
            int cell_w = content_width / 4, cell_h = 13;
            for (int n = 0; n < 16; ++n) {
                int x = content_x + (n % 4) * cell_w, y = (n / 4) * cell_h;
                bool melodee = n == 3 || n == 11 || n == 12;
                g.setColour(on);
                if (melodee) g.fillRect(x, y, cell_w - 1, cell_h - 1);
                g.setColour(melodee ? off : on);
                move::draw_text_centred(g, x, y + 3, cell_w - 1, jobs[n]);
            }
            g.setColour(on);
            detail(g, "4 12 13: MELODEE");
        }},
        {"play-pad", "PLAY: WHEEL ON THE PAD, PUSH: SAMPLES", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "A05");
            items(g, {"KIT", "A05", "FX1", "FX2"}, 1);
            waveform(g, content(header_height + 1, subject_height - header_height - 1), break_peaks(content_width));
            detail(g, "BREAK 94", "2.0S");
        }},
        {"play-track", "PLAY: WHEEL ON THE TRACK, PUSH: TRACKS", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "TRK");
            items(g, {"KIT", "A05", "FX1", "FX2"}, 0);
            move::draw_text(g, content_x + 2, 14, "BREAKS", 2);
            move::draw_text(g, content_x + 2, 34, "KIT  9 PADS USED");
            detail(g, "PUSH: BROWSE");
        }},
        {"play-preset", "PLAY, SYNTH: WHEEL ON THE PRESET", [](juce::Graphics& g) {
            rail(g, "OBXD", "2", "1/5");
            items(g, {"OBXD", "012", "FX1", "FX2"}, 1);
            move::draw_text(g, content_x + 2, 12, "BRASS LEAD");
            knob_cells(g, content(22, 30), {"CUTOFF", "RESO", "ENVAMT", "DETUNE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"},
                       {0.62f, 0.3f, 0.75f, 0.5f, 0.05f, 0.45f, 0.8f, 0.35f});
            detail(g, "PUSH: PRESETS");
        }},
        {"browse-tracks", "PUSH ON THE TRACK: THE BROWSER'S TOP", [](juce::Graphics& g) {
            rail(g, "LOAD", "1", "TRK");
            list(g, content(0, height), {"KITS >", "SCHWUNG SYNTHS >", "KEYGROUPS >", "MIDI TRACK", "AUDIO TRACK"}, 0, 1);
        }},
        {"browse-presets", "PUSH ON THE PRESET: LOADS AS IT TURNS", [](juce::Graphics& g) {
            rail(g, "LOAD", "2", "PRST");
            list(g, content(0, 50), {"009 SOFT PAD", "010 SYNC LEAD", "011 BASS ONE", "012 BRASS LEAD", "013 STRINGS", "014 PLUCK"}, 3, 1);
            detail(g, "OB-XD", "PUSH: OK");
        }},
        {"sample-options", "SHIFT + PUSH ON A05: SAMPLE OPTIONS", [](juce::Graphics& g) {
            rail(g, "OPT", "A05", "SMP");
            list(g, content(0, height), {"CHOP >", "REVERSE", "TRIM >", "NORMALIZE", "SAVE SAMPLE"}, 0, 1);
        }},
        {"chop", "CHOP: THE WHEEL SETS THE AMOUNT", [](juce::Graphics& g) {
            rail(g, "SLC", "07", "/16");
            auto subject = content(0, subject_height);
            auto peaks = break_peaks(content_width);
            waveform(g, subject, peaks, 39, 44);
            std::vector<int> columns;
            for (int slice = 1; slice < 16; ++slice) columns.push_back(slice * content_width / 16);
            cuts(g, subject, columns, 5);
            detail(g, "REGIONS", "16 <>");
        }},
        {"module-menu", "SHIFT + PUSH ON OB-XD: ITS MENU", [](juce::Graphics& g) {
            rail(g, "MENU", "2", "OBXD");
            list(g, content(0, height), {"PRESET       012", "CUTOFF        57", "ENVELOPE >", "LFO >", "BROWSER >"}, 1, 1);
        }},
        {"module-page", "SHIFT + TURN: THE NEXT PAGE", [](juce::Graphics& g) {
            rail(g, "OBXD", "2", "3/5");
            move::draw_text(g, content_x + 2, 1, "ENVELOPE", 2);
            knob_cells(g, content(18, 46), {"ATTACK", "DECAY", "SUSTAIN", "RELEASE", "VELO", "CURVE", "KEYTRK", "GLIDE"},
                       {0.05f, 0.45f, 0.8f, 0.35f, 0.6f, 0.5f, 0.3f, 0.1f});
        }},
        {"sampling", "SAMPLING: PRESS A PAD", [](juce::Graphics& g) {
            rail(g, "SMPL", "", "MIC");
            g.setColour(off); dot(g, 12, 30, 5); g.setColour(on);
            move::draw_text_centred(g, content_x, 2, content_width, "PRESS", 2);
            move::draw_text_centred(g, content_x, 20, content_width, "A PAD", 2);
            for (int side = 0; side < 2; ++side) { // the input's level, left and right
                int y = 38 + side * 6, w = content_width - 4;
                g.drawRect(content_x + 2, y, w, 5);
                g.fillRect(content_x + 3, y + 1, (w - 2) * (side ? 40 : 45) / 100, 3);
            }
            detail(g, "PUSH: SOURCE");
        }},
        {"audio-recall", "SHIFT + SAMPLING: AUDIO RECALL", [](juce::Graphics& g) {
            rail(g, "RCL", "25", "SEC");
            waveform(g, content(0, subject_height), phrase_peaks(content_width));
            detail(g, "NEXT EMPTY: A07");
        }},
        {"track-options", "SHIFT + TRACK 2: ITS OPTIONS", [](juce::Graphics& g) {
            rail(g, "TRK", "2", "OPT");
            list(g, content(0, height), {"COLOUR    VIOLET", "MIDI IN     CH 2", "MIDI OUT     OFF"}, 0, 1);
        }},
        {"setup", "SHIFT + STEP 2: SETUP", [](juce::Graphics& g) {
            rail(g, "SETP", "", "");
            list(g, content(0, height), {"MIDI >", "SYNC >", "BRIGHTNESS   MID", "WEB PAGE >", "BACK TO MOVE"}, 4, 1);
        }},
        {"typing", "TYPING: BACK DELETES, HELD CANCELS", [](juce::Graphics& g) {
            move::draw_text(g, 1, 1, "SEARCH");
            g.fillRect(0, 10, width, 1);
            move::draw_text(g, 1, 24, "NIGHT RADI_");
            move::draw_text(g, 1, 46, "PADS: LETTERS");
            move::draw_text(g, 1, 56, "PUSH: OK  BACK: DEL");
        }},
        {"undo-list", "UNDO HELD: THE UNDO LIST (WAVE 2)", [](juce::Graphics& g) {
            rail(g, "UNDO", "3", "/12");
            list(g, content(0, 50), {"TUNE A05", "TO PADS 16", "CHOP A01", "LOAD BREAK 94"}, 2, 1);
            detail(g, "PUSH: BACK TO");
        }},
    };
}

// ---------------------------------------------------------------- lights

uint8_t pink() { return move::nearest_colour({255, 120, 190}); }
uint8_t dim_white() { return move::nearest_colour({96, 96, 96}); }

// The example kit: A01-A09 hold samples, A05 the last pad.
Panel kit_panel() {
    Panel p;
    p.tracks = {move::colour::azure, move::colour::dark_grey, move::colour::dark_grey, move::colour::dark_grey};
    p.tracks[1] = move::colour::deep_magenta;
    for (unsigned n = 0; n < 9; ++n) p.pads[n] = move::colour::azure;
    p.pads[4] = move::colour::white;
    for (auto b : {Button::Back, Button::Menu, Button::Capture, Button::Loop, Button::Mute, Button::Delete,
                   Button::Copy, Button::Undo, Button::Left, Button::Right, Button::Up, Button::Down})
        p.button(b, move::white_dim);
    p.button(Button::Shift, move::white_dim);
    p.button(Button::Play, move::colour::dark_grey);
    p.button(Button::Rec, move::colour::dark_grey);
    p.button(Button::Sample, move::colour::dark_grey);
    return p;
}

std::vector<Lights> controls_lights() {
    std::vector<Lights> panels;
    {   // Shift held: the steps with a job, the buttons with a Shift job.
        Panel p = kit_panel();
        for (unsigned n = 0; n < move::step_count; ++n) {
            p.steps[n] = move::colour::light_grey;
            p.icons[n] = !(n == 3 || n == 11 || n == 12); // the Move prints no icon under 4, 12, 13
        }
        p.steps[5] = move::colour::white; // the metronome is on
        for (auto b : {Button::Back, Button::Menu, Button::Capture, Button::Undo, Button::Mute, Button::Up, Button::Down,
                       Button::Left, Button::Right, Button::Delete, Button::Copy, Button::Loop})
            p.button(b, 0);
        for (auto b : {Button::Back, Button::Menu, Button::Capture, Button::Undo, Button::Mute, Button::Up, Button::Down,
                       Button::Left, Button::Right})
            p.button(b, move::white_dim);
        p.button(Button::Shift, move::white_bright);
        p.button(Button::Sample, move::colour::light_grey); // Shift + Sampling: Audio Recall
        p.button(Button::Play, move::colour::light_grey);   // Shift + Play: restart
        panels.push_back({"lights-shift", "SHIFT HELD: STEPS WITH A JOB LIT", p});
    }
    {   // Play on the kit: 32 pads, two banks; Plus and Minus move 16.
        Panel p = kit_panel();
        for (unsigned n = 16; n < 20; ++n) p.pads[n] = move::colour::azure;
        panels.push_back({"lights-kit", "PLAY, KIT: 32 PADS, +/- 16 MORE", p});
    }
    {   // 16 Pitches: the left 16 the kit (a bank as on the MPC), the right 16 A05's pitches.
        Panel p = kit_panel();
        p.pads = {};
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column) {
                unsigned pad = row * 8 + column, mpc = row * 4 + column; // MPC pad order: bottom left first, rows of four
                p.pads[pad] = mpc < 9 ? (mpc == 4 ? move::colour::white : move::colour::azure) : move::colour::off;
            }
        // C major from C: a root every eighth note of the scale, the others light grey.
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 4; column < 8; ++column) {
                unsigned degree = row * 4 + (column - 4);
                p.pads[row * 8 + column] = degree % 7 == 0 ? move::colour::azure : move::colour::light_grey;
            }
        p.steps[7] = move::colour::white;
        panels.push_back({"lights-pitches", "SHIFT + STEP 8: 16 PITCHES, RIGHT", p});
    }
    {   // Sampling: the pads that can take a sample pink, the one recording red.
        Panel p = kit_panel();
        for (auto& pad : p.pads) pad = pink();
        p.pads[5] = move::colour::red;
        p.pulsing[5] = true;
        p.button(Button::Sample, move::colour::red);
        p.button(Button::Back, move::white_dim);
        panels.push_back({"lights-sampling", "SAMPLING: PADS PINK, A06 TAKING", p});
    }
    {   // Browsing samples for A05: the target pad pulses.
        Panel p = kit_panel();
        p.pulsing[4] = true;
        panels.push_back({"lights-browser", "BROWSING SAMPLES FOR A05: PULSING", p});
    }
    {   // Mute held: a muted pad dim white; track 3 soloed (blue), track 4 muted (dim).
        Panel p = kit_panel();
        p.pads[2] = dim_white();
        p.tracks = {move::colour::azure, move::colour::deep_magenta, move::colour::blue, move::colour::very_dark_green};
        p.button(Button::Mute, move::white_bright);
        panels.push_back({"lights-mute", "MUTE HELD: A03 MUTED, TRACK 3 SOLO", p});
    }
    {   // Session: a row a track (Track 1 on top), a column a pattern.
        Panel p = kit_panel();
        p.pads = {};
        static const uint8_t colours[4] {move::colour::azure, move::colour::violet, move::colour::orange, move::colour::bright_green};
        static const int clips[4] {5, 3, 2, 4};
        for (unsigned track = 0; track < 4; ++track) {
            unsigned row = 3 - track;
            for (int column = 0; column < clips[track]; ++column) p.pads[row * 8 + unsigned(column)] = colours[track];
            p.tracks[track] = colours[track];
        }
        p.pads[3 * 8 + 1] = move::colour::white; p.pulsing[3 * 8 + 1] = true;     // track 1's pattern 2, selected
        p.pads[2 * 8 + 2] = move::colour::green; p.pulsing[2 * 8 + 2] = true;     // track 2's pattern 3, waiting
        p.button(Button::Play, move::colour::green);
        panels.push_back({"lights-session", "SESSION: 4 TRACKS, 8 PATTERNS", p});
    }
    return panels;
}

// The Move as Ableton's controls figure lays it out, scaled to `w` x `h`:
// the wheel top left with Back and Note/Session under it, the track
// buttons left of the pads, Play and Record left of the steps, the
// buttons right of the pads, the arrows and Plus/Minus bottom right.
juce::Image panel_image(const Panel& p, int w, int h) {
    juce::Image image(juce::Image::RGB, w, h, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    // Ableton's figure: the body is 907 x 423 of its pixels.
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
    // The screen, the knobs and their lights, the volume knob.
    g.setColour(juce::Colour(0xff050505));
    g.fillRect(X(22), Y(25), X(98), Y(52));
    g.setColour(juce::Colour(0xff6c7480));
    for (int knob = 0; knob < 8; ++knob) {
        float cx = X(205 + knob * 77.0f);
        g.drawEllipse(cx - X(16), Y(43) - X(16), X(32), X(32), 1.5f);
        g.fillEllipse(cx - 1.5f, Y(76) - 1.5f, 3, 3);
    }
    g.drawEllipse(X(866) - X(21), Y(48) - X(21), X(42), X(42), 1.5f);
    g.drawEllipse(X(69) - X(38), Y(156) - X(38), X(76), X(76), 1.5f); // the wheel
    // A round button, lit, with its short name.
    auto round = [&](float cx, float cy, float r, juce::Colour fill, const char* name) {
        g.setColour(fill);
        g.fillEllipse(X(cx) - X(r), Y(cy) - X(r), 2 * X(r), 2 * X(r));
        g.setColour(fill.getPerceivedBrightness() > 0.45f ? juce::Colours::black : juce::Colour(0xff8a96a3));
        int tw = move::text_width(name);
        move::draw_text(g, int(X(cx)) - tw / 2, int(Y(cy)) - 3, name);
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
    // The track buttons, Track 1 on top.
    for (int track = 0; track < 4; ++track) {
        g.setColour(rgb(p.tracks[size_t(track)]));
        g.fillRoundedRectangle(X(133), Y(104 + track * 58.0f), X(22), Y(46), 4);
    }
    // The pads, bottom left first.
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
    // The steps and the icons under them.
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

// ---------------------------------------------------------------- the sheet

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
    move::draw_text(g, gap, 20, "MELODEE ON MOVE: CONTROLS", 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, "THE MOVE'S KEYS AS ABLETON MADE THEM. DOCS/MOVE_CONTROLS.MD", 2);
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

bool write_controls_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = controls_screens();
    auto panels = controls_lights();
    auto each = folder.getChildFile("controls");
    each.createDirectory();
    bool ok = true;
    for (const auto& screen : screens)
        ok = write_png(screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = write_png(panel_image(panel.panel, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    auto file = folder.getChildFile("controls.png");
    ok = write_png(sheet(screens, panels), file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace ui
