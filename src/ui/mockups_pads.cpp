// SPDX-License-Identifier: AGPL-3.0-only
// The pad layouts (docs/UI.md, "Pads"; docs/MOVE_CONTROLS.md, "Pads"), as
// mockups: pads.png. Kerem asked (2026-11-20) for a second layout beside the
// Move's rows of eight: two MPC 4x4 banks side by side, the left 1-16 from
// its bottom left, the right 17-32. Every pad job follows the one chosen:
// the kit's pads and banks, Chop's slices, 16 Pitches, the notes and scales,
// the typing keyboard and the lights. The screens are the setting at the
// real 128 x 64; the panels are the pads lit, each pad labelled with what it
// plays, the Rows layout on the left and Banks on the right. Two readings of
// scales in Banks are drawn for Kerem to pick: (a) the 32 pads up the scale
// in pad order, (b) rows of fourths inside each bank.
#include "ui/mockups.h"
#include "app/pad_layout.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/lights.h"
#include "ui/track_style.h"
#include "ui/widgets.h"
#include <cctype>
#include <cmath>
#include <iostream>

namespace ui {
namespace {

using app::PadLayout;
using mockup::Screen;
const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// ---------------------------------------------------------------- the pads, labelled

// The pads as Lights would send them, each with what it plays written on it.
struct PadPanel {
    std::array<uint8_t, move::pad_count> pads {};        // palette, physical: bottom left first
    std::array<std::string, move::pad_count> labels {};  // physical
    std::array<bool, move::pad_count> ring {};            // pulsing or the one in hand
    std::array<uint8_t, move::step_count> steps {};
    bool banks = false;                                   // outline the two 4x4 banks
};
struct Panel { const char* name; const char* caption; PadPanel panel; };

// Sets a pad by its number in `layout` (logical, 0-31).
void put(PadPanel& p, PadLayout layout, unsigned logical, uint8_t colour, std::string label) {
    auto physical = app::physical_pad(uint8_t(logical), layout);
    p.pads[physical] = colour;
    p.labels[physical] = std::move(label);
}

juce::Image pads_image(const PadPanel& p, int w, int h) {
    juce::Image image(juce::Image::RGB, w, h, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff2a2d33));
    auto rgb = [](uint8_t index) {
        if (!index) return juce::Colour(0xff121316);
        auto c = move::palette_rgb(index);
        return juce::Colour(c.r, c.g, c.b);
    };
    const int margin = 14, pitch_x = (w - 2 * margin) / 8, pitch_y = 50, pad_w = pitch_x - 8, pad_h = pitch_y - 8, top = 12;
    if (p.banks) { // the two banks, framed
        g.setColour(juce::Colour(0xff8a96a3));
        for (int bank = 0; bank < 2; ++bank)
            g.drawRoundedRectangle(float(margin - 4 + bank * 4 * pitch_x), float(top - 4), float(4 * pitch_x), float(4 * pitch_y), 6, 2);
    }
    for (unsigned pad = 0; pad < move::pad_count; ++pad) {
        int x = margin + int(move::pad_column(pad)) * pitch_x, y = top + int(3 - move::pad_row(pad)) * pitch_y;
        auto fill = rgb(p.pads[pad]);
        g.setColour(fill);
        g.fillRoundedRectangle(float(x), float(y), float(pad_w), float(pad_h), 4);
        if (p.ring[pad]) {
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(float(x - 3), float(y - 3), float(pad_w + 6), float(pad_h + 6), 5, 2);
        }
        const auto& label = p.labels[pad];
        if (label.empty()) continue;
        g.setColour(p.pads[pad] && fill.getPerceivedBrightness() > 0.5f ? juce::Colours::black : juce::Colour(0xffb8c2cc));
        int scale = move::text_width(label, 2) <= pad_w - 4 ? 2 : 1;
        move::draw_text(g, x + (pad_w - move::text_width(label, scale)) / 2, y + (pad_h - 7 * scale) / 2, label, scale);
    }
    int steps_y = top + 4 * pitch_y + 4, step = (8 * pitch_x) / 16;
    for (unsigned n = 0; n < move::step_count; ++n) {
        g.setColour(rgb(p.steps[n]));
        g.fillRect(margin + int(n) * step, steps_y, step - 6, 10);
    }
    return image;
}

// ---------------------------------------------------------------- what the pads play

constexpr uint8_t kit_colour = track_colour(0).bright, synth_colour = track_colour(1).bright;
constexpr int base_note = 48; // C2, the bottom-left pad at octave 0 (Ableton's names)
constexpr int major[7] {0, 2, 4, 5, 7, 9, 11};

std::string note_name(int note) {
    static const char* const names[12] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(names[note % 12]) + std::to_string(note / 12 - 2);
}
bool in_c_major(int note) {
    for (int degree : major) if (note % 12 == degree) return true;
    return false;
}
int c_major(int index) { return base_note + 12 * (index / 7) + major[index % 7]; } // the index-th note of C major from C2

// A note's light, as the Move lights its notes: roots in the track's
// colour, the key's other notes light grey, the rest dark.
uint8_t note_light(int note) { return note % 12 == 0 ? synth_colour : in_c_major(note) ? move::colour::light_grey : move::colour::off; }

// The notes of a reading: each pad's note from where it sits.
enum class Reading { RowsChromatic, RowsInKey, RunChromatic, RunInKey, BankChromatic, BankInKey };
int note_of(Reading reading, unsigned physical) {
    int column = int(move::pad_column(physical)), row = int(move::pad_row(physical));
    int logical = app::logical_pad(uint8_t(physical), PadLayout::Banks);
    int in_bank = logical % 16, bank = logical / 16, bank_column = in_bank % 4, bank_row = in_bank / 4;
    switch (reading) {
    case Reading::RowsChromatic: return base_note + column + 5 * row;      // today: right a semitone, up a fourth
    case Reading::RowsInKey: return c_major(column + 3 * row);             // In-Key in 4ths: up a row, 3 notes of C major
    case Reading::RunChromatic: return base_note + logical;               // (a) 1-32 up the semitones
    case Reading::RunInKey: return c_major(logical);                      // (a) 1-32 up the key
    case Reading::BankChromatic: return base_note + 12 * bank + bank_column + 5 * bank_row; // (b) a fourth a row, the right bank an octave up
    case Reading::BankInKey: return c_major(7 * bank + bank_column + 3 * bank_row);
    }
    return base_note;
}
PadPanel notes_panel(Reading reading, bool banks) {
    PadPanel p;
    p.banks = banks;
    for (unsigned pad = 0; pad < move::pad_count; ++pad) {
        int note = note_of(reading, pad);
        p.pads[pad] = note_light(note);
        p.labels[pad] = note_name(note);
    }
    return p;
}

// The example kit of rail.png: A01-A09 and B01-B04 hold samples, A05 the
// last pad; banks A and B in view.
std::string kit_pad(unsigned n) { return std::string(1, char('A' + n / 16)) + (n % 16 + 1 < 10 ? "0" : "") + std::to_string(n % 16 + 1); }
bool kit_used(unsigned n) { return n < 9 || (n >= 16 && n < 20); }
PadPanel kit_panel(PadLayout layout) {
    PadPanel p;
    p.banks = layout == PadLayout::Banks;
    for (unsigned n = 0; n < 32; ++n)
        put(p, layout, n, n == 4 ? move::colour::white : kit_used(n) ? kit_colour : move::colour::off, kit_pad(n));
    return p;
}

PadPanel chop_panel(PadLayout layout) { // 20 slices, slice 7 selected
    PadPanel p;
    p.banks = layout == PadLayout::Banks;
    for (unsigned n = 0; n < 32; ++n)
        put(p, layout, n, n >= 20 ? move::colour::off : n == 6 ? move::colour::white : n % 2 ? move::colour::brown_yellow : move::colour::orange,
            n < 20 ? std::to_string(n + 1) : "");
    return p;
}

// 16 Pitches: the left 4x4 one bank of the kit as the MPC lays it, the
// right 4x4 A05 at 16 pitches of C major, in both layouts (controls.png).
PadPanel pitches_panel(bool banks) {
    PadPanel p;
    p.banks = banks;
    for (unsigned n = 0; n < 16; ++n) {
        put(p, PadLayout::Banks, n, n == 4 ? move::colour::white : kit_used(n) ? kit_colour : move::colour::off, kit_pad(n));
        int semitones = 12 * int(n / 7) + major[n % 7];
        put(p, PadLayout::Banks, 16 + n, n % 7 == 0 ? kit_colour : move::colour::light_grey, "+" + std::to_string(semitones));
    }
    p.steps[7] = move::colour::white;
    return p;
}

// Typing: the keys in reading order, top left first: across the eight
// columns in Rows, through the left bank then the right one in Banks.
constexpr const char* keys = "abcdefghijklmnopqrstuvwxyz -.'&\b";
char key_at(unsigned physical, PadLayout layout) {
    if (layout == PadLayout::Rows) return keys[(3 - move::pad_row(physical)) * 8 + move::pad_column(physical)];
    unsigned logical = app::logical_pad(uint8_t(physical), layout), bank = logical / 16, in_bank = logical % 16;
    return keys[bank * 16 + (3 - in_bank / 4) * 4 + in_bank % 4];
}
PadPanel typing_panel(PadLayout layout) {
    PadPanel p;
    p.banks = layout == PadLayout::Banks;
    for (unsigned pad = 0; pad < move::pad_count; ++pad) {
        char c = key_at(pad, layout);
        p.pads[pad] = c == '\b' ? move::colour::red : c == ' ' ? move::colour::azure : std::isalpha(static_cast<unsigned char>(c)) ? move::colour::light_grey : move::colour::dark_grey;
        p.labels[pad] = c == '\b' ? "DEL" : c == ' ' ? "SPC" : std::string(1, char(std::toupper(static_cast<unsigned char>(c))));
    }
    for (unsigned n = 0; n < 10; ++n) p.steps[n] = move::colour::light_grey;
    return p;
}

// The setting, while LAYOUT is turned: each pad its number, 1 white.
PadPanel setting_panel(PadLayout layout) {
    PadPanel p;
    p.banks = layout == PadLayout::Banks;
    for (unsigned n = 0; n < 32; ++n) {
        uint8_t colour = layout == PadLayout::Banks ? (n < 16 ? move::colour::azure : move::colour::violet)
                                                    : (n / 8) % 2 ? move::colour::violet : move::colour::azure;
        put(p, layout, n, n == 0 || (layout == PadLayout::Banks && n == 16) ? move::colour::white : colour, std::to_string(n + 1));
    }
    return p;
}

std::vector<Panel> pads_panels() {
    return {
        {"setting-rows", "ROWS: THE MOVE'S, 8 A ROW", setting_panel(PadLayout::Rows)},
        {"setting-banks", "BANKS: TWO MPC 4X4, 1-16 AND 17-32", setting_panel(PadLayout::Banks)},
        {"kit-rows", "ROWS: KIT, A BOTTOM, B TOP", kit_panel(PadLayout::Rows)},
        {"kit-banks", "BANKS: KIT, A LEFT, B RIGHT", kit_panel(PadLayout::Banks)},
        {"chop-rows", "ROWS: CHOP, 20 SLICES, 7 PICKED", chop_panel(PadLayout::Rows)},
        {"chop-banks", "BANKS: CHOP, 17-20 ON THE RIGHT", chop_panel(PadLayout::Banks)},
        {"pitches-rows", "ROWS: 16 PITCHES, AS APPROVED", pitches_panel(false)},
        {"pitches-banks", "BANKS: 16 PITCHES, THE TWO BANKS", pitches_panel(true)},
        {"notes-rows-key", "ROWS: C MAJOR IN 4THS, C2 BOTTOM LEFT", notes_panel(Reading::RowsInKey, false)},
        {"notes-a-key", "BANKS (A): C MAJOR, 1-32 UP THE KEY", notes_panel(Reading::RunInKey, true)},
        {"notes-rows-chromatic", "ROWS: CHROMATIC IN 4THS (TODAY)", notes_panel(Reading::RowsChromatic, false)},
        {"notes-a-chromatic", "BANKS (A): CHROMATIC, 1-32 UP", notes_panel(Reading::RunChromatic, true)},
        {"notes-b-key", "BANKS (B): C MAJOR, 4THS IN EACH BANK", notes_panel(Reading::BankInKey, true)},
        {"notes-b-chromatic", "BANKS (B): CHROMATIC, E, A, D SKIPPED", notes_panel(Reading::BankChromatic, true)},
        {"typing-rows", "ROWS: TYPING, A-H ACROSS THE TOP", typing_panel(PadLayout::Rows)},
        {"typing-banks", "BANKS: TYPING, A-P LEFT, Q-DEL RIGHT", typing_panel(PadLayout::Banks)},
    };
}

// ---------------------------------------------------------------- the setting, on the screen

// The numbering in `layout` on the subject, as the pads lie: where each run
// of pads starts, its number inverted (1, 9, 17, 25 in Rows; 1, 5, 9, 13 and
// 17, 21, 25, 29 in Banks), the other pads small squares, the banks apart.
void numbering(juce::Graphics& g, PadLayout layout) {
    const int cell = 12, gap = layout == PadLayout::Banks ? 6 : 0;
    const int left = content_x + (content_width - 8 * cell - gap) / 2;
    for (unsigned pad = 0; pad < move::pad_count; ++pad) {
        int column = int(move::pad_column(pad)), row = int(move::pad_row(pad));
        int x = left + column * cell + (column >= 4 ? gap : 0), y = 2 + (3 - row) * cell;
        unsigned number = app::logical_pad(uint8_t(pad), layout) + 1u;
        bool starts = layout == PadLayout::Banks ? column % 4 == 0 : column == 0;
        g.setColour(on);
        if (!starts) { g.drawRect(x + 3, y + 3, cell - 6, cell - 6); continue; }
        g.fillRect(x - 1, y, cell + 1, cell - 1);
        g.setColour(off);
        auto text = std::to_string(number);
        move::draw_text(g, x + (cell - move::text_width(text)) / 2, y + 2, text);
        g.setColour(on);
    }
}

std::string row(const std::string& name, const std::string& value) { // name left, value flush right, 16 characters
    return name + std::string(name.size() + value.size() < 16 ? 16 - name.size() - value.size() : 1, ' ') + value;
}

std::vector<float> loop_peaks() {
    std::vector<float> peaks(size_t(content_width), 0.04f);
    for (int column = 0; column < content_width; ++column)
        for (int at : {0, 25, 51, 76})
            if (column >= at) peaks[size_t(column)] += 0.9f * std::exp(-(column - at) / 8.0f);
    for (auto& peak : peaks) peak = std::min(peak, 1.0f);
    return peaks;
}

std::vector<Screen> pads_screens() {
    return {
        {"setup-pads", "SETUP > PADS: LAYOUT, PUSH TO EDIT", [](juce::Graphics& g) {
            rail(g, "PADS", "", "SETP");
            list(g, content(0, height), {row("LAYOUT", "[BANKS]"), row("RESPONSE", "MED"), row("PRESSURE", "ON")}, 0, 1);
        }},
        {"setup-message", "PUSHED: SET, SAID, THE PADS CHANGE AT ONCE", [](juce::Graphics& g) {
            rail(g, "PADS", "", "SETP");
            list(g, content(0, height), {row("LAYOUT", "BANKS"), row("RESPONSE", "MED"), row("PRESSURE", "ON")}, 0, 1);
            sheet(g, {content_x, detail_y - 2, content_width, 10});
            move::draw_text_centred(g, content_x, detail_y, content_width, "PADS: MPC BANKS");
        }},
        {"layout-rows", "EDITING LAYOUT: MOVE ROWS, THE NUMBERS", [](juce::Graphics& g) {
            rail(g, "PADS", "", "SETP");
            numbering(g, PadLayout::Rows);
            detail(g, "MOVE ROWS", "8 A ROW");
        }},
        {"layout-banks", "EDITING LAYOUT: MPC BANKS, THE NUMBERS", [](juce::Graphics& g) {
            rail(g, "PADS", "", "SETP");
            numbering(g, PadLayout::Banks);
            detail(g, "MPC BANKS", "2 X 4X4");
        }},
        {"shortcut", "IF A SHORTCUT: SHIFT + LOOP FLIPS IT", [](juce::Graphics& g) {
            rail(g, "KIT", "1", "A05");
            waveform(g, content(header_height + 1, subject_height - header_height - 1), loop_peaks());
            sheet(g, {content_x, detail_y - 2, content_width, 10});
            move::draw_text_centred(g, content_x, detail_y, content_width, "PADS: MPC BANKS");
        }},
        {"chop-banks", "CHOP IN BANKS: THE SCREEN AS BEFORE", [](juce::Graphics& g) {
            rail(g, "SLC", "07", "/20");
            auto area = content(0, subject_height);
            auto peaks = loop_peaks();
            waveform(g, area, peaks, 30, 34);
            std::vector<int> columns;
            for (int n = 1; n < 20; ++n) columns.push_back(n * content_width / 20);
            cuts(g, area, columns, 5);
            detail(g, "BEATS 1/16", "94");
        }},
    };
}

// ---------------------------------------------------------------- the sheet

juce::Image pads_sheet(const std::vector<Screen>& screens, const std::vector<Panel>& panels) {
    constexpr int scale = 4, gap = 24, caption = 30, top = 120;
    int screen_w = width * scale, screen_h = height * scale;
    int rows = int(screens.size() + 1) / 2, panel_rows = int(panels.size() + 1) / 2;
    int panels_top = top + rows * (screen_h + caption + gap);
    juce::Image image(juce::Image::RGB, gap + 2 * (screen_w + gap), panels_top + panel_rows * (screen_h + caption + gap), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    g.setColour(juce::Colour(0xffd8f0ff));
    move::draw_text(g, gap, 20, "MELODEE ON MOVE: PADS", 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, "MOVE ROWS OR TWO MPC BANKS; EVERY PAD JOB FOLLOWS. DOCS/UI.MD", 2);
    move::draw_text(g, gap, 84, "LEFT: ROWS. RIGHT: BANKS. SCALES IN BANKS: (A) RECOMMENDED, OR (B)", 2);
    auto place = [&](size_t n, int from) { return juce::Point<int>(gap + int(n % 2) * (screen_w + gap), from + int(n / 2) * (screen_h + caption + gap)); };
    for (size_t n = 0; n < screens.size(); ++n) {
        auto at = place(n, top);
        g.drawImageAt(mockup::screen_image(screens[n].draw, scale), at.x, at.y);
        g.setColour(juce::Colour(0xff8a96a3));
        move::draw_text(g, at.x, at.y + screen_h + 8, screens[n].caption, 2);
    }
    for (size_t n = 0; n < panels.size(); ++n) {
        auto at = place(n, panels_top);
        g.drawImageAt(pads_image(panels[n].panel, screen_w, screen_h), at.x, at.y);
        g.setColour(juce::Colour(0xff8a96a3));
        move::draw_text(g, at.x, at.y + screen_h + 8, panels[n].caption, 2);
    }
    return image;
}

} // namespace

bool write_pads_mockups(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto screens = pads_screens();
    auto panels = pads_panels();
    auto each = folder.getChildFile("pads");
    each.createDirectory();
    bool ok = true;
    for (const auto& screen : screens)
        ok = mockup::write_png(mockup::screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
    for (const auto& panel : panels)
        ok = mockup::write_png(pads_image(panel.panel, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
    auto file = folder.getChildFile("pads.png");
    ok = mockup::write_png(pads_sheet(screens, panels), file) && ok;
    std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace ui
