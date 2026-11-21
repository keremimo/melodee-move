// SPDX-License-Identifier: AGPL-3.0-only
// A synth track on a Schwung module that draws a page of its own (Webstream,
// Radio Garden), as mockups: synth.png. The Device View stays the Device
// View: the wheel walks the items (the track's mix, the module, its
// effects, the plus), the knobs follow the item. The module's own page is a
// door while the wheel is outside it: its knobs work, the push goes in, and
// only then is the wheel the page's, until Back. Shift + turn walks the
// pages (the module's, then ARP and CHORD), Shift + push opens the module's
// menu, whose Browse row goes straight in (Kerem, 2026-11-20).
#include "ui/mockups.h"
#include "move/font.h"
#include "ui/mix_widgets.h"
#include "ui/widgets.h"

namespace ui {
namespace mockup {
namespace {

using mix::Item;
// Webstream's root knobs as the Play screen draws a module's page (its short
// names, an action's bar empty, GAIN at 1.00 of 2).
const std::array<std::string, 8> root_knobs {"PAUSE", "REWIND", "FORWARD", "GAIN", "STOP", "RSTRT", "", ""};
const std::array<float, 8> root_values {0, 0, 0, 0.5f, 0, 0, 0, 0};

std::vector<Item> items(bool effect) {
    if (effect) return {{"WS"}, {"----"}, {"CLDS"}, {"+"}};
    return {{"WS"}, {"----"}, {"+"}};
}

// A page of the Device View: its title, what is on its right, the cells.
void page(juce::Graphics& g, const char* title, const char* right) {
    move::draw_text(g, content_x + 1, 11, title);
    if (right[0]) move::draw_text(g, width - move::text_width(right) - 1, 11, right);
}

// Webstream's Browse page, entered: its own drawing (browser.js draws the
// host's list), the host's strip under it.
void browse_entered(juce::Graphics& g, int selected) {
    move::draw_text(g, 1, 1, "YOUTUBE: DILLA");
    list(g, {0, 10, width, 36}, {"J DILLA - DONUTS", "NUJABES - ARUARIAN", "MADLIB - SHADES", "DILLA - LAST DONUT"}, selected, 1);
    g.setColour(juce::Colours::black);
    g.fillRect(0, 47, width, height - 47);
    g.setColour(juce::Colours::white);
    move::draw_text(g, 1, 56, "WS BROWSE");
    move::draw_text(g, width - move::text_width("BACK: OUT"), 56, "BACK: OUT");
}

} // namespace

std::vector<Screen> synth_screens() {
    return {
        {"open", "TRACK 2 OPENS: THE WHEEL ON WEBSTREAM", [](juce::Graphics& g) {
            rail(g, "WS", "2", "1/4");
            mix::item_row(g, items(false), 1);
            page(g, "BROWSE", "C2");
            knob_cells(g, content(21, subject_height - 21), root_knobs, root_values);
            detail(g, "ARUARIAN", "PUSH: IN");
        }},
        {"entered", "PUSH: IN. THE WHEEL IS THE PAGE'S", [](juce::Graphics& g) { browse_entered(g, 1); }},
        {"plus", "BACK, THEN TURN: PAST THE MODULE, THE PLUS", [](juce::Graphics& g) {
            rail(g, "WS", "2", "FX1");
            mix::item_row(g, items(false), 2);
            move::draw_text_centred(g, content_x, 16, content_width, "ADD AN", 2);
            move::draw_text_centred(g, content_x, 32, content_width, "EFFECT", 2);
            detail(g, "0/6", "PUSH: BROWSE");
        }},
        {"families", "PUSH: THE EFFECTS, BY FAMILY", [](juce::Graphics& g) {
            rail(g, "LOAD", "2", "FX1");
            list(g, content(0, height), {"SPACE >", "TIME >", "MOTION >", "DIRT >", "TONE >", "DYNAMICS >", "SCHWUNG >", "NONE"}, 6, 1);
        }},
        {"schwung", "SCHWUNG'S AUDIO FX: PUSH LOADS", [](juce::Graphics& g) {
            rail(g, "LOAD", "2", "FX1");
            list(g, content(0, 50), {"CLOUDSEED", "MVERB", "PSX VERB", "TAPESCAM", "TAPEDELAY", "DUCKER"}, 0, 1);
            detail(g, "", "PUSH: LOAD");
        }},
        {"effect", "FX1 IN: THE KNOBS ARE ITS OWN", [](juce::Graphics& g) {
            rail(g, "WS", "2", "FX1");
            mix::item_row(g, items(true), 2);
            page(g, "CLOUDSEED", "1/3");
            knob_cells(g, content(20, height - 20), {"MIX", "SIZE", "DECAY", "PREDLY", "DIFFUSE", "MOD", "LOCUT", "HICUT"},
                       {0.35f, 0.7f, 0.6f, 0.1f, 0.8f, 0.3f, 0.2f, 0.7f});
        }},
        {"mix", "TURN LEFT TO THE TRACK: ITS MIX", [](juce::Graphics& g) {
            rail(g, "WS", "2", "MIX");
            mix::item_row(g, items(true), 0);
            move::draw_text(g, content_x + 1, 11, "SYNTH 2");
            knob_cells(g, {content_x, 20, content_width - 6, 34}, {"VOL", "PAN", ">A", ">B", ">C", ">D", "", ""},
                       {100 / 127.0f, 0.5f, 0.2f, 0, 0, 0, 0, 0});
            g.fillRect(width - 4, 26, 2, 28); // its meter, left and right
            g.fillRect(width - 1, 29, 1, 25);
            detail(g, "0.0DB", "");
        }},
        {"knobs", "SHIFT + TURN: WEBSTREAM'S KNOB PAGE", [](juce::Graphics& g) {
            rail(g, "WS", "2", "2/4");
            mix::item_row(g, items(true), 1);
            page(g, "WEBSTREAM", "C2");
            knob_cells(g, content(21, subject_height - 21), root_knobs, root_values);
            detail(g, "DONUTS", "");
        }},
        {"arp", "SHIFT + TURN: ARP, THEN CHORD, LAST", [](juce::Graphics& g) {
            rail(g, "WS", "2", "3/4");
            mix::item_row(g, items(true), 1);
            page(g, "ARP", "OFF");
            knob_cells(g, content(20, subject_height - 20), {"ON", "MODE", "RATE", "OCT", "GATE", "HOLD", "", ""},
                       {0, 0, 0.5f, 0, 0.5f, 0, 0, 0});
            detail(g, "UP 1/16", "X1");
        }},
        {"menu", "SHIFT + PUSH: WEBSTREAM'S MENU", [](juce::Graphics& g) {
            rail(g, "WS", "2", "MENU");
            move::draw_text(g, content_x + 1, 1, "WEBSTREAM");
            list(g, content(10, height - 10),
                 {"BROWSE >", "PLAY/PAUSE  PUSH", "REWIND 15S  PUSH", "FORWARD 15S PUSH", "GAIN        1.00", "STOP        PUSH", "RESTART     PUSH"}, 0, 1);
        }},
        {"menu-browse", "ITS BROWSE ROW: STRAIGHT IN", [](juce::Graphics& g) { browse_entered(g, 0); }},
        {"out", "BACK: OUT, THE WHEEL WALKS THE ITEMS", [](juce::Graphics& g) {
            rail(g, "WS", "2", "1/4");
            mix::item_row(g, items(true), 1);
            page(g, "BROWSE", "C2");
            knob_cells(g, content(21, subject_height - 21), root_knobs, root_values);
            detail(g, "DONUTS", "PUSH: IN");
        }},
    };
}

} // namespace mockup
} // namespace ui
