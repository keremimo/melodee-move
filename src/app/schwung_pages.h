// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/canvas_page.h"
#include <juce_graphics/juce_graphics.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// A Schwung module's pages and menus drawn by Schwung's own knob-page code
// (the Schwung installed beside Melodee: its shared/param_pages), as its
// shadow UI draws them: the knob grid and its widgets, visible_if, the
// module's drawn pages (Schimera's mouth), its presets page and its menus.
// The seam is src/app/schwung_pages/host.js, carried in the binary and run
// as a CanvasPage.
//
// Called as a CanvasPage is: under the app's lock, from the screen or MIDI
// thread, never the audio thread.
namespace app {

class SchwungPages {
public:
    // Where Schwung's shared code is: MELODEE_SCHWUNG_SHARED, else the
    // Move's /data/UserData/schwung/shared. Empty when it is not there (a
    // Mac, the tests): Melodee draws a module's pages itself.
    static std::string shared_folder();

    // `host` reaches the module (CanvasPage's); `module_dir` is where its
    // canvas scripts are; `trailing` the pages Melodee adds after the
    // module's, as JSON ([{name, entries: [{label, action}]}]), and `action`
    // what is called when one of their entries is pushed. Null with `error`
    // set when it cannot start.
    static std::unique_ptr<SchwungPages> load(CanvasPage::Host host, const std::string& module_dir, const std::string& trailing,
                                              std::function<void(const std::string&)> action, std::string& error);

    void draw(juce::Graphics& g);                      // the whole screen
    void midi(uint8_t status, uint8_t number, uint8_t value); // a Move control, raw (Schwung's own numbers)
    void text_entered(const std::string& text, bool cancelled);

    // Where its controller is, as the last hook left it.
    bool wants_wheel() const { return wheel; } // its menu, a list or an entered page: the wheel is its
    bool left() const { return exit; }         // Back at its top: Melodee's to take
    int page_index() const { return index; }
    int page_count() const { return count; }
    const std::string& page_name() const { return name; }

private:
    SchwungPages() = default;
    void take(const std::string& described);
    std::unique_ptr<CanvasPage> page;
    bool wheel = false, exit = false;
    int index = 0, count = 0;
    std::string name;
};

} // namespace app
