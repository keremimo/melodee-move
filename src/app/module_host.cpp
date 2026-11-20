// SPDX-License-Identifier: AGPL-3.0-only
#include "app/module_host.h"
#include <juce_core/juce_core.h>
#include <dlfcn.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

namespace app {
namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- the host table

// The device page a module may read input audio from (mapped_memory): a
// copy, refreshed with each block's input before the module renders.
alignas(64) uint8_t mapped_page[4096] {};
constexpr int audio_out_offset = 256, audio_in_offset = 2048 + 256;
std::atomic<float> tempo {120.0f};
thread_local bool on_audio_thread = false;

void host_log(const char* message) {
    // Never from the audio thread: a write() there is a dropout.
    if (!on_audio_thread && message) std::fprintf(stderr, "schwung module: %s\n", message);
}
int refuse_midi(const uint8_t*, int) { return 0; }       // no Move tracks, no external send yet
int clock_status() { return schwung::clock_stopped; }    // no transport yet
float bpm() { return tempo.load(std::memory_order_relaxed); }
double beat_position() { return 0.0; }
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
        made.midi_send_external = refuse_midi;
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

bool param_from(const juce::var& entry, ModuleParam& out) {
    if (!entry.isObject()) return false;
    out = {};
    out.key = text_of(field(entry, "key"));
    if (out.key.empty()) return false;
    out.name = has(entry, "name") ? text_of(field(entry, "name")) : has(entry, "label") ? text_of(field(entry, "label")) : out.key;
    out.short_name = text_of(field(entry, "short_name"));
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

void set_module_tempo(float bpm_value) { tempo.store(bpm_value, std::memory_order_relaxed); }

// ---------------------------------------------------------------- the catalog

std::vector<ModuleInfo> scan_modules(const std::string& modules_root) {
    std::vector<ModuleInfo> found;
    std::error_code error;
    for (const auto& folder : fs::directory_iterator(fs::path(modules_root) / "sound_generators", fs::directory_options::skip_permission_denied, error)) {
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

LoadedModule::~LoadedModule() {
    if (api && instance && api->destroy_instance) api->destroy_instance(instance);
}

void LoadedModule::set_now(const std::string& key, const std::string& value) {
    if (api->set_param) api->set_param(instance, key.c_str(), value.c_str());
}

std::string LoadedModule::get_now(const std::string& key) {
    if (!api->get_param) return {};
    std::vector<char> buffer(65536);
    int n = api->get_param(instance, key.c_str(), buffer.data(), int(buffer.size()));
    if (n <= 0 || n >= int(buffer.size())) return {};
    return std::string(buffer.data(), size_t(n));
}

ModuleParam* LoadedModule::param(const std::string& key) {
    for (auto& p : params) if (p.key == key) return &p;
    return nullptr;
}

void LoadedModule::read_parameters() {
    auto manifest = parse(about.manifest);
    const auto& capabilities = field(manifest, "capabilities");
    // What the module says now wins over what its module.json says.
    auto chain = parse(get_now("chain_params"));
    if (!chain.isArray()) chain = field(capabilities, "chain_params");
    auto hierarchy = parse(get_now("ui_hierarchy"));
    if (!hierarchy.isObject()) hierarchy = field(capabilities, "ui_hierarchy");

    std::map<std::string, std::string> max_params;
    if (auto* entries = chain.getArray())
        for (const auto& entry : *entries) {
            ModuleParam p;
            if (!param_from(entry, p) || param(p.key)) continue;
            if (has(entry, "max_param")) max_params[p.key] = text_of(field(entry, "max_param"));
            params.push_back(std::move(p));
        }

    // Its pages: the root level, then the levels it names, then the rest;
    // each page the level's knobs, eight at most.
    std::map<std::string, std::string> short_names;
    if (const auto* levels = field(hierarchy, "levels").getDynamicObject()) {
        std::vector<std::string> order {"root"};
        const auto& root = levels->getProperty("root");
        if (auto* items = field(root, "params").getArray())
            for (const auto& item : *items)
                if (has(item, "level")) order.push_back(text_of(field(item, "level")));
        for (const auto& property : levels->getProperties()) order.push_back(property.name.toString().toStdString());
        preset_param = text_of(field(root, "list_param"));
        preset_count_param = text_of(field(root, "count_param"));
        preset_name_param = text_of(field(root, "name_param"));
        std::vector<std::string> done;
        for (const auto& name : order) {
            if (std::find(done.begin(), done.end(), name) != done.end() || !levels->hasProperty(juce::Identifier(name))) continue;
            done.push_back(name);
            const auto& level = levels->getProperty(juce::Identifier(name));
            ModulePage page;
            page.name = has(level, "name") ? text_of(field(level, "name")) : name;
            if (auto* knobs = field(level, "knobs").getArray())
                for (const auto& knob : *knobs) if (page.knobs.size() < 8) page.knobs.push_back(text_of(knob));
            if (auto* items = field(level, "params").getArray())
                for (const auto& item : *items) {
                    if (item.isString() && page.knobs.size() < 8 && !field(level, "knobs").isArray()) page.knobs.push_back(text_of(item));
                    if (has(item, "key") && has(item, "short_name")) short_names[text_of(field(item, "key"))] = text_of(field(item, "short_name"));
                }
            if (!page.knobs.empty()) pages.push_back(std::move(page));
        }
    }
    // A knob a page names but chain_params does not describe: a plain 0-1.
    for (const auto& page : pages)
        for (const auto& key : page.knobs)
            if (!param(key)) { ModuleParam p; p.key = p.name = key; params.push_back(p); }
    if (pages.empty())
        for (size_t at = 0; at < params.size(); at += 8) {
            ModulePage page;
            page.name = about.name + " " + std::to_string(at / 8 + 1);
            for (size_t n = at; n < std::min(params.size(), at + 8); ++n) page.knobs.push_back(params[n].key);
            pages.push_back(std::move(page));
        }
    for (auto& p : params) {
        if (auto shorter = short_names.find(p.key); shorter != short_names.end()) p.short_name = shorter->second;
        if (auto bound = max_params.find(p.key); bound != max_params.end()) {
            auto answer = get_now(bound->second);
            if (!answer.empty()) p.max = std::max(p.min, std::strtod(answer.c_str(), nullptr) - (p.key == preset_param ? 1 : 0));
        }
        p.take(get_now(p.key));
    }
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
    std::snprintf(command.key, key_size, "%s", key.c_str());
    std::snprintf(command.value, value_size, "%s", value.c_str());
    return commands.push(command);
}

bool SynthPlayer::ask(const std::string& key) {
    Command command;
    command.kind = Command::Kind::Ask;
    std::snprintf(command.key, key_size, "%s", key.c_str());
    return commands.push(command);
}

bool SynthPlayer::answer(Answer& out) { return answers.pop(out); }

LoadedModule* SynthPlayer::retired() {
    LoadedModule* module = nullptr;
    return retiring.pop(module) ? module : nullptr;
}

void SynthPlayer::apply() {
    on_audio_thread = true;
    Command command;
    unsigned asked = 0;
    while (asked < 4 && commands.pop(command)) { // a few reads a block: each is the module's own time
        switch (command.kind) {
        case Command::Kind::Module:
            if (current) retiring.push(current);
            current = command.module;
            break;
        case Command::Kind::Set:
            if (current && current->api->set_param) current->api->set_param(current->instance, command.key, command.value);
            break;
        case Command::Kind::Ask: {
            ++asked;
            Answer answer;
            std::memcpy(answer.key, command.key, key_size);
            int n = current && current->api->get_param ? current->api->get_param(current->instance, command.key, answer.value, int(value_size)) : -1;
            answer.value[n > 0 ? std::min(n, int(value_size) - 1) : 0] = 0;
            answers.push(answer);
            break;
        }
        }
    }
}

void SynthPlayer::midi(uint8_t status, uint8_t data1, uint8_t data2) {
    apply();
    if (!current || !current->api->on_midi) return;
    const uint8_t message[3] {status, data1, data2};
    current->api->on_midi(current->instance, message, 3, schwung::midi_source_internal);
}

void SynthPlayer::note_on(int note, int velocity) { midi(0x90, uint8_t(std::clamp(note, 0, 127)), uint8_t(std::clamp(velocity, 1, 127))); }
void SynthPlayer::note_off(int note) { midi(0x80, uint8_t(std::clamp(note, 0, 127)), 0); }
void SynthPlayer::pressure(int note, int value) { midi(0xa0, uint8_t(std::clamp(note, 0, 127)), uint8_t(std::clamp(value, 0, 127))); }

void SynthPlayer::render(const move::AudioBlock& input, move::AudioBlock& output, float gain) {
    apply();
    // The input, where a module reading the device page looks for it.
    for (unsigned n = 0; n < input.size(); ++n) {
        auto sample = uint16_t(int16_t(input[n] >> 16));
        mapped_page[audio_in_offset + 2 * n] = uint8_t(sample);
        mapped_page[audio_in_offset + 2 * n + 1] = uint8_t(sample >> 8);
    }
    if (!current) return;
    block.fill(0);
    current->api->render_block(current->instance, block.data(), int(move::audio_frames));
    for (unsigned n = 0; n < block.size(); ++n) {
        int64_t mixed = int64_t(output[n]) + int64_t(float(block[n]) * gain * 65536.0f);
        output[n] = int32_t(std::clamp<int64_t>(mixed, INT32_MIN + 1, INT32_MAX));
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
    info.playing = 0;
    info.metronome_on = info.midi_clock_sync = info.input_monitoring = 255;
    info.root_note = info.selected_track = info.global_quant = -1;
    info.ts_upper = 4;
    info.ts_lower = 4;
    info.tempo = app::tempo.load(std::memory_order_relaxed);
    info.groove = -1;
    info.master_db = -1000;
    info.song_beats = -1;
    for (auto& track : info.track) { track.color_id = -1; track.type = -1; track.muted = track.soloed = track.selected = 255; track.volume_db = -1000; }
    std::memcpy(out, &info, std::min(capacity, sizeof info));
    return 1;
}
