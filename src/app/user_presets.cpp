// SPDX-License-Identifier: AGPL-3.0-only
#include "app/user_presets.h"
#include "app/schwung_pages.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace app {

namespace {

std::string lower(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// A name as a file name: what a file system will not take, an underscore.
std::string file_name(const std::string& name) {
    std::string out;
    for (char c : name) out += (c == '/' || c == '\\' || c == ':' || c == '\0') ? '_' : c;
    return out.empty() ? std::string("Preset") : out;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

fs::path module_folder(const std::string& folder, const std::string& module) { return fs::path(folder) / module; }

} // namespace

std::string user_presets_folder() {
    if (const char* given = std::getenv("MELODEE_SCHWUNG_PRESETS"); given && *given) return given;
    std::string shared = SchwungPages::shared_folder();
    if (shared.empty()) return {};
    return (fs::path(shared).parent_path() / "presets").string();
}

std::vector<UserPreset> user_presets(const std::string& folder, const std::string& module) {
    std::vector<UserPreset> found;
    std::error_code error;
    if (folder.empty() || !fs::is_directory(module_folder(folder, module), error)) return found;
    for (const auto& entry : fs::directory_iterator(module_folder(folder, module), error)) {
        if (entry.path().extension() != ".json") continue;
        auto parsed = juce::JSON::parse(juce::String(read_file(entry.path())));
        std::string name = parsed.isObject() && parsed.hasProperty("name") ? parsed["name"].toString().toStdString() : entry.path().stem().string();
        found.push_back({name, entry.path().string()});
    }
    std::sort(found.begin(), found.end(), [](const UserPreset& a, const UserPreset& b) { return lower(a.name) < lower(b.name); });
    return found;
}

std::string user_preset_state(const std::string& file) {
    auto parsed = juce::JSON::parse(juce::String(read_file(file)));
    if (!parsed.isObject()) return {};
    const auto& state = parsed["state"];
    if (state.isString()) return state.toString().toStdString();
    if (state.isObject() || state.isArray()) return juce::JSON::toString(state, true).toStdString();
    return {};
}

std::string save_user_preset(const std::string& folder, const std::string& module, const std::string& name,
                             const std::string& state, bool overwrite) {
    if (folder.empty() || state.empty()) return {};
    std::error_code error;
    fs::create_directories(module_folder(folder, module), error);
    std::string chosen = name.empty() ? std::string("Preset") : name;
    if (!overwrite) { // never over another: a number on, as Schwung's Save As
        auto taken = [&](const std::string& candidate) { return fs::exists(module_folder(folder, module) / (file_name(candidate) + ".json"), error); };
        for (int n = 2; taken(chosen) && n < 1000; ++n) chosen = name + " " + std::to_string(n);
    }
    auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
    object->setProperty("name", juce::String(chosen));
    object->setProperty("module", juce::String(module));
    object->setProperty("version", 1);
    auto blob = juce::JSON::parse(juce::String(state));
    object->setProperty("state", blob.isObject() || blob.isArray() ? blob : juce::var(juce::String(state)));
    std::ofstream out(module_folder(folder, module) / (file_name(chosen) + ".json"), std::ios::binary | std::ios::trunc);
    out << juce::JSON::toString(juce::var(object.get()), false).toStdString() << '\n';
    return out ? chosen : std::string();
}

bool delete_user_preset(const std::string& folder, const std::string& module, const std::string& name) {
    std::error_code error;
    for (const auto& preset : user_presets(folder, module))
        if (preset.name == name) return fs::remove(preset.file, error);
    return false;
}

} // namespace app
