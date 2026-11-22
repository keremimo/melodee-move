// SPDX-License-Identifier: AGPL-3.0-only
// Adapted from Schwung's src/host/js_display.c (v1.9.4): Copyright (c)
// 2025-2026 Charles Vestal, MIT License (licenses/Schwung-LICENSE.txt).
#include "app/schwung_display.h"
#include <juce_graphics/juce_graphics.h>
#include <quickjs.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace app {

namespace {

constexpr const char* schwung_folder = "/data/UserData/schwung";

} // namespace

std::string SchwungDisplay::root() {
    const char* given = std::getenv("MELODEE_SCHWUNG_ROOT");
    return given && *given ? given : schwung_folder;
}

std::string SchwungDisplay::schwung_path(const std::string& path) {
    const std::string folder = schwung_folder;
    if (path.compare(0, folder.size(), folder) == 0 && (path.size() == folder.size() || path[folder.size()] == '/'))
        return root() + path.substr(folder.size());
    return path;
}

// ---------------------------------------------------------------- shapes

void SchwungDisplay::set_pixel(int x, int y, int value) {
    if (x >= 0 && x < width && y >= 0 && y < height) pixels[size_t(y * width + x)] = value ? 1 : 0;
}

void SchwungDisplay::draw_rect(int x, int y, int w, int h, int value) {
    if (w <= 0 || h <= 0) return;
    for (int row = y; row < y + h; ++row) { set_pixel(x, row, value); set_pixel(x + w - 1, row, value); }
    for (int column = x; column < x + w; ++column) { set_pixel(column, y, value); set_pixel(column, y + h - 1, value); }
}

void SchwungDisplay::fill_rect(int x, int y, int w, int h, int value) {
    if (w <= 0 || h <= 0) return;
    // Only what lands on the screen can be lit, so the loop starts there.
    for (int row = std::max(0, y); row < y + h && row < height; ++row)
        for (int column = std::max(0, x); column < x + w && column < width; ++column) pixels[size_t(row * width + column)] = value ? 1 : 0;
}

void SchwungDisplay::draw_line(int x0, int y0, int x1, int y1, int value) {
    int dx = x1 - x0, dy = y1 - y0;
    const int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    dx = std::abs(dx);
    dy = std::abs(dy);
    if (dx == 0) {
        for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y) set_pixel(x0, y, value);
        return;
    }
    if (dy == 0) {
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) set_pixel(x, y0, value);
        return;
    }
    int error = dx - dy;
    for (;;) {
        set_pixel(x0, y0, value);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice > -dy) { error -= dy; x0 += sx; }
        if (twice < dx) { error += dx; y0 += sy; }
    }
}

void SchwungDisplay::fill_circle(int cx, int cy, int r, int value) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r) set_pixel(cx + dx, cy + dy, value);
}

void SchwungDisplay::plot_arc(int cx, int cy, int dx, int dy, int start, int sweep, int value) {
    if (sweep < 360) {
        double angle = std::atan2(double(dx), double(-dy)) * 180.0 / M_PI;
        if (angle < 0) angle += 360.0;
        double delta = angle - double(start);
        if (delta < 0) delta += 360.0;
        if (delta > double(sweep)) return;
    }
    set_pixel(cx + dx, cy + dy, value);
}

void SchwungDisplay::draw_arc(int cx, int cy, int r, int start, int sweep, int value) {
    if (r < 0) return;
    if (r == 0) { set_pixel(cx, cy, value); return; }
    if (sweep >= 360) sweep = 360;
    if (sweep <= 0) return;
    start = ((start % 360) + 360) % 360;
    for (int dy = -r; dy <= r; ++dy) { // the nearest pixel on each row
        const int dx = int(std::sqrt(double(r * r - dy * dy)) + 0.5);
        plot_arc(cx, cy, dx, dy, start, sweep, value);
        if (dx != 0) plot_arc(cx, cy, -dx, dy, start, sweep, value);
    }
    for (int dx = -r; dx <= r; ++dx) { // and on each column
        const int dy = int(std::sqrt(double(r * r - dx * dx)) + 0.5);
        plot_arc(cx, cy, dx, dy, start, sweep, value);
        if (dy != 0) plot_arc(cx, cy, dx, -dy, start, sweep, value);
    }
}

bool SchwungDisplay::draw_image(const std::string& path, int x, int y, int threshold, int invert) {
    juce::Image image = juce::ImageFileFormat::loadFrom(juce::File(juce::String(path)));
    if (!image.isValid()) {
        std::fprintf(stderr, "draw_image: failed to load %s\n", path.c_str());
        return false;
    }
    for (int row = 0; row < image.getHeight(); ++row)
        for (int column = 0; column < image.getWidth(); ++column) {
            const int px = x + column, py = y + row;
            if (px < 0 || px >= width || py < 0 || py >= height) continue;
            const juce::Colour colour = image.getPixelAt(column, row);
            const int grey = (77 * colour.getRed() + 150 * colour.getGreen() + 29 * colour.getBlue()) >> 8;
            if (invert ? grey <= threshold : grey > threshold) pixels[size_t(py * width + px)] = 1;
        }
    return true;
}

std::array<uint8_t, SchwungDisplay::width * SchwungDisplay::height / 8> SchwungDisplay::packed() const {
    std::array<uint8_t, width * height / 8> out {};
    size_t at = 0;
    for (int page = 0; page < height / 8; ++page)
        for (int x = 0; x < width; ++x) {
            uint8_t byte = 0;
            for (int bit = 0; bit < 8; ++bit) byte = uint8_t(byte | (pixels[size_t((page * 8 + bit) * width + x)] << bit));
            out[at++] = byte;
        }
    return out;
}

// ---------------------------------------------------------------- text

std::shared_ptr<const SchwungDisplay::Font> SchwungDisplay::load_font(const std::string& png) {
    juce::Image image = juce::ImageFileFormat::loadFrom(juce::File(juce::String(png)));
    if (!image.isValid()) { std::fprintf(stderr, "ERROR loading font: %s\n", png.c_str()); return nullptr; }
    // The characters: the .dat's first line (at most 1023 bytes), one
    // newline taken off, as UTF-8 of one to three bytes, up to 512 of them.
    std::FILE* list = std::fopen((png + ".dat").c_str(), "r");
    if (!list) { std::fprintf(stderr, "ERROR loading font charList from: %s.dat\n", png.c_str()); return nullptr; }
    char raw[1024];
    const bool read = std::fgets(raw, sizeof raw, list) != nullptr;
    std::fclose(list);
    if (!read) return nullptr;
    size_t length = std::strlen(raw);
    if (length > 0 && raw[length - 1] == '\n') raw[--length] = '\0';
    std::vector<int> codes;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(raw); *p && codes.size() < 512;) {
        unsigned code = 0;
        if (*p < 0x80) code = *p++;
        else if ((*p & 0xe0) == 0xc0) {
            code = unsigned(*p++ & 0x1f) << 6;
            if (*p) code |= unsigned(*p++ & 0x3f);
        } else if ((*p & 0xf0) == 0xe0) {
            code = unsigned(*p++ & 0x0f) << 12;
            if (*p) code |= unsigned(*p++ & 0x3f) << 6;
            if (*p) code |= unsigned(*p++ & 0x3f);
        } else { ++p; continue; } // not a lead byte it reads
        codes.push_back(int(code));
    }
    if (codes.empty()) return nullptr;
    const int cell = image.getWidth() / int(codes.size());
    if (cell <= 0) return nullptr;
    auto font = std::make_shared<Font>();
    font->height = image.getHeight();
    font->spacing = 1;
    auto ink = [&](int x, int y) { return image.getPixelAt(x, y).getAlpha() > 0; };
    for (size_t n = 0; n < codes.size(); ++n) {
        const int code = codes[n];
        if (code < 0 || code >= 256) continue; // its cell counted, its glyph left out
        const int x0 = int(n) * cell;
        int first = -1, last = -1;
        for (int x = 0; x < cell; ++x)
            for (int y = 0; y < font->height; ++y)
                if (ink(x0 + x, y)) { if (first < 0) first = x; last = x; break; }
        Font::Glyph glyph;
        if (first < 0) { // blank: the cell's width, nothing lit
            glyph.width = cell;
            glyph.bits.assign(size_t(cell * font->height), '\0');
        } else {
            glyph.width = last - first + 1;
            for (int y = 0; y < font->height; ++y)
                for (int x = 0; x < glyph.width; ++x) glyph.bits.push_back(ink(x0 + first + x, y) ? '\1' : '\0');
        }
        font->glyphs[size_t(code)] = std::move(glyph);
    }
    return font;
}

const SchwungDisplay::Font* SchwungDisplay::current_font() {
    if (!font) font = load_font(schwung_path(std::string(schwung_folder) + "/host/font.png"));
    return font.get();
}

void SchwungDisplay::print(int x, int y, const std::string& bytes, int colour) {
    const Font* with = current_font();
    if (!with) return;
    int cursor = x;
    for (unsigned char c : bytes) {
        if (!c) break;
        const auto& glyph = with->glyphs[c];
        if (glyph.bits.empty()) { cursor += with->spacing; continue; }
        for (int row = 0; row < with->height; ++row)
            for (int column = 0; column < glyph.width; ++column)
                if (glyph.bits[size_t(row * glyph.width + column)]) set_pixel(cursor + column, y + row, colour);
        cursor += glyph.width + with->spacing;
    }
}

int SchwungDisplay::text_width(const std::string& bytes) {
    const Font* with = current_font();
    if (!with) return 0;
    int total = 0;
    for (unsigned char c : bytes) {
        if (!c) break;
        const auto& glyph = with->glyphs[c];
        total += glyph.bits.empty() ? with->spacing : glyph.width + with->spacing;
    }
    return total;
}

bool SchwungDisplay::set_font(const std::string& path) {
    if (auto known = fonts.find(path); known != fonts.end()) { font = known->second; return true; }
    auto loaded = load_font(path);
    if (!loaded) return false;
    if (fonts.size() < 16) fonts[path] = loaded; // past sixteen, read again each time, as Schwung's cache does
    font = std::move(loaded);
    return true;
}

int SchwungDisplay::font_height() {
    const Font* with = current_font();
    if (!with) return 0;
    for (const auto& glyph : with->glyphs)
        if (!glyph.bits.empty()) return with->height;
    return 0;
}

// ---------------------------------------------------------------- the JS globals

namespace {

SchwungDisplay& display_of(JSContext* context, JSValue* data) {
    int64_t address = 0;
    JS_ToInt64(context, &address, data[0]);
    return *reinterpret_cast<SchwungDisplay*>(static_cast<intptr_t>(address));
}

// ToInt32 each of `count` arguments into `out`; false (nothing drawn) when one throws.
bool numbers(JSContext* context, JSValueConst* argv, int count, int* out) {
    for (int n = 0; n < count; ++n)
        if (JS_ToInt32(context, &out[n], argv[n])) return false;
    return true;
}

std::string text_of(JSContext* context, JSValueConst value, bool& ok) {
    const char* chars = JS_ToCString(context, value);
    ok = chars != nullptr;
    std::string text = chars ? chars : ""; // to the first NUL, as strlen reads it
    if (chars) JS_FreeCString(context, chars);
    return text;
}

JSValue set_pixel(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    int v[3];
    if (argc < 3 || !numbers(c, argv, 3, v)) return JS_UNDEFINED;
    display_of(c, data).set_pixel(v[0], v[1], v[2]);
    return JS_UNDEFINED;
}

JSValue draw_rect(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    int v[5];
    if (argc < 5 || !numbers(c, argv, 5, v)) return JS_UNDEFINED;
    display_of(c, data).draw_rect(v[0], v[1], v[2], v[3], v[4]);
    return JS_UNDEFINED;
}

JSValue fill_rect(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    int v[5];
    if (argc < 5 || !numbers(c, argv, 5, v)) return JS_UNDEFINED;
    display_of(c, data).fill_rect(v[0], v[1], v[2], v[3], v[4]);
    return JS_UNDEFINED;
}

JSValue draw_line(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    int v[5] {0, 0, 0, 0, 1};
    if (argc < 4 || !numbers(c, argv, argc >= 5 ? 5 : 4, v)) return JS_UNDEFINED;
    display_of(c, data).draw_line(v[0], v[1], v[2], v[3], v[4]);
    return JS_UNDEFINED;
}

JSValue clear_screen(JSContext* c, JSValueConst, int, JSValueConst*, int, JSValue* data) {
    display_of(c, data).clear();
    return JS_UNDEFINED;
}

JSValue print(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    int xy[2], colour = 0;
    if (argc < 4 || !numbers(c, argv, 2, xy)) return JS_UNDEFINED;
    bool ok = false;
    std::string text = text_of(c, argv[2], ok);
    if (!ok || JS_ToInt32(c, &colour, argv[3])) return JS_UNDEFINED;
    display_of(c, data).print(xy[0], xy[1], text, colour);
    return JS_UNDEFINED;
}

JSValue text_width(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    if (argc < 1) return JS_NewInt32(c, 0);
    bool ok = false;
    std::string text = text_of(c, argv[0], ok);
    return JS_NewInt32(c, ok ? display_of(c, data).text_width(text) : 0);
}

JSValue fill_circle(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    int v[4] {0, 0, 0, 1};
    if (argc < 3 || !numbers(c, argv, argc >= 4 ? 4 : 3, v)) return JS_UNDEFINED;
    display_of(c, data).fill_circle(v[0], v[1], v[2], v[3]);
    return JS_UNDEFINED;
}

JSValue draw_circle(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    int v[4] {0, 0, 0, 1};
    if (argc < 3 || !numbers(c, argv, argc >= 4 ? 4 : 3, v)) return JS_UNDEFINED;
    display_of(c, data).draw_circle(v[0], v[1], v[2], v[3]);
    return JS_UNDEFINED;
}

JSValue draw_arc(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    int v[6] {0, 0, 0, 0, 0, 1};
    if (argc < 5 || !numbers(c, argv, argc >= 6 ? 6 : 5, v)) return JS_UNDEFINED;
    display_of(c, data).draw_arc(v[0], v[1], v[2], v[3], v[4], v[5]);
    return JS_UNDEFINED;
}

JSValue draw_image(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    if (argc < 3) return JS_UNDEFINED;
    bool ok = false;
    std::string path = text_of(c, argv[0], ok);
    int xy[2];
    if (!ok || !numbers(c, argv + 1, 2, xy)) return JS_UNDEFINED;
    int threshold = 128, invert = 0;
    if (argc >= 4) JS_ToInt32(c, &threshold, argv[3]); // a throw leaves 0, as there
    if (argc >= 5) JS_ToInt32(c, &invert, argv[4]);
    return JS_NewBool(c, display_of(c, data).draw_image(path, xy[0], xy[1], threshold, invert));
}

JSValue set_font(JSContext* c, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    if (argc < 1) return JS_NewBool(c, 0);
    bool ok = false;
    std::string path = text_of(c, argv[0], ok);
    return JS_NewBool(c, ok && display_of(c, data).set_font(path));
}

JSValue get_font_height(JSContext* c, JSValueConst, int, JSValueConst*, int, JSValue* data) {
    return JS_NewInt32(c, display_of(c, data).font_height());
}

} // namespace

void SchwungDisplay::install(JSContext* context) {
    struct Binding { const char* name; JSCFunctionData* function; int length; };
    static const Binding bindings[] {
        {"set_pixel", app::set_pixel, 3}, {"draw_rect", app::draw_rect, 5}, {"fill_rect", app::fill_rect, 5},
        {"draw_line", app::draw_line, 5}, {"clear_screen", app::clear_screen, 0}, {"print", app::print, 4},
        {"text_width", app::text_width, 1}, {"fill_circle", app::fill_circle, 4}, {"draw_circle", app::draw_circle, 4},
        {"draw_arc", app::draw_arc, 6}, {"draw_image", app::draw_image, 5}, {"set_font", app::set_font, 1},
        {"get_font_height", app::get_font_height, 0},
    };
    JSValue global = JS_GetGlobalObject(context);
    JSValue self = JS_NewInt64(context, int64_t(reinterpret_cast<intptr_t>(this)));
    for (const auto& b : bindings) {
        JSValue function = JS_NewCFunctionData(context, b.function, b.length, 0, 1, &self);
        JS_DefinePropertyValueStr(context, function, "name", JS_NewString(context, b.name), JS_PROP_CONFIGURABLE);
        JS_SetPropertyStr(context, global, b.name, function);
    }
    JS_FreeValue(context, self);
    JS_FreeValue(context, global);
}

} // namespace app
