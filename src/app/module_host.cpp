// SPDX-License-Identifier: AGPL-3.0-only
#include "app/module_host.h"
#include <juce_core/juce_core.h>
#include <dlfcn.h>
#include "move/memory.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <thread>

namespace app {
namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- the host table

// The device page a module may read input audio from (mapped_memory): a
// copy, refreshed with each block's input before the module renders.
alignas(64) uint8_t mapped_page[4096] {};
constexpr int audio_out_offset = 256, audio_in_offset = 2048 + 256;
std::atomic<float> tempo {120.0f};
// Melodee's transport as the modules read it (set_module_transport()): playing, where, the time signature.
std::atomic<bool> transport_playing {false};
std::atomic<double> transport_beats {0.0};
std::atomic<uint8_t> transport_upper {4}, transport_lower {4};
thread_local bool on_audio_thread = false;

void host_log(const char* message) {
    // Never from the audio thread: a write() there is a dropout.
    if (!on_audio_thread && message) std::fprintf(stderr, "schwung module: %s\n", message);
}
int refuse_midi(const uint8_t*, int) { return 0; }       // no Move tracks to send to
// A module's MIDI out: onto the Move's MIDI port (set_external_midi()).
std::atomic<int (*)(const uint8_t*, int)> external_midi {nullptr};
int send_external(const uint8_t* packets, int length) {
    auto send = external_midi.load(std::memory_order_acquire);
    return send && packets && length > 0 ? send(packets, length) : 0;
}
int clock_status() { return transport_playing.load(std::memory_order_relaxed) ? schwung::clock_running : schwung::clock_stopped; }
float bpm() { return tempo.load(std::memory_order_relaxed); }
double beat_position() { return transport_beats.load(std::memory_order_relaxed); } // quarters since Play
int emit_value(void*, const char*, const char*, const char*, float, float, float, int, int) { return 0; }
void clear_source(void*, const char*) {}

const host_api_v1_t& host_table() {
    static host_api_v1_t table = [] {
        host_api_v1_t made {};
        made.api_version = schwung::host_api_version;
        made.sample_rate = 44100;
        made.frames_per_block = int(move::audio_frames);
        made.mapped_memory = mapped_page;
        made.audio_out_offset = audio_out_offset;
        made.audio_in_offset = audio_in_offset;
        made.log = host_log;
        made.midi_send_internal = refuse_midi;
        made.midi_send_external = send_external;
        made.get_clock_status = clock_status;
        made.mod_emit_value = emit_value;
        made.mod_clear_source = clear_source;
        made.mod_host_ctx = nullptr;
        made.get_bpm = bpm;
        made.midi_inject_to_move = refuse_midi;
        made.slot_recv_channel = nullptr; // no slot context, as the header allows
        made.get_beat_position = beat_position;
        return made;
    }();
    return table;
}

// ---------------------------------------------------------------- JSON

juce::var parse(const std::string& text) {
    juce::var value;
    if (text.empty() || !juce::JSON::parse(juce::String::fromUTF8(text.c_str()), value).wasOk()) return {};
    return value;
}
std::string text_of(const juce::var& value) { return value.isVoid() ? std::string() : value.toString().toStdString(); }
const juce::var& field(const juce::var& object, const char* name) { return object[juce::Identifier(name)]; }
bool has(const juce::var& object, const char* name) {
    auto* dynamic = object.getDynamicObject();
    return dynamic && dynamic->hasProperty(juce::Identifier(name));
}
std::string lower(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// A `visible_if` as Schwung's planner reads it; none: always shown.
ShownIf shown_if_from(const juce::var& entry) {
    ShownIf rule;
    const auto& given = field(entry, "visible_if");
    if (!given.isObject()) return rule;
    rule.param = text_of(field(given, "param"));
    using Test = ShownIf::Test;
    const std::pair<const char*, Test> tests[] {
        {"equals", Test::Equals}, {"not_equals", Test::NotEquals}, {"gt", Test::Greater}, {"greater_than", Test::Greater},
        {"greater", Test::Greater}, {"lt", Test::Less}, {"smaller_than", Test::Less}, {"smaller", Test::Less},
        {"truthy", Test::Truthy}, {"falsey", Test::Falsy}, {"falsy", Test::Falsy},
    };
    for (const auto& [name, test] : tests)
        if (has(given, name)) {
            const auto& value = field(given, name);
            rule.test = test;
            rule.value = value.isBool() ? (bool(value) ? "1" : "0") : text_of(value);
            if ((test == Test::Truthy || test == Test::Falsy) && value.isBool() && !bool(value))
                rule.test = test == Test::Truthy ? Test::Falsy : Test::Truthy; // "truthy": false
            break;
        }
    if (rule.param.empty()) rule.test = Test::Always;
    return rule;
}

bool param_from(const juce::var& entry, ModuleParam& out) {
    if (!entry.isObject()) return false;
    out = {};
    out.key = text_of(field(entry, "key"));
    if (out.key.empty()) return false;
    out.name = has(entry, "name") ? text_of(field(entry, "name")) : has(entry, "label") ? text_of(field(entry, "label")) : out.key;
    out.short_name = text_of(field(entry, "short_name"));
    out.live = bool(field(entry, "live"));
    auto type = lower(text_of(field(entry, "type")));
    if (type == "enum") out.kind = ModuleParam::Kind::Enum;
    else if (type == "int" || type == "integer" || type == "bool" || type == "boolean" || type == "toggle") out.kind = ModuleParam::Kind::Int;
    else if (type == "float" || type == "number" || type.empty()) out.kind = ModuleParam::Kind::Float;
    else return false; // a file path, a string: not a knob
    if (type == "bool" || type == "boolean" || type == "toggle") { out.min = 0; out.max = 1; }
    if (has(entry, "min")) out.min = double(field(entry, "min"));
    if (has(entry, "max")) out.max = double(field(entry, "max"));
    if (has(entry, "step")) out.step = double(field(entry, "step"));
    out.unit = text_of(field(entry, "unit"));
    out.options_as_string = bool(field(entry, "options_as_string"));
    if (auto* options = field(entry, "options").getArray())
        for (const auto& option : *options) out.options.push_back(text_of(option));
    if (out.kind == ModuleParam::Kind::Enum && out.options.empty()) out.kind = ModuleParam::Kind::Int;
    if (has(entry, "default")) out.take(text_of(field(entry, "default")));
    if (has(entry, "value")) out.take(text_of(field(entry, "value")));
    if (out.max < out.min) std::swap(out.min, out.max);
    return true;
}

} // namespace

void set_external_midi(int (*send)(const uint8_t*, int)) { external_midi.store(send, std::memory_order_release); }

void set_module_tempo(float bpm_value) { tempo.store(bpm_value, std::memory_order_relaxed); }

void set_module_transport(bool playing, double beats, unsigned upper, unsigned lower) {
    transport_playing.store(playing, std::memory_order_relaxed);
    transport_beats.store(beats, std::memory_order_relaxed);
    transport_upper.store(uint8_t(upper), std::memory_order_relaxed);
    transport_lower.store(uint8_t(lower), std::memory_order_relaxed);
}

// ---------------------------------------------------------------- the catalog

std::vector<ModuleInfo> scan_modules(const std::string& modules_root, const std::string& kind) {
    std::vector<ModuleInfo> found;
    std::error_code error;
    for (const auto& folder : fs::directory_iterator(fs::path(modules_root) / kind, fs::directory_options::skip_permission_denied, error)) {
        auto manifest_path = folder.path() / "module.json";
        std::ifstream in(manifest_path);
        if (!in) continue;
        ModuleInfo info;
        info.manifest.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        auto manifest = parse(info.manifest);
        if (!manifest.isObject()) continue;
        info.dir = folder.path().string();
        info.id = has(manifest, "id") ? text_of(field(manifest, "id")) : folder.path().filename().string();
        info.name = has(manifest, "name") ? text_of(field(manifest, "name")) : info.id;
        info.abbrev = text_of(field(manifest, "abbrev"));
        if (info.abbrev.empty()) {
            for (char c : info.name) if (std::isalnum(static_cast<unsigned char>(c)) && info.abbrev.size() < 4) info.abbrev += char(std::toupper(static_cast<unsigned char>(c)));
        }
        info.api = has(manifest, "api_version") ? int(field(manifest, "api_version")) : 1;
        info.dsp = (folder.path() / (has(manifest, "dsp") ? text_of(field(manifest, "dsp")) : std::string("dsp.so"))).string();
        if (kind == "audio_fx") { // Signal Chain ignores "dsp": <id>/<id>.so, where there is one
            auto chain_library = folder.path() / (info.id + ".so");
            if (fs::exists(chain_library, error)) info.dsp = chain_library.string();
        }
        found.push_back(std::move(info));
    }
    std::sort(found.begin(), found.end(), [](const ModuleInfo& a, const ModuleInfo& b) { return lower(a.name) < lower(b.name); });
    return found;
}

// ---------------------------------------------------------------- a parameter

std::string ModuleParam::text() const {
    char out[64];
    if (kind == Kind::Enum) {
        auto index = size_t(std::clamp<long>(std::lround(value), 0, long(options.size()) - 1));
        if (options_as_string && index < options.size()) return options[index];
        std::snprintf(out, sizeof out, "%zu", index);
    } else if (kind == Kind::Int) std::snprintf(out, sizeof out, "%ld", std::lround(value));
    else std::snprintf(out, sizeof out, "%.6g", value);
    return out;
}

std::string ModuleParam::shown() const {
    char out[64];
    if (trigger()) return "PUSH";
    if (kind == Kind::Enum) {
        auto index = size_t(std::clamp<long>(std::lround(value), 0, long(options.size()) - 1));
        return index < options.size() ? options[index] : "?";
    }
    if (kind == Kind::Int) std::snprintf(out, sizeof out, "%ld", std::lround(value));
    else if (max - min <= 1.0001 && unit.empty()) std::snprintf(out, sizeof out, "%.0f%%", 100.0 * (value - min) / std::max(1e-9, max - min));
    else std::snprintf(out, sizeof out, std::fabs(value) >= 100 ? "%.0f" : "%.2f", value);
    std::string shown_value = out;
    if (!unit.empty() && kind != Kind::Enum) shown_value += " " + unit;
    return shown_value;
}

bool ModuleParam::trigger() const {
    return kind == Kind::Enum && options.size() == 2 && lower(options[1]) == "trigger";
}

double ModuleParam::fraction() const {
    if (kind == Kind::Enum) return options.size() > 1 ? value / double(options.size() - 1) : 0.0;
    return max > min ? std::clamp((value - min) / (max - min), 0.0, 1.0) : 0.0;
}

void ModuleParam::turn(int detents, bool fine) {
    if (kind == Kind::Enum) { value = std::clamp(std::round(value) + detents, 0.0, double(std::max<size_t>(options.size(), 1) - 1)); return; }
    if (kind == Kind::Int) { value = std::clamp(std::round(value) + detents, min, max); return; }
    double size = step > 0 ? step : (max - min) / 100.0;
    if (fine) size /= 10.0;
    value = std::clamp(value + detents * size, min, max);
}

void ModuleParam::take(const std::string& answer) {
    if (answer.empty()) return;
    char* end = nullptr;
    double number = std::strtod(answer.c_str(), &end);
    bool numeric = end && end != answer.c_str();
    if (kind == Kind::Enum && !numeric) {
        for (size_t n = 0; n < options.size(); ++n)
            if (lower(options[n]) == lower(answer)) { value = double(n); return; }
        return;
    }
    if (numeric) value = number;
}

// ---------------------------------------------------------------- a loaded module

std::unique_ptr<LoadedModule> LoadedModule::load(const ModuleInfo& info, std::string& error) {
    if (info.api != 2) { error = "needs Schwung's v2 API"; return nullptr; }
    void* library = dlopen(info.dsp.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) { error = dlerror(); return nullptr; }
    move::pin_loaded_code(); // its code in memory before the audio thread calls it
    // Never closed: a module may have started threads of its own.
    auto init = reinterpret_cast<move_plugin_init_v2_fn>(dlsym(library, schwung::init_v2_symbol));
    if (!init) { error = "no move_plugin_init_v2"; return nullptr; }
    plugin_api_v2_t* api = init(&host_table());
    if (!api || api->api_version != schwung::plugin_api_version_2 || !api->create_instance || !api->render_block) {
        error = "not Schwung's v2 API";
        return nullptr;
    }
    void* instance = api->create_instance(info.dir.c_str(), nullptr);
    if (!instance) { error = "the module would not start"; return nullptr; }
    std::unique_ptr<LoadedModule> module(new LoadedModule());
    module->about = info;
    module->api = api;
    module->instance = instance;
    module->read_parameters();
    return module;
}

std::unique_ptr<LoadedModule> LoadedModule::load_audio_fx(const ModuleInfo& info, std::string& error) {
    if (info.api != 2) { error = "needs Schwung's v2 API"; return nullptr; }
    void* library = dlopen(info.dsp.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) { error = dlerror(); return nullptr; }
    move::pin_loaded_code(); // its code in memory before the audio thread calls it
    // Never closed, as a synth's library.
    auto init = reinterpret_cast<move_audio_fx_init_v2_fn>(dlsym(library, schwung::audio_fx_init_v2_symbol));
    if (!init) { error = "no move_audio_fx_init_v2"; return nullptr; }
    audio_fx_api_v2_t* fx = init(&host_table());
    if (!fx || fx->api_version != schwung::audio_fx_api_version_2 || !fx->create_instance || !fx->process_block) {
        error = "not Schwung's audio_fx v2 API";
        return nullptr;
    }
    void* instance = fx->create_instance(info.dir.c_str(), nullptr); // as Signal Chain: its folder, no config
    if (!instance) { error = "the effect would not start"; return nullptr; }
    std::unique_ptr<LoadedModule> module(new LoadedModule());
    module->about = info;
    module->fx_api = fx;
    module->instance = instance;
    module->read_parameters();
    return module;
}

LoadedModule::~LoadedModule() {
    if (api && instance && api->destroy_instance) api->destroy_instance(instance);
    if (fx_api && instance && fx_api->destroy_instance) fx_api->destroy_instance(instance);
}

void LoadedModule::set_now(const std::string& key, const std::string& value) {
    auto set = api ? api->set_param : fx_api ? fx_api->set_param : nullptr;
    if (set) set(instance, key.c_str(), value.c_str());
}

std::string LoadedModule::get_now(const std::string& key) {
    auto get = api ? api->get_param : fx_api ? fx_api->get_param : nullptr;
    if (!get) return {};
    std::vector<char> buffer(65536);
    int n = get(instance, key.c_str(), buffer.data(), int(buffer.size()));
    if (n <= 0 || n >= int(buffer.size())) return {};
    return std::string(buffer.data(), size_t(n));
}

ModuleParam* LoadedModule::param(const std::string& key) {
    for (auto& p : params) if (p.key == key) return &p;
    return nullptr;
}

const ModuleParam* LoadedModule::param(const std::string& key) const {
    for (const auto& p : params) if (p.key == key) return &p;
    return nullptr;
}

std::string LoadedModule::declared(const std::string& key) const {
    auto manifest = parse(about.manifest);
    const auto& value = field(field(manifest, "capabilities"), key.c_str());
    if (value.isVoid() || value.isUndefined()) return {};
    return juce::JSON::toString(value, true).toStdString();
}

bool LoadedModule::shown(const ShownIf& rule) const {
    using Test = ShownIf::Test;
    if (rule.test == Test::Always) return true;
    const ModuleParam* p = param(rule.param);
    if (!p) return true; // a value Melodee does not hold: shown, as Schwung fails open
    if (rule.test == Test::Truthy) return std::abs(p->value) > 1e-9;
    if (rule.test == Test::Falsy) return std::abs(p->value) <= 1e-9;
    char* end = nullptr;
    double number = std::strtod(rule.value.c_str(), &end);
    bool numeric = !rule.value.empty() && end && *end == '\0';
    if (numeric) {
        switch (rule.test) {
        case Test::Equals: return std::abs(p->value - number) < 1e-6;
        case Test::NotEquals: return std::abs(p->value - number) >= 1e-6;
        case Test::Greater: return p->value > number;
        case Test::Less: return p->value < number;
        default: return true;
        }
    }
    // A name: an option's ("On"), or the value as the module takes it.
    auto index = size_t(std::max(0L, std::lround(p->value)));
    std::string now = p->kind == ModuleParam::Kind::Enum && index < p->options.size() ? p->options[index] : p->text();
    bool same = lower(now) == lower(rule.value);
    if (rule.test == Test::Equals) return same;
    if (rule.test == Test::NotEquals) return !same;
    return true;
}

bool LoadedModule::level_shown(size_t index) const { return index >= levels.size() || shown(levels[index].shown_if); }

bool LoadedModule::key_shown(size_t index, const std::string& key) const {
    if (index >= levels.size()) return true;
    for (const auto& item : levels[index].items)
        if (item.kind == MenuItem::Kind::Param && item.key == key) return shown(item.shown_if);
    return true;
}

bool LoadedModule::page_shown(size_t n) const {
    if (n >= pages.size()) return false;
    const auto& page = pages[n];
    if (!level_shown(page.level)) return false;
    return page.canvas < 0 || size_t(page.canvas) >= canvases.size() || key_shown(page.level, canvases[size_t(page.canvas)].key);
}

std::vector<std::string> LoadedModule::chain_knobs() const {
    auto row = [&](bool root) -> const ModulePage* {
        for (const auto& page : pages)
            if (page.canvas < 0 && (page.level == 0) == root &&
                std::any_of(page.knobs.begin(), page.knobs.end(), [](const std::string& key) { return !key.empty(); }))
                return &page;
        return nullptr;
    };
    if (const ModulePage* page = row(true)) return page->knobs;
    if (const ModulePage* page = row(false)) return page->knobs;
    std::vector<std::string> keys;
    for (const auto& p : params)
        if (!p.selector && keys.size() < 8) keys.push_back(p.key);
    return keys;
}

std::vector<std::string> LoadedModule::page_knobs(size_t n) const {
    std::vector<std::string> keys;
    if (n >= pages.size()) return keys;
    for (const auto& key : pages[n].knobs)
        if (key.empty() || key_shown(pages[n].level, key)) keys.push_back(key); // a hole stays; a hidden key closes up
    return keys;
}

const ModuleLevel* LoadedModule::level(const std::string& id) const {
    for (const auto& one : levels) if (one.id == id) return &one;
    return nullptr;
}

size_t LoadedModule::level_index(const std::string& id) const {
    for (size_t n = 0; n < levels.size(); ++n) if (levels[n].id == id) return n;
    return 0;
}

void LoadedModule::read_parameters() {
    auto manifest = parse(about.manifest);
    const auto& capabilities = field(manifest, "capabilities");
    // What the module says now wins over what its module.json says.
    served_chain_params = get_now("chain_params");
    served_hierarchy = get_now("ui_hierarchy");
    auto chain = parse(served_chain_params);
    if (!chain.isArray()) chain = field(capabilities, "chain_params");
    auto hierarchy = parse(served_hierarchy);
    if (!hierarchy.isObject()) hierarchy = field(capabilities, "ui_hierarchy");

    std::map<std::string, std::string> max_params;
    std::vector<std::string> no_values; // declared, and not a knob's (a string, a file path): never a plain 0-1
    auto describe = [&](const juce::var& entry) {
        ModuleParam p;
        if (!param_from(entry, p)) {
            if (has(entry, "key")) no_values.push_back(text_of(field(entry, "key")));
            return;
        }
        if (param(p.key)) return;
        if (has(entry, "max_param")) max_params[p.key] = text_of(field(entry, "max_param"));
        params.push_back(std::move(p));
    };
    if (auto* entries = chain.getArray())
        for (const auto& entry : *entries) {
            if (lower(text_of(field(entry, "type"))) == "canvas" && has(entry, "canvas_script")) {
                ModuleCanvas canvas;
                canvas.key = text_of(field(entry, "key"));
                canvas.name = has(entry, "name") ? text_of(field(entry, "name")) : canvas.key;
                canvas.script = (fs::path(about.dir) / text_of(field(entry, "canvas_script"))).string();
                canvas.as_page = bool(field(entry, "as_page"));
                canvas.enterable = bool(field(entry, "enterable"));
                canvas.page_first = bool(field(entry, "page_first"));
                if (auto* keys = field(entry, "extra_keys").getArray())
                    for (const auto& key : *keys) canvas.extra_keys.push_back(text_of(key));
                canvases.push_back(std::move(canvas));
                continue;
            }
            describe(entry);
        }

    // The levels: root first, then the levels it links to, then the rest.
    std::map<std::string, std::string> labels;
    std::vector<std::pair<size_t, std::vector<std::string>>> knob_lists; // a level's knobs, holes as ""
    std::vector<std::string> selectors;
    if (const auto* declared = field(hierarchy, "levels").getDynamicObject()) {
        std::vector<std::string> order {"root"};
        if (auto* items = field(declared->getProperty("root"), "params").getArray())
            for (const auto& item : *items)
                if (has(item, "level")) order.push_back(text_of(field(item, "level")));
        for (const auto& property : declared->getProperties()) order.push_back(property.name.toString().toStdString());
        for (const auto& id : order) {
            if (level(id) || !declared->hasProperty(juce::Identifier(id))) continue;
            const auto& entry = declared->getProperty(juce::Identifier(id));
            ModuleLevel made;
            made.id = id;
            made.name = has(entry, "name") ? text_of(field(entry, "name")) : has(entry, "label") ? text_of(field(entry, "label")) : id;
            if (id == "root" && (made.name.empty() || made.name == "root")) made.name = about.name;
            made.list_param = text_of(field(entry, "list_param"));
            made.count_param = text_of(field(entry, "count_param"));
            made.name_param = text_of(field(entry, "name_param"));
            made.items_param = text_of(field(entry, "items_param"));
            made.select_param = text_of(field(entry, "select_param"));
            made.navigate_to = text_of(field(entry, "navigate_to"));
            made.shown_if = shown_if_from(entry);
            for (const auto* chooser : {&made.list_param, &made.count_param, &made.name_param, &made.items_param, &made.select_param})
                if (!chooser->empty()) selectors.push_back(*chooser);
            if (auto* items = field(entry, "params").getArray())
                for (const auto& item : *items) {
                    MenuItem added;
                    if (!item.isString()) added.shown_if = shown_if_from(item);
                    if (item.isString()) added.key = text_of(item);
                    else if (has(item, "level")) {
                        added.kind = MenuItem::Kind::Level;
                        added.key = text_of(field(item, "level"));
                        added.label = text_of(field(item, "label"));
                    } else if (has(item, "key")) {
                        added.key = text_of(field(item, "key"));
                        added.label = has(item, "label") ? text_of(field(item, "label")) : std::string();
                        bool drawn = std::any_of(canvases.begin(), canvases.end(), [&](const ModuleCanvas& canvas) { return canvas.key == added.key; });
                        bool no_value = std::find(no_values.begin(), no_values.end(), added.key) != no_values.end();
                        if (!drawn && !no_value) describe(item); // its metadata may be here rather than in chain_params; a page, a string are no value
                        if (has(item, "short_name")) labels[added.key + "\n"] = text_of(field(item, "short_name"));
                    } else continue;
                    if (added.key.empty()) continue;
                    if (added.kind == MenuItem::Kind::Param && !added.label.empty()) labels[added.key] = added.label;
                    made.items.push_back(std::move(added));
                }
            if (auto* menu = field(entry, "menu").getArray()) // its own list: the links in it (actions are Schwung's)
                for (const auto& item : *menu)
                    if (has(item, "level")) made.items.push_back({MenuItem::Kind::Level, text_of(field(item, "level")), text_of(field(item, "label"))});
            std::vector<std::string> knobs;
            if (auto* listed = field(entry, "knobs").getArray())
                for (const auto& knob : *listed) knobs.push_back(knob.isString() ? text_of(knob) : std::string()); // null: an empty knob
            knob_lists.emplace_back(levels.size(), std::move(knobs));
            levels.push_back(std::move(made));
        }
    }
    if (levels.empty()) { // no menu: one level of every parameter
        ModuleLevel root;
        root.id = "root";
        root.name = about.name;
        for (const auto& p : params) root.items.push_back({MenuItem::Kind::Param, p.key, {}});
        std::vector<std::string> knobs;
        for (const auto& p : params) knobs.push_back(p.key);
        knob_lists.emplace_back(0, std::move(knobs));
        levels.push_back(std::move(root));
    }
    source = has(capabilities, "midi_in") && !bool(field(capabilities, "midi_in"));
    const auto& root = levels.front();
    preset_param = root.list_param;
    preset_count_param = root.count_param;
    preset_name_param = root.name_param;

    // Each level's knobs, eight a page. A chooser is never a knob (it has its
    // list); a trailing empty knob describes nothing.
    for (auto& [index, knobs] : knob_lists) {
        knobs.erase(std::remove_if(knobs.begin(), knobs.end(),
                                   [&](const std::string& key) { return std::find(selectors.begin(), selectors.end(), key) != selectors.end(); }),
                    knobs.end());
        while (!knobs.empty() && knobs.back().empty()) knobs.pop_back();
        for (size_t at = 0; at < knobs.size(); at += 8) {
            ModulePage page;
            page.level = index;
            page.name = levels[index].name + (at ? " " + std::to_string(at / 8 + 1) : std::string());
            page.knobs.assign(knobs.begin() + long(at), knobs.begin() + long(std::min(knobs.size(), at + 8)));
            pages.push_back(std::move(page));
        }
    }
    // A page the module draws (Schwung's docs/CANVAS_PAGES.md): it belongs
    // to the level that lists it, else to root, and carries that level's
    // knobs. Root's go first of all with `page_first`, else last (Webstream's
    // Browse, Radio Garden's); a deeper level's go before its knob pages with
    // `page_first` (Schimera's Line, Lyrics, Mouth), else after them.
    // Several a level keep the order chain_params gives them.
    for (size_t n = 0; n < canvases.size(); ++n) {
        if (!canvases[n].as_page) continue;
        size_t owner = 0;
        for (size_t at = 0; at < levels.size(); ++at)
            if (std::any_of(levels[at].items.begin(), levels[at].items.end(),
                            [&](const MenuItem& item) { return item.kind == MenuItem::Kind::Param && item.key == canvases[n].key; })) {
                owner = at;
                break;
            }
        ModulePage page;
        page.name = canvases[n].name;
        page.canvas = int(n);
        page.level = owner;
        for (const auto& [index, knobs] : knob_lists)
            if (index == owner) page.knobs.assign(knobs.begin(), knobs.begin() + long(std::min<size_t>(knobs.size(), 8)));
        auto mine = [&](const ModulePage& one) { return one.level == owner; };
        auto first = std::find_if(pages.begin(), pages.end(), [&](const ModulePage& one) { return mine(one) && one.canvas < 0; });
        auto found = std::find_if(pages.rbegin(), pages.rend(), mine);
        auto last = found != pages.rend() ? found.base() : pages.end(); // a level with no page yet: at the end
        if (owner == 0) pages.insert(canvases[n].page_first ? pages.begin() : pages.end(), std::move(page));
        else pages.insert(canvases[n].page_first && first != pages.end() ? first : last, std::move(page));
    }
    // A knob or menu entry chain_params does not describe: a plain 0-1. A
    // page the module draws is no value (Webstream's menu lists "browse").
    auto plain = [&](const std::string& key) {
        if (key.empty() || param(key) || std::find(selectors.begin(), selectors.end(), key) != selectors.end()) return;
        if (std::find(no_values.begin(), no_values.end(), key) != no_values.end()) return; // Schimera's lyrics: a knob would write 0.01 over them
        if (std::any_of(canvases.begin(), canvases.end(), [&](const ModuleCanvas& canvas) { return canvas.key == key; })) return;
        ModuleParam p;
        p.key = p.name = key;
        params.push_back(p);
    };
    for (const auto& page : pages) for (const auto& key : page.knobs) plain(key);
    for (const auto& one : levels)
        for (const auto& item : one.items)
            if (item.kind == MenuItem::Kind::Param) plain(item.key);
    // An items list's choice (a bank, a soundfont) is kept and restored as a
    // parameter of its own, set before the rest.
    for (const auto& one : levels) {
        if (one.select_param.empty()) continue;
        ModuleParam* chosen = param(one.select_param);
        if (!chosen) {
            ModuleParam p;
            p.key = p.name = one.select_param;
            p.kind = ModuleParam::Kind::Int;
            p.max = 1e9;
            params.push_back(p);
            chosen = &params.back();
        }
        chosen->selector = true;
    }
    for (auto& p : params) {
        if (auto named = labels.find(p.key); named != labels.end()) p.label = named->second;
        if (auto shorter = labels.find(p.key + "\n"); shorter != labels.end()) p.short_name = shorter->second;
        if (auto bound = max_params.find(p.key); bound != max_params.end()) {
            auto answer = get_now(bound->second);
            if (!answer.empty()) p.max = std::max(p.min, std::strtod(answer.c_str(), nullptr) - (p.key == preset_param ? 1 : 0));
        }
        p.take(get_now(p.key));
    }
    // Selectors first, so a bank is chosen before the preset in it.
    std::stable_partition(params.begin(), params.end(), [](const ModuleParam& p) { return p.selector; });
    if (!preset_param.empty()) {
        preset = std::atoi(get_now(preset_param).c_str());
        preset_count = std::atoi(get_now(preset_count_param).c_str());
        preset_name = get_now(preset_name_param);
    }
}

// ---------------------------------------------------------------- the audio side

bool SynthPlayer::set_module(LoadedModule* module) {
    Command command;
    command.kind = Command::Kind::Module;
    command.module = module;
    return commands.push(command);
}

bool SynthPlayer::set(const std::string& key, const std::string& value) {
    Command command;
    command.kind = Command::Kind::Set;
    command.serial = ++serial;
    std::snprintf(command.key, key_size, "%s", key.c_str());
    std::snprintf(command.value, value_size, "%s", value.c_str());
    return commands.push(command);
}

void SynthPlayer::ask(const std::string& key, bool first) {
    if (auto waiting = std::find(backlog.begin(), backlog.end(), key); waiting != backlog.end()) {
        if (!first) return; // sent later than this ask: its answer is as new
        backlog.erase(waiting);
    }
    if (first) backlog.push_front(key);
    else backlog.push_back(key);
    send_asks();
}

void SynthPlayer::send_asks() {
    while (!backlog.empty() && asked - answered < answer_slots - 1) {
        Command command;
        command.kind = Command::Kind::Ask;
        command.serial = serial + 1; // its serial as sent: after every set before it
        std::snprintf(command.key, key_size, "%s", backlog.front().c_str());
        if (!commands.push(command)) return; // full of sets: the next service
        serial = command.serial;
        ++asked;
        backlog.pop_front();
    }
}

void SynthPlayer::drop_asks() { backlog.clear(); }

bool SynthPlayer::answer(Answer& out) {
    if (!answers.pop(out)) return false;
    ++answered;
    return true;
}

bool SynthPlayer::ask_long(const std::string& key) {
    if (long_state.load(std::memory_order_acquire) != 0) return false;
    std::snprintf(long_key, key_size, "%s", key.c_str());
    long_state.store(1, std::memory_order_release);
    return true;
}

bool SynthPlayer::rendering() const {
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    return now - rendered_at.load(std::memory_order_relaxed) < 50'000'000;
}

bool SynthPlayer::get(const std::string& key, std::string& out, std::chrono::milliseconds wait) {
    std::lock_guard<std::mutex> one_at_a_time(sync_lock);
    auto until = Clock::now() + wait;
    auto settle = [&](int want) {
        while (sync_state.load(std::memory_order_acquire) != want) {
            if (Clock::now() > until) return false;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return true;
    };
    if (sync_state.load(std::memory_order_acquire) == 2) sync_state.store(0, std::memory_order_release); // one that timed out
    if (!settle(0)) return false;       // still being answered: leave it be
    std::snprintf(sync_key, key_size, "%s", key.c_str());
    sync_state.store(1, std::memory_order_release);
    if (!settle(2)) return false;       // answered later, and dropped by the next caller
    out.assign(sync_buffer.data(), size_t(std::clamp(sync_length, 0, int(sync_buffer.size()) - 1)));
    sync_state.store(0, std::memory_order_release);
    return sync_length >= 0;
}

bool SynthPlayer::long_answer(std::string& out) {
    if (long_state.load(std::memory_order_acquire) != 2) return false;
    out.assign(long_buffer.data(), size_t(std::clamp(long_length, 0, int(long_buffer.size()) - 1)));
    long_state.store(0, std::memory_order_release);
    return true;
}

LoadedModule* SynthPlayer::retired() {
    LoadedModule* module = nullptr;
    return retiring.pop(module) ? module : nullptr;
}

bool SynthPlayer::slow(Slow& out) { return slows.pop(out); }

void SynthPlayer::timed(Clock::time_point begun, const char* kind, const char* key, const char* value) {
    auto took = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begun).count();
    bool ahead_of_time = player.load(std::memory_order_relaxed) == source_thread_plays; // the source's thread, rendering ahead
    if (took < long(ahead_of_time ? source_slow_us : slow_us)) return;
    Slow slow;
    slow.us = uint32_t(std::min<long long>(took, UINT32_MAX));
    std::snprintf(slow.what, sizeof slow.what, "%s%s%s%s%s", kind, key ? " " : "", key ? key : "", value ? "=" : "", value ? value : "");
    slows.push(slow); // full: dropped, the next will tell
}

SynthPlayer::SynthPlayer() : source_thread([this] { work(); }) {}

SynthPlayer::~SynthPlayer() { stop(); }

void SynthPlayer::stop() {
    stopping.store(true);
    if (source_thread.joinable()) source_thread.join();
}

bool SynthPlayer::apply() {
    on_audio_thread = true;
    Command command;
    unsigned asked = 0;
    while (asked < 4 && commands.pop(command)) { // a few reads a block: each is the module's own time
        auto begun = Clock::now();
        switch (command.kind) {
        case Command::Kind::Module: {
            if (current) retiring.push(current);
            current = command.module;
            int wanted = current && current->source ? source_thread_plays : audio_thread_plays;
            if (wanted != player.load(std::memory_order_relaxed)) {
                // Handed over: the other thread takes the commands from the next.
                if (wanted == source_thread_plays) {
                    Block stale;
                    while (source_blocks.pop(stale)) taken.fetch_add(1, std::memory_order_relaxed); // a source before's
                    primed.store(false, std::memory_order_relaxed);
                }
                player.store(wanted, std::memory_order_release);
                return false;
            }
            break;
        }
        case Command::Kind::Set:
            if (current && current->api->set_param) current->api->set_param(current->instance, command.key, command.value);
            timed(begun, "set", command.key, command.value);
            break;
        case Command::Kind::Ask: {
            ++asked;
            Answer answer;
            answer.serial = command.serial;
            std::memcpy(answer.key, command.key, key_size);
            int n = current && current->api->get_param ? current->api->get_param(current->instance, command.key, answer.value, int(value_size)) : -1;
            answer.value[n > 0 ? std::min(n, int(value_size) - 1) : 0] = 0;
            answers.push(answer);
            timed(begun, "get", command.key, nullptr);
            break;
        }
        }
    }
    // The read answered now, then the long read, after the commands: a module
    // handed over in them answers them.
    if (sync_state.load(std::memory_order_acquire) == 1) {
        auto begun = Clock::now();
        sync_length = current && current->api->get_param
                          ? current->api->get_param(current->instance, sync_key, sync_buffer.data(), int(sync_buffer.size()))
                          : -1;
        sync_state.store(2, std::memory_order_release);
        timed(begun, "get", sync_key, nullptr);
    }
    if (long_state.load(std::memory_order_acquire) == 1) {
        auto begun = Clock::now();
        long_length = current && current->api->get_param
                          ? current->api->get_param(current->instance, long_key, long_buffer.data(), int(long_buffer.size()))
                          : -1;
        long_state.store(2, std::memory_order_release);
        timed(begun, "get", long_key, nullptr);
    }
    return true;
}

void SynthPlayer::work() {
    move::prefault_stack(); // it renders ahead of the audio thread: no page faulted in mid-block
    on_audio_thread = true;
    while (!stopping.load(std::memory_order_relaxed)) {
        if (player.load(std::memory_order_acquire) != source_thread_plays || !apply()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        Midi midi;
        while (current && current->api->on_midi && source_midi.pop(midi)) current->api->on_midi(current->instance, midi.bytes, 3, schwung::midi_source_internal);
        if (!current || made.load(std::memory_order_relaxed) - taken.load(std::memory_order_acquire) >= ahead) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        Block next {};
        auto begun = Clock::now();
        current->api->render_block(current->instance, next.data(), int(move::audio_frames));
        timed(begun, "render", nullptr, nullptr);
        if (source_blocks.push(next)) {
            made.fetch_add(1, std::memory_order_release);
            primed.store(true, std::memory_order_relaxed);
        }
    }
}

void SynthPlayer::midi(uint8_t status, uint8_t data1, uint8_t data2) {
    if (player.load(std::memory_order_acquire) == source_thread_plays || !apply()) { // a source's: to its thread
        Midi queued;
        queued.bytes[0] = status;
        queued.bytes[1] = data1;
        queued.bytes[2] = data2;
        source_midi.push(queued);
        return;
    }
    if (!current || !current->api->on_midi) return;
    const uint8_t message[3] {status, data1, data2};
    auto begun = Clock::now();
    current->api->on_midi(current->instance, message, 3, schwung::midi_source_internal);
    timed(begun, (status & 0xf0) == 0x90 ? "note on" : (status & 0xf0) == 0x80 ? "note off" : "pressure", nullptr, nullptr);
}

void SynthPlayer::note_on(int note, int velocity) { midi(0x90, uint8_t(std::clamp(note, 0, 127)), uint8_t(std::clamp(velocity, 1, 127))); }
void SynthPlayer::note_off(int note) { midi(0x80, uint8_t(std::clamp(note, 0, 127)), 0); }
void SynthPlayer::pressure(int note, int value) { midi(0xa0, uint8_t(std::clamp(note, 0, 127)), uint8_t(std::clamp(value, 0, 127))); }

void module_input(const move::AudioBlock& input) {
    for (unsigned n = 0; n < input.size(); ++n) {
        auto sample = uint16_t(int16_t(input[n] >> 16));
        mapped_page[audio_in_offset + 2 * n] = uint8_t(sample);
        mapped_page[audio_in_offset + 2 * n + 1] = uint8_t(sample >> 8);
    }
}

bool SynthPlayer::next_block() {
    rendered_at.store(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(), std::memory_order_relaxed);
    if (player.load(std::memory_order_acquire) == source_thread_plays || !apply()) {
        // A source: the next block its thread made, if it is there.
        if (source_blocks.pop(block)) {
            taken.fetch_add(1, std::memory_order_release);
            return true;
        }
        if (primed.load(std::memory_order_relaxed)) late.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!current) return false;
    block.fill(0);
    auto begun = Clock::now();
    current->api->render_block(current->instance, block.data(), int(move::audio_frames));
    timed(begun, "render", nullptr, nullptr);
    return true;
}

void SynthPlayer::render(const move::AudioBlock& input, move::AudioBlock& output, float gain) {
    module_input(input);
    if (!next_block() || bypassed.load(std::memory_order_relaxed)) return;
    for (unsigned n = 0; n < block.size(); ++n) {
        int64_t mixed = int64_t(output[n]) + int64_t(float(block[n]) * gain * 65536.0f);
        output[n] = int32_t(std::clamp<int64_t>(mixed, INT32_MIN + 1, INT32_MAX));
    }
}

void SynthPlayer::render_add(float* left, float* right, unsigned frames) {
    constexpr float scale = 1.0f / 32768.0f;
    while (frames) {
        if (block_used >= move::audio_frames) {
            if (!next_block()) block.fill(0);
            block_used = 0;
        }
        unsigned n = std::min(frames, move::audio_frames - block_used);
        const int16_t* from = block.data() + 2 * block_used;
        for (unsigned f = 0; f < n && !bypassed.load(std::memory_order_relaxed); ++f) {
            left[f] += float(from[2 * f]) * scale;
            right[f] += float(from[2 * f + 1]) * scale;
        }
        left += n;
        right += n;
        frames -= n;
        block_used += n;
    }
}

} // namespace app

// What a module reads of Move's settings (Schwung's move_info.h): it looks
// for this function in the process before falling back to a file the last
// Schwung session left behind, frozen. Melodee's tempo; the rest unknown.
namespace {
struct MoveInfoTrack {
    char name[32];
    int16_t color_id;
    int8_t type;
    uint8_t muted, soloed, selected;
    uint8_t pad[2];
    float volume_db;
};
struct MoveInfo {
    uint32_t size, version, changes;
    uint8_t valid, playing, metronome_on, midi_clock_sync, input_monitoring;
    int8_t root_note, selected_track, global_quant;
    uint8_t ts_upper, ts_lower;
    uint8_t pad[2];
    float tempo, groove, master_db;
    char scale[24];
    char global_quant_name[24];
    double song_beats;
    MoveInfoTrack track[4];
};
static_assert(sizeof(MoveInfo) == 272, "Schwung's move_info_t, version 1");
} // namespace

extern "C" __attribute__((visibility("default"))) int schwung_move_info(void* out, size_t capacity) {
    if (!out || capacity < 8) return 0;
    MoveInfo info {};
    info.size = sizeof info;
    info.version = 1;
    info.valid = 1;
    info.playing = app::transport_playing.load(std::memory_order_relaxed);
    info.metronome_on = info.midi_clock_sync = info.input_monitoring = 255;
    info.root_note = info.selected_track = info.global_quant = -1;
    info.ts_upper = app::transport_upper.load(std::memory_order_relaxed);
    info.ts_lower = app::transport_lower.load(std::memory_order_relaxed);
    info.tempo = app::tempo.load(std::memory_order_relaxed);
    info.groove = -1;
    info.master_db = -1000;
    info.song_beats = app::transport_playing.load(std::memory_order_relaxed) ? app::transport_beats.load(std::memory_order_relaxed) : -1;
    for (auto& track : info.track) { track.color_id = -1; track.type = -1; track.muted = track.soloed = track.selected = 255; track.volume_db = -1000; }
    std::memcpy(out, &info, std::min(capacity, sizeof info));
    return 1;
}
