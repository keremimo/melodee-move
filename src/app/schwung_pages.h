// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/canvas_page.h"
#include <juce_graphics/juce_graphics.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// A Schwung module's pages and menus as Schwung's own knob-page code decides
// them (the Schwung installed beside Melodee: its shared/param_pages), for
// Melodee to draw in its own look. Schwung's page controller is the model:
// the level walk, visible_if, doors, the Sections picker, presets, Back one
// layer at a time, the enum peek, the footer's verbs; this is its view model
// (ModuleView) and the way the Move's controls reach it. The seam is
// src/app/schwung_pages/host.js, carried in the binary and run as a
// CanvasPage. Melodee's own look draws it: screens/module_pages.cpp.
//
// Called as a CanvasPage is: under the app's lock, from the screen or MIDI
// thread, never the audio thread.
namespace app {

// The page as data (host.js view()).
struct ModuleView {
    bool ready = false;       // a page has been published
    bool unsupported = false; // the Move's Schwung is older than describePage()
    bool unresolved = false;  // the module's contract could not be read: hold the last frame
    std::string kind;         // "knobs", "preset", "items", "menu"
    std::string page;         // the page's name
    int index = 0, count = 0;
    struct Header { std::string left, right; bool inverted = false; } header;
    struct Cell {
        bool empty = true; // a hole
        std::string key, name, short_name, value, header_value, widget;
        double normalized = -1; // 0-1 where it has a position, else -1
        int enum_index = -1, options = 0;
        bool read_only = false, turnable = false, divable = false, touched = false;
    };
    std::vector<Cell> cells; // the page's knobs, in knob order (eight at most)
    struct Row { std::string label, value; };
    struct List {
        bool shown = false, entered = false;
        int cursor = 0, current = -1;
        std::string title;
        std::vector<Row> rows;
    };
    List menu, items, picker, peek, enum_picker;
    struct Preset { bool shown = false, entered = false; std::string name; int index = 0, count = 0; } preset;
    struct Canvas { bool shown = false, enterable = false, entered = false, opened = false; std::string key; } canvas;
    std::vector<std::pair<std::string, std::string>> footer; // Schwung's footer: ("JOG", "PAGE"), ("CLK", "MENU") ...
    std::string notice;
    std::string hint_title;
    std::vector<std::string> hint;
    bool wheel = false;      // its menu, a list, a picker or an entered page: the wheel and Back are its
    bool exit = false;       // Back at its top: Melodee's to take
    bool live_press = false; // the module wants to know a pad was hit
};

class SchwungPages {
public:
    // Where Schwung's shared code is: MELODEE_SCHWUNG_SHARED, else the
    // Move's /data/UserData/schwung/shared. Empty when it is not there.
    static std::string shared_folder();

    struct Hooks {
        std::string module_dir;                           // its canvas scripts
        std::function<std::string()> trailing;            // the pages Melodee adds after the module's (JSON, Schwung's trailingMenus shape)
        std::function<void(const std::string&)> action;   // one of their entries pushed
        std::function<void(const std::string&)> say;      // a word for Melodee's message line
        std::string page_states;                          // the drawn pages' kept state (JSON), from the project
    };
    // `host` reaches the module (CanvasPage's). Null with `error` set when
    // it cannot start.
    static std::unique_ptr<SchwungPages> load(CanvasPage::Host host, Hooks hooks, std::string& error);

    // A frame: Schwung's ticks, the page published (view()), and a drawn
    // page painted into `band` when the page is one (the rest of `g` black).
    void frame(juce::Graphics& g, juce::Rectangle<int> band);
    void midi(uint8_t status, uint8_t number, uint8_t value); // a Move control, raw (Schwung's numbers)
    void text_entered(const std::string& text, bool cancelled);

    const ModuleView& view() const { return model; }
    // The drawn pages' state, to keep with the project (JSON; empty: none).
    const std::string& page_states() const { return states; }

    // Short forms of view(), for the screens' routing.
    bool wants_wheel() const { return model.wheel; }
    bool left() const { return model.exit; }
    int page_index() const { return model.index; }
    int page_count() const { return model.count; }
    const std::string& page_name() const { return model.page; }

private:
    SchwungPages() = default;
    void take(const std::string& view);
    std::unique_ptr<CanvasPage> page;
    ModuleView model;
    std::string states;
};

} // namespace app
