// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/song_view.h"
#include "move/font.h"
#include "ui/widgets.h"
#include <algorithm>
#include <cmath>

namespace ui {
namespace {

const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// The subject's columns: a two-pixel mark for the selected lane, then time.
constexpr int lane_mark_x = content_x, time_x = content_x + 3, time_width = width - time_x;
constexpr int marker_y = 0, brace_y = 9, lanes_y = 13, lane_height = 10;

void dotted_h(juce::Graphics& g, int x0, int x1, int y) {
    for (int x = x0; x <= x1; x += 2) g.fillRect(x, y, 1, 1);
}
void dotted_v(juce::Graphics& g, int x, int y0, int y1) {
    for (int y = y0; y <= y1; y += 2) g.fillRect(x, y, 1, 1);
}

} // namespace

int song_x(const SongView& view, int bar) {
    int zoom = std::max(1, view.zoom);
    return time_x + (bar - view.first) * time_width / zoom;
}

void song_view(juce::Graphics& g, const SongView& view) {
    auto clip_region = [&](int x0, int x1) { return std::pair<int, int>(std::max(x0, time_x), std::min(x1, width - 1)); };
    // The markers: a tick down to the brace and the name after it, cut where
    // the next one starts.
    std::vector<SongViewMarker> markers = view.markers;
    std::sort(markers.begin(), markers.end(), [](const auto& a, const auto& b) { return a.bar < b.bar; });
    for (size_t n = 0; n < markers.size(); ++n) {
        int x = song_x(view, markers[n].bar);
        int next = n + 1 < markers.size() ? song_x(view, markers[n + 1].bar) - 1 : width;
        if (next < time_x || x >= width) continue;
        g.setColour(on);
        if (x >= time_x) g.fillRect(x, marker_y, 1, brace_y + 2);
        int text_x = std::max(x + 2, time_x);
        int room = (next - text_x) / move::glyph_advance;
        if (room > 0) move::draw_text(g, text_x, marker_y, std::string(markers[n].name).substr(0, size_t(room)));
    }
    if (view.recording) { g.setColour(on); dot(g, width - 4, marker_y + 3, 3); }
    // The loop brace: a line over its bars, its ends turned down.
    if (view.loop_to > view.loop_from) {
        auto [x0, x1] = clip_region(song_x(view, view.loop_from), song_x(view, view.loop_to) - 1);
        if (x1 > x0) {
            g.setColour(on);
            g.fillRect(x0, brace_y, x1 - x0 + 1, 1);
            if (song_x(view, view.loop_from) >= time_x) g.fillRect(x0, brace_y, 1, 3);
            if (song_x(view, view.loop_to) - 1 < width) g.fillRect(x1, brace_y, 1, 3);
        }
    }
    // The tempo lane's points: a small notch above the brace.
    for (int bar : view.tempo_points) {
        int x = song_x(view, bar);
        if (x < time_x || x >= width) continue;
        g.setColour(on);
        g.fillRect(x - 1, brace_y - 2, 3, 1);
        g.fillRect(x, brace_y - 1, 1, 1);
    }
    // The lanes.
    for (int lane = 0; lane < song_lanes; ++lane) {
        int y = lanes_y + lane * lane_height;
        g.setColour(on);
        if (lane == view.lane) g.fillRect(lane_mark_x, y, 2, lane_height - 1);
        if (!view.lane_used[lane]) dotted_h(g, time_x, width - 1, y + lane_height / 2);
    }
    for (const auto& clip : view.clips) {
        if (clip.lane < 0 || clip.lane >= song_lanes) continue;
        int y = lanes_y + clip.lane * lane_height, h = lane_height - 1;
        int left = song_x(view, clip.bar), right = song_x(view, clip.bar + clip.bars) - 2;
        auto [x0, x1] = clip_region(left, right);
        if (x1 < x0) continue;
        bool chosen = clip.lane == view.lane && view.cursor >= clip.bar && view.cursor < clip.bar + clip.bars;
        g.setColour(on);
        if (chosen) g.fillRect(x0, y, x1 - x0 + 1, h);
        else if (clip.muted) {
            dotted_h(g, x0, x1, y);
            dotted_h(g, x0, x1, y + h - 1);
            if (left >= time_x) dotted_v(g, x0, y, y + h - 1);
            dotted_v(g, x1, y, y + h - 1);
        } else {
            g.fillRect(x0, y, x1 - x0 + 1, 1);
            g.fillRect(x0, y + h - 1, x1 - x0 + 1, 1);
            if (left >= time_x) g.fillRect(x0, y, 1, h);
            g.fillRect(x1, y, 1, h);
        }
        int room = (x1 - x0 - 2) / move::glyph_advance;
        if (room > 0) {
            g.setColour(chosen ? off : on);
            move::draw_text(g, x0 + 2, y + 1, clip.name.substr(0, size_t(room)));
        }
    }
    // The cursor, down the lanes; the playhead dotted.
    int cx = song_x(view, view.cursor);
    if (cx >= time_x && cx < width) {
        g.setColour(on);
        for (int y = brace_y + 2; y < lanes_y + song_lanes * lane_height - 1; ++y) {
            bool inside = false; // over a filled clip the line is black
            for (const auto& clip : view.clips)
                if (clip.lane == view.lane && view.cursor >= clip.bar && view.cursor < clip.bar + clip.bars) {
                    int top = lanes_y + clip.lane * lane_height;
                    inside |= y >= top && y < top + lane_height - 1 && cx >= std::max(song_x(view, clip.bar), time_x);
                }
            g.setColour(inside ? off : on);
            g.fillRect(cx, y, 1, 1);
        }
        g.setColour(on);
        g.fillRect(cx - 1, brace_y + 2, 3, 1);
    }
    if (view.playhead >= 0) {
        int zoom = std::max(1, view.zoom);
        int px = time_x + int(std::floor((view.playhead - float(view.first)) * float(time_width) / float(zoom)));
        if (px >= time_x && px < width) {
            g.setColour(on);
            for (int y = lanes_y; y < lanes_y + song_lanes * lane_height - 1; y += 2) {
                g.setColour(on);
                g.fillRect(px, y, 1, 1);
            }
        }
    }
    g.setColour(on);
}

void snowflake(juce::Graphics& g, int x, int y) {
    static const char* rows[7] {"...#...", ".#.#.#.", "..###..", "#######", "..###..", ".#.#.#.", "...#..."};
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 7; ++c)
            if (rows[r][c] == '#') g.fillRect(x + c, y + r, 1, 1);
}

void progress(juce::Graphics& g, juce::Rectangle<int> area, float fraction) {
    g.setColour(on);
    g.drawRect(area);
    int inner = area.getWidth() - 4;
    int filled = int(std::lround(std::clamp(fraction, 0.0f, 1.0f) * float(inner)));
    if (filled > 0) g.fillRect(area.getX() + 2, area.getY() + 2, filled, area.getHeight() - 4);
}

} // namespace ui
