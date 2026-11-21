// SPDX-License-Identifier: AGPL-3.0-only
#include "app/sampling.h"
#include "app/sampling_jobs.h"
#include "app/studio.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/recall_ring.h"
#include "melodee/engine/take_recorder.h"
#include "melodee/shell/chop.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace app {
namespace {

namespace fs = std::filesystem;
using melodee::Command;
using melodee::InputSource;
using melodee::Monitor;
using State = melodee::TakeRecorder::State;
using Type = melodee::Command::Type;

constexpr auto held_long = std::chrono::milliseconds(400); // the Move's hold: a pad held this long records until let go
constexpr auto arm_settles = std::chrono::milliseconds(200); // a take armed and let go before it recorded, after this
constexpr int threshold_lowest = -72; // below it: at once
constexpr unsigned most_cuts = pad_count; // a pad a part

std::string pad_name(unsigned pad) {
    char text[8];
    std::snprintf(text, sizeof text, "%c%02u", 'A' + int(pad / 16), pad % 16 + 1);
    return text;
}

// RecordArm's rec length: 0 Free, 1..16 bars, 255 Seq (as Melodee's shell sends it).
uint8_t length_code(const melodee::InputConfig& in) {
    return in.length == melodee::rec_free ? 0 : in.length == melodee::rec_seq ? 255 : uint8_t(melodee::rec_bars(in.length));
}

bool hardware(InputSource s) { return s == InputSource::Mic || melodee::from_rear(s); }

} // namespace

Sampling::Sampling(Studio& owner) : studio(owner) {
    input.threshold = int8_t(melodee::InputConfig::threshold_min); // the Move records the moment a pad is pressed
    input.source = InputSource::Rear;                               // the input, whatever the jack has (until it says)
    read_lists();
}

Sampling::~Sampling() = default;

// ---------------------------------------------------------------- the jack

void Sampling::sense_input(uint8_t value) {
    bool now = value >= 64;
    bool first = !input_known;
    input_known = true;
    if (!first && now == line) return;
    line = now;
    // A hardware source follows the jack: the mic with no cable, the line (as it was last picked) with one.
    if (!hardware(input.source)) return;
    if (studio.host().recorder().state() != State::Idle) return; // stays while a take runs; next time
    if (line && input.source == InputSource::Mic) set_source({line_choice});
    else if (!line && input.source != InputSource::Mic) { line_choice = input.source; set_source({InputSource::Mic}); }
    else changed();
}

void Sampling::sense_output(uint8_t value) {
    bool now = value < 64;
    if (now == to_speaker) return;
    to_speaker = now;
    // The mic heard through the speaker howls: Monitor off, as Melodee's Input Config turns it off.
    if (to_speaker && input.source == InputSource::Mic && input.monitor != Monitor::Off) {
        input.before_mic = int8_t(input.monitor);
        input.monitor = Monitor::Off;
        changed();
    }
}

// ---------------------------------------------------------------- the input

std::vector<Sampling::Source> Sampling::sources() const {
    std::vector<Source> list;
    if (!input_known || line) {
        list.push_back({InputSource::Rear});
        list.push_back({InputSource::RearL});
        list.push_back({InputSource::RearR});
    }
    if (!input_known || !line) list.insert(list.begin(), {InputSource::Mic});
    list.push_back({InputSource::Resample});
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        if (t == studio.track()) continue; // the kit recorded onto
        auto kind = studio.kind(t);
        if (kind == TrackKind::Empty || kind == TrackKind::Midi) continue;
        list.push_back({InputSource::Track, t});
    }
    return list;
}

int Sampling::source_index() const {
    auto list = sources();
    for (size_t n = 0; n < list.size(); ++n)
        if (list[n].source == input.source && (input.source != InputSource::Track || list[n].track == input.track)) return int(n);
    return -1;
}

void Sampling::set_source(const Source& source) {
    if (studio.host().recorder().state() != State::Idle) { studio.say("SOURCE STAYS"); return; }
    if (melodee::from_rear(source.source)) line_choice = source.source;
    Monitor was = input.monitor;
    input.set_source(source.source, to_speaker); // the mic through the speaker: Monitor off
    input.track = uint8_t(source.track % melodee::track_count);
    if (input.monitor != was) studio.say(std::string("MONITOR ") + (input.monitor == Monitor::Off ? "OFF" : input.monitor == Monitor::On ? "ON" : "AUTO"));
    changed();
}

std::string Sampling::source_tag() const {
    switch (input.source) {
    case InputSource::Mic: return "MIC";
    case InputSource::Rear: return input_known ? "LINE" : "IN";
    case InputSource::RearL: return input_known ? "LN L" : "IN L";
    case InputSource::RearR: return input_known ? "LN R" : "IN R";
    case InputSource::Resample: return "RSMP";
    case InputSource::Track: return "TRK" + std::to_string(input.track + 1);
    default: return "IN";
    }
}

std::string Sampling::source_name() const {
    switch (input.source) {
    case InputSource::Mic: return "MIC";
    case InputSource::Rear: return input_known ? "LINE" : "INPUT";
    case InputSource::RearL: return input_known ? "LINE L" : "INPUT L";
    case InputSource::RearR: return input_known ? "LINE R" : "INPUT R";
    case InputSource::Resample: return "RESAMPLE";
    case InputSource::Track: return "TRACK " + std::to_string(input.track + 1);
    default: return "INPUT";
    }
}

void Sampling::step_source(int delta) {
    auto list = sources();
    if (list.empty() || !delta) return;
    int at = source_index();
    int next = at < 0 ? 0 : std::clamp(at + delta, 0, int(list.size()) - 1);
    if (next != at) set_source(list[size_t(next)]);
}

void Sampling::step_gain(int delta) {
    input.gain = int8_t(std::clamp(input.gain + delta, -12, 24));
    changed();
}

void Sampling::step_monitor(int delta) {
    int next = std::clamp(int(input.monitor) + delta, 0, int(Monitor::Count) - 1);
    input.monitor = Monitor(next);
    input.before_mic = -1;
    changed();
}

void Sampling::step_threshold(int delta) {
    int t = input.at_once() ? (delta > 0 ? threshold_lowest - 1 : input.threshold) : int(input.threshold);
    t += delta;
    if (t < threshold_lowest) t = melodee::InputConfig::threshold_min;
    input.threshold = int8_t(std::clamp(t, int(melodee::InputConfig::threshold_min), 0));
    changed();
}

void Sampling::step_length(int delta) {
    input.length = uint8_t(std::clamp(int(input.length) + delta, 0, int(melodee::rec_lengths) - 1));
    changed();
}

std::string Sampling::gain_text() const {
    char text[16];
    std::snprintf(text, sizeof text, "%+d DB", int(input.gain));
    return text;
}
std::string Sampling::monitor_text() const {
    return input.monitor == Monitor::Off ? "OFF" : input.monitor == Monitor::On ? "ON" : "AUTO";
}
std::string Sampling::threshold_text() const {
    if (input.at_once()) return "AT ONCE";
    return std::to_string(int(input.threshold)) + " DB";
}
std::string Sampling::length_text() const {
    if (input.length == melodee::rec_free) return "FREE";
    if (input.length == melodee::rec_seq) return "SEQ";
    unsigned bars = melodee::rec_bars(input.length);
    return std::to_string(bars) + (bars == 1 ? " BAR" : " BARS");
}

void Sampling::send_source() {
    studio.send({Type::RecordSource, uint8_t(input.source), 0, uint16_t(input.track % melodee::track_count), int16_t(input.post)});
    studio.send({Type::RecordGain, 0, 0, 0, int16_t(input.gain)});
}

void Sampling::changed() {
    input.clamp();
    if (studio.host().recorder().state() == State::Idle) send_source(); // the engine takes no new source mid-take
    else studio.send({Type::RecordGain, 0, 0, 0, int16_t(input.gain)});
    studio.changed();
}

// ---------------------------------------------------------------- a take

Sampling::Phase Sampling::phase() const {
    if (saving) return Phase::Saving;
    switch (studio.host().recorder().state()) {
    case State::Armed: return Phase::Armed;
    case State::Cued: return Phase::Cued;
    case State::Recording: return Phase::Recording;
    case State::Done: return Phase::Saving;
    default: return job && armed_here ? Phase::Armed : Phase::Idle; // armed, the audio thread not there yet
    }
}

uint32_t Sampling::take_frames() const { return studio.host().recorder().frames(); }
uint32_t Sampling::take_length() const { return input.length == melodee::rec_free ? 0 : studio.host().recorder().length(); }
uint8_t Sampling::take_peak(unsigned column) const { return studio.host().recorder().peak(column); }
float Sampling::level_left() const { return studio.host().recorder().level_left(); }
float Sampling::level_right() const { return studio.host().recorder().level_right(); }

bool Sampling::pads_play_source() const {
    if (!melodee::internal_source(input.source)) return false;
    auto p = phase();
    return p == Phase::Armed || p == Phase::Cued || p == Phase::Recording;
}

std::string Sampling::folder() const { return (fs::path(studio.home_folder()) / "Samples" / "Recorded").string(); }

unsigned Sampling::next_number(const std::string& where, const char* stem) const {
    std::error_code error;
    for (unsigned n = 1; n < 100000; ++n) {
        auto name = (fs::path(where) / (std::string(stem) + " " + std::to_string(n) + ".wav")).string();
        if (fs::exists(name, error) || fs::exists(name + ".part", error)) continue;
        if (job && job->path == name) continue;
        bool taken = false;
        for (const auto& recall : recalls) taken |= recall->path == name;
        if (!taken) return n;
    }
    return 0;
}

bool Sampling::arm(unsigned pad) {
    unsigned kit = studio.track();
    if (studio.kind(kit) != TrackKind::Kit) { studio.say("SAMPLING: KITS"); return false; }
    if (job || saving) { studio.say("SAVING TAKE"); return false; }
    if (input.source == InputSource::Track) {
        if (input.track == kit) { studio.say("SOURCE: ANOTHER"); return false; }
        if (studio.kind(input.track) == TrackKind::Empty) { studio.say("SOURCE IS EMPTY"); return false; }
    }
    const char* stem = input.source == InputSource::Resample ? "RESAMPLE" : "TAKE";
    unsigned number = next_number(folder(), stem);
    job = std::make_unique<Job>();
    job->kind = Job::Kind::Take;
    job->label = std::string(stem) + " " + std::to_string(number);
    job->path = (fs::path(folder()) / (job->label + ".wav")).string();
    job->track = kit;
    job->armed_at = Clock::now();
    track = kit;
    pads = {uint8_t(pad)};
    cuts = {0};
    armed_here = true;
    armed_at = job->armed_at;
    studio.select_pad(pad);
    // Free: the take ends on its own after a quiet stretch where the SAMPLING rows say so for this source.
    Command quiet {Type::RecordAutoEnd, 0, 0, 0, int16_t(prefs.silence_db())};
    if (input.length == melodee::rec_free && prefs.auto_ends(input.source))
        quiet.position = uint32_t(melodee::SamplingPrefs::end_seconds[prefs.end_after % melodee::SamplingPrefs::end_count] * 44100.0f);
    studio.send(quiet);
    send_source();
    // Melodee's own sound with SEQ while stopped: Play first, as the Move's Resampling starts playback, so the take is
    // one pass of the pattern from its top.
    bool seq = input.length == melodee::rec_seq;
    if (seq && melodee::internal_source(input.source) && !studio.playing()) studio.play();
    Command arming {Type::RecordArm, uint8_t(input.source), length_code(input), 0, int16_t(input.threshold)};
    arming.position = input.source == InputSource::Track ? input.track : kit; // SEQ: the source track's pattern
    studio.send(arming);
    studio.say((seq && studio.playing() ? "NEXT LOOP > " : input.at_once() ? "REC > " : "WAITING > ") + pad_name(pad));
    return true;
}

void Sampling::press_pad(unsigned pad) {
    pad %= pad_count;
    switch (phase()) {
    case Phase::Idle: arm(pad); return;
    case Phase::Armed: case Phase::Cued:
        if (pads_play_source()) return; // the pads are the source's
        if (!pads.empty() && pad == pads.back()) studio.send({Type::RecordNow}); // the pad again: at once
        else studio.say("REC > " + pad_name(pads.empty() ? pad : pads.back()));
        return;
    case Phase::Recording:
        if (pads_play_source()) return;
        if (!pads.empty() && pad == pads.back()) { stop(); return; } // the pad again: done
        if (std::find(pads.begin(), pads.end(), uint8_t(pad)) != pads.end()) return; // a part of it already
        if (pads.size() >= most_cuts) { studio.say("32 PARTS AT MOST"); return; }
        // Another pad: the take goes on onto it from here (Melodee: other pads cut the take there).
        pads.push_back(uint8_t(pad));
        cuts.push_back(take_frames());
        studio.say(pad_name(pads[pads.size() - 2]) + " > " + pad_name(pad));
        return;
    case Phase::Saving: studio.say("SAVING TAKE"); return;
    }
}

void Sampling::release_pad(unsigned pad, Clock::duration held) {
    if (phase() != Phase::Recording || pads_play_source()) return;
    if (!pads.empty() && pad == pads.back() && held >= held_long) stop(); // held: records until let go
}

void Sampling::stop() {
    auto p = phase();
    if (p == Phase::Armed || p == Phase::Cued || p == Phase::Recording) studio.send({Type::RecordStop});
}

void Sampling::cancel() { stop(); }

// ---------------------------------------------------------------- Audio Recall

bool Sampling::recall() {
    unsigned kit = studio.track();
    if (studio.kind(kit) != TrackKind::Kit) { studio.say("RECALL: KITS"); return false; }
    const auto& ring = studio.host().recall();
    if (ring.filled() < ring.rate() / 100) { studio.say("NOTHING HEARD YET"); return false; }
    const auto& pads_of = studio.project().tracks[kit].pads;
    int target = -1;
    for (unsigned n = 1; n <= pad_count && target < 0; ++n) {
        unsigned at = (studio.pad() + n) % pad_count;
        if (!pads_of[at].used()) target = int(at);
    }
    if (target < 0) { studio.say("NO EMPTY PAD"); return false; }
    auto made = std::make_unique<Job>();
    made->kind = Job::Kind::Recall;
    made->label = "RECALL " + std::to_string(next_number(folder(), "RECALL"));
    made->path = (fs::path(folder()) / (made->label + ".wav")).string();
    made->track = kit;
    made->pad = target;
    made->options.grab = uint8_t(prefs.grab);
    made->options.gap = melodee::SamplingPrefs::gap_seconds[prefs.gap % melodee::SamplingPrefs::gap_count];
    made->options.trim_end = prefs.trim_end;
    made->options.silence = prefs.silence_level();
    recalls.push_back(std::move(made));
    recall_pad = target;
    recall_waiting = true;
    recall_peaks.clear();
    return true;
}

// ---------------------------------------------------------------- every lights()

void Sampling::tick(bool in_view) {
    auto& host = studio.host();
    // An audio track selected, armed or recording takes its own source, its
    // monitor heard while it is armed or recording (Melodee's record_tick);
    // else Input Config's: Auto hears the input only while Sampling is in view.
    int recording = audio_recording();
    int audio_t = recording >= 0 ? recording : audio_arm >= 0 ? audio_arm : studio.kind() == TrackKind::Audio ? int(studio.track()) : -1;
    InputSource want = input.source;
    unsigned want_track = input.track % melodee::track_count;
    bool want_post = input.post;
    bool monitoring = input.monitoring(in_view), mic_heard = input.mutes_speaker(in_view);
    if (audio_t >= 0) {
        const auto& setting = studio.project().tracks[unsigned(audio_t)].audio;
        want = setting.source;
        want_track = 0;
        want_post = true;
        monitoring = setting.monitor == melodee::TrackMonitor::On && (audio_arm == audio_t || recording == audio_t);
        mic_heard = monitoring && want == InputSource::Mic;
    }
    if (!monitor_known || monitoring != monitor_sent) {
        studio.send({Type::Monitor, 0, 0, 0, int16_t(monitoring)});
        monitor_sent = monitoring;
        monitor_known = true;
    }
    bool quiet = to_speaker && mic_heard; // the mic through the speaker would howl
    if (quiet != quiet_sent) { studio.quiet_speaker(quiet); quiet_sent = quiet; }
    // While a track records its take, its old one is not heard.
    if (recording != audio_quiet_sent) {
        if (audio_quiet_sent >= 0) studio.send({Type::AudioQuiet, uint8_t(audio_quiet_sent), 0, 0, 0});
        if (recording >= 0) studio.send({Type::AudioQuiet, uint8_t(recording), 0, 0, 1});
        audio_quiet_sent = recording;
    }
    // The engine hears the source as set (after a take, a source changed meanwhile).
    if (host.recorder().state() == State::Idle && !job
        && (host.record_source() != unsigned(want)
            || (want == InputSource::Track && (host.record_track() != want_track || host.record_post() != want_post))))
        studio.send({Type::RecordSource, uint8_t(want), 0, uint16_t(want_track), int16_t(want_post)});
    // The pads play Melodee's own sound while a take of it waits or runs.
    int redirect = pads_play_source() ? int(input.source == InputSource::Track ? input.track : track) : -1;
    if (redirect != redirect_sent) { studio.pads_play(redirect); redirect_sent = redirect; }
    if (armed_here && host.recorder().state() != State::Idle) armed_here = false; // the audio thread has it
    if (armed_here && Clock::now() - armed_at > arm_settles) armed_here = false;
}

// ---------------------------------------------------------------- settings

bool Sampling::setting(const std::string& word, unsigned value) {
    if (word == "in_source") input.source = InputSource(std::min<unsigned>(value, unsigned(InputSource::Count) - 1));
    else if (word == "in_track") input.track = uint8_t(value % melodee::track_count);
    else if (word == "in_post") input.post = value != 0;
    else if (word == "in_monitor") input.monitor = Monitor(std::min<unsigned>(value, unsigned(Monitor::Count) - 1));
    else if (word == "in_threshold") input.threshold = int8_t(int(std::min(value, 96u)) - 96); // kept + 96
    else if (word == "in_length") input.length = uint8_t(value);
    else if (word == "in_gain") input.gain = int8_t(int(std::min(value, 36u)) - 12); // kept + 12
    else if (word == "in_line") line_choice = melodee::from_rear(InputSource(value)) ? InputSource(value) : InputSource::Rear;
    else if (word == "recall_grab") prefs.grab = melodee::RecallGrab(value);
    else if (word == "recall_gap") prefs.gap = uint8_t(value);
    else if (word == "recall_trim") prefs.trim_end = value != 0;
    else if (word == "take_auto_end") prefs.auto_end = melodee::AutoEnd(value);
    else if (word == "take_end_after") prefs.end_after = uint8_t(value);
    else if (word == "take_silence") prefs.silence = uint8_t(value);
    else if (word == "browser_preview") preview = value != 0;
    else return false;
    if (input.source == InputSource::Usb || input.source == InputSource::UsbL || input.source == InputSource::UsbR) input.source = InputSource::Rear;
    input.clamp();
    prefs.clamp();
    return true;
}

std::string Sampling::settings_text() const {
    std::ostringstream out;
    out << "in_source " << unsigned(input.source) << '\n' << "in_track " << unsigned(input.track) << '\n'
        << "in_post " << int(input.post) << '\n' << "in_monitor " << unsigned(input.monitor) << '\n'
        << "in_threshold " << int(input.threshold) + 96 << '\n' << "in_length " << unsigned(input.length) << '\n'
        << "in_gain " << int(input.gain) + 12 << '\n' << "in_line " << unsigned(line_choice) << '\n'
        << "recall_grab " << unsigned(prefs.grab) << '\n' << "recall_gap " << unsigned(prefs.gap) << '\n'
        << "recall_trim " << int(prefs.trim_end) << '\n' << "take_auto_end " << unsigned(prefs.auto_end) << '\n'
        << "take_end_after " << unsigned(prefs.end_after) << '\n' << "take_silence " << unsigned(prefs.silence) << '\n'
        << "browser_preview " << int(preview) << '\n';
    return out.str();
}

std::string Sampling::status() const {
    static const char* phases[] {"idle", "armed", "cued", "recording", "saving"};
    char text[200];
    std::snprintf(text, sizeof text, " rec=%s source=%s in=%s out=%s take_pads=%u last_take=%s recall=%d monitor=%d",
                  phases[unsigned(phase())], source_tag().c_str(), !input_known ? "?" : line ? "line" : "mic", to_speaker ? "speaker" : "line",
                  unsigned(pads.size()), last_name.empty() ? "-" : last_name.c_str(), recall_pad, int(monitor_sent));
    return text;
}

// ---------------------------------------------------------------- service thread

void Sampling::service() {
    if (!folders_made) { // Melodee's samples folders (MELODEE.md, Files on the card)
        folders_made = true;
        std::error_code error;
        for (const char* kept : {"Recorded", "Edits", "Banks", "Looper", "Expansions", "Keygroups", "Stems"}) {
            auto folder = fs::path(studio.home_folder()) / "Samples" / kept;
            fs::create_directories(folder, error);
            hand_back(folder.string(), studio.home_folder());
        }
    }
    // A take: its file opened as it starts, drained as it comes, made whole as it ends.
    Job* taking_job = nullptr;
    State state;
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        taking_job = job.get();
        state = studio.host().recorder().state();
        if (taking_job && !taking_job->open && state == State::Idle && Clock::now() - taking_job->armed_at > arm_settles) {
            job.reset(); // armed and let go: nothing was taken
            taking_job = nullptr;
        }
        if (taking_job && (state == State::Done || (taking_job->open && state == State::Idle))) saving = true;
    }
    if (taking_job) {
        auto& recorder = studio.host().recorder();
        auto& j = *taking_job;
        if (!j.open && (state == State::Recording || state == State::Done)) {
            std::error_code error;
            fs::create_directories(fs::path(j.path).parent_path(), error);
            j.open = j.file.open(j.path + ".part", recorder.take_rate(), 32);
            if (!j.open) {
                std::lock_guard<std::mutex> guard(studio.lock);
                studio.say("CANNOT WRITE TAKE");
                j.open = true; // drained into memory alone, 60 s at most
                j.too_long = false;
            }
            recorder.spill(true); // the buffer a ring from now: the take may go on past 60 s
        }
        if (j.open) {
            const uint32_t taken = recorder.taken(), capacity = melodee::TakeRecorder::capacity;
            const uint32_t keep_most = 60 * recorder.take_rate();
            std::vector<float> chunk;
            while (j.drained < taken) {
                uint32_t n = std::min<uint32_t>(taken - j.drained, 4096);
                chunk.resize(size_t(n) * 2);
                for (uint32_t f = 0; f < n; ++f) {
                    uint32_t at = (j.drained + f) % capacity;
                    chunk[2 * f] = recorder.left()[at];
                    chunk[2 * f + 1] = recorder.right()[at];
                }
                j.file.add(chunk.data(), n);
                if (!j.too_long) {
                    if (j.left.size() + n > keep_most) { j.too_long = true; j.left = {}; j.right = {}; }
                    else for (uint32_t f = 0; f < n; ++f) { j.left.push_back(chunk[2 * f]); j.right.push_back(chunk[2 * f + 1]); }
                }
                j.drained += n;
                recorder.drained(j.drained);
            }
            if (state == State::Done || state == State::Idle) land_take(j);
        }
    }
    // Audio Recall: the ring copied out, cut, written, onto its pad.
    std::vector<std::unique_ptr<Job>> todo;
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        todo.swap(recalls);
    }
    for (auto& recall : todo) land_recall(*recall);
    service_files();
    service_audio();
}

// The take ended: its file made whole (24-bit, or 32-bit float where it went
// past full scale, as Melodee keeps a capture), read as a sample, and onto its
// pads, each its part.
void Sampling::land_take(Job& j) {
    auto& recorder = studio.host().recorder();
    j.file.close();
    const unsigned rate = recorder.take_rate();
    std::string error;
    std::unique_ptr<Sample> sample;
    std::error_code fs_error;
    if (j.drained == 0) {
        fs::remove(j.path + ".part", fs_error);
    } else if (!j.too_long) {
        auto data = std::make_unique<melodee::SampleData>();
        data->left = std::move(j.left);
        data->right = std::move(j.right);
        data->frames = uint32_t(data->left.size());
        data->rate = rate;
        error = melodee::keep_capture(*data, rate, j.path); // as written: what plays is what the file holds, to the bit
        if (error.empty()) fs::remove(j.path + ".part", fs_error);
        else fs::rename(j.path + ".part", j.path, fs_error);
        sample = make_sample(j.path, std::move(data));
    } else {
        fs::rename(j.path + ".part", j.path, fs_error); // longer than a pad holds: the file keeps it all, the pad its first 60 s
        sample = read_sample(j.path, error);
    }
    if (j.drained) hand_back(j.path, studio.home_folder());
    recorder.spill(false);
    recorder.release();
    std::lock_guard<std::mutex> guard(studio.lock);
    saving = false;
    auto parts = pads;
    auto starts = cuts;
    unsigned kit = j.track;
    job.reset();
    pads.clear();
    cuts.clear();
    if (!sample) { if (!error.empty()) studio.say("TAKE NOT KEPT"); return; }
    uint32_t frames = sample->data->model_frames();
    auto overview = sample->overview;
    std::string name = fs::path(sample->path).stem().string();
    std::string failed;
    int slot = studio.add_sample(std::move(sample), failed);
    if (slot < 0) { studio.say(failed); return; }
    // Each pad its part, from where it was pressed to where the next one was, on a zero crossing.
    const auto* data = studio.sample_of_slot(unsigned(slot));
    for (size_t n = 0; n < parts.size(); ++n) {
        melodee::PadSound sound;
        sound.sample = uint16_t(slot);
        uint32_t start = std::min(starts[n], frames);
        if (n && data) start = melodee::zero_crossing(*data, start);
        uint32_t end = n + 1 < parts.size() ? std::min(starts[n + 1], frames) : 0;
        if (end && data) end = melodee::zero_crossing(*data, end);
        if (end && end <= start + melodee::min_slice) continue; // a part too short to keep
        sound.start = start;
        sound.end = end;
        studio.set_pad(kit, parts[n], sound);
        starts[n] = start;
    }
    if (kit == studio.track() && !parts.empty()) studio.select_pad(parts.back());
    last_name = name;
    last_frames = frames;
    last_pads = parts;
    last_cuts = starts;
    last_overview = overview;
    studio.say(name + " > " + pad_name(parts.front()));
}

void Sampling::land_recall(Job& j) {
    const auto& ring = studio.host().recall();
    const unsigned rate = ring.rate();
    uint32_t frames = ring.filled();
    auto data = std::make_unique<melodee::SampleData>();
    data->left.resize(frames);
    data->right.resize(frames);
    bool copied = frames && ring.copy_last(frames, data->left.data(), data->right.data());
    uint32_t first = 0, end = frames;
    bool sound = copied && melodee::recall_bounds(data->left.data(), data->right.data(), frames, rate, j.options, first, end);
    std::unique_ptr<Sample> sample;
    std::string error;
    if (sound) {
        data->left.erase(data->left.begin() + end, data->left.end());
        data->right.erase(data->right.begin() + end, data->right.end());
        data->left.erase(data->left.begin(), data->left.begin() + first);
        data->right.erase(data->right.begin(), data->right.begin() + first);
        data->frames = end - first;
        data->rate = rate;
        std::error_code fs_error;
        fs::create_directories(fs::path(j.path).parent_path(), fs_error);
        error = melodee::keep_capture(*data, rate, j.path);
        if (error.empty()) hand_back(j.path, studio.home_folder());
        sample = make_sample(j.path, std::move(data));
    }
    std::lock_guard<std::mutex> guard(studio.lock);
    recall_waiting = false;
    if (!copied) { studio.say("RECALL: TRY AGAIN"); recall_pad = -1; return; }
    if (!sound) { studio.say("ONLY SILENCE"); recall_pad = -1; return; }
    if (!error.empty()) studio.say("NOT WRITTEN");
    recall_peaks.clear();
    for (auto peak : sample->overview) recall_peaks.push_back(peak / 255.0f);
    std::string failed;
    std::string name = fs::path(sample->path).stem().string();
    int slot = studio.add_sample(std::move(sample), failed);
    if (slot < 0) { studio.say(failed); recall_pad = -1; return; }
    melodee::PadSound pad;
    pad.sample = uint16_t(slot);
    studio.set_pad(j.track, unsigned(j.pad), pad);
    if (j.track == studio.track()) studio.select_pad(unsigned(j.pad));
    studio.say(name + " > " + pad_name(unsigned(j.pad)));
}

} // namespace app
