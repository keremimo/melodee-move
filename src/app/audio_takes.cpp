// SPDX-License-Identifier: AGPL-3.0-only
// An audio track's takes (src/app/sampling.h), as Melodee's audio tracks
// (shell/audio_tracks.cpp, guide/audio-tracks.md) on the Move: Rec arms the
// track; the engine host records one pass of its pattern (Command::AudioRecord,
// EngineHost::audio_take()); the service thread drains it into its file as it
// comes and, once whole, keeps it in memory and gives the track a new pack of
// its patterns' takes (Command::AudioRegions), which the audio thread plays as
// the pattern does. A take's CLIP and WARP are the pattern's AudioRegion.
#include "app/sampling.h"
#include "app/sampling_jobs.h"
#include "app/seq_edit.h"
#include "app/sequencer.h"
#include "melodee/engine/audio_player.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/onsets.h"
#include "melodee/model/audio_track.h"
#include "melodee/shell/follow_tempo.h"
#include "melodee/shell/sample_decode.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace app {
namespace {

namespace fs = std::filesystem;
using melodee::InputSource;
using Type = melodee::Command::Type;
using BounceState = melodee::Bounce::State;

constexpr unsigned longest_take_seconds = 600; // a pattern's pass: 16 bars at 30 BPM is 128 s

// The loudest of each 441 frames (44.1 kHz), as Melodee's take peaks (model/audio_track.h).
void add_peaks(std::vector<uint8_t>& peaks, const float* left, const float* right, size_t from, size_t to, unsigned rate) {
    const double step = double(melodee::take_peak_frames) * rate / 44100.0;
    for (size_t f = from; f < to; ++f) {
        size_t column = size_t(double(f) / step);
        if (column >= peaks.size()) peaks.resize(column + 1, 0);
        auto v = uint8_t(std::min(1.0f, std::max(std::fabs(left[f]), std::fabs(right[f]))) * 255.0f);
        peaks[column] = std::max(peaks[column], v);
    }
}

} // namespace

std::vector<Sampling::Source> Sampling::audio_sources() const {
    std::vector<Source> list;
    if (!input_known || !line) list.push_back({InputSource::Mic});
    if (!input_known || line) {
        list.push_back({InputSource::Rear});
        list.push_back({InputSource::RearL});
        list.push_back({InputSource::RearR});
    }
    return list;
}

void Sampling::step_audio_source(unsigned t, int delta) {
    auto& setting = studio.project().tracks[t % melodee::track_count].audio;
    auto list = audio_sources();
    int at = 0;
    for (size_t n = 0; n < list.size(); ++n) if (list[n].source == setting.source) at = int(n);
    at = std::clamp(at + delta, 0, int(list.size()) - 1);
    if (setting.source == list[size_t(at)].source) return;
    setting.source = list[size_t(at)].source;
    studio.changed();
}

int Sampling::audio_recording() const {
    const auto& take = studio.host().audio_take();
    return take.running() ? studio.host().audio_take_track() : -1;
}

// The next take's number, after every take of the project (a file left by a take undone counts too), the ones in
// memory and the one armed. Under the lock.
unsigned Sampling::next_take(const std::string& folder) const {
    unsigned number = 1;
    std::error_code error;
    for (const auto& item : fs::directory_iterator(folder, error)) {
        auto name = item.path().filename().string();
        unsigned n = 0;
        if (std::sscanf(name.c_str(), "Take %u", &n) == 1) number = std::max(number, n + 1);
    }
    for (const auto& [n, data] : takes) number = std::max(number, n + 1);
    if (audio_job) number = std::max(number, audio_job->take + 1);
    return number;
}

void Sampling::import_take(const std::string& path, unsigned t) {
    t %= melodee::track_count;
    if (studio.kind(t) != TrackKind::Audio) return;
    if (studio.project_path().empty()) { studio.say("SAVE PROJECT 1ST"); return; }
    imports.push_back({path, t, studio.project().tracks[t].pattern % melodee::pattern_count});
    studio.say("LOADING");
}

void Sampling::land_import(const Import& job) {
    melodee::SampleData data;
    bool read = melodee::read_audio(job.path, data, longest_take_seconds, nullptr).empty() && data.frames && !data.left.empty();
    if (read && data.right.empty()) data.right = data.left;
    std::string folder, file;
    unsigned number = 0;
    uint16_t project_tempo = 1200;
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        if (!read) { studio.say("CANNOT READ IT"); return; }
        folder = melodee::audio_folder(studio.project_path());
        if (folder.empty()) { studio.say("SAVE PROJECT 1ST"); return; }
        number = next_take(folder);
        takes[number] = std::make_unique<TakeData>(); // its number kept from a take armed meanwhile
        project_tempo = studio.project().tempo;
    }
    file = (fs::path(folder) / melodee::audio_take_name(number)).string();
    std::error_code error;
    fs::create_directories(folder, error);
    bool written = melodee::write_wav(file, data).empty();
    if (written) hand_back(file, studio.home_folder());
    // Its tempo, as Follow finds a pad's (studio.cpp): the beats' tempo, its octave nearest the project's, exact at whole bars.
    const uint32_t frames = uint32_t(uint64_t(data.frames) * 44100 / std::max(1u, data.rate));
    std::array<melodee::Hit, melodee::max_hits> hits;
    melodee::BeatGrid beats;
    melodee::find_hits(data, hits, beats);
    uint16_t found = melodee::follow_tempo(frames, beats.found() ? beats.tempo : 0, project_tempo);
    auto kept = std::make_unique<TakeData>();
    kept->left = std::move(data.left);
    kept->right = std::move(data.right);
    kept->rate = data.rate;
    add_peaks(kept->peaks, kept->left.data(), kept->right.data(), 0, kept->left.size(), kept->rate);
    std::lock_guard<std::mutex> guard(studio.lock);
    if (!written) { takes.erase(number); studio.say("CANNOT WRITE TAKE"); return; }
    takes[number] = std::move(kept);
    if (studio.kind(job.track) != TrackKind::Audio) return; // the track changed meanwhile: the take stays on the card
    studio.close_step(); // what came before is a step of its own
    melodee::AudioRegion region;
    region.take = uint16_t(number);
    region.frames = region.take_frames = frames;
    region.warp = found != 0;
    region.tempo = found ? found : project_tempo;
    auto& pattern = studio.patterns().at[job.track][job.pattern];
    pattern.audio = region;
    const uint32_t bar = studio.project().bar_ticks();
    const double bars = double(melodee::frames_to_ticks(frames, region.tempo)) / double(std::max(1u, bar));
    const uint32_t whole = std::max(1u, unsigned(region.warp ? std::lround(bars) : std::ceil(bars - 0.01)));
    seq::set_length(pattern, whole * bar, studio.project().max_length());
    studio.sequencer().sync(job.track, job.pattern);
    send_pack(job.track);
    studio.name_step("LOAD AUDIO", int(job.track));
    studio.close_step();
    studio.changed();
    char text[24];
    std::snprintf(text, sizeof text, "%u BAR%s %s", whole, whole == 1 ? "" : "S", region.warp ? "WARPED" : "AS IS");
    studio.say(text);
}

void Sampling::arm_audio(unsigned t) {
    t %= melodee::track_count;
    if (audio_recording() == int(t)) { // recording: it ends here, kept
        studio.send({Type::AudioRecord, uint8_t(t), 0, 0, -2});
        audio_arm = -1;
        studio.say("TAKE ENDS");
        return;
    }
    if (audio_arm == int(t)) { // armed: let go
        studio.send({Type::AudioRecord, uint8_t(t), 0, 0, -1});
        audio_arm = -1;
        audio_job.reset();
        studio.say("NOT ARMED");
        return;
    }
    if (audio_job || studio.host().audio_take().state() != BounceState::Idle) { studio.say("SAVING TAKE"); return; }
    if (studio.kind(t) != TrackKind::Audio) return;
        std::string folder = melodee::audio_folder(studio.project_path());
    unsigned number = next_take(folder);
    audio_job = std::make_unique<AudioJob>();
    audio_job->track = t;
    audio_job->take = number;
    audio_job->path = (fs::path(folder) / melodee::audio_take_name(number)).string();
    audio_job->tempo = uint16_t(studio.project().tempo);
    studio.send({Type::AudioRecord, uint8_t(t), 0, 0, 0});
    audio_arm = int(t);
    recording_peaks.clear();
    recording_frames = 0;
    studio.say(studio.playing() ? "ARMED: NEXT PASS" : "ARMED: PLAY");
}

const Sampling::TakeData* Sampling::take_data(unsigned take) const {
    auto at = takes.find(take);
    return at == takes.end() ? nullptr : at->second.get();
}

void Sampling::audio_changed(unsigned t) { send_pack(t); }

void Sampling::project_opened() {
    if (audio_job) return; // a take armed or being written: it ends first (rare: a project opened while recording)
    audio_arm = -1;
    takes.clear(); // numbered by the project: another project's Take 1 is another take
    takes_wanted.clear();
    packs_wanted.clear();
    audio_restored = false; // service_audio() sends the new project's packs
}

void Sampling::restore_audio() {
    for (unsigned t = 0; t < melodee::track_count; ++t)
        if (studio.kind(t) == TrackKind::Audio) send_pack(t);
}

// The track's patterns' takes as a pack for the audio thread, from the takes in memory; a take not read yet is
// asked for, and the pack follows once it is.
void Sampling::send_pack(unsigned t) {
    t %= melodee::track_count;
    const auto& patterns = studio.patterns().at[t];
    bool waiting = false;
    for (const auto& pattern : patterns)
        if (pattern.audio.used() && !takes.count(pattern.audio.take)) {
            if (std::find(takes_wanted.begin(), takes_wanted.end(), pattern.audio.take) == takes_wanted.end()) takes_wanted.push_back(pattern.audio.take);
            waiting = true;
        }
    if (waiting) {
        if (std::find(packs_wanted.begin(), packs_wanted.end(), t) == packs_wanted.end()) packs_wanted.push_back(t);
        return;
    }
    auto* pack = new melodee::AudioPack;
    pack->track = uint8_t(t);
    for (unsigned p = 0; p < melodee::pattern_count; ++p) {
        const auto& region = patterns[p].audio;
        if (!region.used()) continue;
        const auto& data = *takes[region.take];
        auto part = std::make_unique<melodee::ZonePart>();
        part->left = data.left.data();
        part->right = data.right.data();
        part->rate = float(data.rate);
        part->total = uint32_t(data.left.size());
        part->frames.store(part->total);
        auto own = [&](uint32_t model) { return uint32_t(std::min<uint64_t>(uint64_t(model) * data.rate / 44100, part->total)); };
        auto& clip = pack->add(p);
        clip.take = part.get();
        clip.offset = own(region.offset);
        clip.frames = std::min(own(region.frames), part->total - clip.offset);
        clip.fade_in = own(region.fade_in);
        clip.fade_out = own(region.fade_out);
        clip.gain = std::pow(10.0f, float(region.gain) / 200.0f); // tenths of a dB
        clip.reverse = region.reverse;
        clip.tempo = region.tempo;
        clip.mode = region.mode;
        clip.warp = region.warp;
        clip.pitch = std::pow(2.0f, (float(region.semi) + float(region.fine) / 100.0f) / 12.0f);
        clip.resident = true; // every frame in memory: no pager
        clip.held = clip.frames;
        pack->takes.push_back(std::move(part));
    }
    melodee::Command command {Type::AudioRegions, uint8_t(t)};
    command.audio = pack;
    if (!studio.send(command)) delete pack;
}

void Sampling::service_audio() {
    auto& host = studio.host();
    auto& take = host.audio_take();
    AudioJob* job_now = nullptr;
    BounceState state;
    std::vector<unsigned> to_read;
    std::string folder;
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        if (!audio_restored) { audio_restored = true; restore_audio(); }
        job_now = audio_job.get();
        state = take.state();
        to_read.swap(takes_wanted);
        folder = melodee::audio_folder(studio.project_path());
    }
    // Files made takes (import_take()), one at a time.
    std::vector<Import> to_import;
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        to_import.swap(imports);
    }
    for (const auto& job : to_import) land_import(job);
    // Takes the packs wait for: read from their files, kept for the run.
    for (unsigned number : to_read) {
        melodee::SampleData data;
        auto path = (fs::path(folder) / melodee::audio_take_name(number)).string();
        bool read = melodee::read_audio(path, data, longest_take_seconds, nullptr).empty() && data.frames && !data.left.empty();
        auto kept = std::make_unique<TakeData>();
        if (read) {
            kept->left = std::move(data.left);
            kept->right = data.right.empty() ? kept->left : std::move(data.right);
            kept->rate = data.rate;
            add_peaks(kept->peaks, kept->left.data(), kept->right.data(), 0, kept->left.size(), kept->rate);
        } else { // gone: a moment of silence keeps its place
            kept->left.assign(64, 0.0f);
            kept->right.assign(64, 0.0f);
        }
        std::lock_guard<std::mutex> guard(studio.lock);
        if (!takes.count(number)) takes[number] = std::move(kept); // one there already may be playing: it stays
    }
    if (!to_read.empty()) {
        std::lock_guard<std::mutex> guard(studio.lock);
        std::vector<unsigned> tracks;
        tracks.swap(packs_wanted);
        for (unsigned t : tracks) send_pack(t);
    }
    if (!job_now) {
        if (state == BounceState::Done || state == BounceState::Cancelled || state == BounceState::Failed) { // nobody's
            float drop[2 * 1024];
            while (take.pop(drop, 1024)) {}
            take.release();
        }
        return;
    }
    auto& j = *job_now;
    if (!j.open && state != BounceState::Idle && state != BounceState::Cued) {
        std::error_code error;
        fs::create_directories(fs::path(j.path).parent_path(), error);
        j.file.open(j.path + ".part", take.rate(), 24);
        j.open = true;
    }
    if (!j.open) return;
    std::vector<float> chunk(2 * 4096);
    std::vector<uint8_t> peaks;
    size_t before = j.left.size();
    for (unsigned n; (n = take.pop(chunk.data(), 4096)) > 0;) {
        j.file.add(chunk.data(), n);
        for (unsigned f = 0; f < n; ++f) { j.left.push_back(chunk[2 * f]); j.right.push_back(chunk[2 * f + 1]); }
    }
    if (j.left.size() > before) {
        std::lock_guard<std::mutex> guard(studio.lock);
        add_peaks(recording_peaks, j.left.data(), j.right.data(), before, j.left.size(), take.rate());
        recording_frames = uint32_t(uint64_t(j.left.size()) * 44100 / std::max(1u, take.rate()));
    }
    bool ended = state == BounceState::Done || state == BounceState::Cancelled || state == BounceState::Failed;
    if (!ended || !take.drained()) return;
    // Whole: kept, onto the pattern it recorded; or dropped.
    bool kept = j.file.close() && state == BounceState::Done && !j.left.empty();
    std::error_code error;
    if (kept) { fs::rename(j.path + ".part", j.path, error); hand_back(j.path, studio.home_folder()); }
    else fs::remove(j.path + ".part", error);
    const unsigned rate = take.rate();
    int pattern = host.audio_take_pattern();
    take.release();
    std::lock_guard<std::mutex> guard(studio.lock);
    unsigned t = j.track, number = j.take;
    uint16_t tempo = j.tempo;
    auto data = std::make_unique<TakeData>();
    data->left = std::move(j.left);
    data->right = std::move(j.right);
    data->rate = rate;
    add_peaks(data->peaks, data->left.data(), data->right.data(), 0, data->left.size(), rate);
    uint32_t frames = uint32_t(uint64_t(data->left.size()) * 44100 / std::max(1u, rate));
    audio_job.reset();
    if (audio_arm == int(t)) audio_arm = -1;
    recording_peaks.clear();
    if (!kept || pattern < 0) { studio.say(state == BounceState::Failed ? "TAKE: TOO SLOW" : "NO TAKE"); return; }
    takes[number] = std::move(data);
    melodee::AudioRegion region;
    region.take = uint16_t(number);
    region.frames = region.take_frames = frames;
    region.tempo = tempo;
    studio.patterns().at[t][unsigned(pattern) % melodee::pattern_count].audio = region; // a take recorded over: Undo would bring it back
    send_pack(t);
    studio.changed();
    char text[24];
    std::snprintf(text, sizeof text, "TAKE %u > PAT %d", number, pattern + 1);
    studio.say(text);
}

} // namespace app
