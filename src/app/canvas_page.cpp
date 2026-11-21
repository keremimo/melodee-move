// SPDX-License-Identifier: AGPL-3.0-only
#include "app/canvas_page.h"
#include "app/schwung_display.h"
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

// ---------------------------------------------------------------- modules

// A module's name, as QuickJS's own default makes it (a relative one from
// the importing module's folder), then Schwung's folder moved where
// MELODEE_SCHWUNG_ROOT says (schwung_display.h): Schwung's code imports its
// own by absolute paths under /data/UserData/schwung.
char* module_name(JSContext* context, const char* base, const char* name, void*) {
    std::string out;
    if (name[0] != '.') out = name;
    else {
        std::string folder = base;
        auto slash = folder.rfind('/');
        folder = slash == std::string::npos ? std::string() : folder.substr(0, slash);
        std::string rest = name;
        for (;;) {
            if (rest.rfind("./", 0) == 0) rest = rest.substr(2);
            else if (rest.rfind("../", 0) == 0 && !folder.empty() && folder != "." && folder != "..") {
                auto up = folder.rfind('/');
                folder = up == std::string::npos ? std::string() : folder.substr(0, up);
                rest = rest.substr(3);
            } else break;
        }
        out = folder.empty() ? rest : folder + "/" + rest;
    }
    out = SchwungDisplay::schwung_path(out);
    return js_strdup(context, out.c_str());
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

    // ctx's drawing, on Schwung's screen with Schwung's shapes and font
    // (schwung_display.h); the colour may be left out (white), as a page's
    // frame-scoped ctx under Schwung allows.
    static JSValue fill_rect(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 4) return JS_UNDEFINED;
        page(context).display.fill_rect(int_of(context, argv[0]), int_of(context, argv[1]), int_of(context, argv[2]), int_of(context, argv[3]),
                                        argc > 4 ? int_of(context, argv[4], 1) : 1);
        return JS_UNDEFINED;
    }

    static JSValue pixel(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc >= 2) page(context).display.set_pixel(int_of(context, argv[0]), int_of(context, argv[1]), argc > 2 ? int_of(context, argv[2], 1) : 1);
        return JS_UNDEFINED;
    }

    static JSValue line(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 4) return JS_UNDEFINED;
        page(context).display.draw_line(int_of(context, argv[0]), int_of(context, argv[1]), int_of(context, argv[2]), int_of(context, argv[3]),
                                        argc > 4 ? int_of(context, argv[4], 1) : 1);
        return JS_UNDEFINED;
    }

    static JSValue print(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 3) return JS_UNDEFINED;
        page(context).display.print(int_of(context, argv[0]), int_of(context, argv[1]), string_of(context, argv[2]), argc > 3 ? int_of(context, argv[3], 1) : 1);
        return JS_UNDEFINED;
    }

    static JSValue text_width(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 1) return JS_NewInt32(context, 0);
        return JS_NewInt32(context, page(context).display.text_width(string_of(context, argv[0])));
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

    // shadow_load_ui_module(path): a module script loaded as Schwung's
    // shadow_ui.c loads one, under its path and a count ("canvas.js#3") so
    // QuickJS's module cache never hands back an earlier load; true when it
    // ran (its top-level awaits awaited).
    static JSValue load_ui_module(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 1) return JS_FALSE;
        std::string path = string_of(context, argv[0]);
        std::ifstream in(path, std::ios::binary);
        if (!in) { std::perror(path.c_str()); return JS_FALSE; }
        std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        static int loads = 0;
        std::string name = path + "#" + std::to_string(++loads);
        JSValue value = JS_Eval(context, source.c_str(), source.size(), name.c_str(),
                                JS_EVAL_FLAG_STRICT | JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (!JS_IsException(value)) {
            js_module_set_import_meta(context, value, 1, 1);
            value = JS_EvalFunction(context, value);
        }
        value = js_std_await(context, value);
        bool ran = !JS_IsException(value);
        if (!ran) std::fprintf(stderr, "melodee-move: %s: %s\n", path.c_str(), exception_text(context).c_str());
        JS_FreeValue(context, value);
        return JS_NewBool(context, ran);
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
    JS_SetModuleLoaderFunc(engine.runtime, module_name, js_module_loader, nullptr);
    JS_SetInterruptHandler(engine.runtime, CanvasBindings::interrupt, &engine);
    engine.context = JS_NewContext(engine.runtime);
    if (!engine.context) { error = "no memory for a script"; return nullptr; }
    JSContext* context = engine.context;
    JS_SetContextOpaque(context, page.get());
    js_init_module_std(context, "std");
    js_init_module_os(context, "os");
    js_std_add_helpers(context, 0, nullptr);
    page->display.install(context); // after the helpers: print is the screen's, as in Schwung's shadow UI
    {
        JSValue global = JS_GetGlobalObject(context);
        JS_SetPropertyStr(context, global, "shadow_load_ui_module", JS_NewCFunction(context, CanvasBindings::load_ui_module, "shadow_load_ui_module", 1));
        JS_FreeValue(context, global);
    }

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
    if (frame.clear) display.clear();
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
            for (int x = 0; x < width; ++x) pixels.setPixelColour(x, y, display.pixels[size_t(y * width + x)] ? juce::Colours::white : juce::Colours::black);
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
