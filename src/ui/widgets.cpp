// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/widgets.h"
#include "move/font.h"
#include <algorithm>
#include <cmath>

namespace ui {
namespace {

const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

void pixel(juce::Graphics& g, int x, int y) { g.fillRect(x, y, 1, 1); }

} // namespace

void rail(juce::Graphics& g, std::string_view top, std::string_view big, std::string_view bottom) {
    g.setColour(on);
    g.fillRect(0, 0, rail_width, height);
    g.setColour(off);
    move::draw_text_centred(g, 0, 2, rail_width, top.substr(0, 4));
    if (!big.empty()) {
        int scale = big.size() <= 2 ? 2 : 1;
        move::draw_text_centred(g, 0, scale == 2 ? 25 : 28, rail_width, big.substr(0, 4), scale);
    }
    move::draw_text_centred(g, 0, 54, rail_width, bottom.substr(0, 4));
    g.setColour(on);
}

void detail(juce::Graphics& g, std::string_view left, std::string_view right) {
    g.setColour(on);
    move::draw_text(g, content_x + 1, detail_y, left);
    if (!right.empty()) move::draw_text(g, width - move::text_width(right), detail_y, right);
}

void playhead(juce::Graphics& g, juce::Rectangle<int> area, int column) {
    int x = area.getX() + column;
    g.setColour(off);
    g.fillRect(x - 1, area.getY(), 3, area.getHeight());
    g.setColour(on);
    g.fillRect(x, area.getY(), 1, area.getHeight());
}

void dot(juce::Graphics& g, int cx, int cy, int radius) {
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius) g.fillRect(cx + x, cy + y, 1, 1);
}

void header(juce::Graphics& g, std::string_view left, std::string_view right) {
    g.setColour(on);
    g.fillRect(0, 0, width, header_height);
    g.setColour(off);
    move::draw_text(g, 2, 1, left);
    if (!right.empty()) move::draw_text(g, width - 2 - move::text_width(right), 1, right);
    g.setColour(on);
}

void waveform(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<float>& peaks, int selected_from, int selected_to) {
    int middle = area.getY() + area.getHeight() / 2, reach = area.getHeight() / 2;
    for (int column = 0; column < area.getWidth(); ++column) {
        size_t index = peaks.empty() ? 0 : size_t(column) * peaks.size() / size_t(area.getWidth());
        float peak = peaks.empty() ? 0.0f : std::clamp(peaks[index], 0.0f, 1.0f);
        int half = std::max(0, int(std::lround(peak * float(reach))));
        int x = area.getX() + column;
        bool selected = column >= selected_from && column <= selected_to;
        g.setColour(selected ? on : off);
        g.fillRect(x, area.getY(), 1, area.getHeight());
        g.setColour(selected ? off : on);
        g.fillRect(x, middle - half, 1, std::max(1, 2 * half));
    }
    g.setColour(on);
}

void cuts(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<int>& columns, int selected) {
    for (size_t n = 0; n < columns.size(); ++n) {
        int x = area.getX() + columns[n];
        if (int(n) == selected) {
            g.setColour(on);
            g.fillRect(x, area.getY(), 1, area.getHeight());
            g.fillRect(x, area.getY(), 3, 2); // a tab on top
            continue;
        }
        for (int y = area.getY(); y < area.getBottom(); y += 2) {
            g.setColour(on); pixel(g, x, y);
            g.setColour(off); pixel(g, x, y + 1);
        }
    }
    g.setColour(on);
}

void knob_strip(juce::Graphics& g, const std::array<float, 8>& values, int touched) {
    int top = height - strip_height;
    for (int knob = 0; knob < 8; ++knob) {
        int x = knob * 16 + 1, cell = 14;
        g.setColour(on);
        if (knob == touched) { g.fillRect(x, top, cell, strip_height); continue; }
        g.drawRect(x, top, cell, strip_height);
        int fill = int(std::lround(std::clamp(values[size_t(knob)], 0.0f, 1.0f) * float(cell - 2)));
        g.fillRect(x + 1, top + 1, fill, strip_height - 2);
    }
}

void big_parameter(juce::Graphics& g, juce::Rectangle<int> area, std::string_view name, std::string_view value, float fraction) {
    g.setColour(on);
    move::draw_text_centred(g, area.getX(), area.getY(), area.getWidth(), name, 1);
    int value_scale = move::text_width(value, 3) <= area.getWidth() ? 3 : 2;
    move::draw_text_centred(g, area.getX(), area.getY() + 10, area.getWidth(), value, value_scale);
    int bar_y = area.getBottom() - 4;
    g.drawRect(area.getX() + 4, bar_y, area.getWidth() - 8, 4);
    g.fillRect(area.getX() + 5, bar_y + 1, int(std::lround(std::clamp(fraction, 0.0f, 1.0f) * float(area.getWidth() - 10))), 2);
}

void list(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<std::string>& rows, int selected, int scale) {
    int row_height = move::glyph_height * scale + 2 * scale;
    int visible = std::max(1, area.getHeight() / row_height);
    int first = std::clamp(selected - visible / 2, 0, std::max(0, int(rows.size()) - visible));
    for (int n = 0; n < visible && first + n < int(rows.size()); ++n) {
        int y = area.getY() + n * row_height;
        bool chosen = first + n == selected;
        g.setColour(on);
        if (chosen) g.fillRect(area.getX(), y, area.getWidth() - 3, row_height);
        g.setColour(chosen ? off : on);
        move::draw_text(g, area.getX() + 2 * scale, y + scale, rows[size_t(first + n)], scale);
    }
    // A scroll bar on the right.
    g.setColour(on);
    if (int(rows.size()) > visible) {
        int track = area.getHeight(), thumb = std::max(3, track * visible / int(rows.size()));
        int at = (track - thumb) * first / std::max(1, int(rows.size()) - visible);
        g.fillRect(area.getRight() - 1, area.getY() + at, 1, thumb);
    }
}

void meter(juce::Graphics& g, juce::Rectangle<int> area, float left, float right, float threshold) {
    g.setColour(on);
    int bar = (area.getHeight() - 2) / 2;
    for (int side = 0; side < 2; ++side) {
        int y = area.getY() + side * (bar + 2);
        float level = side == 0 ? left : right;
        g.drawRect(area.getX(), y, area.getWidth(), bar);
        g.fillRect(area.getX() + 1, y + 1, int(std::lround(std::clamp(level, 0.0f, 1.0f) * float(area.getWidth() - 2))), bar - 2);
    }
    int x = area.getX() + int(std::lround(std::clamp(threshold, 0.0f, 1.0f) * float(area.getWidth() - 1)));
    for (int y = area.getY() - 2; y < area.getBottom() + 2; ++y) {
        g.setColour((y & 1) ? off : on);
        pixel(g, x, y);
    }
    g.setColour(on);
}

void sheet(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(off);
    g.fillRect(area.expanded(1));
    g.setColour(on);
    g.drawRect(area);
}

void knob_cells(juce::Graphics& g, juce::Rectangle<int> area, const std::array<std::string, 8>& names,
                const std::array<float, 8>& values, int touched) {
    int cell_width = area.getWidth() / 4, cell_height = area.getHeight() / 2;
    for (int knob = 0; knob < 8; ++knob) {
        if (names[size_t(knob)].empty()) continue; // no knob there: an empty cell
        int x = area.getX() + (knob % 4) * cell_width, y = area.getY() + (knob / 4) * cell_height;
        bool chosen = knob == touched;
        g.setColour(on);
        if (chosen) g.fillRect(x, y, cell_width - 1, cell_height - 1);
        g.setColour(chosen ? off : on);
        size_t fits = size_t(std::max(1, (cell_width - 2) / move::glyph_advance)); // a gap before the next cell's name
        move::draw_text(g, x + 1, y + 1, std::string_view(names[size_t(knob)]).substr(0, fits));
        // The value as a solid bar on a one-pixel track.
        int bar_y = y + cell_height - 5, bar_width = cell_width - 4;
        g.fillRect(x + 1, bar_y + 3, bar_width, 1);
        g.fillRect(x + 1, bar_y, std::max(1, int(std::lround(std::clamp(values[size_t(knob)], 0.0f, 1.0f) * float(bar_width)))), 3);
    }
    g.setColour(on);
}

void knob_values(juce::Graphics& g, juce::Rectangle<int> area, const std::array<std::string, 8>& names,
                 const std::array<std::string, 8>& values, int touched) {
    int cell_width = area.getWidth() / 4, cell_height = area.getHeight() / 2;
    size_t fits = size_t(std::max(1, cell_width / move::glyph_advance));
    for (int knob = 0; knob < 8; ++knob) {
        if (names[size_t(knob)].empty()) continue; // no knob there: an empty cell
        int x = area.getX() + (knob % 4) * cell_width, y = area.getY() + (knob / 4) * cell_height;
        bool chosen = knob == touched;
        // The name inverted, a pixel between cells; the value under it,
        // centred (the touched knob's whole cell inverted).
        g.setColour(on);
        g.fillRect(x, y, cell_width - 1, chosen ? cell_height - 1 : move::glyph_height + 1);
        g.setColour(off);
        move::draw_text(g, x, y + 1, std::string_view(names[size_t(knob)]).substr(0, fits));
        g.setColour(chosen ? off : on); // centred, so values side by side keep apart
        move::draw_text_centred(g, x, y + cell_height - move::glyph_height - 1, cell_width - 1, std::string_view(values[size_t(knob)]).substr(0, fits));
    }
    g.setColour(on);
}

} // namespace ui
