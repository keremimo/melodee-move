// SPDX-License-Identifier: AGPL-3.0-only
// Melodee's MIDI and sync on the Move, as mockups: midi.png. The Move has one
// MIDI port for Melodee (docs/MIDI.md): USB-MIDI cable 2 of its device page,
// where a class-compliant device on USB-A and a computer on USB-C's
// "Ableton Move Standalone Port" both arrive. So there is no port to pick;
// the rest is Melodee's MIDI as melodee-sample has it (docs/MIDI_TRACKS.md,
// guide/midi-and-sync.md): a MIDI track's pages on the knobs (Shift + the
// wheel walks them, as a module's pages), its Sounds through the Device
// View's push, MIDI Config from Setup (Shift + Step 2), each track's MIDI In
// and Out channel in the Move's track options (Shift + a track button).
// The example: track 3, "JUNO", a MIDI track on channel 2 playing a Juno-106
// through a USB-MIDI interface on USB-A, heard back on the line in.
#include "ui/mockups.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/lights.h"
#include "ui/track_style.h"
#include "ui/widgets.h"
#include <algorithm>
#include <functional>
#include <iostream>

namespace ui {
namespace {

using Draw = std::function<void(juce::Graphics&)>;
struct Screen { const char* name; const char* caption; Draw draw; };

using move::Button;
const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// The Move's lights, laid out as on the panel (mockups_controls.cpp's).
struct Panel {
    std::array<uint8_t, 4> tracks {};
    std::array<uint8_t, move::pad_count> pads {};     // bottom left first
    std::array<bool, move::pad_count> pulsing {};
    std::array<uint8_t, move::step_count> steps {};
    std::array<bool, move::step_count> icons {};
    std::array<uint8_t, size_t(Button::Count)> buttons {};
    void button(Button b, uint8_t value) { buttons[size_t(b)] = value; }
};
struct Lights { const char* name; const char* caption; Panel panel; };

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

// A MIDI track's page: the Device View's items, the page's title, its knobs
// as names and values, the detail line (the last message out, bar.beat).
void midi_page(juce::Graphics& g, const char* page, const char* title, const std::array<std::string, 8>& names,
               const std::array<std::string, 8>& values, const char* left, const char* right = "", int touched = -1) {
    rail(g, "MIDI", "3", page);
    items(g, {"JUNO", "PAD", "FX1", "FX2"}, 0);
    move::draw_text(g, content_x + 1, 11, title);
    knob_values(g, content(20, 32), names, values, touched);
    detail(g, left, right);
}

std::vector<Screen> midi_screens() {
    return {
        {"midi-device", "MIDI TRACK, DEVICE: CHANNEL, DELAY", [](juce::Graphics& g) {
            midi_page(g, "1/9", "DEVICE  USB MIDI", {"CHAN", "DLY"}, {"2", "-12"}, "OUT C3 100", "2.3");
        }},
        {"midi-program", "PROGRAM: BANK, PROGRAM, THIS PATTERN'S", [](juce::Graphics& g) {
            midi_page(g, "2/9", "PROGRAM", {"MSB", "LSB", "PROG", "PAT"}, {"OFF", "OFF", "12", "TRK"}, "SENT AT LAUNCH");
        }},
        {"midi-cc", "CC 1-8: EIGHT SLOTS, RECORDED AS MOTION", [](juce::Graphics& g) {
            midi_page(g, "3/9", "CC 1-8", {"CC74", "CC71", "MOD", "CC73", "CC72", "N300", "BEND", "SLOT"},
                      {"64", "30", "0", "12", "88", "512", "0", "-"}, "SHIFT+KNOB: SENDS");
        }},
        {"midi-slot", "SHIFT + KNOB 1: WHAT IT SENDS", [](juce::Graphics& g) {
            rail(g, "KNOB", "1", "ADR");
            big_parameter(g, content(2, 46), "KNOB 1 SENDS", "CC 74", 74.0f / 120.0f);
            detail(g, "BRIGHTNESS");
        }},
        {"midi-knob", "KNOB 1 TOUCHED: ITS VALUE", [](juce::Graphics& g) {
            rail(g, "KNOB", "1", "JUNO");
            big_parameter(g, content(2, 46), "CC 74 BRIGHTNESS", "64", 64.0f / 127.0f);
            detail(g, "SHIFT: ADDRESS");
        }},
        {"midi-express", "EXPRESS: BEND, PAD PRESSURE OUT", [](juce::Graphics& g) {
            midi_page(g, "6/9", "EXPRESS", {"BEND", "PRES"}, {"0", "POLY"}, "PRESSURE: POLY AT");
        }},
        {"midi-pads", "PADS: DRUMS, THE LAST PAD'S NOTE", [](juce::Graphics& g) {
            midi_page(g, "7/9", "PADS  A02 SNARE", {"LAYT", "NOTE"}, {"DRUM", "38"}, "GENERAL MIDI");
        }},
        {"midi-audio", "AUDIO: THE JUNO BACK ON LINE IN", [](juce::Graphics& g) {
            midi_page(g, "8/9", "AUDIO  LINE IN", {"IN", "MON", "LVL"}, {"L+R", "ON", "-6"}, "STRIP, FX, SENDS");
        }},
        {"midi-sysex", "SYSEX: A .SYX, KNOB 3 UP SENDS", [](juce::Graphics& g) {
            midi_page(g, "9/9", "SYSEX  3 OF 7", {"FILE", "LOAD", "SEND"}, {"JUNO", "ON", "UP"}, "JUNO106.SYX", "42%");
        }},
        {"midi-sounds", "PUSH ON THE SOUND: SOUNDS, LOAD AS IT TURNS", [](juce::Graphics& g) {
            rail(g, "LOAD", "3", "SND");
            list(g, content(0, 50), {"MIDI OUT", "* JUNO PAD", "JUNO BRASS", "MS20 BASS", "TR-8S KIT"}, 1, 1);
            detail(g, "ALL", "SHIFT: OPT");
        }},
        {"midi-sound-options", "SHIFT IN SOUNDS: SAVE, STAR, SHOW", [](juce::Graphics& g) {
            rail(g, "OPT", "3", "SND");
            list(g, content(0, height), {"SAVE SOUND", "UNSTAR", "SHOW        ALL"}, 0, 1);
        }},
        {"midi-config", "SHIFT + STEP 2, MIDI: MIDI CONFIG", [](juce::Graphics& g) {
            rail(g, "MIDI", "", "IN");
            list(g, content(0, 50), {"IN CHANNEL   ALL", "OUT CHANNEL    1", "PAD IN        ON", "PAD OUT    NEVER", "SYNC IN    CLOCK"}, 0, 1);
            detail(g, "EXT CLOCK", "121.8");
        }},
        {"midi-config-edit", "PUSH: THE ROW'S VALUE, THE WHEEL SETS IT", [](juce::Graphics& g) {
            rail(g, "MIDI", "", "OUT");
            list(g, content(0, 50), {"SYNC IN    CLOCK", "SYNC OUT   [MTC]", "THRU         OFF", "PROG CHANGE  OFF", "REMOTE CH    OFF"}, 1, 1);
            detail(g, "MTC 25 FPS OUT");
        }},
        {"midi-config-remote", "REMOTE CCS; PANIC: PUSH", [](juce::Graphics& g) {
            rail(g, "MIDI", "", "");
            list(g, content(0, 50), {"REMOTE CH     16", "TRACK CC     102", "PATTERN CC   103", "SEQUENCE CC  104", "PANIC"}, 4, 1);
            detail(g, "PUSH: ALL OFF");
        }},
        {"track-options", "SHIFT + TRACK 3: MIDI IN AND OUT", [](juce::Graphics& g) {
            rail(g, "TRK", "3", "OPT");
            list(g, content(0, 50), {"COLOUR    VIOLET", "MIDI IN     AUTO", "MIDI OUT       2"}, 1, 1);
            detail(g, "AUTO: SELECTED");
        }},
        {"track-options-edit", "MIDI IN 5: CHANNEL 5 PLAYS TRACK 3", [](juce::Graphics& g) {
            rail(g, "TRK", "3", "OPT");
            list(g, content(0, 50), {"COLOUR    VIOLET", "MIDI IN     [5]", "MIDI OUT       2"}, 1, 1);
            detail(g, "CH 5: TRACK 3");
        }},
        {"tempo-ext", "TEMPO WHILE MIDI CLOCK LEADS", [](juce::Graphics& g) {
            rail(g, "TMPO", "", "4/4");
            move::draw_text_centred(g, content_x, 10, content_width, "121.8", 3);
            move::draw_text_centred(g, content_x, 38, content_width, "MIDI CLOCK IN");
            detail(g, "EXT", "3.2");
        }},
        {"midi-panic", "PANIC PUSHED: EVERY NOTE ENDS", [](juce::Graphics& g) {
            rail(g, "MIDI", "", "OUT");
            move::draw_text_centred(g, content_x, 12, content_width, "PANIC", 2);
            move::draw_text_centred(g, content_x, 32, content_width, "ALL NOTES OFF");
            move::draw_text_centred(g, content_x, 42, content_width, "16 CHANNELS");
            detail(g, "BACK: MIDI");
        }},
    };
}

// ---------------------------------------------------------------- lights

const uint8_t violet = track_colour(2).bright;

Panel midi_panel() {
    Panel p;
    p.tracks = {track_colour(0).dim, track_colour(1).dim, violet, track_colour(3).dim};
    p.button(Button::Shift, move::white_dim);
    p.button(Button::Back, 0);
    p.button(Button::Play, move::colour::green);
    p.button(Button::Rec, move::colour::dark_grey);
    p.button(Button::Sample, move::colour::dark_grey);
    for (auto b : {Button::Mute, Button::Copy, Button::Delete, Button::Undo, Button::Capture}) p.button(b, move::white_dim);
    p.steps[5] = move::colour::white; // the playhead
    return p;
}

std::vector<Lights> midi_lights() {
    std::vector<Lights> panels;
    {   // Keys: rows of fourths from C2, every C in the track's colour, the naturals dim, a held pad white.
        Panel p = midi_panel();
        for (unsigned pad = 0; pad < move::pad_count; ++pad) {
            int note = 36 + int(move::pad_row(pad)) * 5 + int(move::pad_column(pad));
            int in_octave = note % 12;
            bool natural = in_octave != 1 && in_octave != 3 && in_octave != 6 && in_octave != 8 && in_octave != 10;
            p.pads[pad] = in_octave == 0 ? violet : natural ? move::colour::dark_grey : move::colour::off;
        }
        p.pads[9] = move::colour::white;
        p.button(Button::Up, move::white_dim);
        p.button(Button::Down, move::white_dim);
        panels.push_back({"lights-midi-keys", "MIDI TRACK, KEYS: AS A SYNTH'S", p});
    }
    {   // Drums: 32 pads as a kit's, those with a General MIDI name in the track's colour, the last one white.
        Panel p = midi_panel();
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 8; ++column) {
                unsigned pad = row * 8 + column;
                p.pads[pad] = pad < 24 ? violet : move::colour::off;
            }
        p.pads[1] = move::colour::white;
        p.button(Button::Up, move::white_dim);
        p.button(Button::Down, 0);
        panels.push_back({"lights-midi-drums", "MIDI TRACK, DRUMS: A02 LAST, +/- 16", p});
    }
    {   // MIDI Config over Play: the pads still play the track; Back lit (out to Setup).
        Panel p = midi_panel();
        for (unsigned pad = 0; pad < move::pad_count; ++pad) {
            int note = 36 + int(move::pad_row(pad)) * 5 + int(move::pad_column(pad));
            p.pads[pad] = note % 12 == 0 ? violet : move::colour::off;
        }
        p.button(Button::Back, move::white_dim);
        p.button(Button::Play, move::colour::green);
        panels.push_back({"lights-midi-config", "MIDI CONFIG: BACK LIT, PADS PLAY", p});
    }
    {   // Shift + a track button: the track's options, Back lit.
        Panel p = midi_panel();
        p.tracks[2] = move::colour::white;
        p.button(Button::Shift, move::white_bright);
        p.button(Button::Back, move::white_dim);
        panels.push_back({"lights-track-options", "SHIFT + TRACK 3: ITS OPTIONS", p});
    }
    {   // Following MIDI clock: Play lit green while the clock in runs, the steps follow it.
        Panel p = midi_panel();
        p.steps = {};
        for (unsigned n = 0; n < move::step_count; ++n) p.steps[n] = n % 4 == 0 ? move::colour::dark_grey : move::colour::off;
        p.steps[8] = move::colour::white;
        panels.push_back({"lights-midi-clock", "CLOCK IN: PLAY AND STEPS FOLLOW", p});
    }
    return panels;
}

// The Move as Ableton's controls figure lays it out (as mockups_controls.cpp).
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

juce::Image midi_sheet(const std::vector<Screen>& screens, const std::vector<Lights>& panels) {
    constexpr int scale = 4, gap = 24, caption = 30, top = 92;
    int screen_w = width * scale, screen_h = height * scale;
    int rows = int(screens.size() + 1) / 2, panel_rows = int(panels.size() + 1) / 2;
    int panels_top = top + rows * (screen_h + caption + gap);
    juce::Image image(juce::Image::RGB, gap + 2 * (screen_w + gap), panels_top + panel_rows * (screen_h + caption + gap), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    g.setColour(juce::Colour(0xffd8f0ff));
    move::draw_text(g, gap, 20, "MELODEE ON MOVE: MIDI AND SYNC", 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, "ONE PORT: USB-A AND USB-C ON CABLE 2. DOCS/UI.MD", 2);
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

bool write_midi_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = midi_screens();
    auto panels = midi_lights();
    auto each = folder.getChildFile("midi");
    each.createDirectory();
    bool ok = true;
    for (const auto& screen : screens)
        ok = mockup::write_png(mockup::screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = mockup::write_png(panel_image(panel.panel, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    auto file = folder.getChildFile("midi.png");
    ok = mockup::write_png(midi_sheet(screens, panels), file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace ui
