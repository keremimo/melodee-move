// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/looper_view.h"
#include "move/font.h"
#include <algorithm>
#include <cmath>

namespace ui::looper {
namespace {

const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// The row's columns (x within the subject, from its left edge).
constexpr int row_height = 13, tag_x = 0, tag_w = 14, state_x = 18, wave_x = 44, wave_w = 32, length_right = 102;

} // namespace

void rows(juce::Graphics& g, juce::Rectangle<int> area, const std::array<Row, loops>& all) {
    for (unsigned n = 0; n < loops; ++n) {
        const Row& row = all[n];
        int x = area.getX(), y = area.getY() + int(n) * row_height, text_y = y + 3;
        // The tag, inverted for the loop the knobs and the wheel are on.
        g.setColour(on);
        if (row.selected) g.fillRect(x + tag_x, y + 1, tag_w, row_height - 2);
        g.setColour(row.selected ? off : on);
        move::draw_text(g, x + tag_x + 2, text_y, "L" + std::to_string(n + 1));
        // The state: inverted while it records or overdubs, as the record light is lit.
        g.setColour(on);
        if (!row.state.empty()) {
            int w = move::text_width(row.state.substr(0, 4));
            if (row.writing) g.fillRect(x + state_x - 1, y + 2, w + 2, row_height - 4);
            g.setColour(row.writing ? off : on);
            move::draw_text(g, x + state_x, text_y, row.state.substr(0, 4));
            g.setColour(on);
        }
        // The waveform, as much of it as the audio fills; an empty loop a dotted line.
        juce::Rectangle<int> wave(x + wave_x, y + 2, wave_w, row_height - 4);
        int middle = wave.getY() + wave.getHeight() / 2, reach = wave.getHeight() / 2;
        if (row.peaks.empty() || row.state.empty()) {
            for (int c = 0; c < wave_w; c += 2) g.fillRect(wave.getX() + c, middle, 1, 1);
        } else {
            int filled = int(std::lround(std::clamp(row.span, 0.0f, 1.0f) * float(wave_w)));
            for (int c = 0; c < filled; ++c) {
                if (row.muted && c % 2) continue;
                size_t index = size_t(c) * row.peaks.size() / size_t(wave_w);
                float peak = std::clamp(row.peaks[std::min(index, row.peaks.size() - 1)], 0.0f, 1.0f);
                int half = std::max(0, int(std::lround(peak * float(reach))));
                g.fillRect(wave.getX() + c, middle - half, 1, std::max(1, 2 * half + 1));
            }
            if (filled < wave_w) // the rest of a set take, still to come
                for (int c = filled; c < wave_w; c += 3) g.fillRect(wave.getX() + c, middle, 1, 1);
        }
        if (row.head >= 0) {
            int hx = wave.getX() + std::clamp(int(std::lround(row.head * float(wave_w - 1))), 0, wave_w - 1);
            g.setColour(off);
            g.fillRect(hx - 1, wave.getY(), 3, wave.getHeight());
            g.setColour(on);
            g.fillRect(hx, wave.getY() - 1, 1, wave.getHeight() + 2);
        }
        // The length, flush right.
        if (!row.length.empty()) {
            std::string text = row.length.substr(0, 4);
            move::draw_text(g, x + length_right - move::text_width(text), text_y, text);
        }
    }
    g.setColour(on);
}

void strips(juce::Graphics& g, juce::Rectangle<int> area, const std::array<Strip, loops>& all) {
    int column = area.getWidth() / int(loops);
    for (unsigned n = 0; n < loops; ++n) {
        const Strip& s = all[n];
        int x = area.getX() + int(n) * column, y = area.getY();
        // The tag, inverted for the selected loop.
        g.setColour(on);
        if (s.selected) g.fillRect(x, y, column - 1, 9);
        g.setColour(s.selected ? off : on);
        move::draw_text_centred(g, x, y + 1, column - 1, "L" + std::to_string(n + 1));
        g.setColour(on);
        // The fader, filled to its level, a tick at unity.
        constexpr int top = 11, tall = 34;
        juce::Rectangle<int> fader(x + 3, y + top, 7, tall);
        g.drawRect(fader);
        int fill = int(std::lround(std::clamp(s.level, 0.0f, 1.0f) * float(tall - 2)));
        g.fillRect(fader.getX() + 1, fader.getBottom() - 1 - fill, fader.getWidth() - 2, fill);
        int unity = fader.getBottom() - 1 - int(std::lround(100.0f / 127.0f * float(tall - 2)));
        g.fillRect(fader.getX() - 2, unity, 2, 1);
        // The meter beside it.
        int lit = int(std::lround(std::clamp(s.meter, 0.0f, 1.0f) * float(tall)));
        for (int k = 0; k < lit; k += 2) g.fillRect(x + 12, y + top + tall - 1 - k, 3, 1);
        // Mute and solo, inverted letters when on.
        auto flag = [&](int fy, const char* letter, bool lit_now) {
            if (!lit_now) return;
            g.fillRect(x + 17, fy - 1, 7, 9);
            g.setColour(off);
            move::draw_text(g, x + 18, fy, letter);
            g.setColour(on);
        };
        flag(y + top, "M", s.mute);
        flag(y + top + 11, "S", s.solo);
        // The pan: a line with a tick where it sits.
        int pan_y = y + top + tall + 3, half = (column - 6) / 2, centre = x + 2 + half;
        for (int k = -half; k <= half; k += 2) g.fillRect(centre + k, pan_y, 1, 1);
        int at = centre + int(std::lround(std::clamp(s.pan, -1.0f, 1.0f) * float(half)));
        g.fillRect(at - 1, pan_y - 2, 3, 5);
    }
    g.setColour(on);
}

void question(juce::Graphics& g, const std::string& first, const std::string& second, const std::string& third) {
    g.setColour(on);
    move::draw_text(g, content_x + 2, 4, first);
    if (!second.empty()) move::draw_text(g, content_x + 2, 18, second);
    if (!third.empty()) move::draw_text(g, content_x + 2, 28, third);
    detail(g, "PUSH YES", "BACK NO");
}

} // namespace ui::looper
