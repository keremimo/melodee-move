// SPDX-License-Identifier: AGPL-3.0-only
#include "app/auto_sampler.h"
#include "melodee/engine/auto_probe.h"
#include "melodee/engine/loop_find.h"
#include "melodee/engine/release_fit.h"
#include "melodee/engine/sample_data.h"
#include "melodee/model/scales.h"
#include "melodee/shell/xpm_write.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <pthread.h>
#include <sched.h>
#include <sstream>
#include "melodee/engine/sfz.h"

namespace app {
namespace fs = std::filesystem;
namespace {

constexpr float rate = 44100.0f;
constexpr unsigned block = 128; // as Schwung's modules render
float gain_of(float db) { return std::pow(10.0f, db / 20.0f); }

// A note as it was played: both sides, and where its key was let go.
struct Take {
    std::vector<float> left, right;
    uint64_t held = 0;
    bool cut = false; // the tail ran out before it fell silent
};

float block_peak(const float* left, const float* right, unsigned frames) {
    float peak = 0;
    for (unsigned n = 0; n < frames; ++n) peak = std::max({peak, std::fabs(left[n]), std::fabs(right[n])});
    return peak;
}
void render_into(NoteSource& source, Take& take, uint64_t frames) {
    float left[block], right[block];
    while (frames) {
        std::fill(left, left + block, 0.0f);
        std::fill(right, right + block, 0.0f);
        source.render(left, right, block);
        unsigned n = unsigned(std::min<uint64_t>(block, frames));
        take.left.insert(take.left.end(), left, left + n);
        take.right.insert(take.right.end(), right, right + n);
        frames -= n;
    }
}
// A key held `hold` seconds and let go, kept until it has been under -70 dB
// for 0.2 s, `tail` seconds at most.
Take play(NoteSource& source, int key, int velocity, float hold, float tail) {
    Take take;
    source.note(key, velocity);
    render_into(source, take, uint64_t(hold * rate));
    take.held = take.left.size();
    source.note(key, 0);
    const float silent = gain_of(-70.0f);
    uint64_t quiet = 0, most = uint64_t(std::max(tail, 0.0f) * rate);
    take.cut = true;
    while (take.left.size() - take.held < most) {
        size_t at = take.left.size();
        render_into(source, take, block);
        quiet = block_peak(take.left.data() + at, take.right.data() + at, unsigned(take.left.size() - at)) < silent ? quiet + block : 0;
        if (quiet >= uint64_t(0.2f * rate)) {
            take.left.resize(take.left.size() - quiet + uint64_t(0.05f * rate)); // a little of the silence kept
            take.right.resize(take.left.size());
            take.cut = false;
            break;
        }
    }
    return take;
}
// Quiet between notes: every note off, then until silent (2 s at most).
void settle(NoteSource& source) {
    source.quiet();
    float left[block], right[block];
    const float silent = gain_of(-80.0f);
    unsigned quiet = 0;
    for (unsigned n = 0; n < unsigned(2 * rate) / block && quiet < unsigned(0.1f * rate); ++n) {
        std::fill(left, left + block, 0.0f);
        std::fill(right, right + block, 0.0f);
        source.render(left, right, block);
        quiet = block_peak(left, right, block) < silent ? quiet + block : 0;
    }
}
std::vector<float> mono(const Take& take, size_t from = 0, size_t to = SIZE_MAX) {
    to = std::min(to, take.left.size());
    std::vector<float> out;
    for (size_t n = from; n < to; ++n) out.push_back(0.5f * (take.left[n] + take.right[n]));
    return out;
}
float peak_db(const Take& take) {
    float peak = block_peak(take.left.data(), take.right.data(), unsigned(take.left.size()));
    return peak > 0 ? 20.0f * std::log10(peak) : -120.0f;
}

// A WAV: 32-bit float (a part, kept until all are made) or 24-bit PCM (the
// program's, at the device's rate), one side where both are the same.
bool write_wav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right, bool floating, float gain = 1) {
    bool one = left == right;
    const uint16_t channels = one ? 1 : 2, bytes = floating ? 4 : 3;
    const uint32_t frames = uint32_t(left.size()), data = frames * channels * bytes;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    auto u32 = [&](uint32_t v) { char b[4] {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; file.write(b, 4); };
    auto u16 = [&](uint16_t v) { char b[2] {char(v), char(v >> 8)}; file.write(b, 2); };
    file.write("RIFF", 4); u32(36 + data); file.write("WAVE", 4);
    file.write("fmt ", 4); u32(16); u16(floating ? 3 : 1); u16(channels); u32(uint32_t(rate)); u32(uint32_t(rate) * channels * bytes);
    u16(uint16_t(channels * bytes)); u16(uint16_t(bytes * 8));
    file.write("data", 4); u32(data);
    std::vector<char> out;
    out.reserve(size_t(data));
    for (uint32_t n = 0; n < frames; ++n)
        for (unsigned side = 0; side < channels; ++side) {
            float v = (side ? right[n] : left[n]) * gain;
            if (floating) { char b[4]; std::memcpy(b, &v, 4); out.insert(out.end(), b, b + 4); continue; }
            auto s = int32_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 8388607.0f));
            out.push_back(char(s)); out.push_back(char(s >> 8)); out.push_back(char(s >> 16));
        }
    file.write(out.data(), std::streamsize(out.size()));
    return bool(file);
}

// A module's instance of its own: its notes and its render on this thread.
class ModuleSource final : public NoteSource {
public:
    explicit ModuleSource(std::unique_ptr<LoadedModule> module) : module(std::move(module)) {}
    void note(int key, int velocity) override {
        const uint8_t msg[3] {uint8_t(velocity ? 0x90 : 0x80), uint8_t(key & 0x7f), uint8_t(velocity & 0x7f)};
        if (module->api->on_midi) module->api->on_midi(module->instance, msg, 3, schwung::midi_source_internal);
    }
    void quiet() override {
        for (uint8_t controller : {uint8_t(64), uint8_t(123)}) {
            const uint8_t msg[3] {0xb0, controller, 0};
            if (module->api->on_midi) module->api->on_midi(module->instance, msg, 3, schwung::midi_source_internal);
        }
    }
    void render(float* left, float* right, unsigned frames) override {
        int16_t out[2 * block] {};
        module->api->render_block(module->instance, out, int(std::min(frames, block)));
        for (unsigned n = 0; n < std::min(frames, block); ++n) { left[n] = out[2 * n] / 32768.0f; right[n] = out[2 * n + 1] / 32768.0f; }
    }

private:
    std::unique_ptr<LoadedModule> module;
};

std::string seconds_text(float s) { char text[16]; std::snprintf(text, sizeof text, "%.2fS", s); return text; }

} // namespace

std::unique_ptr<NoteSource> module_source(const ModuleInfo& info, const SynthFile& state, std::string& error) {
    auto module = LoadedModule::load(info, error);
    if (!module) return nullptr;
    if (module->source) { error = "Not an instrument"; return nullptr; }
    if (state.preset >= 0 && !module->preset_param.empty()) module->set_now(module->preset_param, std::to_string(state.preset));
    for (const auto& [key, value] : state.values) module->set_now(key, value);
    return std::make_unique<ModuleSource>(std::move(module));
}

bool parse_autosample(const std::string& text, AutoCommand& out, std::string& error) {
    std::istringstream words(text);
    std::string word;
    auto number = [](const std::string& w, double& v) { char* end = nullptr; v = std::strtod(w.c_str(), &end); return end && *end == 0 && !w.empty(); };
    auto key = [](const std::string& w) -> int {
        char* end = nullptr;
        long n = std::strtol(w.c_str(), &end, 10);
        if (end && *end == 0 && !w.empty()) return n >= 0 && n <= 127 ? int(n) : -1;
        auto named = melodee::sfz_note(w); // c4 is 60, c#4, db4
        return named && *named >= 0 && *named <= 127 ? *named : -1;
    };
    if (!(words >> word)) { error = "which track?"; return false; }
    double value = 0;
    if (!number(word, value) || value < 1 || value > 16) { error = "a track is 1-16"; return false; }
    out.track = unsigned(value) - 1;
    auto& s = out.settings;
    while (words >> word) {
        std::string next;
        auto take = [&](double low, double high, double& v) {
            if (!(words >> next) || !number(next, v) || v < low || v > high) { error = word + " wants a number from " + std::to_string(int(low)) + " to " + std::to_string(int(high)); return false; }
            return true;
        };
        if (auto dash = word.find('-'); dash != std::string::npos && dash > 0) {
            int low = key(word.substr(0, dash)), high = key(word.substr(dash + 1));
            if (low < 0 || high < low) { error = "keys: LOW-HIGH, " + word; return false; }
            s.low = uint8_t(low); s.high = uint8_t(high);
        } else if (word == "step") { if (!take(1, 24, value)) return false; s.step = uint8_t(value); }
        else if (word == "layers") { if (!take(0, 8, value)) return false; s.layers = uint8_t(value); }
        else if (word == "hold") { if (!take(0, 20, value)) return false; s.hold = float(value); s.hold_auto = value == 0; }
        else if (word == "tail") { if (!take(0, 30, value)) return false; s.tail = float(value); }
        else if (word == "pre") s.post_fx = false;
        else if (word == "post") s.post_fx = true;
        else if (word == "loop") { s.loop = true; s.loop_auto = false; }
        else if (word == "raw") s.normalize = false;
        else if (word == "peak") s.normalize = true;
        else if (word == "check") out.check = true;
        else { error = "not known: " + word; return false; }
    }
    return true;
}

const char* phase_name(AutoSampler::Phase phase) {
    static const char* names[] {"IDLE", "OPENING", "CHECKING", "CHECKED", "SAMPLING", "WRITING", "DONE", "FAILED"};
    return names[unsigned(phase) % 8];
}

melodee::AutoFindings check_source(NoteSource& source, const melodee::AutoSampleSettings& settings, std::vector<std::string>* rows,
                                   const std::atomic<bool>* cancelled) {
    using namespace melodee;
    AutoFindings f;
    auto stop = [&] { return cancelled && cancelled->load(); };
    auto say = [&](const std::string& row) { if (rows) rows->push_back(row); };
    const auto began = std::chrono::steady_clock::now();
    double played = 0;
    // Pitched or drums: five keys a fourth apart (never an octave: a pitch read an octave off still follows), the
    // pitch each one sounds.
    const std::vector<uint8_t> keys {48, 53, 58, 63, 68};
    std::vector<float> hz;
    for (uint8_t key : keys) {
        if (stop()) return f;
        settle(source);
        auto take = play(source, key, 100, 0.5f, 0.3f);
        played += take.left.size() / rate;
        hz.push_back(probe_pitch(mono(take), size_t(0.05f * rate), size_t(0.3f * rate), rate));
    }
    const bool drums = probe_drums(keys, hz);
    if (drums) {
        f.kind = AutoKind::Drums;
        f.scan_low = settings.low;
        f.scan_high = settings.high;
        for (int key = settings.low; key <= settings.high && key <= 127; ++key) { // each key that sounds
            if (stop()) return f;
            settle(source);
            auto take = play(source, key, 100, 0.15f, 0.5f);
            played += take.left.size() / rate;
            if (peak_db(take) > -60.0f) f.keys[size_t(key) / 64] |= uint64_t(1) << (key % 64);
        }
        say("DRUMS, " + std::to_string(f.sounding()) + " SOUNDS");
    } else {
        say("KEYS");
    }
    // Held 8 s: whether it sustains, moves, or fades (and how long to hold each key).
    if (stop()) return f;
    settle(source);
    auto held = play(source, 60, 100, 8.0f, 0.1f);
    played += held.left.size() / rate;
    auto held_mono = mono(held, 0, held.held);
    f.sustains = !drums && probe_sustains(held_mono, size_t(0.5f * rate), held.held, rate, 9.0f);
    f.moves = !drums && sound_moves(held_mono, held.held, rate);
    float fade = probe_fade_seconds(held_mono, rate);
    f.hold = drums ? 0.3f : f.sustains ? (f.moves ? 8.0f : 2.0f) : std::clamp(fade, 1.0f, 8.0f);
    if (f.sustains) say(f.moves ? "LOOPS, IT MOVES" : "LOOPS");
    say("HOLD " + seconds_text(f.hold));
    // Sixteen velocities of one key: how its level follows, an accent, layers.
    std::vector<uint8_t> velocities;
    std::vector<float> db, bright;
    for (unsigned n = 0; n < 16; ++n) {
        if (stop()) return f;
        uint8_t v = uint8_t(std::clamp<unsigned>((n + 1) * 8 - 1, 1, 127));
        settle(source);
        auto take = play(source, 60, v, 0.4f, 0.0f);
        played += take.left.size() / rate;
        auto m = mono(take);
        velocities.push_back(v);
        db.push_back(probe_db(m, 0, size_t(0.3f * rate)));
        bright.push_back(probe_brightness(m, 0, size_t(0.3f * rate), rate));
    }
    f.velocity = probe_velocity(velocities, db);
    f.accent = probe_accent(velocities, db, bright);
    f.sweep_count = 16;
    for (unsigned n = 0; n < 16; ++n) { f.sweep_velocity[n] = velocities[n]; f.sweep_bright[n] = bright[n]; }
    f.layers = uint8_t(std::clamp<size_t>(probe_layers(velocities, db, bright, f.velocity, f.accent, 0).size(), 1, 8));
    for (unsigned count = 1; count <= 8; ++count) {
        auto tops = probe_layers(velocities, db, bright, f.velocity, f.accent, count);
        for (size_t n = 0; n < tops.size() && n < 8; ++n) f.layer_tops[count - 1][n] = tops[n];
    }
    if (f.accent) say("ACCENT AT " + std::to_string(f.accent));
    say(f.layers > 1 ? "TONE, " + std::to_string(f.layers) + " LAYERS" : f.velocity > 0 ? "LEVEL ONLY" : "ONE LEVEL");
    // Five hits: takes, where they differ.
    std::vector<std::vector<float>> hits;
    for (unsigned n = 0; n < 5; ++n) {
        if (stop()) return f;
        settle(source);
        auto take = play(source, 60, 100, 0.3f, 0.0f);
        played += take.left.size() / rate;
        hits.push_back(mono(take));
    }
    f.takes = probe_varies(hits, rate) ? 3 : 1;
    if (f.takes > 1) say("TAKES X3");
    // Mono and glide: two keys legato, against the first held alone.
    const float from_hz = hz[2], to_hz = hz[3];
    const int from_key = keys[2], to_key = keys[3];
    if (!drums && from_hz > 0 && to_hz > 0) {
        settle(source);
        auto alone = play(source, from_key, 100, 1.0f, 0.0f);
        settle(source);
        Take both;
        source.note(from_key, 100);
        render_into(source, both, uint64_t(0.5f * rate));
        const size_t onset = both.left.size();
        source.note(to_key, 100);
        render_into(source, both, uint64_t(0.5f * rate));
        source.note(to_key, 0);
        source.note(from_key, 0);
        render_into(source, both, uint64_t(0.1f * rate));
        played += (alone.left.size() + both.left.size()) / rate;
        auto glide = probe_glide(mono(both), mono(alone), onset, from_hz, to_hz, rate);
        if (glide.mono) {
            f.kind = AutoKind::Mono;
            f.glide = glide.seconds;
            settle(source); // apart: a slide there too is a portamento that is not legato
            Take apart;
            source.note(from_key, 100);
            render_into(source, apart, uint64_t(0.4f * rate));
            source.note(from_key, 0);
            render_into(source, apart, uint64_t(0.05f * rate));
            const size_t second = apart.left.size();
            source.note(to_key, 100);
            render_into(source, apart, uint64_t(0.5f * rate));
            source.note(to_key, 0);
            f.legato = !probe_slides_apart(mono(apart), second, from_hz, to_hz, rate);
            settle(source);
            auto fresh = play(source, to_key, 100, 0.5f, 0.0f);
            f.tied = probe_retriggers(mono(both), mono(alone), mono(fresh), onset, rate) != 1;
            say(std::string("MONO, ") + (f.tied ? "LEGATO" : "RETRIGGER"));
            if (f.glide > 0) say("GLIDE " + seconds_text(f.glide));
        }
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    f.speed = float(wall > 0 ? played / wall : 1);
    f.heard = true;
    return f;
}

AutoSampler::~AutoSampler() {
    cancelled.store(true);
    if (thread.joinable()) thread.join();
}

bool AutoSampler::start(const Job& job, const melodee::AutoSampleSettings& settings, Opener open) {
    if (running()) return false;
    if (thread.joinable()) thread.join();
    cancelled.store(false);
    notes_done.store(0);
    notes_total.store(0);
    {
        std::lock_guard<std::mutex> guard(lock);
        why.clear();
        made.clear();
        if (!(job.sample && settings.found.heard)) rows.clear();
        job_now = job;
    }
    phase.store(Phase::Opening);
    thread = std::thread([this, job, settings, open] { run(job, settings, open); });
    return true;
}

void AutoSampler::cancel() {
    cancelled.store(true);
    if (thread.joinable()) thread.join();
    if (phase.load() != Phase::Done) phase.store(Phase::Idle);
}

void AutoSampler::reset() {
    if (running()) return;
    if (thread.joinable()) thread.join();
    phase.store(Phase::Idle);
}

melodee::AutoFindings AutoSampler::findings() const { std::lock_guard<std::mutex> guard(lock); return found; }
std::string AutoSampler::error() const { std::lock_guard<std::mutex> guard(lock); return why; }
std::string AutoSampler::program() const { std::lock_guard<std::mutex> guard(lock); return made; }
std::vector<std::string> AutoSampler::heard() const { std::lock_guard<std::mutex> guard(lock); return rows; }

void AutoSampler::fail(const std::string& reason) {
    {
        std::lock_guard<std::mutex> guard(lock);
        why = reason;
    }
    phase.store(Phase::Failed);
}

void AutoSampler::run(Job job, melodee::AutoSampleSettings settings, Opener open) {
    using namespace melodee;
#if defined(SCHED_IDLE)
    sched_param idle {};
    pthread_setschedparam(pthread_self(), SCHED_IDLE, &idle); // a spare core's, never the audio thread's (best effort)
#endif
    std::string error;
    auto source = open(error);
    if (!source) { fail(error.empty() ? "Cannot open it" : error); return; }
    if (!(job.sample && settings.found.heard)) { // a check the sheet already has is not run again
        phase.store(Phase::Checking);
        std::vector<std::string> heard_rows;
        settings.found = check_source(*source, settings, &heard_rows, &cancelled);
        std::lock_guard<std::mutex> guard(lock);
        found = settings.found;
        rows = heard_rows;
    }
    if (cancelled.load()) { phase.store(Phase::Idle); return; }
    if (!job.sample) { phase.store(Phase::Checked); return; }

    // Sampling: each root, each layer, each take, kept as a float part until all are made.
    phase.store(Phase::Sampling);
    const auto roots = settings.roots();
    const unsigned layers = settings.layer_count(), takes = settings.take_count();
    if (roots.empty()) { fail("No keys sound"); return; }
    notes_total.store(unsigned(roots.size()) * layers * takes);
    std::string name = job.name.empty() ? "Auto" : job.name;
    for (auto& c : name) if (c == '/' || c == '\\' || c == ':') c = '-';
    fs::path dir = fs::path(job.folder) / name;
    std::error_code fs_error;
    for (unsigned n = 2; fs::exists(dir, fs_error); ++n) dir = fs::path(job.folder) / (name + " " + std::to_string(n));
    const std::string stem_name = dir.filename().string();
    fs::create_directories(dir / ".parts", fs_error);
    if (fs_error) { fail("Storage full"); return; }
    struct Made { uint8_t root, low, high; unsigned take; std::string stem; uint32_t frames = 0, loop_start = 0, loop_end = 0; };
    std::vector<Made> notes;
    std::vector<ReleaseShape> shapes;
    float loudest = 0;
    const bool drums = settings.found.kind == AutoKind::Drums;
    for (uint8_t root : roots)
        for (unsigned layer = 0; layer < layers; ++layer)
            for (unsigned take = 0; take < takes; ++take) {
                if (cancelled.load()) { fs::remove_all(dir, fs_error); phase.store(Phase::Idle); return; }
                const uint8_t velocity = settings.velocity(layer);
                settle(*source);
                auto note = play(*source, root, velocity, drums ? 0.2f : settings.hold_seconds(root), settings.tail);
                if (!drums) shapes.push_back(release_shape(note.left, note.right, note.held, rate, note.cut));
                Made m;
                m.root = root;
                m.low = layer ? uint8_t(settings.velocity(layer - 1) + 1) : 0;
                m.high = takes > 1 ? 127 : velocity;
                m.take = take;
                m.stem = stem_name + "-" + note_name(root) + "-V" + std::to_string(velocity) + (takes > 1 ? "-T" + std::to_string(take + 1) : "");
                if (!drums && settings.looping(root)) {
                    auto loop = find_loop(note.left, note.right, note.held, rate);
                    if (loop.found()) {
                        m.loop_start = loop.start;
                        m.loop_end = loop.end;
                        note.left.resize(loop.end); // a looped sample's file ends at its loop
                        note.right.resize(loop.end);
                    }
                }
                m.frames = uint32_t(note.left.size());
                loudest = std::max(loudest, block_peak(note.left.data(), note.right.data(), unsigned(note.left.size())));
                if (!write_wav((dir / ".parts" / (m.stem + ".wav")).string(), note.left, note.right, true)) { fs::remove_all(dir, fs_error); fail("Storage full"); return; }
                notes.push_back(m);
                notes_done.fetch_add(1);
            }

    // The program: its WAVs at one gain, its keygroups split halfway between roots, its release fitted.
    phase.store(Phase::Writing);
    const auto release = shapes.empty() ? ReleaseModel {} : fit_release(shapes);
    float gain = 1;
    if (settings.normalize && loudest > 0) gain = std::min(gain_of(-1.0f) / loudest, gain_of(24.0f));
    for (const auto& m : notes) {
        SampleData part;
        if (!read_wav((dir / ".parts" / (m.stem + ".wav")).string(), part, 600).empty()) { fs::remove_all(dir, fs_error); fail("Storage full"); return; }
        if (!write_wav((dir / (m.stem + ".wav")).string(), part.left, part.right, false, gain)) { fs::remove_all(dir, fs_error); fail("Storage full"); return; }
    }
    fs::remove_all(dir / ".parts", fs_error);
    XpmProgram program;
    program.name = stem_name;
    program.volume_db = 3; // a centred sample plays 3 dB down a side: taken back, as loud as the module
    program.release = release.fast;
    program.mono = settings.found.kind == AutoKind::Mono;
    program.legato = settings.found.legato;
    program.tied = settings.found.tied;
    program.glide = settings.found.glide;
    for (size_t r = 0; r < roots.size(); ++r) {
        XpmKeygroup keygroup;
        keygroup.low = drums ? roots[r] : r == 0 ? 0 : uint8_t((roots[r - 1] + roots[r]) / 2 + 1);
        keygroup.high = drums ? roots[r] : r + 1 == roots.size() ? 127 : uint8_t((roots[r] + roots[r + 1]) / 2);
        keygroup.one_shot = drums;
        keygroup.cycle = takes > 1;
        keygroup.velocity = settings.found.heard ? settings.found.velocity : -1;
        for (const auto& m : notes) {
            if (m.root != roots[r]) continue;
            XpmLayer layer;
            layer.sample = m.stem;
            layer.root = m.root;
            layer.low_velocity = m.low;
            layer.high_velocity = m.high;
            layer.frames = m.frames;
            layer.loop_start = m.loop_start;
            layer.loop_end = m.loop_end;
            // Recorded at its loudest velocity: raised by what the keygroup's Velocity takes off there.
            const float amount = std::clamp(settings.found.velocity, 0.0f, 1.0f);
            layer.gain = 1.0 / std::max(0.05, 1.0 - amount * (1.0 - m.high / 127.0));
            keygroup.layers.push_back(layer);
        }
        program.keygroups.push_back(keygroup);
    }
    if (release.two() && program.keygroups.size() * 2 <= 128) { // a drop, then echoes: each keygroup twice
        std::vector<XpmKeygroup> both;
        for (const auto& k : program.keygroups) {
            XpmKeygroup fast = k, slow = k;
            fast.release = release.fast; fast.gain = 1 - release.share;
            slow.release = release.slow; slow.gain = release.share;
            both.push_back(fast);
            both.push_back(slow);
        }
        program.keygroups = both;
    }
    const auto path = (dir / (stem_name + ".xpm")).string();
    if (auto why_not = write_xpm(path, program); !why_not.empty()) { fs::remove_all(dir, fs_error); fail(why_not); return; }
    {
        std::lock_guard<std::mutex> guard(lock);
        made = path;
    }
    phase.store(Phase::Done);
}

} // namespace app
