// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/seq_widgets.h"
#include "move/font.h"
#include "ui/widgets.h"
#include <algorithm>
#include <cmath>

namespace ui::seq {
namespace {

const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

std::string cut(const std::string& text, size_t most) { return text.substr(0, most); }

// A rectangle's outline with every other pixel: something waiting, or past the end.
void dashed(juce::Graphics& g, int x, int y, int w, int h) {
    for (int at = 0; at < w; at += 2) { g.fillRect(x + at, y, 1, 1); g.fillRect(x + at, y + h - 1, 1, 1); }
    for (int at = 0; at < h; at += 2) { g.fillRect(x, y + at, 1, 1); g.fillRect(x + w - 1, y + at, 1, 1); }
}

} // namespace

void value_cells(juce::Graphics& g, juce::Rectangle<int> area, const std::array<std::string, 8>& names,
                 const std::array<std::string, 8>& values, int touched) {
    int cell_w = area.getWidth() / 4, cell_h = area.getHeight() / 2;
    for (int n = 0; n < 8; ++n) {
        int x = area.getX() + (n % 4) * cell_w, y = area.getY() + (n / 4) * cell_h;
        if (names[size_t(n)].empty()) continue;
        g.setColour(on);
        if (n == touched) {
            g.fillRect(x, y, cell_w - 1, cell_h - 1);
            g.setColour(off);
        }
        move::draw_text(g, x + 1, y + 1, cut(names[size_t(n)], 3));
        move::draw_text(g, x + 1, y + 10, cut(values[size_t(n)], 4));
        g.setColour(on);
        if (n != touched) g.fillRect(x, y + cell_h - 2, cell_w - 2, 1);
    }
}

void title(juce::Graphics& g, const std::string& left, const std::string& right) {
    g.setColour(on);
    move::draw_text(g, content_x + 1, 1, cut(left, right.empty() ? 17 : 16 - right.size()));
    if (!right.empty()) move::draw_text(g, width - move::text_width(right), 1, right);
    g.fillRect(content_x, 9, content_width, 1);
}

void bar_blocks(juce::Graphics& g, juce::Rectangle<int> area, const Bars& bars) {
    unsigned shown = std::min(16u, bars.count > bars.first ? bars.count - bars.first : 0u);
    int rows = shown > 8 ? 2 : 1, cell_w = area.getWidth() / 8, row_h = area.getHeight() / rows;
    for (unsigned i = 0; i < shown; ++i) {
        unsigned bar = bars.first + i;
        int x = area.getX() + int(i % 8) * cell_w, y = area.getY() + int(i / 8) * row_h;
        int w = cell_w - 2, h = row_h - 5;
        bool in_loop = bar >= bars.loop_from && bar < bars.loop_to;
        g.setColour(on);
        if (int(bar) == bars.selected) g.fillRect(x, y, w, h);
        else if (in_loop) g.drawRect(x, y, w, h);
        else dashed(g, x, y, w, h);
        if (bars.notes[i]) {
            g.setColour(int(bar) == bars.selected ? off : on);
            int dots = std::min<int>(bars.notes[i], 3);
            for (int d = 0; d < dots; ++d) g.fillRect(x + w / 2 - 1, y + 3 + d * 3, 2, 2);
        }
        g.setColour(on);
        if (int(bar) == bars.playing) g.fillRect(x, y + h + 1, w, 2);
    }
}

void pattern_grid(juce::Graphics& g, juce::Rectangle<int> area, const std::array<GridRow, 4>& rows, unsigned first_pattern,
                  int selected_row, int selected_column) {
    (void)first_pattern;
    constexpr int label_w = 14, cell_w = 11;
    int row_h = area.getHeight() / 4;
    for (int r = 0; r < 4; ++r) {
        const auto& row = rows[size_t(r)];
        int y = area.getY() + r * row_h;
        g.setColour(on);
        move::draw_text(g, area.getX(), y + 2, row.tag.empty() ? "-" : cut(row.tag, 2));
        if (row.tag.empty()) continue;
        for (int c = 0; c < 8; ++c) {
            int x = area.getX() + label_w + c * cell_w;
            g.setColour(on);
            switch (row.cells[size_t(c)]) {
            case Cell::Empty: g.fillRect(x + 4, y + 4, 2, 2); break;
            case Cell::Notes: g.drawRect(x, y, cell_w - 1, cell_w - 1); break;
            case Cell::Queued: dashed(g, x, y, cell_w - 1, cell_w - 1); g.fillRect(x + 3, y + 3, 4, 4); break;
            case Cell::Stopping: g.drawRect(x, y, cell_w - 1, cell_w - 1); g.drawRect(x + 2, y + 2, cell_w - 5, cell_w - 5); break;
            case Cell::Playing:
                g.fillRect(x, y, cell_w - 1, cell_w - 1);
                g.setColour(off);
                for (int t = 0; t < 4; ++t) g.fillRect(x + 3 + t, y + 1 + t, 1, 8 - 2 * t); // a triangle: playing
                break;
            }
            g.setColour(on);
            if (row.follows >> c & 1) { // a follow action: an arrow at its right edge
                g.setColour(row.cells[size_t(c)] == Cell::Playing ? off : on);
                g.fillRect(x + cell_w - 3, y + 4, 1, 3);
                g.fillRect(x + cell_w - 4, y + 3, 1, 1);
                g.fillRect(x + cell_w - 4, y + 7, 1, 1);
                g.setColour(on);
            }
            if (row.cells[size_t(c)] == Cell::Playing && row.progress) g.fillRect(x, y + cell_w + 1, std::min<int>(row.progress, cell_w - 1), 1);
            if (r == selected_row && c == selected_column) g.fillRect(x - 1, y + cell_w + 2, cell_w + 1, 1);
        }
    }
}

void roll(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<RollNote>& notes, uint32_t from, uint32_t span,
          int low_key, int high_key, int focus, int playhead_tick, uint32_t bar_ticks) {
    span = std::max<uint32_t>(span, 1);
    int keys = std::max(1, high_key - low_key + 1);
    int row_h = std::max(1, area.getHeight() / keys);
    auto x_of = [&](uint32_t tick) { return area.getX() + int(int64_t(tick - from) * area.getWidth() / int64_t(span)); };
    g.setColour(on);
    for (uint32_t bar = (from / std::max<uint32_t>(bar_ticks, 1) + 1) * bar_ticks; bar_ticks && bar < from + span; bar += bar_ticks)
        for (int y = area.getY(); y < area.getBottom(); y += 3) g.fillRect(x_of(bar), y, 1, 1);
    for (size_t n = 0; n < notes.size(); ++n) {
        const auto& note = notes[n];
        if (note.tick + note.length <= from || note.tick >= from + span || note.key < low_key || note.key > high_key) continue;
        int x = std::max(area.getX(), x_of(note.tick)), right = std::min(area.getRight(), x_of(note.tick + note.length));
        int y = area.getBottom() - (note.key - low_key + 1) * row_h;
        int w = std::max(2, right - x - 1), h = std::max(1, row_h - 1);
        g.setColour(on);
        g.fillRect(x, y, w, h);
        if (int(n) == focus) {
            g.drawRect(x - 2, y - 2, w + 4, h + 4);
        }
        if (note.marked) g.fillRect(x, y + h + 1, w, 1);
    }
    if (playhead_tick >= 0 && uint32_t(playhead_tick) >= from && uint32_t(playhead_tick) < from + span)
        g.fillRect(x_of(uint32_t(playhead_tick)), area.getY(), 1, area.getHeight());
}

void step_row(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<bool>& hits, unsigned shown) {
    unsigned count = unsigned(hits.size());
    if (!count) return;
    unsigned per_row = std::min(count, 16u), rows = (count + per_row - 1) / per_row;
    int cell = std::min(area.getWidth() / int(per_row), area.getHeight() / int(rows));
    for (unsigned n = 0; n < count; ++n) {
        int x = area.getX() + int(n % per_row) * cell, y = area.getY() + int(n / per_row) * cell;
        g.setColour(on);
        if (n >= shown) dashed(g, x, y, cell - 1, cell - 1);
        else if (hits[n]) g.fillRect(x, y, cell - 1, cell - 1);
        else g.drawRect(x, y, cell - 1, cell - 1);
    }
}

void lfo_shape(juce::Graphics& g, juce::Rectangle<int> area, unsigned shape) {
    int w = area.getWidth(), h = area.getHeight() - 1;
    static const float randoms[8] {0.2f, 0.9f, 0.5f, 0.1f, 0.7f, 0.35f, 0.95f, 0.6f};
    auto value = [&](float phase) -> float {
        switch (shape % 7) {
        case 0: return 0.5f + 0.5f * std::sin(phase * 6.2831853f);
        case 1: return phase < 0.5f ? phase * 2 : 2 - phase * 2;
        case 2: return phase;
        case 3: return 1 - phase;
        case 4: return phase < 0.5f ? 1.0f : 0.0f;
        case 5: return randoms[int(phase * 8) % 8];
        default: { float at = phase * 8; int i = int(at) % 8; float f = at - std::floor(at); return randoms[i] + (randoms[(i + 1) % 8] - randoms[i]) * f; }
        }
    };
    g.setColour(on);
    int last = -1;
    for (int x = 0; x < w; ++x) {
        int y = area.getY() + h - int(value(float(x) / float(w)) * float(h) + 0.5f);
        if (last >= 0 && std::abs(y - last) > 1) g.fillRect(area.getX() + x, std::min(y, last), 1, std::abs(y - last));
        g.fillRect(area.getX() + x, y, 1, 1);
        last = y;
    }
}

void step_card(juce::Graphics& g, juce::Rectangle<int> area, const StepCard& card) {
    int x = area.getX() + 1, y = area.getY();
    g.setColour(on);
    move::draw_text(g, x, y + 1, cut(card.what, 17));
    if (card.empty) {
        move::draw_text(g, x, y + 16, "PRESS A PAD", 1);
        move::draw_text(g, x, y + 28, "TO ADD IT HERE", 1);
        return;
    }
    move::draw_text(g, x, y + 12, std::to_string(card.velocity), 2);
    move::draw_text(g, x, y + 28, "VEL");
    move::draw_text(g, x + 44, y + 12, "LEN " + cut(card.length, 4));
    move::draw_text(g, x + 44, y + 22, "NDG " + cut(card.nudge, 4));
    std::string marks;
    for (const auto& mark : card.marks) {
        if (marks.size() + mark.size() + (marks.empty() ? 0 : 1) > 17) break;
        marks += (marks.empty() ? "" : " ") + mark;
    }
    move::draw_text(g, x, y + 40, marks);
}

void count_in(juce::Graphics& g, juce::Rectangle<int> area, unsigned beat, unsigned beats) {
    g.setColour(on);
    move::draw_text_centred(g, area.getX(), area.getY() + 2, area.getWidth(), std::to_string(beat), 4);
    beats = std::clamp(beats, 1u, 16u);
    int spacing = std::min(12, area.getWidth() / int(beats));
    int x0 = area.getX() + (area.getWidth() - spacing * int(beats)) / 2 + spacing / 2;
    for (unsigned b = 0; b < beats; ++b) {
        int cx = x0 + int(b) * spacing, cy = area.getY() + 40;
        if (b + 1 <= beat) dot(g, cx, cy, 3);
        else { g.drawRect(cx - 3, cy - 3, 7, 7); }
    }
}

} // namespace ui::seq
