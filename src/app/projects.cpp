// SPDX-License-Identifier: AGPL-3.0-only
#include "app/projects.h"
#include "app/project_store.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace app::projects {
namespace {

namespace fs = std::filesystem;
constexpr const char* pads_file = ".move-pads";

bool ends_with(const std::string& text, const std::string& end) {
    return text.size() >= end.size() && !text.compare(text.size() - end.size(), end.size(), end);
}
std::string lower(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::map<std::string, int> read_pads(const std::string& folder) {
    std::map<std::string, int> pads;
    std::ifstream in(fs::path(folder) / pads_file);
    std::string line;
    while (std::getline(in, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        int pad = std::atoi(line.substr(0, tab).c_str());
        if (pad >= 0) pads[line.substr(tab + 1)] = pad;
    }
    return pads;
}

void write_pads(const std::string& folder, const std::map<std::string, int>& pads) {
    std::ostringstream out;
    for (const auto& [file, pad] : pads) out << pad << '\t' << file << '\n';
    auto text = out.str();
    auto path = (fs::path(folder) / pads_file).string();
    write_whole(path, text.data(), text.size());
    give_to_owner_of(path, folder);
}

bool listed(const fs::directory_entry& entry) {
    auto name = entry.path().filename().string();
    if (name.empty() || name[0] == '.') return false;
    std::error_code error;
    if (entry.is_directory(error)) { // a project's loops are its own, not a folder of projects
        if (ends_with(name, " Loops") && fs::exists(entry.path().parent_path() / (name.substr(0, name.size() - 6) + extension), error)) return false;
        return true;
    }
    return ends_with(name, extension) || ends_with(lower(name), ".xpj");
}

// A project's own folder beside it: its loops (the Looper's "<name> Loops").
std::string loops_of(const std::string& path, const std::string& name) {
    return (fs::path(path).parent_path() / (name + " Loops")).string();
}

} // namespace

std::string name_of(const std::string& path) {
    auto name = fs::path(path).filename().string();
    if (ends_with(name, extension)) return name.substr(0, name.size() - std::string(extension).size());
    if (ends_with(lower(name), ".xpj")) return name.substr(0, name.size() - 4);
    return name;
}

std::string path_for(const std::string& folder, const std::string& name) { return (fs::path(folder) / (name + extension)).string(); }

bool read_facts(const std::string& path, Entry& entry) {
    entry.path = path;
    entry.file = fs::path(path).filename().string();
    entry.name = name_of(path);
    std::error_code error;
    if (fs::is_directory(path, error)) {
        entry.kind = Entry::Kind::Folder;
        entry.tracks = 0;
        for (const auto& inside : fs::directory_iterator(path, error))
            if (listed(inside) && !inside.is_directory(error)) ++entry.tracks;
        return true;
    }
    if (ends_with(lower(path), ".xpj")) { entry.kind = Entry::Kind::Mpc; return true; }
    auto project = std::make_unique<melodee::Project>();
    auto bank = std::make_unique<melodee::PatternBank>();
    PluginStates states;
    MoveExtras extras;
    if (!read_project_file(path, *project, *bank, states, &extras).empty()) return false;
    entry.kind = Entry::Kind::Project;
    entry.tracks = project->used_tracks();
    entry.tempo = project->tempo;
    entry.colour = extras.colour;
    return true;
}

void keep_pad(const std::string& folder, const std::string& file, int pad) {
    auto pads = read_pads(folder);
    if (pad < 0) pads.erase(file);
    else {
        for (auto at = pads.begin(); at != pads.end();) at = at->second == pad && at->first != file ? pads.erase(at) : std::next(at);
        pads[file] = pad;
    }
    write_pads(folder, pads);
}

int free_pad(const std::string& folder, int from) {
    auto pads = read_pads(folder);
    std::error_code error;
    for (int pad = std::max(0, from);; ++pad) {
        bool taken = false;
        for (const auto& [file, at] : pads) taken |= at == pad && fs::exists(fs::path(folder) / file, error);
        if (!taken) return pad;
    }
}

Folder read_folder(const std::string& root, const std::string& relative) {
    Folder folder;
    folder.root = root;
    folder.relative = relative;
    std::error_code error;
    fs::create_directories(folder.path(), error);
    auto pads = read_pads(folder.path());
    std::vector<Entry> found;
    for (const auto& inside : fs::directory_iterator(folder.path(), error)) {
        if (!listed(inside)) continue;
        Entry entry;
        if (read_facts(inside.path().string(), entry)) found.push_back(entry);
    }
    // Folders first, then by name: the order new ones take free pads in.
    std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) {
        if ((a.kind == Entry::Kind::Folder) != (b.kind == Entry::Kind::Folder)) return a.kind == Entry::Kind::Folder;
        return lower(a.name) < lower(b.name);
    });
    std::map<std::string, int> kept;
    std::vector<bool> taken;
    auto take = [&](int pad) { if (size_t(pad) >= taken.size()) taken.resize(size_t(pad) + 1); taken[size_t(pad)] = true; };
    for (auto& entry : found)
        if (auto at = pads.find(entry.file); at != pads.end() && (size_t(at->second) >= taken.size() || !taken[size_t(at->second)])) {
            entry.pad = at->second;
            take(entry.pad);
            kept[entry.file] = entry.pad;
        }
    bool placed = false;
    for (auto& entry : found) {
        if (entry.pad >= 0) continue;
        int pad = 0;
        while (size_t(pad) < taken.size() && taken[size_t(pad)]) ++pad;
        entry.pad = pad;
        take(pad);
        kept[entry.file] = pad;
        placed = true;
    }
    if (placed || kept.size() != pads.size()) write_pads(folder.path(), kept);
    std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) { return a.pad < b.pad; });
    folder.items = std::move(found);
    // Pages: as many as the pads in use reach, and one more once the last is full.
    int last = folder.items.empty() ? -1 : folder.items.back().pad;
    folder.pages = unsigned(last / int(pads_a_page) + 1);
    unsigned on_last = 0;
    for (const auto& item : folder.items) on_last += unsigned(item.pad) / pads_a_page == folder.pages - 1;
    if (on_last >= pads_a_page) ++folder.pages;
    return folder;
}

std::string free_name(const std::string& folder, const std::string& base) {
    std::error_code error;
    auto free = [&](const std::string& name) {
        return !fs::exists(fs::path(folder) / (name + extension), error) && !fs::exists(fs::path(folder) / name, error);
    };
    if (base != "Project" && free(base)) return base;
    for (unsigned n = base == "Project" ? 1 : 2;; ++n) {
        auto name = base + " " + std::to_string(n);
        if (free(name)) return name;
    }
}

std::string duplicate(const std::string& path, std::string& made) {
    auto folder = fs::path(path).parent_path().string();
    made = path_for(folder, free_name(folder, name_of(path)));
    std::error_code error;
    if (!fs::copy_file(path, made, error)) return "NOT COPIED";
    give_to_owner_of(made, folder);
    auto loops = loops_of(path, name_of(path)); // its loops come along
    if (fs::is_directory(loops, error)) fs::copy(loops, loops_of(made, name_of(made)), fs::copy_options::recursive, error);
    return {};
}

std::string rename(const std::string& path, const std::string& name, std::string& made) {
    if (name.empty() || name.find('/') != std::string::npos || name[0] == '.') return "NOT A NAME";
    auto folder = fs::path(path).parent_path().string();
    std::error_code error;
    bool is_folder = fs::is_directory(path, error);
    made = is_folder ? (fs::path(folder) / name).string() : path_for(folder, name);
    if (made == path) return {};
    if (fs::exists(made, error)) return "NAME TAKEN";
    auto pads = read_pads(folder);
    int pad = pads.count(fs::path(path).filename().string()) ? pads[fs::path(path).filename().string()] : -1;
    fs::rename(path, made, error);
    if (error) return "NOT RENAMED";
    if (!is_folder && fs::is_directory(loops_of(path, name_of(path)), error)) fs::rename(loops_of(path, name_of(path)), loops_of(made, name), error);
    keep_pad(folder, fs::path(path).filename().string(), -1);
    if (pad >= 0) keep_pad(folder, fs::path(made).filename().string(), pad);
    return {};
}

std::string remove(const std::string& path) {
    std::error_code error;
    if (fs::is_directory(path, error)) {
        for (const auto& inside : fs::directory_iterator(path, error))
            if (inside.path().filename().string() != pads_file) return "FOLDER NOT EMPTY";
        fs::remove(fs::path(path) / pads_file, error);
    }
    if (!fs::remove(path, error)) return "NOT DELETED";
    keep_pad(fs::path(path).parent_path().string(), fs::path(path).filename().string(), -1);
    if (auto loops = loops_of(path, name_of(path)); fs::is_directory(loops, error)) fs::remove_all(loops, error); // its loops go with it
    return {};
}

std::string move_to(const std::string& path, const std::string& folder, std::string& made) {
    std::error_code error;
    fs::create_directories(folder, error);
    made = path_for(folder, free_name(folder, name_of(path)));
    fs::rename(path, made, error);
    if (error) return "NOT MOVED";
    if (auto loops = loops_of(path, name_of(path)); fs::is_directory(loops, error)) fs::rename(loops, loops_of(made, name_of(made)), error);
    keep_pad(fs::path(path).parent_path().string(), fs::path(path).filename().string(), -1);
    give_to_owner_of(made, folder);
    return {};
}

std::string new_folder(const std::string& parent, std::string& made) {
    made = (fs::path(parent) / free_name(parent, "Folder")).string();
    std::error_code error;
    if (!fs::create_directories(made, error)) return "NO FOLDER";
    give_to_owner_of(made, parent);
    return {};
}

} // namespace app::projects
