// SPDX-License-Identifier: AGPL-3.0-only
// Projects, Setup, undo and naming on the Move, as
// mockups: projects.png. Ableton's keys keep their meaning (its manual,
// docs/MOVE_CONTROLS.md): Shift + Step 1 is the Set Overview, here Melodee's
// projects on the 32 pads; Shift + Step 2 is Setup, here Melodee's
// Settings; Undo and Shift + Undo undo and redo, and Undo held 0.4 s opens
// Melodee's undo list (Kerem's decision); Shift + a track button is the
// track's options. The screens are drawn by ui::system (system_draw.h),
// which the app's screens draw with too.
#include "ui/mockups.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/lights.h"
#include "ui/system_draw.h"
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
namespace sys = ui::system;
const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// ---------------------------------------------------------------- the example

// The projects folder: two folders, then nine projects; NIGHT BUS (pad 7)
// open, with changes not saved yet.
struct ExampleProject { const char* name; unsigned tracks, tempo; uint8_t colour; };
constexpr ExampleProject projects[] {
    {"BEATS", 0, 0, 0}, {"SKETCHES", 0, 0, 0},
    {"DUST", 4, 900, move::colour::azure}, {"LATE TAPE", 6, 860, move::colour::orange}, {"BROKEN CLOCK", 3, 1740, move::colour::violet},
    {"MORNING", 5, 1200, move::colour::bright_green}, {"NIGHT BUS", 7, 940, move::colour::yellow},
    {"SOFT LOOP", 2, 700, move::colour::cyan}, {"VOX CHOP", 4, 960, move::colour::hot_magenta},
    {"KICK TEST", 1, 1200, move::colour::royal_blue}, {"MPC JAM", 8, 920, move::colour::bright_red},
};
constexpr unsigned project_count = sizeof projects / sizeof projects[0];
constexpr unsigned open_project = 6; // NIGHT BUS

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

// Track 1's kit as Play shows it (controls.png), for the sheets drawn over it.
void kit_play(juce::Graphics& g) {
    rail(g, "KIT", "1", "A05");
    int cell = content_width / 4;
    const char* names[] {"KIT", "A05", "FX1", "FX2"};
    for (int n = 0; n < 4; ++n) {
        int x = content_x + n * cell;
        g.setColour(on);
        if (n == 1) g.fillRect(x, 0, cell - 1, header_height);
        else g.fillRect(x, header_height - 1, cell - 1, 1);
        g.setColour(n == 1 ? off : on);
        move::draw_text_centred(g, x, 1, cell - 1, names[n]);
    }
    g.setColour(on);
    waveform(g, content(header_height + 1, subject_height - header_height - 1), break_peaks(content_width));
    detail(g, "BREAK 94", "2.0S");
}
void message(juce::Graphics& g, const char* text) { // Studio::say(), as the app draws it
    sheet(g, {content_x, detail_y - 2, content_width, 10});
    move::draw_text_centred(g, content_x, detail_y, content_width, text);
}

sys::ProjectFacts facts_of(unsigned n) {
    sys::ProjectFacts f;
    const auto& p = projects[n];
    f.name = p.name;
    if (!p.tracks) { f.kind = sys::ProjectFacts::Kind::Folder; f.tracks = n == 0 ? 12 : 5; return f; }
    f.tracks = p.tracks;
    f.tempo = p.tempo;
    f.open = n == open_project;
    f.unsaved = f.open;
    f.mpc = n == project_count - 1;
    return f;
}

// ---------------------------------------------------------------- screens

std::vector<Screen> projects_screens() {
    return {
        {"overview", "SHIFT + STEP 1: PROJECTS, PAD 7", [](juce::Graphics& g) {
            sys::overview(g, open_project, "1/1", "", facts_of(open_project), "PUSH: OPTIONS");
        }},
        {"overview-other", "A PAD PICKS; BACK OPENS IT", [](juce::Graphics& g) {
            sys::overview(g, 4, "1/1", "", facts_of(4), "BACK: OPEN", "PLAY");
        }},
        {"overview-folder", "A FOLDER PAD: PUSH GOES IN", [](juce::Graphics& g) {
            sys::overview(g, 0, "1/1", "", facts_of(0), "PUSH: IN");
        }},
        {"overview-empty", "AN EMPTY PAD: BACK MAKES ONE", [](juce::Graphics& g) {
            sys::ProjectFacts f;
            f.kind = sys::ProjectFacts::Kind::Empty;
            sys::overview(g, 11, "1/1", "BEATS", f, "BACK: NEW");
        }},
        {"overview-mpc", "AN MPC PROJECT OPENS AS A COPY", [](juce::Graphics& g) {
            sys::overview(g, project_count - 1, "1/1", "", facts_of(project_count - 1), "BACK: OPEN");
        }},
        {"project-options", "SHIFT + PAD 7, OR PUSH: OPTIONS", [](juce::Graphics& g) {
            sys::setup_list(g, "OPT", "PRJ",
                            {{"SAVE", "", false}, {"SAVE AS NEW", "", false}, {"RENAME", "", true}, {"COLOUR", "YELLOW"},
                             {"DUPLICATE", ""}, {"MOVE TO FOLDER", "", true}, {"NEW FOLDER", ""}, {"OPEN AT START", "OFF"}},
                            0);
        }},
        {"project-options-other", "OPTIONS OF ANOTHER: LOAD KIT", [](juce::Graphics& g) {
            sys::setup_list(g, "OPT", "PRJ",
                            {{"OPEN", ""}, {"LOAD KIT", ""}, {"RENAME", "", true}, {"COLOUR", "VIOLET"}, {"DUPLICATE", ""},
                             {"MOVE TO FOLDER", "", true}, {"OPEN AT START", "OFF"}},
                            1);
        }},
        {"delete-project", "DELETE + PAD 5: THE PAD AGAIN", [](juce::Graphics& g) {
            sys::Question q;
            q.rail_top = "DEL"; q.rail_big = "5"; q.rail_bottom = "PRJ";
            q.asked = "DELETE PROJECT 5?";
            q.lines = {"BROKEN CLOCK", "ITS SAMPLES STAY"};
            q.yes = "PAD: YES"; q.no = "BACK NO";
            sys::question(g, q);
        }},
        {"copy-project", "COPY + PAD 5, THEN PAD 9: ASKS", [](juce::Graphics& g) {
            sys::Question q;
            q.rail_top = "COPY"; q.rail_big = "5"; q.rail_bottom = "> 9";
            q.asked = "REPLACE PROJECT 9?";
            q.lines = {"SOFT LOOP, WITH", "BROKEN CLOCK"};
            q.yes = "PAD: YES"; q.no = "BACK NO";
            sys::question(g, q);
        }},
        {"loading", "OPENING: THE SOUND FADES, THEN BACK", [](juce::Graphics& g) {
            sys::loading(g, "LATE TAPE", 12, 30, 1, 2, "RHODES C3.WAV");
        }},
        {"unsaved", "PLAY: THE RAIL'S MARK, UNSAVED", [](juce::Graphics& g) {
            kit_play(g);
            sys::unsaved_mark(g);
        }},
        {"name", "RENAME: THE WHEEL PICKS, PUSH TYPES", [](juce::Graphics& g) {
            sys::name_sheet(g, "RENAME PROJECT", "NIGHT BU", 31, {"R", "S", "T", "U", "V", "W", "X"}, 3, true);
        }},
        {"name-keys", "THE STRIP ENDS IN SPACE, DEL, OK", [](juce::Graphics& g) {
            sys::name_sheet(g, "NAME TRACK 2", "LEAD", 31, {"8", "9", "-", ".", "SP", "DL", "OK"}, 6, true);
        }},
        {"undo", "UNDO: THE STEP, NAMED", [](juce::Graphics& g) {
            kit_play(g);
            message(g, "UNDO TUNE A05");
        }},
        {"redo", "SHIFT + UNDO: REDO", [](juce::Graphics& g) {
            kit_play(g);
            message(g, "REDO TUNE A05");
        }},
        {"undo-list", "UNDO HELD: THE LIST, 2 STEPS BACK", [](juce::Graphics& g) {
            sys::undo_list(g,
                           {{"LEVEL A05", 0, "NOW", true}, {"TUNE A05", 0, "NOW"}, {"CHOP A01", 0, "1M"}, {"TO PADS", 0, "1M"},
                            {"NEW TRACK", 1, "4M"}, {"TEMPO", -1, "9M"}, {"", -1, "", false, true}},
                           3, 1);
        }},
        {"setup", "SHIFT + STEP 2: SETUP", [](juce::Graphics& g) {
            sys::setup_list(g, "SETP", "",
                            {{"PADS", "", true}, {"LIGHTS", "", true}, {"MIDI", "", true}, {"SYNC", "", true}, {"WEB PAGE", "", true},
                             {"USAGE LOG", "OFF"}, {"ABOUT", "", true}, {"BACK TO MOVE", ""}},
                            0);
        }},
        {"setup-pads", "SETUP > PADS: PUSH EDITS A VALUE", [](juce::Graphics& g) {
            sys::setup_list(g, "PADS", "SETP", {{"RESPONSE", "MED", false, true}, {"PRESSURE", "ON"}}, 0);
        }},
        {"setup-lights", "SETUP > LIGHTS", [](juce::Graphics& g) {
            sys::setup_list(g, "LGHT", "SETP",
                            {{"PAD LIGHTS", "COLOUR"}, {"PAD FADE", "100 MS"}, {"BRIGHTNESS", "HIGH"}, {"LIGHT SHOW", "OFF"},
                             {"SHOW STYLE", "PULSE"}},
                            3);
        }},
        {"about", "SETUP > ABOUT", [](juce::Graphics& g) {
            sys::About a;
            a.cpu_percent = 2.3f; a.peak_us = 191; a.late = 0; a.free_gb = 3.1; a.synths = 2; a.slow_calls = 0; a.version = "0.6";
            sys::about(g, a);
        }},
        {"track-options", "SHIFT + TRACK 2: COLOUR, NAME", [](juce::Graphics& g) {
            sys::setup_list(g, "TRK", "OPT", {{"COLOUR", "ORANGE", false, true}, {"NAME", "LEAD", true}, {"MIDI IN", "CH 2"}, {"MIDI OUT", "OFF"}}, 0);
        }},
        {"move-track", "TRACK HELD + SHIFT + COPY: MOVE", [](juce::Graphics& g) {
            rail(g, "TRKS", "2", "/16");
            auto area = content(0, 52);
            int cw = area.getWidth() / 4, ch = area.getHeight() / 4;
            const char* tags[16] {"KIT", "OBXD", "KEYS", "MIDI", "KIT", "AUD", "SURG", "8", "KIT", "10", "11", "12", "13", "14", "15", "16"};
            for (int t = 0; t < 16; ++t) {
                int x = area.getX() + (t % 4) * cw, y = area.getY() + (t / 4) * ch;
                bool used = t < 7 || t == 8, chosen = t == 1;
                g.setColour(on);
                if (chosen) g.fillRect(x, y, cw - 1, ch - 1);
                else if (used) g.drawRect(x, y, cw - 1, ch - 1);
                g.setColour(chosen ? off : on);
                move::draw_text_centred(g, x, y + (ch - 7) / 2, cw - 1, tags[t]);
            }
            g.setColour(on);
            detail(g, "MOVE 2 TO: A STEP");
        }},
    };
}

// ---------------------------------------------------------------- lights

struct Panel {
    std::array<uint8_t, 4> tracks {};
    std::array<uint8_t, move::pad_count> pads {};
    std::array<bool, move::pad_count> pulsing {};
    std::array<uint8_t, move::step_count> steps {};
    std::array<bool, move::step_count> icons {};
    std::array<uint8_t, size_t(Button::Count)> buttons {};
    std::array<bool, 8> knobs {}; // a knob's light, lit
    void button(Button b, uint8_t value) { buttons[size_t(b)] = value; }
};
struct Lights { const char* name; const char* caption; Panel panel; };

// The Move's pads hold the folder's items as the Set Overview holds Sets:
// the top row first, left to right (pad 25 is the first).
unsigned overview_pad(unsigned item) { return (3 - item / 8) * 8 + item % 8; }

Panel base_panel() {
    Panel p;
    p.tracks = {move::colour::azure, move::colour::orange, move::colour::dark_grey, move::colour::dark_grey};
    for (auto b : {Button::Back, Button::Menu, Button::Undo, Button::Shift}) p.button(b, move::white_dim);
    p.button(Button::Play, move::colour::dark_grey);
    return p;
}

std::vector<Lights> projects_lights() {
    std::vector<Lights> panels;
    {   // The overview: folders white, projects in their colours, the one picked pulsing; empty pads dark.
        Panel p = base_panel();
        for (unsigned n = 0; n < project_count; ++n) p.pads[overview_pad(n)] = projects[n].tracks ? projects[n].colour : move::colour::light_grey;
        p.pulsing[overview_pad(open_project)] = true;
        for (auto b : {Button::Copy, Button::Delete}) p.button(b, move::white_dim);
        p.button(Button::Play, move::colour::light_grey); // previews the project picked
        panels.push_back({"lights-overview", "PROJECTS: 2 FOLDERS, 9 PROJECTS, PAD 7", p});
    }
    {   // An empty pad picked: white.
        Panel p = base_panel();
        for (unsigned n = 0; n < project_count; ++n) p.pads[overview_pad(n)] = projects[n].tracks ? projects[n].colour : move::colour::light_grey;
        p.pads[overview_pad(11)] = move::colour::white;
        p.pulsing[overview_pad(11)] = true;
        panels.push_back({"lights-overview-empty", "AN EMPTY PAD PICKED: WHITE", p});
    }
    {   // Delete held, pad 5 pressed: it flashes red until pressed again.
        Panel p = base_panel();
        for (unsigned n = 0; n < project_count; ++n) p.pads[overview_pad(n)] = projects[n].tracks ? projects[n].colour : move::colour::light_grey;
        p.pads[overview_pad(4)] = move::colour::red;
        p.pulsing[overview_pad(4)] = true;
        p.button(Button::Delete, move::white_bright);
        panels.push_back({"lights-delete", "DELETE + PAD 5: RED UNTIL AGAIN", p});
    }
    {   // The name sheet: the pads type letters (grey), space blue, delete red; the steps digits.
        Panel p = base_panel();
        const char* rows[4] {"abcdefgh", "ijklmnop", "qrstuvwx", "yz -.'&\b"};
        for (unsigned pad = 0; pad < move::pad_count; ++pad) {
            char c = rows[3 - pad / 8][pad % 8];
            p.pads[pad] = c == '\b' ? move::colour::red : c == ' ' ? move::colour::azure : (c >= 'a' && c <= 'z') ? move::colour::light_grey : move::colour::dark_grey;
        }
        p.pads[overview_pad(4)] = move::colour::white; // 'e', just typed
        for (unsigned s = 0; s < 10; ++s) p.steps[s] = move::colour::light_grey;
        p.button(Button::Delete, move::white_dim);
        panels.push_back({"lights-name", "NAMING: PADS LETTERS, STEPS DIGITS", p});
    }
    {   // The undo list: Undo bright; the rest as they were.
        Panel p = base_panel();
        for (unsigned n = 0; n < 9; ++n) p.pads[n] = move::colour::azure;
        p.pads[4] = move::colour::white;
        p.button(Button::Undo, move::white_bright);
        panels.push_back({"lights-undo", "UNDO HELD: UNDO BRIGHT", p});
    }
    {   // Shift + a track button: its options; the colour turned, the button shows it.
        Panel p = base_panel();
        p.tracks[1] = move::colour::bright_green;
        p.button(Button::Shift, move::white_bright);
        panels.push_back({"lights-track-colour", "TRACK 2'S COLOUR: ITS BUTTON SHOWS IT", p});
    }
    {   // The light show, Pulse on beat 1: the pads in the tracks' colours, harder on the one.
        Panel p = base_panel();
        for (unsigned pad = 0; pad < move::pad_count; ++pad) p.pads[pad] = ui::track_colour(pad % 8).bright;
        for (unsigned s = 0; s < 4; ++s) p.steps[s] = move::colour::white;
        p.button(Button::Play, move::colour::green);
        panels.push_back({"lights-show", "LIGHT SHOW: PULSE, BEAT 1", p});
    }
    return panels;
}

// The Move as Ableton's controls figure lays it out (as mockups_controls.cpp
// draws it), with the knobs' lights.
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
    for (int knob = 0; knob < 8; ++knob) {
        float cx = X(205 + knob * 77.0f);
        g.setColour(p.knobs[size_t(knob)] ? juce::Colours::white : juce::Colour(0xff6c7480));
        g.drawEllipse(cx - X(16), Y(43) - X(16), X(32), X(32), p.knobs[size_t(knob)] ? 3.0f : 1.5f);
        g.fillEllipse(cx - 1.5f, Y(76) - 1.5f, 3, 3);
    }
    g.setColour(juce::Colour(0xff6c7480));
    g.drawEllipse(X(866) - X(21), Y(48) - X(21), X(42), X(42), 1.5f);
    g.drawEllipse(X(69) - X(38), Y(156) - X(38), X(76), X(76), 1.5f);
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
    move::draw_text(g, gap, 20, "MELODEE ON MOVE: PROJECTS AND SYSTEM", 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, "SETS, SETUP, UNDO, NAMES, TRACK OPTIONS. DOCS/UI.MD", 2);
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

bool write_projects_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = projects_screens();
    auto panels = projects_lights();
    auto each = folder.getChildFile("projects");
    each.createDirectory();
    bool ok = true;
    for (const auto& screen : screens)
        ok = mockup::write_png(mockup::screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = mockup::write_png(panel_image(panel.panel, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    auto file = folder.getChildFile("projects.png");
    ok = mockup::write_png(sheet(screens, panels), file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace ui
