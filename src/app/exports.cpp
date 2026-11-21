// SPDX-License-Identifier: AGPL-3.0-only
#include "app/exports.h"
#include "app/prints.h"
#include "app/project_store.h"
#include "app/song_desk.h"
#include "app/studio.h"
#include "app/wav_out.h"
#include "melodee/model/print.h"
#include "melodee/shell/mpc3_export.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace app {
namespace fs = std::filesystem;
using Type = melodee::Command::Type;
using Kind = Exports::Kind;

namespace {

// The export's name: the project's own, or Song.
std::string name_of(const melodee::Project& project) {
    std::string name;
    for (char c : std::string(project.name)) name += std::string("/\\:\"*?<>|").find(c) != std::string::npos ? '-' : c;
    if (name.empty() || name == "Untitled") name = "Song";
    return name;
}

std::string upper(std::string text) {
    for (auto& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

} // namespace

const char* Exports::kind_name(Kind kind) {
    switch (kind) {
    case Kind::Mix32: return "MIXDOWN 32 FLOAT";
    case Kind::Mix24: return "MIXDOWN 24 BIT";
    case Kind::Mix16: return "MIXDOWN 16 BIT";
    case Kind::Stems: return "STEMS";
    case Kind::Midi: return "MIDI FILE";
    case Kind::Mpc3: return "MPC 3 PROJECT";
    case Kind::ToPattern: return "SONG TO PATTERN";
    default: return "";
    }
}

std::string Exports::folder_of(Kind kind) {
    switch (kind) {
    case Kind::Mix32: case Kind::Mix24: case Kind::Mix16: return "Mixdowns";
    case Kind::Stems: return "Stems";
    case Kind::Midi: return "MIDI";
    case Kind::Mpc3: return "MPC 3";
    default: return "";
    }
}

std::string Exports::free_name(const std::string& folder, const std::string& stem, const std::string& extension) {
    std::error_code e;
    for (unsigned n = 1; n < 10000; ++n) {
        std::string name = stem + (n > 1 ? " " + std::to_string(n) : std::string());
        auto candidate = fs::path(folder) / (name + extension);
        // An MPC 3 project is its file and its data folder.
        bool taken = fs::exists(candidate, e) || (extension == ".xpj" && fs::exists(fs::path(folder) / (name + "_[ProjectData]"), e));
        if (!taken) return name;
    }
    return stem;
}

Exports::Exports(Studio& owner) : studio(owner) { thread = std::thread([this] { run(); }); }
Exports::~Exports() { stop(); }

void Exports::stop() {
    {
        std::lock_guard<std::mutex> guard(lock);
        if (stopping) return;
        stopping = true;
    }
    cancelled.store(true);
    wake.notify_all();
    if (thread.joinable()) thread.join();
}

std::string Exports::start(Kind what) {
    if (running.load()) return "AN EXPORT RUNS";
    auto& project = studio.project();
    auto& desk = studio.desk();
    home = studio.home_folder();
    std::string stem = name_of(project);
    {
        std::lock_guard<std::mutex> guard(lock);
        now = Shown {};
        now.kind = what;
    }
    auto finished = [&](std::string name, std::string file, uint64_t bytes, std::string said) {
        std::lock_guard<std::mutex> guard(lock);
        now.done = true;
        now.name = std::move(name);
        now.file = std::move(file);
        now.bytes = bytes;
        now.said = std::move(said);
        ++exported;
        return std::string();
    };
    switch (what) {
    case Kind::ToPattern: {
        std::string said;
        if (desk.to_pattern(said) < 0) return said;
        return finished("", "", 0, said);
    }
    case Kind::Midi: {
        auto bytes = desk.midi_file();
        if (bytes.empty()) return "NO NOTES";
        std::string folder = (fs::path(home) / folder_of(what)).string();
        std::error_code e;
        fs::create_directories(folder, e);
        std::string name = free_name(folder, stem, ".mid");
        std::string file = (fs::path(folder) / (name + ".mid")).string();
        if (auto error = write_whole(file, bytes.data(), bytes.size()); !error.empty()) return "CANNOT WRITE";
        give_to_owner_of(folder, home);
        give_to_owner_of(file, home);
        return finished(name, folder_of(what) + "/" + name + ".mid", bytes.size(), "");
    }
    case Kind::Mpc3: {
        std::string folder = (fs::path(home) / folder_of(what)).string();
        kind = what;
        path = folder;
        {
            std::lock_guard<std::mutex> guard(lock);
            now.name = free_name(folder, stem, ".xpj");
            now.file = folder_of(what) + "/" + now.name + ".xpj";
            now.busy = true;
        }
        // Synth tracks with notes go as their prints: those printed first (tick()).
        mpc3_pending = 0;
        for (unsigned t = 0; t < melodee::track_count; ++t) {
            if (studio.kind(t) != TrackKind::Synth || desk.printer().deep(t) || !desk.printer().printable(t)) continue;
            auto with = desk.printer().with_notes(t);
            if (with[0] | with[1]) mpc3_pending |= 1u << t;
        }
        if (mpc3_pending && !studio.project_path().empty()) {
            mpc3_waiting = true;
            cancelled.store(false);
            running.store(true, std::memory_order_release);
            return {};
        }
        mpc3_pending = 0;
        mpc3_project = std::make_unique<melodee::Project>(project);
        mpc3_bank = std::make_unique<melodee::PatternBank>(studio.patterns());
        mpc3_prints = std::make_unique<melodee::PrintTable>(desk.printer().table());
        mpc3_prints_dir = melodee::prints_folder(studio.project_path());
        break;
    }
    case Kind::Mix32: case Kind::Mix24: case Kind::Mix16: case Kind::Stems: {
        const auto& song = project.song;
        if (!song.length()) return "SONG EMPTY";
        if (studio.host().bounce().state() != melodee::Bounce::State::Idle) return "A RENDER RUNS";
        kind = what;
        std::string folder = (fs::path(home) / folder_of(what)).string();
        std::string name = free_name(folder, stem, what == Kind::Stems ? "" : ".wav");
        path = what == Kind::Stems ? (fs::path(folder) / name).string() : (fs::path(folder) / (name + ".wav")).string();
        stem_mask = 0;
        if (what == Kind::Stems) {
            for (unsigned t = 0; t < melodee::track_count; ++t) {
                const auto& track = project.tracks[t];
                if (!track.used()) continue;
                stem_mask |= 1u << t;
                char text[40];
                std::snprintf(text, sizeof text, "%02u %s", t + 1, track.name[0] ? track.name : ("Track " + std::to_string(t + 1)).c_str());
                stem_names[t] = text;
            }
            for (unsigned b = 0; b < melodee::bus_count; ++b) { // a bus something sends into
                bool fed = false;
                for (const auto& track : project.tracks) fed |= track.used() && track.mix.sends[b] > 0;
                if (!fed) continue;
                stem_mask |= 1u << (melodee::track_count + b);
                stem_names[melodee::track_count + b] = std::string("Bus ") + char('A' + b);
            }
            if (!stems) stems = std::make_unique<melodee::StemRing>(); // 21 MB, made once, when first wanted
            stems->arm(stem_mask);
            studio.host().set_stems(stems.get());
        } else {
            studio.host().set_stems(nullptr);
        }
        song_seconds = desk.seconds_at(song.length());
        {
            std::lock_guard<std::mutex> guard(lock);
            now.name = name;
            now.file = folder_of(what) + "/" + name + (what == Kind::Stems ? "/" : ".wav");
            now.total = song_seconds;
            now.busy = true;
        }
        desk.set_recording(false);
        studio.send({Type::Stop});
        desk.send_song();
        studio.send({Type::BounceArm, 0, 0, 0, 1});
        studio.send({Type::Play, 0, 0, 0, 1}); // the song from bar 1
        break;
    }
    default: return "NOTHING";
    }
    cancelled.store(false);
    running.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> guard(lock);
        job_waiting = true;
    }
    wake.notify_all();
    return {};
}

void Exports::tick() {
    if (!mpc3_waiting) return;
    auto& printer = studio.desk().printer();
    printer.tick(); // the last prints' records in the table first
    if (printer.busy()) return;
    while (mpc3_pending) {
        unsigned t = unsigned(__builtin_ctz(mpc3_pending));
        mpc3_pending &= mpc3_pending - 1;
        Printer::Job job;
        job.track = t;
        auto with = printer.with_notes(t);
        for (unsigned p = 0; p < melodee::pattern_count; ++p) if (with[p / 64] >> (p % 64) & 1) job.patterns.push_back(uint8_t(p));
        job.freeze = false;
        if (!printer.start(std::move(job)).empty()) continue; // a module missing: that track is left out (the report says)
        std::lock_guard<std::mutex> guard(lock);
        now.said = "PRINTING TRACK " + std::to_string(t + 1);
        return;
    }
    // Every print made: the project, as it is now, written on the export thread.
    mpc3_waiting = false;
    mpc3_project = std::make_unique<melodee::Project>(studio.project());
    mpc3_bank = std::make_unique<melodee::PatternBank>(studio.patterns());
    mpc3_prints = std::make_unique<melodee::PrintTable>(printer.table());
    mpc3_prints_dir = melodee::prints_folder(studio.project_path());
    {
        std::lock_guard<std::mutex> guard(lock);
        now.said.clear();
        job_waiting = true;
    }
    wake.notify_all();
}

void Exports::cancel() {
    if (!running.load()) return;
    if (mpc3_waiting) { // its prints called off: nothing written
        studio.desk().printer().cancel();
        mpc3_waiting = false;
        mpc3_pending = 0;
        running.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> guard(lock);
        now.busy = false;
        now.error = "stopped";
        return;
    }
    cancelled.store(true);
    if (kind != Kind::Mpc3) studio.send({Type::Stop}); // the render stops with the song
}

Exports::Shown Exports::shown() const {
    std::lock_guard<std::mutex> guard(lock);
    return now;
}

void Exports::seen() {
    std::lock_guard<std::mutex> guard(lock);
    if (!now.busy) now = Shown {};
}

std::string Exports::status() const {
    std::lock_guard<std::mutex> guard(lock);
    char text[200];
    std::string file = now.file.empty() ? "-" : now.file;
    std::replace(file.begin(), file.end(), ' ', '_'); // one field
    std::snprintf(text, sizeof text, " export=%s export_busy=%d export_done=%d exports=%u export_frac=%.2f", file.c_str(),
                  int(now.busy), int(now.done), exported, now.fraction);
    std::string line = text;
    if (!now.error.empty()) line += " export_error=" + upper(now.error.substr(0, 24));
    return line;
}

// ---------------------------------------------------------------- the export thread

void Exports::run() {
    for (;;) {
        {
            std::unique_lock<std::mutex> guard(lock);
            wake.wait(guard, [&] { return stopping || job_waiting; });
            if (stopping) return;
            job_waiting = false;
        }
        if (kind == Kind::Mpc3) mpc3_job();
        else render_job();
        running.store(false, std::memory_order_release);
    }
}

void Exports::render_job() {
    auto& bounce = studio.host().bounce();
    const bool with_stems = kind == Kind::Stems;
    std::error_code e;
    fs::create_directories(with_stems ? fs::path(path) : fs::path(path).parent_path(), e);
    using State = melodee::Bounce::State;
    // Wait for the audio thread to take the arm (the song starts with it).
    auto t0 = std::chrono::steady_clock::now();
    while (bounce.state() == State::Idle && !cancelled.load() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3))
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    unsigned rate = bounce.rate();
    WavOut mix;
    std::string mix_path = with_stems ? (fs::path(path) / "Mix.wav").string() : path;
    unsigned bits = kind == Kind::Mix24 ? 24 : kind == Kind::Mix16 ? 16 : 32;
    bool ok = mix.open(mix_path + ".part", rate, bits, 2);
    std::array<std::unique_ptr<WavOut>, melodee::StemRing::stem_count> stem_files;
    if (with_stems)
        for (unsigned s = 0; s < melodee::StemRing::stem_count; ++s) {
            if (!((stem_mask >> s) & 1)) continue;
            stem_files[s] = std::make_unique<WavOut>();
            ok &= stem_files[s]->open((fs::path(path) / (stem_names[s] + ".wav.part")).string(), rate, 32, 2);
        }
    constexpr unsigned chunk = 4096;
    std::vector<float> buffer(size_t(chunk) * 2), stem_buffer(with_stems ? size_t(chunk) * 2 * melodee::StemRing::stem_count : 0), one(size_t(chunk) * 2);
    double total = std::max(0.1, song_seconds) * rate;
    bool failed = !ok;
    for (;;) {
        State state = bounce.state();
        unsigned n = bounce.pop(buffer.data(), chunk);
        if (n && !failed) failed |= !mix.add(buffer.data(), n);
        unsigned m = 0;
        if (with_stems) {
            m = stems->pop(stem_buffer.data(), chunk);
            for (unsigned s = 0; s < melodee::StemRing::stem_count && m; ++s)
                if (stem_files[s]) failed |= !stem_files[s]->add(stem_buffer.data() + size_t(s) * 2 * chunk, m);
            failed |= stems->failed();
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            now.seconds = double(mix.written()) / rate;
            now.tail = std::max(0.0, now.seconds - song_seconds);
            now.fraction = float(std::min(1.0, double(mix.written()) / total));
        }
        bool over = state == State::Done || state == State::Cancelled || state == State::Failed;
        bool drained = bounce.drained() && (!with_stems || stems->drained());
        if (cancelled.load() || (over && drained)) {
            bool kept = state == State::Done && !cancelled.load() && !failed;
            if (state == State::Failed) failed = true;
            bool closed = mix.close();
            for (auto& f : stem_files) if (f) closed &= f->close();
            kept &= closed;
            auto finish = [&](const std::string& part) {
                std::string final_path = part.substr(0, part.size() - 5);
                if (kept) fs::rename(part, final_path, e);
                else fs::remove(part, e);
                if (kept) give_to_owner_of(final_path, studio.home_folder());
            };
            finish(mix_path + ".part");
            for (unsigned s = 0; s < stem_files.size(); ++s)
                if (stem_files[s]) finish((fs::path(path) / (stem_names[s] + ".wav.part")).string());
            if (!kept && with_stems) fs::remove(path, e);
            if (kept) give_to_owner_of(with_stems ? path : fs::path(path).parent_path().string(), studio.home_folder());
            bounce.release();
            studio.host().set_stems(nullptr);
            std::lock_guard<std::mutex> guard(lock);
            now.busy = false;
            now.done = kept;
            now.bytes = mix.bytes();
            if (!kept) now.error = cancelled.load() || state == State::Cancelled ? "stopped" : failed ? "could not write" : "stopped";
            if (kept) ++exported;
            return;
        }
        if (!n && !m) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void Exports::mpc3_job() {
    melodee::Mpc3Export report;
    std::string name;
    {
        std::lock_guard<std::mutex> guard(lock);
        name = now.name;
    }
    std::error_code e;
    fs::create_directories(path, e);
    auto error = melodee::export_mpc3_project(*mpc3_project, *mpc3_bank, path, name, report, mpc3_prints.get(), mpc3_prints_dir);
    mpc3_project.reset();
    mpc3_bank.reset();
    mpc3_prints.reset();
    if (error.empty()) give_to_owner_of(path, studio.home_folder());
    std::lock_guard<std::mutex> guard(lock);
    now.busy = false;
    now.done = error.empty();
    now.error = error;
    if (error.empty()) {
        ++exported;
        char text[64];
        unsigned left = report.skipped_tracks + report.skipped_pads;
        std::snprintf(text, sizeof text, "%u TRACKS %u SEQ%s", report.tracks + report.keygroups + report.audio + report.midi, report.sequences,
                      left ? (" " + std::to_string(left) + " LEFT").c_str() : "");
        now.said = text;
    }
}

} // namespace app
