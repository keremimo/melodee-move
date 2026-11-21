// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/mix_widgets.h"
#include "move/font.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui::mix {
namespace {

const juce::Colour on = juce::Colours::white, off = juce::Colours::black;
void pixel(juce::Graphics& g, int x, int y) { g.fillRect(x, y, 1, 1); }

} // namespace

void item_row(juce::Graphics& g, const std::vector<Item>& items, int selected) {
    constexpr int shown = 3; // four letters each, with room between
    int cell = content_width / shown, count = int(items.size());
    int first = std::clamp(selected - 1, 0, std::max(0, count - shown));
    for (int n = 0; n < shown && first + n < count; ++n) {
        const Item& item = items[size_t(first + n)];
        int x = content_x + n * cell;
        bool chosen = first + n == selected;
        g.setColour(on);
        if (chosen) {
            g.fillRect(x, 0, cell - 1, header_height);
        } else if (item.pad) {
            for (int px = x; px < x + cell - 1; px += 2) pixel(g, px, header_height - 1); // a kit pad's own
        } else {
            g.fillRect(x, header_height - 1, cell - 1, 1);
        }
        g.setColour(chosen ? off : on);
        std::string name = item.name.substr(0, 4);
        move::draw_text_centred(g, x, 1, cell - 1, name);
        if (item.off) { // bypassed: struck through
            int w = move::text_width(name);
            g.fillRect(x + (cell - 1 - w) / 2 - 1, 4, w + 2, 1);
        }
    }
    g.setColour(on);
    if (first > 0) { pixel(g, 24, 4); g.fillRect(25, 3, 1, 3); }             // more to the left
    if (first + shown < count) { g.fillRect(126, 3, 1, 3); pixel(g, 127, 4); } // and to the right
}

void strips(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<Strip>& list, int selected) {
    if (list.empty()) return;
    int n = int(list.size()), w = area.getWidth() / n;
    int label_y = area.getBottom() - move::glyph_height, top = area.getY() + 8, floor = label_y - 2;
    int height_now = std::max(1, floor - top);
    for (int k = 0; k < n; ++k) {
        const Strip& s = list[size_t(k)];
        int x = area.getX() + k * w;
        // The label, the selected one inverted.
        g.setColour(on);
        if (k == selected) g.fillRect(x, label_y - 1, w - 1, move::glyph_height + 1);
        g.setColour(k == selected ? off : on);
        move::draw_text_centred(g, x, label_y, w - 1, s.label.substr(0, size_t(std::max(1, (w - 1) / move::glyph_advance))));
        g.setColour(on);
        if (!s.used) { g.fillRect(x + w / 2 - 1, floor, 1, 1); continue; }
        if (!s.text.empty()) {
            // Text mode: a chip at the top, the text cut out of it (hollow
            // when muted), "-" alone; what it plays now a thin bar under it.
            // A pixel in from the column's left: three between chips, so
            // side by side they never read as one number.
            int chip_x = x + 1, chip_w = std::max(move::glyph_width + 2, w - 3), chip_h = move::glyph_height + 4;
            std::string text = s.text.substr(0, size_t(std::max(1, (chip_w - 2) / move::glyph_advance)));
            if (s.soloed) move::draw_text_centred(g, chip_x, area.getY(), chip_w, "S");
            if (s.text == "-") {
                move::draw_text_centred(g, chip_x, top + 2, chip_w, text);
            } else if (s.muted) {
                g.drawRect(chip_x, top, chip_w, chip_h);
                move::draw_text_centred(g, chip_x, top + 2, chip_w, text);
            } else {
                g.fillRect(chip_x, top, chip_w, chip_h);
                g.setColour(off);
                move::draw_text_centred(g, chip_x, top + 2, chip_w, text);
                g.setColour(on);
            }
            int under = top + chip_h + 2;
            int m = int(std::lround(std::clamp(s.meter, 0.0f, 1.0f) * float(std::max(0, floor - under))));
            if (m > 0) g.fillRect(chip_x + chip_w / 2 - 1, floor - m + 1, 2, m);
            continue;
        }
        int meter_w = w >= 9 ? 2 : 1, bar_w = std::max(2, w - meter_w - 3);
        if (s.soloed) move::draw_text(g, x + (bar_w - move::glyph_width) / 2, area.getY(), "S");
        if (s.bipolar) {
            // A pan: out from the middle, along a line across.
            int mid = top + height_now / 2, centre = x + bar_w / 2;
            g.fillRect(x, mid, bar_w, 1);
            int reach = int(std::lround(std::clamp(s.value, -1.0f, 1.0f) * float(bar_w / 2)));
            int from = std::min(centre, centre + reach), to = std::max(centre, centre + reach);
            if (s.muted) g.drawRect(from, mid - 2, std::max(1, to - from + 1), 5);
            else g.fillRect(from, mid - 2, std::max(1, to - from + 1), 5);
            g.fillRect(centre, mid - 3, 1, 7);
        } else {
            int h = int(std::lround(std::clamp(s.value, 0.0f, 1.0f) * float(height_now)));
            if (s.muted) g.drawRect(x, floor - std::max(h, 2), bar_w, std::max(h, 2) + 1);
            else g.fillRect(x, floor - h, bar_w, h + 1);
        }
        // What it plays now, beside the bar.
        int m = int(std::lround(std::clamp(s.meter, 0.0f, 1.0f) * float(height_now)));
        if (m > 0) g.fillRect(x + bar_w + 1, floor - m + 1, meter_w, m);
    }
    g.setColour(on);
}

void reduction(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<float>& taken, int beats, int now) {
    g.setColour(on);
    g.fillRect(area.getX(), area.getY(), area.getWidth(), 1); // nothing taken off: the top
    for (int column = 0; column < area.getWidth(); ++column) {
        float amount = taken.empty() ? 0.0f : taken[size_t(column) * taken.size() / size_t(area.getWidth())];
        int h = int(std::lround(std::clamp(amount, 0.0f, 1.0f) * float(area.getHeight() - 4)));
        if (h > 0) g.fillRect(area.getX() + column, area.getY() + 1, 1, h);
    }
    for (int beat = 0; beat < std::max(beats, 1); ++beat) {
        int x = area.getX() + beat * area.getWidth() / std::max(beats, 1);
        g.fillRect(x, area.getBottom() - (beat == 0 ? 3 : 2), 1, beat == 0 ? 3 : 2);
    }
    if (now >= 0 && now < area.getWidth())
        for (int y = area.getY(); y < area.getBottom(); y += 2) pixel(g, area.getX() + now, y);
}

void punch_lanes(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<Lane>& lanes, int now) {
    int column = area.getWidth() / 16, row = 6;
    g.setColour(on);
    for (int beat = 0; beat < 4; ++beat) g.fillRect(area.getX() + beat * 4 * column, area.getY(), 1, 3); // the beats
    g.fillRect(area.getX(), area.getY() + 3, column * 16, 1);
    for (size_t n = 0; n < lanes.size() && n < 6; ++n) {
        const Lane& lane = lanes[n];
        int x = area.getX() + lane.from * column, w = std::max(1, (lane.to - lane.from) * column - 1);
        int y = area.getY() + 6 + int(n) * row;
        if (lane.kept) {
            for (int py = y; py < y + row - 2; ++py)
                for (int px = x + (py & 1); px < x + w; px += 2) pixel(g, px, py);
            g.drawRect(x, y, w, row - 2);
        } else {
            g.fillRect(x, y, w, row - 2);
        }
    }
    if (now >= 0 && now < 16) {
        int x = area.getX() + now * column;
        g.fillRect(x, area.getY(), column - 1, 3);
    }
}

std::string db_text(float db) {
    if (db <= -60.0f) return "-INF";
    char text[16];
    std::snprintf(text, sizeof text, "%.1fDB", double(db));
    return text;
}

} // namespace ui::mix
