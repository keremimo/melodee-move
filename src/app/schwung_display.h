// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

struct JSContext;

namespace app {

// Schwung's screen as its shadow UI draws it, so that Schwung's own screen
// code (a module's pages, run from the Move: schwung_pages.h) draws here the
// pixels it draws there. Adapted from Schwung's src/host/js_display.c (MIT,
// Charles Vestal; schwung_display.cpp). Its rules, each one kept:
//
//   - one byte a pixel, 0 or 1; any non-zero colour is white; nothing XORs;
//     every write is clipped pixel by pixel; the host never clears it
//   - fill_rect and draw_rect draw nothing for a width or height <= 0
//   - draw_line is a straight run when vertical or horizontal, else the
//     Bresenham variant whose pixels depend on which end it starts from
//   - fill_circle lights dx*dx + dy*dy <= r*r; draw_arc is a one-pixel
//     outline (the nearest pixel per row, unioned with per column) gated by
//     angle, 0 at twelve o'clock, clockwise, both ends in; draw_circle is
//     the whole arc
//   - print draws a string's UTF-8 bytes in the current font from y down
//     (no baseline), only the ink; a byte with no glyph moves 1 pixel on;
//     text_width counts the same, the gap after the last glyph included
//   - a font is a PNG strip and its .dat (the characters, UTF-8, first line):
//     cells of width / characters, ink where alpha > 0, each glyph trimmed
//     to its ink (a blank one keeps the cell's width), code points from 256
//     up left out; the default is host/font.png, 1 pixel between glyphs
//   - the JS globals answer as Schwung's bindings do: too few arguments
//     draw nothing (fill_rect and print need all five and four), numbers go
//     through ToInt32, draw_line and the circles default to white
class SchwungDisplay {
public:
    static constexpr int width = 128, height = 64;

    // Where Schwung is: MELODEE_SCHWUNG_ROOT, else the Move's
    // /data/UserData/schwung. `path` under the Move's folder, moved there.
    static std::string root();
    static std::string schwung_path(const std::string& path);

    void clear() { pixels.fill(0); }
    void set_pixel(int x, int y, int value);
    bool pixel(int x, int y) const { return x >= 0 && x < width && y >= 0 && y < height && pixels[size_t(y * width + x)]; }
    void draw_rect(int x, int y, int w, int h, int value);
    void fill_rect(int x, int y, int w, int h, int value);
    void draw_line(int x0, int y0, int x1, int y1, int value);
    void fill_circle(int cx, int cy, int r, int value);
    void draw_arc(int cx, int cy, int r, int start, int sweep, int value);
    void draw_circle(int cx, int cy, int r, int value) { draw_arc(cx, cy, r, 0, 360, value); }
    // A PNG's grey ((77 R + 150 G + 29 B) >> 8, alpha ignored, as stb_image
    // makes it) above `threshold` (at or below it when `invert`), lit;
    // nothing written black. False when it cannot be read. Where a PNG is
    // partly transparent JUCE's decoder may round its colour differently
    // from stb_image's (nothing of Schwung's or the Move's modules calls it).
    bool draw_image(const std::string& path, int x, int y, int threshold, int invert);
    void print(int x, int y, const std::string& bytes, int colour);
    int text_width(const std::string& bytes);
    bool set_font(const std::string& path);
    int font_height();

    // The 1024 bytes of Schwung's packed screen (/dev/shm/schwung-display):
    // a byte a column of 8 rows, bit j the row page * 8 + j.
    std::array<uint8_t, width * height / 8> packed() const;

    // Schwung's display globals on `context`'s global object, drawing here
    // (`this` must outlive the context). Called after quickjs-libc's helpers,
    // so print is the display's, as in Schwung's shadow UI.
    void install(JSContext* context);

    std::array<uint8_t, width * height> pixels {};

    struct Font {
        struct Glyph {
            int width = 0;
            std::string bits; // width x height, a byte a pixel; empty: no glyph
        };
        int height = 0, spacing = 1;
        std::array<Glyph, 256> glyphs;
    };
    // A font as Schwung's loader reads it, null when it cannot.
    static std::shared_ptr<const Font> load_font(const std::string& png);

private:
    std::shared_ptr<const Font> font; // the current one; loaded on first use
    std::map<std::string, std::shared_ptr<const Font>> fonts; // set_font's, up to 16, as Schwung caches them
    const Font* current_font();
    void plot_arc(int cx, int cy, int dx, int dy, int start, int sweep, int value);
};

} // namespace app
