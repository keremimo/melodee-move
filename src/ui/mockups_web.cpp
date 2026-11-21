// SPDX-License-Identifier: AGPL-3.0-only
// The web page's screens on the Move (docs/UI.md, "The web page"), as
// mockups: web.png. They are the screens the web layer draws
// (src/web/screens.h), with an example Move on a USB cable and Wi-Fi.
#include "ui/mockups.h"
#include "web/screens.h"

namespace ui {
namespace mockup {

std::vector<Screen> web_screens() {
    return {
        {"where", "SHIFT + CAPTURE: WHERE THE PAGE IS", [](juce::Graphics& g) {
            web::draw_where(g, {"172.16.42.1", "192.168.1.23", 8080, 1});
        }},
        {"where-wifi", "THE SAME, NO USB CABLE", [](juce::Graphics& g) {
            web::draw_where(g, {"", "192.168.1.23", 8080, 0});
        }},
        {"code", "A WI-FI BROWSER ASKS: ITS CODE", [](juce::Graphics& g) {
            web::draw_code(g, "482913", 84, "192.168.1.40");
        }},
        {"paired", "THE CODE TYPED: PAIRED", [](juce::Graphics& g) {
            web::draw_paired(g, "192.168.1.40");
        }},
        {"refused", "BACK, TIME UP OR 5 WRONG: REFUSED", [](juce::Graphics& g) {
            web::draw_refused(g, "5 WRONG CODES");
        }},
    };
}

} // namespace mockup
} // namespace ui
