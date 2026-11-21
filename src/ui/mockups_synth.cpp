// SPDX-License-Identifier: AGPL-3.0-only
// A synth track on a Schwung module that draws a page of its own (Webstream,
// Radio Garden), as mockups: synth.png (Kerem, 2026-11-21). The Device View
// is Schwung's chain editor in Melodee's look: the wheel walks the items (the
// track's mix, ARP and CHORD, the module, its effects, the plus), the knobs
// are the item's (the module's box knobs). The push enters the module's
// pages, which behave as Schwung's and look like Melodee's: the wheel pages,
// Shift + the wheel jumps a section, the push is Schwung's click (no knob
// held: its Sections), a page it draws is one of them, entered with the push,
// and Back goes up one layer at a time, then out to the Device View.
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
    if (effect) return {{"WS"}, {"ARP", false, true}, {"CHRD", false, true}, {"----"}, {"CLDS"}, {"+"}};
    return {{"WS"}, {"ARP", false, true}, {"CHRD", false, true}, {"----"}, {"+"}};
}
constexpr int at_arp = 1, at_sound = 3, at_effect = 4;

// The strip over a module's pages: the page's name, inverted.
void strip(juce::Graphics& g, const char* name) {
    g.setColour(juce::Colours::white);
    g.fillRect(content_x, 0, content_width, header_height);
    g.setColour(juce::Colours::black);
    move::draw_text(g, content_x + 2, 1, name);
    g.setColour(juce::Colours::white);
}

// A page of the Device View: its title, what is on its right, the cells.
void page(juce::Graphics& g, const char* title, const char* right) {
    move::draw_text(g, content_x + 1, 11, title);
    if (right[0]) move::draw_text(g, width - move::text_width(right) - 1, 11, right);
}

// Webstream's Browse page on its pages: its own drawing (browser.js draws
// the host's list) in the band under the strip; `selected` -1 while it is a
// door not yet entered.
void browse(juce::Graphics& g, int selected, std::vector<std::pair<std::string, std::string>> footer) {
    rail(g, "WS", "2", "1/4");
    strip(g, "BROWSE");
    move::draw_text(g, content_x + 1, 11, "YOUTUBE: DILLA");
    list(g, {content_x, 20, content_width, 34}, {"J DILLA - DONUTS", "NUJABES - ARUA", "MADLIB - SHADES"}, selected, 1);
    hints(g, footer);
}

} // namespace

std::vector<Screen> synth_screens() {
    return {
        {"open", "TRACK 2 OPENS ON THE MODULE: ITS BOX'S KNOBS", [](juce::Graphics& g) {
            rail(g, "WS", "2", "SND");
            mix::item_row(g, items(false), at_sound);
            page(g, "WEBSTREAM", "C2");
            knob_cells(g, content(21, subject_height - 21), root_knobs, root_values);
            detail(g, "ARUARIAN", "");
        }},
        {"pages", "PUSH: INTO ITS PAGES, BROWSE FIRST", [](juce::Graphics& g) { browse(g, -1, {{"JOG", "PAGE"}, {"CLK", "ENTER"}}); }},
        {"entered", "PUSH AGAIN: THE PAGE HAS THE WHEEL", [](juce::Graphics& g) { browse(g, 1, {{"JOG", "SEL"}, {"BACK", "OUT"}}); }},
        {"knobs", "BACK, THEN TURN: ITS KNOB PAGE", [](juce::Graphics& g) {
            rail(g, "WS", "2", "2/4");
            strip(g, "WEBSTREAM");
            knob_cells(g, content(12, subject_height - 12), root_knobs, root_values);
            hints(g, {{"JOG", "PAGE"}, {"CLK", "MENU"}});
        }},
        {"sections", "PUSH, NO KNOB HELD: ITS SECTIONS", [](juce::Graphics& g) {
            rail(g, "WS", "2", "2/4");
            strip(g, "SECTIONS");
            list(g, content(11, subject_height - 11), {"BROWSE", "WEBSTREAM", "MY PRESETS", "MODULE"}, 1, 1);
            hints(g, {{"JOG", "SECT"}, {"CLK", "GO"}, {"BACK", "EXIT"}});
        }},
        {"module", "ITS LAST PAGE: SWAP, REMOVE, AUTO SAMPLE", [](juce::Graphics& g) {
            rail(g, "WS", "2", "4/4");
            strip(g, "MODULE");
            list(g, content(11, subject_height - 11), {"MODULE HELP", "SWAP MODULE", "REMOVE MODULE", "AUTO SAMPLE"}, 3, 1);
            hints(g, {{"JOG", "SEL"}, {"CLK", "OPEN"}, {"BACK", "OUT"}});
        }},
        {"out", "BACK, BACK: OUT TO THE DEVICE VIEW", [](juce::Graphics& g) {
            rail(g, "WS", "2", "SND");
            mix::item_row(g, items(false), at_sound);
            page(g, "WEBSTREAM", "C2");
            knob_cells(g, content(21, subject_height - 21), root_knobs, root_values);
            detail(g, "DONUTS", "");
        }},
        {"arp", "TURN LEFT: CHORD, THEN ARP, BEFORE IT", [](juce::Graphics& g) {
            rail(g, "WS", "2", "ARP");
            mix::item_row(g, items(false), at_arp);
            page(g, "ARP", "OFF");
            knob_cells(g, content(20, subject_height - 20), {"ON", "MODE", "RATE", "OCT", "GATE", "HOLD", "", ""},
                       {0, 0, 0.5f, 0, 0.5f, 0, 0, 0});
            detail(g, "UP 1/16", "X1");
        }},
        {"plus", "PAST THE MODULE, THE PLUS", [](juce::Graphics& g) {
            rail(g, "WS", "2", "FX1");
            mix::item_row(g, items(false), at_effect);
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
        {"effect", "FX1 IN: ITS BOX'S KNOBS; PUSH, ITS PAGES", [](juce::Graphics& g) {
            rail(g, "WS", "2", "FX1");
            mix::item_row(g, items(true), at_effect);
            page(g, "CLOUDSEED", "");
            knob_cells(g, content(20, height - 20), {"MIX", "SIZE", "DECAY", "PREDLY", "DIFFUSE", "MOD", "LOCUT", "HICUT"},
                       {0.35f, 0.7f, 0.6f, 0.1f, 0.8f, 0.3f, 0.2f, 0.7f});
        }},
        {"mix", "ALL THE WAY LEFT, THE TRACK: ITS MIX", [](juce::Graphics& g) {
            rail(g, "WS", "2", "MIX");
            mix::item_row(g, items(true), 0);
            move::draw_text(g, content_x + 1, 11, "SYNTH 2");
            knob_cells(g, {content_x, 20, content_width - 6, 34}, {"VOL", "PAN", ">A", ">B", ">C", ">D", "", ""},
                       {100 / 127.0f, 0.5f, 0.2f, 0, 0, 0, 0, 0});
            g.fillRect(width - 4, 26, 2, 28); // its meter, left and right
            g.fillRect(width - 1, 29, 1, 25);
            detail(g, "0.0DB", "");
        }},
    };
}

} // namespace mockup
} // namespace ui
