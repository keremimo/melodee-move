// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/canvas_page.h"
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// A Schwung module's pages, shown by Schwung's own screen code (Kerem,
// 2026-11-21): from the push into a module until Back out, every screen is
// what Schwung's shadow UI draws, its code run from the Schwung on the Move
// (shadow/shadow_ui_param_pages.mjs, shared/...). Melodee gives that code
// what Schwung's shadow UI gives it: its screen (schwung_display.h), the
// glue shadow_ui.js provides (src/app/schwung_pages/host.js, carried in the
// binary and run as a CanvasPage), and the Move's controls as the shim
// forwards them. screens/module_pages.cpp shows the screen and nothing else.
//
// Called as a CanvasPage is: under the app's lock, from the screen or MIDI
// thread, never the audio thread.
namespace app {

class SchwungPages {
public:
    // Where Schwung's shared code is: MELODEE_SCHWUNG_ROOT's shared/, else the
    // Move's /data/UserData/schwung/shared. Empty when it is not there.
    static std::string shared_folder();

    struct Hooks {
        int slot = 0;                    // Melodee's track, Schwung's slot: its pages say "S2 > ..."
        std::string component = "synth"; // Schwung's component: "synth", "fx1" ...
        std::string module_id, module_name, module_abbrev, module_dir;
        std::function<bool()> shift;     // Shift held or locked
        std::function<bool()> reader;    // the screen reader on (Schwung's list layout then)
        std::function<void(const std::string&)> say; // Schwung's screen reader's words
        std::function<std::string()> trailing;          // the pages after the module's (JSON, Schwung's trailingMenus shape)
        std::function<void(const std::string&)> action; // one of their rows pushed
        // Schwung's keyboard took the pads (its pad typing) or gave them
        // back: while they are its keys none reaches the module (Melodee's
        // audio thread plays none).
        std::function<void(bool)> pad_block;
        std::string page_states;         // the drawn pages' kept state (JSON), from the project
    };
    // `host` reaches the module (CanvasPage's). Null with `error` set when
    // it cannot start.
    static std::unique_ptr<SchwungPages> load(CanvasPage::Host host, Hooks hooks, std::string& error);

    // Entered from the Device View (or back from a sheet that changed the
    // module): Schwung's pages start over on the next frame, as Schwung's
    // host makes a new controller on every entry, landing on `page` by name
    // when given (its door open when `open`).
    void enter(const std::string& page = {}, bool open = false);
    // A frame: Schwung's ticks since the last, then its screen, all of it.
    void frame(juce::Graphics& g);
    // A Move control, raw, in Schwung's numbers, as the shim forwards it.
    void midi(uint8_t status, uint8_t number, uint8_t value);

    // Back at the top of the pages: out to the Device View. Cleared by enter().
    bool left() const { return exited; }
    // What is shown, for the status line and the tests.
    struct Status {
        std::string view, page, kind; // Schwung's view ("parampages" ...), the page's name and kind
        bool canvas = false, picker = false;
        bool typing = false;          // Schwung's keyboard up (over the view)
        bool warning = false;         // its warning card up
        std::string text;             // what the keyboard holds
        std::string title;            // a list screen's (option list, file browser, file list)
        int index = -1;               // and the row it is on
        std::vector<std::pair<std::string, std::string>> footer; // ("JOG", "PAGE"), ("CLK", "MENU") ...
        // A menu page's rows: label, value; a list screen's: label, "*" on
        // the one set (the browser: the row's kind).
        std::vector<std::pair<std::string, std::string>> rows;
    };
    const Status& status() const { return shown; }
    // The knob rings Schwung's pages lit: its colour each, -1 where it left
    // the ring to Melodee.
    const std::array<int, 8>& knob_rings() const { return rings; }
    // While Schwung's keyboard has the pads: the pads go to it alone (their
    // notes and pressure, raw), lit in its colours, -1 where it lit none;
    // pad n is note 68 + n, as Schwung numbers them.
    bool pad_blocked() const { return blocked; }
    const std::array<int, 32>& pad_colours() const { return pads; }
    // The screen gone with the pads still the keyboard's: given back.
    void release_pads();
    // The drawn pages' state, to keep with the project (JSON; empty: none).
    const std::string& page_states() const { return states; }
    const SchwungDisplay& screen() const { return page->screen(); }

private:
    SchwungPages() = default;
    void take_status(const std::string& json);
    std::unique_ptr<CanvasPage> page;
    Hooks hooks;
    std::string entry; // host.js reads it once as "melodee:entry"
    std::string states;
    Status shown;
    std::array<int, 8> rings {-1, -1, -1, -1, -1, -1, -1, -1};
    std::array<int, 32> pads {};
    bool blocked = false;
    bool exited = false;
};

} // namespace app
