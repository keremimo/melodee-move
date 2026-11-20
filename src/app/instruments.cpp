// SPDX-License-Identifier: AGPL-3.0-only
#include "app/instruments.h"
#include "app/studio.h"
#include "melodee/engine/dspreset.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/exs.h"
#include "melodee/engine/keygroup.h"
#include "melodee/engine/sample_data.h"
#include "melodee/engine/sf2.h"
#include "melodee/engine/stream_pager.h"
#include "melodee/shell/dspreset_file.h"
#include "melodee/shell/mpc_program.h"
#include "melodee/shell/sample_decode.h"
#include "melodee/shell/sf2_file.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace app {
namespace fs = std::filesystem;
namespace {

constexpr unsigned part_seconds = 600; // one file of an instrument, read to be laid out
constexpr size_t most_text = 16u << 20;

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
std::string extension(const std::string& path) { return lowered(fs::path(path).extension().string()); }

std::optional<std::string> read_text(const fs::path& path) {
    std::error_code error;
    auto bytes = fs::file_size(path, error);
    if (error || bytes > most_text) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// A path as a file names it, exactly or, on storage written elsewhere,
// whatever the case (as Melodee finds an instrument's samples on the card).
fs::path find_beside(const fs::path& folder, const std::string& relative) {
    fs::path at = folder;
    std::stringstream parts(relative);
    std::string part;
    std::error_code error;
    while (std::getline(parts, part, '/')) {
        if (part.empty() || part == ".") continue;
        if (part == "..") { at = at.parent_path(); continue; }
        if (fs::exists(at / part, error)) { at /= part; continue; }
        bool found = false;
        const auto wanted = lowered(part);
        for (auto it = fs::directory_iterator(at, error); !error && it != fs::directory_iterator(); it.increment(error))
            if (lowered(it->path().filename().string()) == wanted) { at = it->path(); found = true; break; }
        if (!found) return {};
    }
    return at;
}

// An MPC program's or DecentSampler preset's zones whose files are not beside
// it are left out, as the MPC plays them (silent); only one with none found
// is refused.
melodee::SfzRead found_only(melodee::SfzRead read, const fs::path& folder, std::string& error) {
    std::map<std::string, bool> there;
    const size_t had = read.regions.size();
    read.regions.erase(std::remove_if(read.regions.begin(), read.regions.end(), [&](const melodee::SfzRegion& r) {
        auto known = there.find(r.sample);
        if (known == there.end()) known = there.emplace(r.sample, !find_beside(folder, r.sample).empty()).first;
        return !known->second;
    }), read.regions.end());
    if (read.regions.size() < had) {
        read.left += unsigned(had - read.regions.size());
        read.left_by.insert("missing files");
        if (read.regions.empty()) error = "Files missing";
    }
    return read;
}

// Where an EXS instrument's sample is on this storage, as Logic looks: beside
// it, under the folder it was saved from (that path's last parts, under the
// instrument's folder or the two above it), else by its name under those.
class ExsFinder {
public:
    explicit ExsFinder(fs::path folder) : folder(std::move(folder)) {}
    std::string operator()(const std::string& name, const std::string& saved_in) {
        if (name.empty() || name.find('/') != std::string::npos) return {};
        std::vector<fs::path> roots {folder};
        if (folder.has_parent_path() && folder.parent_path() != folder) roots.push_back(folder.parent_path());
        if (roots.back().has_parent_path() && roots.back().parent_path() != roots.back()) roots.push_back(roots.back().parent_path());
        std::vector<std::string> tails {""};
        std::vector<std::string> parts;
        std::stringstream saved(saved_in);
        for (std::string part; std::getline(saved, part, '/');) if (!part.empty()) parts.push_back(part);
        for (size_t take = 1; take <= std::min<size_t>(parts.size(), 3); ++take) {
            std::string tail;
            for (size_t n = parts.size() - take; n < parts.size(); ++n) tail += parts[n] + "/";
            tails.push_back(tail);
        }
        for (const auto& root : roots)
            for (const auto& tail : tails) {
                const auto wanted = (root.lexically_relative(folder) / tail / name).lexically_normal().generic_string();
                if (!find_beside(folder, wanted).empty()) return wanted;
            }
        if (!indexed) {
            indexed = true;
            unsigned budget = 20000;
            for (const auto& root : roots) index(root, 0, budget);
        }
        auto it = by_name.find(lowered(name));
        return it == by_name.end() ? std::string() : it->second.lexically_relative(folder).generic_string();
    }

private:
    void index(const fs::path& at, unsigned depth, unsigned& budget) {
        std::error_code error;
        std::vector<fs::path> below;
        for (auto it = fs::directory_iterator(at, fs::directory_options::skip_permission_denied, error);
             !error && it != fs::directory_iterator() && budget; it.increment(error), --budget) {
            const auto name = it->path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            if (it->is_directory(error)) { if (depth < 5) below.push_back(it->path()); continue; }
            by_name.emplace(lowered(name), it->path());
        }
        for (const auto& folder_below : below) index(folder_below, depth + 1, budget);
    }
    fs::path folder;
    bool indexed = false;
    std::map<std::string, fs::path> by_name;
};

// Keyswitches (a range not named is the keys its zones switch on) and whether
// any zone plays on a key let go, as Melodee sets them.
void switches(const melodee::SfzRead& read, melodee::Multisample& multi) {
    int low = read.switch_low, high = read.switch_high;
    for (const auto& z : multi.zones) {
        multi.has_release = multi.has_release || z.on_release;
        if (z.switch_key < 0 || read.switch_low >= 0) continue;
        low = low < 0 ? z.switch_key : std::min<int>(low, z.switch_key);
        high = std::max<int>(high, z.switch_key);
    }
    if (low < 0) return;
    if (high < low) high = low;
    multi.switch_low = int16_t(low);
    multi.switch_high = int16_t(high);
    multi.switch_default = read.switch_default >= 0 ? read.switch_default : int16_t(low);
}

melodee::SfzRead read_file(const std::string& path, Instrument& out, std::string& error) {
    std::string file;
    unsigned preset = 0;
    if (melodee::split_sf2_preset(path, file, preset)) {
        out.sf2 = file;
        out.folder = fs::path(file).parent_path().string();
        auto mapped = melodee::open_sf2(file);
        if (!mapped->error.empty()) { error = mapped->error; return {}; }
        const melodee::Sf2Preset* found = mapped->preset(preset);
        if (!found) { error = "Preset not found"; return {}; }
        return mapped->sf2.read(*found);
    }
    out.folder = fs::path(path).parent_path().string();
    auto text = read_text(path);
    if (!text) { error = "Cannot read it"; return {}; }
    if (melodee::is_dspreset(path)) {
        auto read = melodee::read_dspreset_text(*text, &error);
        if (!error.empty()) return {};
        return found_only(std::move(read), out.folder, error);
    }
    if (melodee::is_mpc_program(path)) {
        melodee::KeygroupProgram program;
        error = melodee::read_program(path, *text, program);
        if (error.empty() && program.type != "Keygroup") error = program.type == "Drum" ? "A drum kit" : "Not a keygroup";
        if (!error.empty()) return {};
        return found_only(melodee::read_keygroup(program), out.folder, error);
    }
    if (extension(path) == ".exs") {
        ExsFinder finder(out.folder);
        auto read = melodee::read_exs(*text, std::ref(finder), &error);
        if (error.empty() && read.regions.empty() && read.left_by.count("missing files")) error = "Files missing";
        return read;
    }
    const fs::path folder = out.folder;
    return melodee::read_sfz(*text, [&](const std::string& name) -> std::optional<std::string> {
        auto found = find_beside(folder, name);
        if (found.empty()) return std::nullopt;
        return read_text(found);
    });
}

// A decoded file laid out as a 32-bit float WAV in the cache, to be read in
// place as any WAV: as Melodee lays a FLAC or a SoundFont's sound out on the
// card. One side or two; its loop in a smpl chunk is not needed (the zone has it).
bool write_float_wav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right, unsigned rate) {
    const uint32_t frames = uint32_t(left.size());
    const uint16_t channels = right.empty() ? 1 : 2;
    const uint32_t data = frames * channels * 4;
    std::string temporary = path + ".part";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    auto u32 = [&](uint32_t v) { char b[4] {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; file.write(b, 4); };
    auto u16 = [&](uint16_t v) { char b[2] {char(v), char(v >> 8)}; file.write(b, 2); };
    file.write("RIFF", 4); u32(36 + data); file.write("WAVE", 4);
    file.write("fmt ", 4); u32(16); u16(3); u16(channels); u32(rate); u32(rate * channels * 4); u16(uint16_t(channels * 4)); u16(32);
    file.write("data", 4); u32(data);
    std::vector<float> frame(channels);
    for (uint32_t n = 0; n < frames; ++n) {
        frame[0] = left[n];
        if (channels == 2) frame[1] = right[n];
        file.write(reinterpret_cast<const char*>(frame.data()), std::streamsize(channels * 4));
    }
    file.close();
    if (!file) return false;
    std::error_code error;
    fs::rename(temporary, path, error);
    return !error;
}

// The cache's file for a source (and a SoundFont's sound in it): named by
// what it was made from, so a file changed since is laid out again.
std::string cache_file(const std::string& cache, const std::string& source, const std::string& sound) {
    std::error_code error;
    auto size = fs::file_size(source, error);
    auto stamp = error ? 0 : fs::last_write_time(source, error).time_since_epoch().count();
    std::string key = source + "|" + sound + "|" + std::to_string(size) + "|" + std::to_string(stamp);
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : key) { hash ^= c; hash *= 1099511628211ull; }
    char name[40];
    std::snprintf(name, sizeof name, "%016llx.wav", static_cast<unsigned long long>(hash));
    return (fs::path(cache) / name).string();
}

// Where decoded files are laid out: Melodee's Cache beside the projects,
// set by the Studio as it starts (its home folder).
std::string& cache_folder() { static std::string folder; return folder; }

} // namespace

bool soundfont_file(const std::string& path) { return extension(path) == ".sf2"; }

bool instrument_file(const std::string& path) {
    std::string file;
    unsigned preset = 0;
    if (melodee::split_sf2_preset(path, file, preset)) return true;
    auto ext = extension(path);
    return ext == ".sfz" || ext == ".xpm" || ext == ".xty" || ext == ".exs" || ext == ".dspreset";
}

std::string instrument_format(const std::string& path) {
    std::string file;
    unsigned preset = 0;
    if (melodee::split_sf2_preset(path, file, preset)) return "SF2";
    auto ext = extension(path);
    if (ext == ".sfz") return "SFZ";
    if (ext == ".xpm" || ext == ".xty") return "MPC";
    if (ext == ".exs") return "EXS";
    if (ext == ".dspreset") return "DECENT";
    return {};
}

std::string preset_path(const std::string& sf2, unsigned index) { return sf2 + "#" + std::to_string(index); }

std::vector<SoundfontPreset> soundfont_presets(const std::string& path, std::string& error) {
    std::vector<SoundfontPreset> out;
    auto mapped = melodee::open_sf2(path);
    if (!mapped->error.empty()) { error = mapped->error; return out; }
    for (const auto& preset : mapped->sf2.presets())
        if (mapped->sf2.plays(preset)) out.push_back({preset.index, preset.bank, preset.program, preset.name});
    return out;
}

void set_instrument_cache(const std::string& folder) { cache_folder() = folder; }

std::unique_ptr<Sample> read_instrument_sample(const std::string& path, std::string& error) {
    error.clear();
    auto instrument = std::make_shared<Instrument>();
    instrument->format = instrument_format(path);
    auto read = read_file(path, *instrument, error);
    if (!error.empty()) return nullptr;
    if (read.regions.empty()) { error = "No sounds"; return nullptr; }
    auto multi = std::make_shared<melodee::Multisample>();
    std::map<std::string, uint16_t> part_of;
    for (size_t n = 0; n < read.regions.size(); ++n) {
        auto& region = read.regions[n];
        auto found = part_of.find(region.sample);
        if (found == part_of.end()) {
            found = part_of.emplace(region.sample, uint16_t(instrument->parts.size())).first;
            instrument->parts.push_back(region.sample);
            instrument->zones_of.emplace_back();
        }
        region.zone.part = found->second;
        instrument->zones_of[found->second].push_back(uint16_t(n));
        multi->zones.push_back(region.zone);
    }
    multi->parts = std::make_unique<melodee::ZonePart[]>(instrument->parts.size());
    multi->part_count = unsigned(instrument->parts.size());
    multi->turns.assign(multi->zones.size(), 0);
    switches(read, *multi);
    multi->mono = read.mono; multi->legato = read.legato; multi->tied = read.tied; multi->glide = float(read.glide);
    multi->mono_low = read.mono_low; multi->mono_high = read.mono_high;
    instrument->read = std::move(read);
    instrument->held.resize(instrument->parts.size());

    auto sample = std::make_unique<Sample>();
    sample->path = path;
    std::string file;
    unsigned preset = 0;
    sample->name = fs::path(path).stem().string();
    if (melodee::split_sf2_preset(path, file, preset)) {
        auto mapped = melodee::open_sf2(file);
        if (const auto* p = mapped->preset(preset)) sample->name = p->name;
    }
    sample->data = std::make_unique<melodee::SampleData>();
    sample->data->multi = multi;
    sample->instrument = instrument;
    return sample;
}

std::unique_ptr<Sample> read_any_sample(const std::string& path, std::string& error) {
    return instrument_file(path) ? read_instrument_sample(path, error) : read_sample(path, error);
}

std::string land_part(Instrument& instrument, melodee::SampleData& data, unsigned index, melodee::EngineHost* host) {
    auto& multi = *data.multi;
    auto& part = multi.parts[index];
    const std::string name = instrument.parts[index];
    std::string wav;
    int64_t loop_start = -1, loop_end = -1;
    if (instrument.sf2.empty()) {
        auto source = find_beside(instrument.folder, name);
        if (source.empty()) return "Files missing";
        wav = source.string();
        melodee::WavReader reader;
        melodee::WavInfo info;
        if (reader.open(wav, part_seconds, &info).empty() && reader.layout().channels <= 2) {
            loop_start = info.loop_start; loop_end = info.loop_end;
        } else {
            // FLAC, AIFF, Ogg, MP3, or a WAV read_in_place() takes not: decoded, laid out.
            melodee::SampleData decoded;
            melodee::WavInfo decoded_info;
            if (!melodee::read_audio(wav, decoded, part_seconds, &decoded_info).empty()) return "Files unreadable";
            loop_start = decoded_info.loop_start; loop_end = decoded_info.loop_end;
            auto made = cache_file(cache_folder(), wav, "");
            std::error_code error;
            if (!fs::exists(made, error)) {
                fs::create_directories(cache_folder(), error);
                bool one_side = decoded_info.channels == 1;
                if (!write_float_wav(made, decoded.left, one_side ? std::vector<float>() : decoded.right, decoded.rate)) return "Storage full";
            }
            wav = made;
        }
    } else {
        auto made = cache_file(cache_folder(), instrument.sf2, name);
        std::error_code error;
        auto mapped = melodee::open_sf2(instrument.sf2);
        if (!mapped->error.empty()) return "Files unreadable";
        std::vector<float> left, right;
        unsigned rate = 0;
        if (!mapped->sf2.sound(name, left, right, rate, loop_start, loop_end) || left.empty()) return "Files unreadable";
        if (!fs::exists(made, error)) {
            fs::create_directories(cache_folder(), error);
            if (!write_float_wav(made, left, right, rate)) return "Storage full";
        }
        wav = made;
    }
    melodee::WavReader reader;
    melodee::WavInfo info;
    if (!reader.open(wav, part_seconds, &info).empty()) return "Files unreadable";
    if (!melodee::read_in_place(wav, reader.layout(), part)) return "Files unreadable";
    const uint32_t frames = reader.frames();
    for (uint16_t n : instrument.zones_of[index]) {
        const auto& region = instrument.read.regions[n];
        auto& z = multi.zones[n];
        auto clamp = [&](int64_t frame) { return uint32_t(std::clamp<int64_t>(frame, 0, frames)); };
        z.start = region.offset >= 0 ? clamp(region.offset) : 0;
        z.end = region.end >= 0 ? clamp(region.end) : frames;
        uint32_t from = region.zone.loop_start, to = region.zone.loop_end;
        // the file's own loop where the region names none, and no mode turned it off
        if (to <= from && loop_end > loop_start && (!region.loop_named || region.loop_asked)) {
            from = clamp(loop_start);
            to = clamp(loop_end);
            if (z.loop == melodee::ZoneLoop::Off) z.loop = melodee::ZoneLoop::On;
        }
        from = std::max(from, z.start);
        to = std::min(to, z.end);
        if (to <= from) z.loop = melodee::ZoneLoop::Off;
        z.loop_start = from;
        z.loop_end = to;
        if (host) {
            if (!host->streams().hold(part, z.start, melodee::stream_chunk)) return "Too big for memory";
            if (z.loop != melodee::ZoneLoop::Off && !host->streams().hold(part, z.loop_start, melodee::stream_chunk)) return "Too big for memory";
        }
    }
    part.frames.store(frames, std::memory_order_release);
    return {};
}

Landings::~Landings() {
    for (auto& r : running) r.instrument->cancelled.store(true);
    for (auto& r : running) if (r.thread.joinable()) r.thread.join();
}

void Landings::start(Sample& sample) {
    reap();
    if (!sample.instrument || !sample.data || !sample.data->multi) return;
    host.streams().start(); // the instruments' reading from storage (idempotent)
    Running r;
    r.instrument = sample.instrument.get();
    r.finished = std::make_shared<std::atomic<bool>>(false);
    auto finished = r.finished;
    Instrument* instrument = sample.instrument.get();
    melodee::SampleData* data = sample.data.get();
    melodee::EngineHost* to = &host;
    r.thread = std::thread([instrument, data, to, finished] {
        for (unsigned n = 0; n < instrument->parts.size(); ++n) {
            if (instrument->cancelled.load()) break;
            auto why = land_part(*instrument, *data, n, to);
            if (why.empty()) instrument->landed.fetch_add(1);
            else {
                if (!instrument->failed.fetch_add(1)) instrument->failure = why;
            }
        }
        finished->store(true);
    });
    running.push_back(std::move(r));
}

void Landings::reap() {
    for (auto it = running.begin(); it != running.end();) {
        if (it->finished->load()) { if (it->thread.joinable()) it->thread.join(); it = running.erase(it); }
        else ++it;
    }
}

} // namespace app
