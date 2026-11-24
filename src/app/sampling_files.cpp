// SPDX-License-Identifier: AGPL-3.0-only
// Sampling's files (src/app/sampling.h): the browser's starred and recent
// lists, its preview, deleting from storage, Trim Sample, Save Sample,
// Extract, and an MPC expansion installed. The MIDI thread asks under the
// studio's lock; every file is read and written here, on the service thread.
#include "app/move_kits.h"
#include "app/sampling.h"
#include "app/sampling_jobs.h"
#include "app/studio.h"
#include "melodee/engine/engine_host.h"
#include "melodee/shell/sample_decode.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace app {
namespace {

namespace fs = std::filesystem;
using Type = melodee::Command::Type;

constexpr unsigned recent_most = 16;
constexpr unsigned preview_seconds = 30; // Melodee lays a preview out at up to 30 s
constexpr const char* core_library = "/data/CoreLibrary";
constexpr const char* user_library = "/data/UserData/UserLibrary"; // the Move's own: My Samples, Recordings

std::string upper_text(std::string text) {
    for (auto& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}
std::string pad_text(unsigned pad) {
    char text[8];
    std::snprintf(text, sizeof text, "%c%02u", 'A' + int(pad / 16), pad % 16 + 1);
    return text;
}
// `inner` is `outer` or inside it.
bool within(const fs::path& inner, const fs::path& outer) {
    if (outer.empty()) return false;
    auto i = inner.begin(), o = outer.begin();
    for (; o != outer.end(); ++o, ++i) {
        if (o->empty()) continue; // a trailing slash
        if (i == inner.end() || *i != *o) return false;
    }
    return true;
}
fs::path clean(const std::string& path) {
    std::error_code error;
    auto full = fs::weakly_canonical(fs::absolute(path, error), error);
    return full.empty() ? fs::path(path).lexically_normal() : full;
}
// `folder`/`stem`.wav, or `stem 2`.wav and on, the first not there.
std::string free_name(const fs::path& folder, const std::string& stem) {
    std::error_code error;
    for (unsigned n = 1; n < 10000; ++n) {
        auto name = folder / ((n == 1 ? stem : stem + " " + std::to_string(n)) + ".wav");
        if (!fs::exists(name, error)) return name.string();
    }
    return (folder / (stem + ".wav")).string();
}
std::vector<std::string> read_lines(const fs::path& file) {
    std::vector<std::string> lines;
    std::ifstream in(file);
    std::error_code error;
    for (std::string line; std::getline(in, line);)
        if (!line.empty() && fs::exists(line, error)) lines.push_back(line); // a file that has gone is left out
    return lines;
}
bool write_lines(const fs::path& file, const std::vector<std::string>& lines) {
    std::string text;
    for (const auto& line : lines) text += line + '\n';
    return write_whole(file.string(), text.data(), text.size()).empty();
}

} // namespace


// ---------------------------------------------------------------- starred and recent

void Sampling::read_lists() {
    fs::path home = studio.home_folder();
    starred = read_lines(home / "Starred.txt");
    recent = read_lines(home / "Recent.txt");
    std::sort(starred.begin(), starred.end(), [](const std::string& a, const std::string& b) {
        return upper_text(fs::path(a).filename().string()) < upper_text(fs::path(b).filename().string()); // by name, as Melodee lists them
    });
}

bool Sampling::is_starred(const std::string& path) const { return std::find(starred.begin(), starred.end(), path) != starred.end(); }

bool Sampling::star(const std::string& path) {
    auto at = std::find(starred.begin(), starred.end(), path);
    bool now = at == starred.end();
    if (now) {
        starred.push_back(path);
        std::sort(starred.begin(), starred.end(), [](const std::string& a, const std::string& b) {
            return upper_text(fs::path(a).filename().string()) < upper_text(fs::path(b).filename().string());
        });
    } else starred.erase(at);
    lists_dirty = true;
    return now;
}

void Sampling::loaded(const std::string& path) {
    recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
    recent.insert(recent.begin(), path);
    if (recent.size() > recent_most) recent.resize(recent_most);
    lists_dirty = true;
}

// ---------------------------------------------------------------- preview

void Sampling::preview_file(const std::string& path, unsigned track) {
    preview_wanted = preview ? path : std::string();
    preview_track = track;
    preview_asked = true;
    if (preview_wanted.empty() && !previewing.empty()) {
        melodee::Command stop {Type::Audition, uint8_t(track)};
        stop.pad.sample = melodee::no_sample;
        studio.send(stop);
        previewing.clear();
    }
}

void Sampling::retired(const melodee::SampleData* sample) {
    // Only the preview slot's are ours; the project's samples stay for the run.
    previews.erase(std::remove_if(previews.begin(), previews.end(), [sample](const auto& held) { return held.get() == sample; }), previews.end());
}

// ---------------------------------------------------------------- deleting

std::string Sampling::may_delete(const std::string& path) const {
    auto target = clean(path);
    fs::path home = clean(studio.home_folder());
    if (within(target, clean(core_library))) return "CORE LIBRARY";
    std::vector<fs::path> roots {home, clean(user_library)};
    if (!studio.library.empty()) roots.push_back(clean(studio.library)); // a folder given on the command line (--samples)
    bool inside = false;
    for (const auto& root : roots) {
        if (target == root) return "A PLACE'S TOP";
        inside |= within(target, root);
    }
    if (!inside) return "NOT HERE";
    if (target == home / "Samples" || within(target, home / "Projects") || target == home / "Starred.txt" || target == home / "Recent.txt")
        return "MELODEE'S OWN";
    // What the project plays, at or under it.
    const auto& project = studio.project();
    for (unsigned slot = 0; slot < melodee::sample_slots; ++slot) {
        const auto& ref = project.samples[slot];
        if (!ref.path[0] || !within(clean(ref.path), target)) continue;
        for (unsigned t = 0; t < melodee::track_count; ++t)
            for (unsigned at = 0; at < pad_count; ++at)
                if (project.tracks[t].engine == melodee::EngineKind::Sampler && project.tracks[t].pads[at].sample == slot)
                    return "PLAYS ON " + (t == studio.track() ? std::string() : std::to_string(t + 1) + " ") + pad_text(at);
        return "IN THE PROJECT";
    }
    if (job && within(clean(job->path), target)) return "BEING RECORDED";
    return {};
}

void Sampling::delete_path(const std::string& path) {
    if (!may_delete(path).empty()) return;
    deleting.push_back(path);
    starred.erase(std::remove_if(starred.begin(), starred.end(), [&](const std::string& p) { return within(clean(p), clean(path)); }), starred.end());
    recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const std::string& p) { return within(clean(p), clean(path)); }), recent.end());
    lists_dirty = true;
}

// ---------------------------------------------------------------- edits

void Sampling::trim(unsigned track_at, unsigned pad) {
    const auto& sound = studio.project().tracks[track_at].pads[pad % pad_count];
    const Sample* sample = studio.sample_of(track_at, pad);
    if (!sample || sample->data->left.empty()) { studio.say("NOTHING TO TRIM"); return; }
    auto edit = std::make_unique<Edit>();
    edit->kind = Edit::Kind::Trim;
    edit->track = track_at;
    edit->pad = pad;
    edit->start = sound.start;
    edit->end = sound.end;
    edit->source = sample->data.get();
    edit->sound = sound;
    edit->name = fs::path(sample->path).stem().string();
    edits.push_back(std::move(edit));
    studio.say("TRIMMING");
}

void Sampling::save(unsigned track_at, unsigned pad, const std::string& name) {
    const auto& sound = studio.project().tracks[track_at].pads[pad % pad_count];
    const Sample* sample = studio.sample_of(track_at, pad);
    if (!sample || sample->data->left.empty()) { studio.say("NOTHING TO SAVE"); return; }
    std::string kept;
    for (char c : name) kept += (c == '/' || c == '\\' || c < ' ') ? '-' : c; // a name, never a path
    while (!kept.empty() && (kept.back() == ' ' || kept.back() == '.')) kept.pop_back();
    if (kept.empty()) { studio.say("NO NAME"); return; }
    auto edit = std::make_unique<Edit>();
    edit->kind = Edit::Kind::Save;
    edit->track = track_at;
    edit->pad = pad;
    edit->start = sound.start;
    edit->end = sound.end;
    edit->source = sample->data.get();
    edit->name = kept;
    edits.push_back(std::move(edit));
    studio.say("SAVING");
}

void Sampling::extract(unsigned track_at, unsigned pad, uint32_t start, uint32_t end, unsigned slice) {
    const Sample* sample = studio.sample_of(track_at, pad);
    if (!sample || sample->data->left.empty()) return;
    const auto& pads_of = studio.project().tracks[track_at].pads;
    int target = -1;
    for (unsigned n = 1; n <= pad_count && target < 0; ++n) {
        unsigned at = (pad + n) % pad_count;
        if (!pads_of[at].used()) target = int(at);
    }
    if (target < 0) { studio.say("NO EMPTY PAD"); return; }
    auto edit = std::make_unique<Edit>();
    edit->kind = Edit::Kind::Extract;
    edit->track = track_at;
    edit->pad = pad;
    edit->start = start;
    edit->end = end;
    edit->slice = slice;
    edit->target = target;
    edit->source = sample->data.get();
    edit->sound = pads_of[pad];
    edit->name = fs::path(sample->path).stem().string();
    edits.push_back(std::move(edit));
    studio.say("EXTRACTING");
}

// ---------------------------------------------------------------- expansions

void Sampling::install(const std::string& xpn) {
    if (!installing.empty()) { studio.say("INSTALLING"); return; }
    installing = upper_text(fs::path(xpn).stem().string());
    install_done = install_total = 0;
    to_install.push_back(xpn);
}

// ---------------------------------------------------------------- service thread

void Sampling::service_files() {
    std::vector<std::unique_ptr<Edit>> todo_edits;
    std::vector<std::string> todo_deletes, todo_installs, star_list, recent_list;
    std::string wanted;
    unsigned track_now = 0;
    bool asked = false, lists = false;
    fs::path home;
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        todo_edits.swap(edits);
        todo_deletes.swap(deleting);
        todo_installs.swap(to_install);
        asked = preview_asked;
        preview_asked = false;
        wanted = preview_wanted;
        track_now = preview_track;
        lists = lists_dirty;
        lists_dirty = false;
        if (lists) { star_list = starred; recent_list = recent; }
        home = studio.home_folder();
    }
    if (lists) {
        std::error_code error;
        fs::create_directories(home, error);
        write_lines(home / "Starred.txt", star_list);
        write_lines(home / "Recent.txt", recent_list);
        hand_back((home / "Starred.txt").string(), home.string());
        hand_back((home / "Recent.txt").string(), home.string());
    }
    // The preview: read (up to 30 s), offered to the preview slot, then played.
    if (asked && !wanted.empty()) {
        std::string file = wanted;
        auto extension = upper_text(fs::path(file).extension().string());
        if (extension == ".XPM" || extension == ".XTY") { // a program: its expansion's own preview, if it has one
            std::error_code error;
            file.clear();
            for (const char* kind : {".mp3", ".wav", ".MP3", ".WAV"}) {
                auto candidate = fs::path(wanted).parent_path() / "[Previews]" / (fs::path(wanted).filename().string() + kind);
                if (fs::exists(candidate, error)) { file = candidate.string(); break; }
            }
        } else if (extension == ".JSON") { // a Move drum kit: its lowest pad's sample, as the kit's first hit
            file.clear();
            std::ifstream in(wanted, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            melodee::DrumRack rack;
            int lowest = 1 << 30;
            std::error_code error;
            if (melodee::parse_drum_rack(text, wanted, move_libraries(), rack).empty())
                for (const auto& pad : rack.pads)
                    if (pad.note < lowest && !pad.file.empty() && fs::is_regular_file(pad.file, error)) { lowest = pad.note; file = pad.file; }
        }
        auto data = std::make_unique<melodee::SampleData>();
        bool read = !file.empty() && melodee::read_audio(file, *data, preview_seconds, nullptr).empty() && data->frames;
        std::lock_guard<std::mutex> guard(studio.lock);
        if (read && preview_wanted == wanted && studio.host().offer_sample(melodee::preview_slot, data.get())) {
            melodee::Command play {melodee::Command::Type::Audition, uint8_t(track_now), 0, 0, 100};
            play.pad.sample = uint16_t(melodee::preview_slot);
            studio.send(play);
            previewing = wanted;
            preview_frames = data->model_frames();
            preview_at = Clock::now();
            previews.push_back(std::move(data)); // until the audio thread hands it back
        }
    }
    // Deleting: the file, or the folder with all in it.
    for (const auto& path : todo_deletes) {
        std::error_code error;
        fs::remove_all(path, error);
        auto extension = upper_text(fs::path(path).extension().string());
        if (!error && (extension == ".XPM" || extension == ".XTY")) { // an MPC program goes with the data folder that is its own
            std::error_code ignored;
            fs::remove_all(fs::path(path).parent_path() / (fs::path(path).stem().string() + "_[TrackData]"), ignored);
        }
        std::lock_guard<std::mutex> guard(studio.lock);
        ++deletes;
        studio.say(error ? "NOT DELETED" : "DELETED");
    }
    // Trim, Save, Extract: the region copied out of the sample in memory, written, and onto its pad.
    for (auto& edit : todo_edits) {
        const auto& source = *edit->source;
        uint32_t from = source.own(edit->start), to = edit->end ? source.own(edit->end) : source.frames;
        to = std::min<uint32_t>(to, uint32_t(source.left.size()));
        from = std::min(from, to);
        auto data = std::make_unique<melodee::SampleData>();
        data->left.assign(source.left.begin() + from, source.left.begin() + to);
        data->right.assign(source.right.begin() + from, source.right.begin() + to);
        data->frames = to - from;
        data->rate = source.rate;
        if (edit->kind == Edit::Kind::Extract) { // its edges faded over 2 ms, as Melodee's Extract writes them
            uint32_t fade = std::min<uint32_t>(data->frames / 2, source.rate / 500);
            for (uint32_t f = 0; f < fade; ++f) {
                float g = float(f) / float(fade);
                data->left[f] *= g; data->right[f] *= g;
                data->left[data->frames - 1 - f] *= g; data->right[data->frames - 1 - f] *= g;
            }
        }
        std::error_code error;
        fs::path folder = edit->kind == Edit::Kind::Save ? home / "Samples" : home / "Samples" / "Edits";
        fs::create_directories(folder, error);
        std::string stem = edit->kind == Edit::Kind::Save ? edit->name
                         : edit->kind == Edit::Kind::Trim ? upper_text(edit->name) + "-Trimmed"
                                                          : upper_text(edit->name) + "-Slice " + std::to_string(edit->slice + 1);
        std::string path = free_name(folder, stem);
        std::string failed = melodee::keep_capture(*data, data->rate, path);
        if (failed.empty()) hand_back(path, home.string());
        std::unique_ptr<Sample> sample = failed.empty() ? make_sample(path, std::move(data)) : nullptr;
        std::lock_guard<std::mutex> guard(studio.lock);
        if (!sample) { studio.say("NOT WRITTEN"); continue; }
        if (edit->kind == Edit::Kind::Save) { studio.say("SAVED " + upper_text(fs::path(path).stem().string()).substr(0, 10)); continue; }
        std::string why;
        int slot = studio.add_sample(std::move(sample), why);
        if (slot < 0) { studio.say(why); continue; }
        melodee::PadSound sound = edit->sound;
        sound.sample = uint16_t(slot);
        sound.loop_start = sound.loop_start > edit->start ? sound.loop_start - edit->start : 0;
        sound.start = 0;
        sound.end = 0;
        unsigned onto = edit->kind == Edit::Kind::Trim ? edit->pad : unsigned(edit->target);
        if (edit->kind == Edit::Kind::Extract) { sound = melodee::PadSound {}; sound.sample = uint16_t(slot); }
        studio.set_pad(edit->track, onto, sound);
        if (edit->track == studio.track()) studio.select_pad(onto);
        studio.say(edit->kind == Edit::Kind::Trim ? "TRIMMED " + pad_text(onto) : "EXTRACTED > " + pad_text(onto));
    }
    // An expansion: a file a pass would be Melodee's; here the whole of it in one go, its progress shown.
    for (const auto& xpn : todo_installs) {
        juce::ZipFile zip {juce::File(juce::String(fs::absolute(xpn).string()))};
        std::string title = fs::path(xpn).stem().string();
        for (int n = 0; n < zip.getNumEntries(); ++n) // its Expansion.xml names it
            if (const auto* entry = zip.getEntry(n); entry && entry->filename.endsWithIgnoreCase("Expansion.xml")) {
                std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(n));
                if (!stream) break;
                if (auto xml = juce::parseXML(stream->readEntireStreamAsString()))
                    if (auto* named = xml->getChildByName("title"); named && named->getAllSubText().trim().isNotEmpty())
                        title = named->getAllSubText().trim().toStdString();
                break;
            }
        for (auto& c : title) if (c == '/' || c == '\\') c = '-';
        fs::path folder = home / "Samples" / "Expansions" / title;
        std::error_code error;
        fs::create_directories(folder, error);
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            install_total = unsigned(zip.getNumEntries());
            install_done = 0;
        }
        bool ok = zip.getNumEntries() > 0;
        for (int n = 0; n < zip.getNumEntries(); ++n) {
            const auto* entry = zip.getEntry(n);
            if (!entry) continue;
            auto inside = fs::path(entry->filename.toStdString()).lexically_normal();
            if (inside.empty() || inside.is_absolute() || *inside.begin() == "..") continue; // nothing outside its folder
            auto to = folder / inside;
            if (entry->filename.endsWithChar('/')) { fs::create_directories(to, error); continue; }
            if (fs::exists(to, error) && int64_t(fs::file_size(to, error)) == entry->uncompressedSize) continue; // there already
            ok = zip.uncompressEntry(n, juce::File(juce::String(fs::absolute(folder).string())), true).wasOk() && ok;
            std::lock_guard<std::mutex> guard(studio.lock);
            install_done = unsigned(n + 1);
        }
        for (const auto& item : fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, error))
            hand_back(item.path().string(), home.string());
        hand_back(folder.string(), home.string());
        std::lock_guard<std::mutex> guard(studio.lock);
        install_done = install_total;
        installing.clear();
        installed = folder.string();
        ++installs;
        studio.say(ok ? "INSTALLED " + upper_text(title).substr(0, 7) : "INSTALL FAILED");
    }
}

} // namespace app
