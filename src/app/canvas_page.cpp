// SPDX-License-Identifier: AGPL-3.0-only
#include "app/canvas_page.h"
#include "move/font.h"
#include <quickjs-libc.h>
#include <quickjs.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

namespace app {
namespace {

// ---------------------------------------------------------------- the font

// Schwung's: a strip of equal cells, one a character of the .dat file's
// list, each glyph trimmed to its pixels and followed by one blank column
// (Schwung's js_display.c). Melodee's own 5 x 7 where the Move has none.
struct Glyph { int width = 0; std::vector<uint8_t> bits; };
struct PageFont {
    int height = move::glyph_height, spacing = 1;
    std::array<Glyph, 256> glyphs;
};

constexpr const char* schwung_font = "/data/UserData/schwung/host/font.png";

bool read_schwung_font(PageFont& font) {
    juce::File png(schwung_font), list_file(juce::String(schwung_font) + ".dat");
    if (!png.existsAsFile() || !list_file.existsAsFile()) return false;
    auto image = juce::ImageFileFormat::loadFrom(png);
    juce::MemoryBlock bytes;
    if (!image.isValid() || !list_file.loadFileAsData(bytes)) return false;
    auto list = juce::String::fromUTF8(static_cast<const char*>(bytes.getData()), int(bytes.getSize())).upToFirstOccurrenceOf("\n", false, false);
    int count = list.length(), cell = count ? image.getWidth() / count : 0;
    if (cell <= 0) return false;
    font.height = image.getHeight();
    juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    for (int n = 0; n < count; ++n) {
        auto code = list[n];
        if (code >= 256) continue;
        int x0 = n * cell, start = -1, end = -1;
        for (int x = 0; x < cell; ++x)
            for (int y = 0; y < font.height; ++y)
                if (pixels.getPixelColour(x0 + x, y).getAlpha() > 0) {
                    if (start < 0) start = x;
                    end = x;
                    break;
                }
        Glyph glyph;
        if (start < 0) { // blank: a space as wide as the cell
            glyph.width = cell;
            glyph.bits.assign(size_t(cell * font.height), 0);
        } else {
            glyph.width = end - start + 1;
            for (int y = 0; y < font.height; ++y)
                for (int x = 0; x < glyph.width; ++x) glyph.bits.push_back(pixels.getPixelColour(x0 + start + x, y).getAlpha() > 0);
        }
        font.glyphs[size_t(code)] = std::move(glyph);
    }
    return true;
}

const PageFont& page_font() {
    static const PageFont font = [] {
        PageFont made;
        if (read_schwung_font(made)) return made;
        for (int c = 32; c < 127; ++c) {
            juce::Image image(juce::Image::ARGB, move::glyph_advance, move::glyph_height, true);
            {
                juce::Graphics g(image);
                g.setColour(juce::Colours::white);
                move::draw_text(g, 0, 0, std::string(1, char(c)));
            }
            Glyph glyph;
            glyph.width = move::glyph_width;
            for (int y = 0; y < move::glyph_height; ++y)
                for (int x = 0; x < move::glyph_width; ++x) glyph.bits.push_back(image.getPixelAt(x, y).getAlpha() > 127);
            made.glyphs[size_t(c)] = std::move(glyph);
        }
        return made;
    }();
    return font;
}

std::string exception_text(JSContext* context) {
    JSValue exception = JS_GetException(context);
    std::string text;
    if (const char* message = JS_ToCString(context, exception)) { text = message; JS_FreeCString(context, message); }
    JSValue stack = JS_GetPropertyStr(context, exception, "stack");
    if (!JS_IsUndefined(stack))
        if (const char* where = JS_ToCString(context, stack)) { text += "\n"; text += where; JS_FreeCString(context, where); }
    JS_FreeValue(context, stack);
    JS_FreeValue(context, exception);
    return text;
}

std::string string_of(JSContext* context, JSValueConst value) {
    std::string text;
    if (const char* chars = JS_ToCString(context, value)) { text = chars; JS_FreeCString(context, chars); }
    return text;
}

int int_of(JSContext* context, JSValueConst value, int otherwise = 0) {
    int32_t number = otherwise;
    if (JS_IsUndefined(value) || JS_ToInt32(context, &number, value)) return otherwise;
    return number;
}

} // namespace

// ---------------------------------------------------------------- the engine

struct CanvasPage::Engine {
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    JSValue overlay = JS_UNDEFINED, state = JS_UNDEFINED, ctx = JS_UNDEFINED;
    std::set<std::string> broken; // hooks that threw: not called again
    std::chrono::steady_clock::time_point deadline {};

    ~Engine() {
        if (context) {
            JS_FreeValue(context, ctx);
            JS_FreeValue(context, state);
            JS_FreeValue(context, overlay);
            JS_FreeContext(context);
        }
        if (runtime) {
            js_std_free_handlers(runtime);
            JS_FreeRuntime(runtime);
        }
    }

    // A hook, if the page has it and it has not failed; the jobs it queued
    // run before it returns.
    JSValue call(const char* hook, std::initializer_list<JSValue> arguments) {
        if (broken.count(hook)) return JS_UNDEFINED;
        JSValue function = JS_GetPropertyStr(context, overlay, hook);
        if (!JS_IsFunction(context, function)) { JS_FreeValue(context, function); return JS_UNDEFINED; }
        JS_UpdateStackTop(runtime); // whichever thread holds the app's lock calls it
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::vector<JSValueConst> argv(arguments.begin(), arguments.end());
        JSValue result = JS_Call(context, function, overlay, int(argv.size()), argv.data());
        JS_FreeValue(context, function);
        if (JS_IsException(result)) {
            std::fprintf(stderr, "melodee-move: page script's %s failed, not called again: %s\n", hook, exception_text(context).c_str());
            broken.insert(hook);
            result = JS_UNDEFINED;
        }
        JSContext* job_context = nullptr;
        while (JS_ExecutePendingJob(runtime, &job_context) > 0) {}
        return result;
    }
};

// What the page's ctx functions reach: the page, through the context.
struct CanvasBindings {
    static CanvasPage& page(JSContext* context) { return *static_cast<CanvasPage*>(JS_GetContextOpaque(context)); }

    static void set_pixel(CanvasPage& on, int x, int y, int value) {
        if (x >= 0 && x < CanvasPage::width && y >= 0 && y < CanvasPage::height) on.screen[size_t(y * CanvasPage::width + x)] = value ? 1 : 0;
    }

    static JSValue fill_rect(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 4) return JS_UNDEFINED;
        int x = int_of(context, argv[0]), y = int_of(context, argv[1]), w = int_of(context, argv[2]), h = int_of(context, argv[3]);
        int value = argc > 4 ? int_of(context, argv[4], 1) : 1;
        auto& on = page(context);
        for (int row = std::max(0, y); row < std::min(CanvasPage::height, y + h); ++row)
            for (int column = std::max(0, x); column < std::min(CanvasPage::width, x + w); ++column) set_pixel(on, column, row, value);
        return JS_UNDEFINED;
    }

    static JSValue pixel(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc >= 2) set_pixel(page(context), int_of(context, argv[0]), int_of(context, argv[1]), argc > 2 ? int_of(context, argv[2], 1) : 1);
        return JS_UNDEFINED;
    }

    static JSValue line(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 4) return JS_UNDEFINED;
        int x0 = int_of(context, argv[0]), y0 = int_of(context, argv[1]), x1 = int_of(context, argv[2]), y1 = int_of(context, argv[3]);
        int value = argc > 4 ? int_of(context, argv[4], 1) : 1;
        int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, error = dx + dy;
        for (int steps = 0; steps < 1024; ++steps) {
            set_pixel(page(context), x0, y0, value);
            if (x0 == x1 && y0 == y1) break;
            int twice = 2 * error;
            if (twice >= dy) { error += dy; x0 += sx; }
            if (twice <= dx) { error += dx; y0 += sy; }
        }
        return JS_UNDEFINED;
    }

    static JSValue print(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 3) return JS_UNDEFINED;
        int x = int_of(context, argv[0]), y = int_of(context, argv[1]);
        int value = argc > 3 ? int_of(context, argv[3], 1) : 1;
        const auto& font = page_font();
        auto& on = page(context);
        for (unsigned char c : string_of(context, argv[2])) {
            const auto& glyph = font.glyphs[c];
            for (int row = 0; row < font.height && !glyph.bits.empty(); ++row)
                for (int column = 0; column < glyph.width; ++column)
                    if (glyph.bits[size_t(row * glyph.width + column)]) set_pixel(on, x + column, y + row, value);
            x += glyph.width + font.spacing;
        }
        return JS_UNDEFINED;
    }

    static JSValue text_width(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 1) return JS_NewInt32(context, 0);
        const auto& font = page_font();
        int width = 0;
        for (unsigned char c : string_of(context, argv[0])) width += font.glyphs[c].width + font.spacing;
        return JS_NewInt32(context, width);
    }

    static JSValue get_param(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 1) return JS_NULL;
        auto& on = page(context);
        if (on.host.read) {
            auto answer = on.host.read(string_of(context, argv[0]));
            return answer ? JS_NewStringLen(context, answer->data(), answer->size()) : JS_NULL;
        }
        auto value = on.host.get ? on.host.get(string_of(context, argv[0])) : std::string();
        return value.empty() ? JS_NULL : JS_NewStringLen(context, value.data(), value.size());
    }

    static JSValue set_param(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        auto& on = page(context);
        if (argc >= 2 && on.host.set) on.host.set(string_of(context, argv[0]), string_of(context, argv[1]));
        return JS_UNDEFINED;
    }

    static JSValue open_text_entry(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        auto& on = page(context);
        if (!on.host.text_entry) return JS_UNDEFINED;
        std::string title, initial;
        if (argc >= 1 && JS_IsObject(argv[0])) {
            JSValue t = JS_GetPropertyStr(context, argv[0], "title"), i = JS_GetPropertyStr(context, argv[0], "initial");
            if (!JS_IsUndefined(t)) title = string_of(context, t);
            if (!JS_IsUndefined(i)) initial = string_of(context, i);
            JS_FreeValue(context, t);
            JS_FreeValue(context, i);
        }
        on.host.text_entry(title, initial);
        return JS_UNDEFINED;
    }

    static JSValue open_file(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        auto& on = page(context);
        if (argc >= 1 && on.host.open_file) on.host.open_file(string_of(context, argv[0]));
        return JS_UNDEFINED;
    }

    static int interrupt(JSRuntime*, void* opaque) {
        auto* engine = static_cast<CanvasPage::Engine*>(opaque);
        return std::chrono::steady_clock::now() > engine->deadline; // a hook stuck in a loop
    }
};

// ---------------------------------------------------------------- the page

std::unique_ptr<CanvasPage> CanvasPage::load(const std::string& script, Host host, std::string& error) {
    std::ifstream in(script, std::ios::binary);
    if (!in) { error = "no " + script; return nullptr; }
    std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return load_source(source, script, std::move(host), error);
}

std::unique_ptr<CanvasPage> CanvasPage::load_source(const std::string& source, const std::string& script, Host host, std::string& error) {

    std::unique_ptr<CanvasPage> page(new CanvasPage());
    page->host = std::move(host);
    page->engine = std::make_unique<Engine>();
    auto& engine = *page->engine;
    engine.runtime = JS_NewRuntime();
    if (!engine.runtime) { error = "no memory for a script"; return nullptr; }
    JS_SetMemoryLimit(engine.runtime, 64u << 20);
    JS_SetMaxStackSize(engine.runtime, 1u << 20);
    js_std_init_handlers(engine.runtime);
    JS_SetModuleLoaderFunc(engine.runtime, nullptr, js_module_loader, nullptr);
    JS_SetInterruptHandler(engine.runtime, CanvasBindings::interrupt, &engine);
    engine.context = JS_NewContext(engine.runtime);
    if (!engine.context) { error = "no memory for a script"; return nullptr; }
    JSContext* context = engine.context;
    JS_SetContextOpaque(context, page.get());
    js_init_module_std(context, "std");
    js_init_module_os(context, "os");
    js_std_add_helpers(context, 0, nullptr);

    // The script, as a module (it imports std and os).
    engine.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    JSValue compiled = JS_Eval(context, source.c_str(), source.size(), script.c_str(), JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) { error = exception_text(context); return nullptr; }
    js_module_set_import_meta(context, compiled, 1, 1);
    JSValue result = js_std_await(context, JS_EvalFunction(context, compiled));
    if (JS_IsException(result)) { error = exception_text(context); return nullptr; }
    JS_FreeValue(context, result);

    JSValue global = JS_GetGlobalObject(context);
    engine.overlay = JS_GetPropertyStr(context, global, "canvas_overlay");
    JS_FreeValue(context, global);
    if (!JS_IsObject(engine.overlay)) { error = "the script sets no canvas_overlay"; return nullptr; }

    engine.state = JS_NewObject(context);
    engine.ctx = JS_NewObject(context);
    struct Function { const char* name; JSCFunction* function; int length; };
    const Function functions[] {
        {"fillRect", CanvasBindings::fill_rect, 5}, {"setPixel", CanvasBindings::pixel, 3}, {"line", CanvasBindings::line, 5},
        {"print", CanvasBindings::print, 4}, {"textWidth", CanvasBindings::text_width, 1}, {"getParam", CanvasBindings::get_param, 1},
        {"setParam", CanvasBindings::set_param, 2}, {"openTextEntry", CanvasBindings::open_text_entry, 1},
        {"openFileInTool", CanvasBindings::open_file, 2},
    };
    for (const auto& f : functions) JS_SetPropertyStr(context, engine.ctx, f.name, JS_NewCFunction(context, f.function, f.name, f.length));
    JS_SetPropertyStr(context, engine.ctx, "state", JS_DupValue(context, engine.state));
    JS_SetPropertyStr(context, engine.ctx, "width", JS_NewInt32(context, width));
    JS_SetPropertyStr(context, engine.ctx, "height", JS_NewInt32(context, height));
    return page;
}

CanvasPage::~CanvasPage() = default;

void CanvasPage::draw(juce::Graphics& g, const std::map<std::string, std::string>& values, const Frame& frame) {
    screen.fill(0);
    JSContext* context = engine->context;
    // The band, on ctx as Schwung's frame-scoped ctx has it: a page lays
    // itself out from ctx.width and ctx.height.
    JS_SetPropertyStr(context, engine->ctx, "width", JS_NewInt32(context, frame.width));
    JS_SetPropertyStr(context, engine->ctx, "height", JS_NewInt32(context, frame.height));
    JSValue info = JS_NewObject(context);
    JS_SetPropertyStr(context, info, "key", JS_NewStringLen(context, frame.key.data(), frame.key.size()));
    JS_SetPropertyStr(context, info, "state", JS_DupValue(context, engine->state));
    JS_SetPropertyStr(context, info, "nowMs", JS_NewFloat64(context, double(juce::Time::currentTimeMillis())));
    JS_SetPropertyStr(context, info, "x", JS_NewInt32(context, frame.x));
    JS_SetPropertyStr(context, info, "y", JS_NewInt32(context, frame.y));
    JS_SetPropertyStr(context, info, "width", JS_NewInt32(context, frame.width));
    JS_SetPropertyStr(context, info, "height", JS_NewInt32(context, frame.height));
    JS_SetPropertyStr(context, info, "touched", JS_NewInt32(context, -1));
    JS_SetPropertyStr(context, info, "preset", JS_NULL);
    JSValue keys = JS_NewArray(context);
    for (uint32_t n = 0; n < frame.keys.size(); ++n)
        JS_SetPropertyUint32(context, keys, n, JS_NewStringLen(context, frame.keys[n].data(), frame.keys[n].size()));
    JS_SetPropertyStr(context, info, "keys", keys);
    JSValue given = JS_NewObject(context), base = JS_NewObject(context);
    for (const auto& [key, value] : values) {
        JS_SetPropertyStr(context, given, key.c_str(), JS_NewStringLen(context, value.data(), value.size()));
        JS_SetPropertyStr(context, base, key.c_str(), JS_NewStringLen(context, value.data(), value.size())); // nothing modulates here
    }
    JS_SetPropertyStr(context, info, "values", given);
    JS_SetPropertyStr(context, info, "base", base);
    JS_FreeValue(context, engine->call("drawPage", {engine->ctx, info}));
    JS_FreeValue(context, info);

    juce::Image image(juce::Image::ARGB, width, height, false);
    {
        juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) pixels.setPixelColour(x, y, screen[size_t(y * width + x)] ? juce::Colours::white : juce::Colours::black);
    }
    g.drawImageAt(image, 0, 0);
}

void CanvasPage::midi(uint8_t status, uint8_t number, uint8_t value) {
    JSContext* context = engine->context;
    JSValue message = JS_NewObject(context), data = JS_NewArray(context);
    const uint8_t bytes[] {status, number, value};
    for (uint32_t n = 0; n < 3; ++n) JS_SetPropertyUint32(context, data, n, JS_NewInt32(context, bytes[n]));
    JS_SetPropertyStr(context, message, "data", data);
    JS_FreeValue(context, engine->call("onMidi", {engine->ctx, message}));
    JS_FreeValue(context, message);
    if (host.changed) host.changed();
}

bool CanvasPage::back() {
    JSValue result = engine->call("handleBack", {engine->ctx});
    bool stay = JS_IsBool(result) && JS_ToBool(engine->context, result); // only `true` keeps the page, as Schwung reads it
    JS_FreeValue(engine->context, result);
    if (host.changed) host.changed();
    return stay;
}

void CanvasPage::text_entered(const std::string& text, bool cancelled) {
    JSContext* context = engine->context;
    JSValue answer = JS_NewObject(context);
    JS_SetPropertyStr(context, answer, "text", JS_NewStringLen(context, text.data(), text.size()));
    JS_SetPropertyStr(context, answer, "cancelled", JS_NewBool(context, cancelled));
    JS_FreeValue(context, engine->call("onTextEntry", {engine->ctx, answer}));
    JS_FreeValue(context, answer);
    if (host.changed) host.changed();
}

std::string CanvasPage::saved_state() const {
    JSContext* context = engine->context;
    JSValue text = JS_JSONStringify(context, engine->state, JS_UNDEFINED, JS_UNDEFINED);
    std::string saved;
    if (JS_IsException(text)) JS_FreeValue(context, JS_GetException(context)); // a cycle, a BigInt: not kept
    else if (JS_IsString(text)) {
        size_t length = 0;
        if (const char* bytes = JS_ToCStringLen(context, &length, text)) {
            saved.assign(bytes, length);
            JS_FreeCString(context, bytes);
        }
    }
    JS_FreeValue(context, text);
    return saved == "{}" ? std::string() : saved;
}

void CanvasPage::restore_state(const std::string& json) {
    if (json.empty()) return;
    JSContext* context = engine->context;
    JSValue saved = JS_ParseJSON(context, json.c_str(), json.size(), "<saved state>");
    if (JS_IsException(saved)) { JS_FreeValue(context, JS_GetException(context)); return; }
    JSPropertyEnum* names = nullptr;
    uint32_t count = 0;
    if (JS_IsObject(saved) && !JS_IsArray(context, saved)
        && JS_GetOwnPropertyNames(context, &names, &count, saved, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
        for (uint32_t n = 0; n < count; ++n) {
            JS_SetProperty(context, engine->state, names[n].atom, JS_GetProperty(context, saved, names[n].atom));
            JS_FreeAtom(context, names[n].atom);
        }
        js_free(context, names);
    }
    JS_FreeValue(context, saved);
}

} // namespace app
