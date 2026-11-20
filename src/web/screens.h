// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <string>

// The Move's screens for Melodee's web page (docs/UI.md, "The web page"),
// drawn with the UI kit in the Rail design. Each fills the whole screen,
// over whatever the app was showing; the web layer (web_layer.h) draws them
// on the Move and the mockups (src/ui/mockups_web.cpp) draw them as PNGs.
namespace web {

// Shift + Capture: where the page is. `usb` and `wifi` are the Move's
// addresses there, empty when it has none (no cable, Wi-Fi not joined).
struct Where {
    std::string usb, wifi;
    unsigned port = 0;
    unsigned paired = 0; // browsers on Wi-Fi that entered a code this run
};
void draw_where(juce::Graphics& g, const Where& where);

// A browser on Wi-Fi asked to pair: the code to type there, the seconds it
// stays good, the browser's address.
void draw_code(juce::Graphics& g, const std::string& code, unsigned seconds_left, const std::string& from);

// The code was typed: `from` may use the page until melodee-move stops.
void draw_paired(juce::Graphics& g, const std::string& from);

// No pairing: Back was pressed, the code ran out, or too many wrong tries.
void draw_refused(juce::Graphics& g, const std::string& why);

// "482913" as "482 913", easier to read off the screen and type.
std::string spaced_code(const std::string& code);

} // namespace web
