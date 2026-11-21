// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// A Schwung module's own page: the script a `type: "canvas"` parameter
// names (its canvas_script), run in QuickJS as Schwung's shadow UI runs it
// (Schwung's docs/CANVAS_PAGES.md). Radio Garden's station browser and
// Webstream's search are such pages.
//
// The script sets globalThis.canvas_overlay to an object of hooks, all
// optional:
//
//   drawPage(ctx, info)   paint the page; info, as Schwung's shadow UI hands
//                         it: {key, values, base, keys, touched, preset,
//                         nowMs, width, height, state}. key: the canvas
//                         parameter's (one script may draw several pages and
//                         tell them apart by it); values: the page's knobs'
//                         and the parameter's extra_keys'; width and height
//                         (also ctx.width, ctx.height): the band it owns
//   onMidi(ctx, msg)      a jogwheel turn or push while the page is entered;
//                         msg: {data: [status, number, value]}
//   handleBack(ctx)       true: "I went up a level, keep me"; anything else:
//                         "I am at my top", and the page is left
//   onTextEntry(ctx, p)   the keyboard's answer: {text, cancelled}
//
// and reaches the host through ctx: fillRect, setPixel, line, print and
// textWidth on the 128 x 64 screen (Schwung's own font when the Move has
// it, /data/UserData/schwung/host/font.png); getParam and setParam on the
// module; state, an object kept for the page; openTextEntry({title,
// initial}); openFileInTool(path). QuickJS's std and os modules are there
// to import, as under Schwung (the pages fetch with wget, read and write
// files).
//
// A hook that throws is reported and not called again, as Schwung does. One
// page at a time is called, under the app's lock, from whichever thread
// holds it (the screen's, the MIDI thread's).
namespace app {

class CanvasPage {
public:
    struct Host { // called from inside the hooks, under the app's lock
        std::function<std::string(const std::string& key)> get;
        std::function<void(const std::string& key, const std::string& value)> set;
        std::function<void(const std::string& title, const std::string& initial)> text_entry;
        std::function<void(const std::string& path)> open_file;
        // After each gesture the page was given (a turn, a push, Back, the
        // keyboard's answer): what it keeps in ctx.state may have changed.
        std::function<void()> changed;
    };
    // Reads and starts `script`. Null with `error` set when it cannot.
    static std::unique_ptr<CanvasPage> load(const std::string& script, Host host, std::string& error);
    // The same from a script held in memory, `script` its name (where its
    // relative imports start from).
    static std::unique_ptr<CanvasPage> load_source(const std::string& source, const std::string& script, Host host, std::string& error);
    ~CanvasPage();

    // What a draw is told besides the values: which page (a canvas
    // parameter's key), the knobs it carries, and the band it may paint,
    // from the top left (Melodee keeps the rows below it for its own).
    struct Frame {
        std::string key;
        std::vector<std::string> keys;
        int x = 0, y = 0; // where the band starts (info.x, info.y)
        int width = CanvasPage::width, height = CanvasPage::height;
    };
    // The page on the screen, `values` its knobs' and extra_keys' values.
    void draw(juce::Graphics& g, const std::map<std::string, std::string>& values, const Frame& frame);
    void draw(juce::Graphics& g, const std::map<std::string, std::string>& values) { draw(g, values, Frame {}); }
    void midi(uint8_t status, uint8_t number, uint8_t value);
    bool back();
    void text_entered(const std::string& text, bool cancelled);

    // What the page keeps in ctx.state (a provider picked, where its menus
    // are), as JSON, to save with the module; empty when there is nothing
    // or it cannot be written as JSON. restore_state() puts a saved one's
    // properties back onto ctx.state (the same object the page holds), as
    // the page left them; anything not JSON is ignored.
    std::string saved_state() const;
    void restore_state(const std::string& json);

    // The screen as the last draw left it, for tests.
    bool pixel(int x, int y) const { return x >= 0 && x < width && y >= 0 && y < height && screen[size_t(y * width + x)]; }

    static constexpr int width = 128, height = 64;

private:
    struct Engine;
    CanvasPage() = default;
    std::unique_ptr<Engine> engine;
    Host host;
    std::array<uint8_t, width * height> screen {};
    friend struct CanvasBindings;
};

} // namespace app
