// SPDX-License-Identifier: AGPL-3.0-only
#include "web/screens.h"
#include "move/font.h"
#include "ui/widgets.h"
#include <algorithm>

namespace web {
namespace {

const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// A small inverted tag, as a label over a value: "USB", "WI-FI".
void tag(juce::Graphics& g, int x, int y, const std::string& text) {
    g.setColour(on);
    g.fillRect(x, y, move::text_width(text) + 3, ui::header_height);
    g.setColour(off);
    move::draw_text(g, x + 2, y + 1, text);
    g.setColour(on);
}

void clear(juce::Graphics& g) {
    g.setColour(off);
    g.fillAll();
    g.setColour(on);
}

} // namespace

std::string spaced_code(const std::string& code) {
    return code.size() == 6 ? code.substr(0, 3) + " " + code.substr(3) : code;
}

void draw_where(juce::Graphics& g, const Where& where) {
    clear(g);
    ui::rail(g, "WEB", std::to_string(where.port), "PORT");
    const int x = ui::content_x + 1;
    tag(g, x, 0, "USB");
    move::draw_text(g, x + 1, 11, where.usb.empty() ? "NO CABLE" : where.usb);
    tag(g, x, 23, "WI-FI");
    move::draw_text(g, x + 1, 34, where.wifi.empty() ? "NOT JOINED" : where.wifi);
    if (!where.wifi.empty()) move::draw_text(g, x + 1, 44, "ASKS A CODE");
    ui::detail(g, where.paired ? std::to_string(where.paired) + " PAIRED" : std::string(), "BACK");
}

void draw_code(juce::Graphics& g, const std::string& code, unsigned seconds_left, const std::string& from) {
    clear(g);
    ui::rail(g, "PAIR", std::to_string(std::min(seconds_left, 99u)), "SEC");
    move::draw_text_centred(g, ui::content_x, 1, ui::content_width, "WI-FI BROWSER");
    g.drawRect(ui::content_x + 3, 12, ui::content_width - 6, 22);
    move::draw_text_centred(g, ui::content_x, 16, ui::content_width, spaced_code(code), 2);
    move::draw_text_centred(g, ui::content_x, 38, ui::content_width, from);
    ui::detail(g, "BACK: REFUSE");
}

void draw_paired(juce::Graphics& g, const std::string& from) {
    clear(g);
    ui::rail(g, "PAIR", "OK", "WIFI");
    move::draw_text_centred(g, ui::content_x, 10, ui::content_width, "PAIRED", 2);
    move::draw_text_centred(g, ui::content_x, 32, ui::content_width, from);
    ui::detail(g, "UNTIL YOU QUIT");
}

void draw_refused(juce::Graphics& g, const std::string& why) {
    clear(g);
    ui::rail(g, "PAIR", "NO", "WIFI");
    move::draw_text_centred(g, ui::content_x, 10, ui::content_width, "REFUSED", 2);
    move::draw_text_centred(g, ui::content_x, 32, ui::content_width, why);
    ui::detail(g, "ASK AGAIN THERE");
}

} // namespace web
