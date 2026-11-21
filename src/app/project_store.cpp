// SPDX-License-Identifier: AGPL-3.0-only
#include "app/project_store.h"
#include "melodee/model/project_file.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>

namespace app {
namespace {

constexpr const char* chops_header = "melodee-move chops 1";
constexpr const char* synth_header = "melodee-move synth 1";

template <size_t N> void copy_text(char (&to)[N], const std::string& from) {
    std::snprintf(to, N, "%s", from.c_str());
}

std::string chops_path(const std::string& project) { return project + ".chops"; }
std::string synth_path(const std::string& project) { return project + ".synth"; }

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::string();
}

} // namespace

// A file whole or not at all: written beside, flushed, renamed over.
std::string write_whole(const std::string& path, const void* bytes, size_t size) {
    std::string temporary = path + ".writing";
    int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return std::string("cannot write ") + temporary + ": " + std::strerror(errno);
    const auto* data = static_cast<const uint8_t*>(bytes);
    size_t written = 0;
    while (written < size) {
        ssize_t n = write(fd, data + written, size - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { int error = errno; close(fd); unlink(temporary.c_str()); return std::string("write failed: ") + std::strerror(error); }
        written += size_t(n);
    }
    bool flushed = fsync(fd) == 0;
    close(fd);
    if (!flushed || rename(temporary.c_str(), path.c_str()) != 0) {
        int error = errno;
        unlink(temporary.c_str());
        return std::string("cannot save: ") + std::strerror(error);
    }
    return {};
}

std::string chops_text(const KitFile& kit) {
    std::ostringstream out;
    out << chops_header << '\n';
    for (const auto& sample : kit.samples) {
        const auto& chop = sample.chop;
        if (!chop.used()) continue;
        out << sample.slot << ' ' << chop.from << ' ' << chop.to << ' ' << unsigned(chop.mode) << ' ' << chop.amount << ' ' << chop.slices.count;
        for (unsigned n = 0; n < chop.slices.count; ++n) out << ' ' << chop.slices.start[n] << ' ' << chop.slices.stop[n];
        out << '\n';
    }
    return out.str();
}

void read_chops(const std::string& text, KitFile& kit) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != chops_header) return;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        unsigned slot = 0, mode = 0, count = 0;
        int amount = 0;
        melodee::SampleChop chop;
        if (!(fields >> slot >> chop.from >> chop.to >> mode >> amount >> count)) continue;
        if (mode >= unsigned(melodee::ChopMode::Count) || count == 0 || count > melodee::max_slices) continue;
        chop.mode = melodee::ChopMode(mode);
        chop.amount = int16_t(amount);
        chop.slices.count = count;
        bool sound = true;
        for (unsigned n = 0; n < count && sound; ++n) {
            sound = bool(fields >> chop.slices.start[n] >> chop.slices.stop[n]);
            if (n && chop.slices.start[n] < chop.slices.start[n - 1]) sound = false; // a damaged line is chopped afresh
        }
        if (!sound) continue;
        for (auto& sample : kit.samples)
            if (sample.slot == slot) sample.chop = chop;
    }
}

namespace {

// Melodee keeps a sample's chop in the sample's own WAV file, as the MPC
// does. Here the samples are the Move's own library, left untouched: each
// chop goes into the project instead, in a section of the Move's own
// ("MVCH", the chops' text lines) after Melodee's. Melodee's reader skips a
// section it does not know, so the file stays a Melodee project.
constexpr char chops_tag[4] {'M', 'V', 'C', 'H'};
// The Move's own part of the project (MoveExtras), the same way.
constexpr char move_tag[4] {'M', 'V', 'P', 'J'};
constexpr const char* move_header = "melodee-move project 1";
constexpr char project_magic[8] {'M', 'E', 'L', 'O', 'D', 'E', 'E', '1'};

KitFile chops_of(const melodee::Project& project) {
    KitFile kit;
    for (size_t slot = 0; slot < melodee::sample_slots; ++slot)
        if (project.samples[slot].path[0] && project.chops[slot].used())
            kit.samples.push_back({uint16_t(slot), project.samples[slot].path, project.samples[slot].name, 0, project.chops[slot]});
    return kit;
}

} // namespace

std::vector<uint8_t> project_file_bytes(const melodee::Project& project, const melodee::PatternBank& patterns, const PluginStates& states,
                                        const MoveExtras* extras, const melodee::PrintTable* prints) {
    auto bytes = melodee::save_project(project, patterns, &states, prints);
    auto section = [&](const char (&tag)[4], const std::string& text) {
        bytes.insert(bytes.end(), tag, tag + 4);
        auto size = uint32_t(text.size());
        for (int k = 0; k < 4; ++k) bytes.push_back(uint8_t(size >> (8 * k)));
        bytes.insert(bytes.end(), text.begin(), text.end());
    };
    section(chops_tag, chops_text(chops_of(project)));
    if (extras) {
        std::ostringstream out;
        out << move_header << '\n';
        if (extras->colour) out << "colour " << unsigned(extras->colour) << '\n';
        for (unsigned t = 0; t < melodee::track_count; ++t)
            if (extras->track_colour[t]) out << "track_colour " << t << ' ' << unsigned(extras->track_colour[t]) << '\n';
        section(move_tag, out.str());
    }
    return bytes;
}

std::string read_project_file(const std::string& path, melodee::Project& project, melodee::PatternBank& patterns, PluginStates& states,
                              MoveExtras* extras, melodee::PrintTable* prints) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "missing";
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return read_project_bytes(bytes, project, patterns, states, extras, prints);
}

std::string read_project_bytes(const std::vector<uint8_t>& bytes, melodee::Project& project, melodee::PatternBank& patterns, PluginStates& states,
                               MoveExtras* extras, melodee::PrintTable* prints) {
    if (extras) *extras = {};
    if (auto error = melodee::load_project(bytes, project, patterns, nullptr, &states, prints); !error.empty()) return error;
    // The Move's chops, where the file has them.
    if (bytes.size() < 8 || std::memcmp(bytes.data(), project_magic, 8)) return {};
    for (size_t at = 8; at + 8 <= bytes.size();) {
        uint32_t size = 0;
        for (int k = 0; k < 4; ++k) size |= uint32_t(bytes[at + 4 + size_t(k)]) << (8 * k);
        if (at + 8 + size > bytes.size()) break;
        if (!std::memcmp(bytes.data() + at, chops_tag, 4)) {
            KitFile kit;
            for (size_t slot = 0; slot < melodee::sample_slots; ++slot)
                if (project.samples[slot].path[0]) kit.samples.push_back({uint16_t(slot), project.samples[slot].path, project.samples[slot].name, 0, {}});
            read_chops(std::string(bytes.begin() + long(at + 8), bytes.begin() + long(at + 8 + size)), kit);
            for (const auto& sample : kit.samples)
                if (sample.chop.used()) project.chops.edit(sample.slot) = sample.chop;
        }
        if (extras && !std::memcmp(bytes.data() + at, move_tag, 4)) {
            std::istringstream text(std::string(bytes.begin() + long(at + 8), bytes.begin() + long(at + 8 + size)));
            std::string line;
            if (std::getline(text, line) && line == move_header)
                while (std::getline(text, line)) {
                    std::istringstream fields(line);
                    std::string word;
                    unsigned a = 0, b = 0;
                    fields >> word >> a;
                    if (word == "colour") extras->colour = uint8_t(std::min(a, 127u));
                    else if (word == "track_colour" && (fields >> b) && a < melodee::track_count) extras->track_colour[a] = uint8_t(std::min(b, 127u));
                }
        }
        at += 8 + size;
    }
    return {};
}

std::string synth_text(const SynthFile& synth) {
    std::ostringstream out;
    out << synth_header << '\n' << "track " << synth.track << '\n' << "octave " << synth.octave << '\n';
    if (!synth.module.empty()) {
        out << "module " << synth.module << '\n';
        if (synth.preset >= 0) out << "preset " << synth.preset << '\n';
        for (const auto& [key, value] : synth.values)
            if (key.find_first_of(" \n") == std::string::npos && value.find('\n') == std::string::npos) out << "param " << key << ' ' << value << '\n';
        for (const auto& [key, state] : synth.pages) // JSON never holds a raw newline
            if (!key.empty() && key.find_first_of(" \n") == std::string::npos && !state.empty() && state.find('\n') == std::string::npos)
                out << "page " << key << ' ' << state << '\n';
    }
    return out.str();
}

bool read_synth(const std::string& text, SynthFile& synth) {
    synth = {};
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != synth_header) return false;
    while (std::getline(in, line)) {
        auto space = line.find(' ');
        if (space == std::string::npos) continue;
        auto word = line.substr(0, space), rest = line.substr(space + 1);
        if (word == "track") synth.track = unsigned(std::strtoul(rest.c_str(), nullptr, 10)) % melodee::track_count;
        else if (word == "octave") synth.octave = std::clamp(int(std::strtol(rest.c_str(), nullptr, 10)), -4, 4);
        else if (word == "module") synth.module = rest;
        else if (word == "preset") synth.preset = int(std::strtol(rest.c_str(), nullptr, 10));
        else if (word == "param") {
            auto gap = rest.find(' ');
            if (gap != std::string::npos && gap) synth.values.emplace_back(rest.substr(0, gap), rest.substr(gap + 1));
        } else if (word == "page") {
            auto gap = rest.find(' ');
            if (gap != std::string::npos && gap) synth.pages.emplace_back(rest.substr(0, gap), rest.substr(gap + 1));
        }
    }
    return true;
}

std::vector<uint8_t> synth_state(const SynthFile& synth) {
    auto text = synth_text(synth);
    return {text.begin(), text.end()};
}

bool read_synth_state(const std::vector<uint8_t>& state, SynthFile& synth) {
    return read_synth(std::string(state.begin(), state.end()), synth);
}

bool migrate(const std::string& path, melodee::Project& project, PluginStates& states,
             std::string (*module_folder)(const std::string& id, const void* context), const void* context, unsigned& played) {
    auto chops = read_text(chops_path(path)), synth = read_text(synth_path(path));
    if (chops.empty() && synth.empty()) return false;
    // Each chop onto its sample's slot, where the file has none of its own.
    KitFile kit;
    for (size_t slot = 0; slot < melodee::sample_slots; ++slot)
        if (project.samples[slot].path[0]) kit.samples.push_back({uint16_t(slot), project.samples[slot].path, project.samples[slot].name, 0, {}});
    read_chops(chops, kit);
    for (const auto& sample : kit.samples)
        if (sample.chop.used() && !project.chops[sample.slot].used()) project.chops.edit(sample.slot) = sample.chop;
    // The synth track: track 2, a PLUGIN track on the module.
    SynthFile saved;
    played = 0;
    if (read_synth(synth, saved)) {
        played = std::min(saved.track, 1u);
        auto& track = project.tracks[1];
        if (!track.used()) {
            track = melodee::Track {};
            track.engine = melodee::EngineKind::Plugin;
            copy_text(track.name, "SYNTH");
            track.octave = int8_t(3 + saved.octave);
            if (!saved.module.empty()) {
                unsigned entry = 0;
                while (entry < melodee::max_plugins && project.plugins[entry].used()) ++entry;
                if (entry < melodee::max_plugins) {
                    auto& ref = project.plugins[entry];
                    copy_text(ref.path, module_folder(saved.module, context));
                    copy_text(ref.name, saved.module);
                    track.plugin = uint8_t(entry);
                    saved.track = 1;
                    states[entry] = synth_state(saved);
                }
            }
        }
    }
    return true;
}

void retire_side_files(const std::string& path) {
    for (const auto& side : {chops_path(path), synth_path(path)}) {
        std::ifstream exists(side);
        if (exists) { exists.close(); std::rename(side.c_str(), (side + ".migrated").c_str()); }
    }
}

void give_to_owner_of(const std::string& path, const std::string& reference) {
    if (geteuid() != 0) return;
    struct stat owner {};
    if (stat(reference.c_str(), &owner) != 0) return;
    [[maybe_unused]] int changed = chown(path.c_str(), owner.st_uid, owner.st_gid);
}

} // namespace app
