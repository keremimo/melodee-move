// SPDX-License-Identifier: AGPL-3.0-only
#include "app/project_store.h"
#include "melodee/engine/engine_info.h"
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

std::string chops_path(const std::string& project) { return project + ".chops"; }
std::string synth_path(const std::string& project) { return project + ".synth"; }

} // namespace

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

std::string save_kit(const KitFile& kit, const std::string& path) {
    // A project and its patterns are large: on the heap, off the audio thread.
    auto project = std::make_unique<melodee::Project>();
    auto patterns = std::make_unique<melodee::PatternBank>();
    copy_text(project->name, "Move");
    auto& track = project->tracks[0];
    track.engine = melodee::EngineKind::Sampler;
    copy_text(track.name, "KIT 1");
    const auto& info = melodee::engine_info(melodee::EngineKind::Sampler);
    for (unsigned n = 0; n < info.param_count && n < track.params.size(); ++n) track.params[n] = info.params[n].initial;
    for (unsigned pad = 0; pad < pad_count; ++pad) track.pads[pad] = kit.pads[pad];
    track.last_pad = uint8_t(kit.last_pad);
    for (const auto& sample : kit.samples) {
        if (sample.slot >= melodee::sample_slots) continue;
        auto& ref = project->samples.edit(sample.slot);
        copy_text(ref.path, sample.path);
        copy_text(ref.name, sample.name);
        ref.frames = sample.frames;
    }
    auto bytes = melodee::save_project(*project, *patterns);
    if (auto error = write_whole(path, bytes.data(), bytes.size()); !error.empty()) return error;
    auto chops = chops_text(kit);
    return write_whole(chops_path(path), chops.data(), chops.size());
}

std::string load_kit(const std::string& path, KitFile& kit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "missing";
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto project = std::make_unique<melodee::Project>();
    auto patterns = std::make_unique<melodee::PatternBank>();
    if (auto error = melodee::load_project(bytes, *project, *patterns); !error.empty()) return error;
    const auto& track = project->tracks[0];
    if (track.engine != melodee::EngineKind::Sampler) return "track 1 is not a kit";
    for (unsigned pad = 0; pad < pad_count; ++pad) kit.pads[pad] = track.pads[pad];
    kit.last_pad = track.last_pad % pad_count;
    kit.samples.clear();
    for (size_t slot = 0; slot < melodee::sample_slots; ++slot) {
        const auto& ref = project->samples[slot];
        if (!ref.path[0]) continue;
        KitFile::Sample sample;
        sample.slot = uint16_t(slot);
        sample.path = ref.path;
        sample.name = ref.name;
        sample.frames = ref.frames;
        sample.chop = project->chops[slot]; // an older Melodee file's CHOP, if it had one
        kit.samples.push_back(std::move(sample));
    }
    std::ifstream chops(chops_path(path));
    if (chops) read_chops(std::string((std::istreambuf_iterator<char>(chops)), std::istreambuf_iterator<char>()), kit);
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
    }
    return out.str();
}

void read_synth(const std::string& text, SynthFile& synth) {
    synth = {};
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != synth_header) return;
    while (std::getline(in, line)) {
        auto space = line.find(' ');
        if (space == std::string::npos) continue;
        auto word = line.substr(0, space), rest = line.substr(space + 1);
        if (word == "track") synth.track = unsigned(std::strtoul(rest.c_str(), nullptr, 10)) % 2;
        else if (word == "octave") synth.octave = std::clamp(int(std::strtol(rest.c_str(), nullptr, 10)), -4, 4);
        else if (word == "module") synth.module = rest;
        else if (word == "preset") synth.preset = int(std::strtol(rest.c_str(), nullptr, 10));
        else if (word == "param") {
            auto gap = rest.find(' ');
            if (gap != std::string::npos && gap) synth.values.emplace_back(rest.substr(0, gap), rest.substr(gap + 1));
        }
    }
}

std::string save_synth(const SynthFile& synth, const std::string& project) {
    auto text = synth_text(synth);
    return write_whole(synth_path(project), text.data(), text.size());
}

void load_synth(const std::string& project, SynthFile& synth) {
    std::ifstream in(synth_path(project));
    synth = {};
    if (in) read_synth(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()), synth);
}

void give_to_owner_of(const std::string& path, const std::string& reference) {
    if (geteuid() != 0) return;
    struct stat owner {};
    if (stat(reference.c_str(), &owner) != 0) return;
    [[maybe_unused]] int changed = chown(path.c_str(), owner.st_uid, owner.st_gid);
}

} // namespace app
