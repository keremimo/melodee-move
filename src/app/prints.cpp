// SPDX-License-Identifier: AGPL-3.0-only
#include "app/prints.h"
#include "app/module_host.h"
#include "app/project_store.h"
#include "app/studio.h"
#include "app/wav_out.h"
#include "melodee/engine/dsp.h"
#include "melodee/engine/effects.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/print_render.h"
#include "melodee/model/pattern.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <pthread.h>
#include <sched.h>
#include <sstream>
#include <unistd.h>

namespace app {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Type = melodee::Command::Type;
using melodee::PrintTail;
using melodee::PrintTap;

namespace {

constexpr unsigned print_data_at = 4096; // a print's frames on a page, as Melodee writes them (its mapping wants it)
constexpr auto deep_wait = std::chrono::seconds(8);   // a deep freeze lets its module go once the engine sleeps, or after this
constexpr auto thaw_settle = std::chrono::milliseconds(1500); // a thawed module open this long before the track plays live
constexpr double module_wait_seconds = 30;

double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// Delay memory a print host needs: its chains' delays and reverbs (as Melodee's print engine counts them).
unsigned delay_blocks_for(const melodee::Track& t) {
    unsigned n = 2;
    auto count = [&](const melodee::Chain& c) { for (const auto& s : c.slots) n += s.used() && melodee::effect_needs_memory(melodee::EffectKind(s.kind)); };
    count(t.fx);
    for (const auto& c : t.pad_fx.at) count(c);
    if (t.engine == melodee::EngineKind::Plugin) n += 2;
    return std::min(n, melodee::DelayPool::blocks);
}

// The print thread: the time nothing else wants, on the last core (the audio thread and Melodee's own keep the rest).
void lowest_priority() {
    sched_param none {};
    pthread_setschedparam(pthread_self(), SCHED_IDLE, &none);
#ifdef __linux__
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores > 1) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(int(cores - 1), &set);
        pthread_setaffinity_np(pthread_self(), sizeof set, &set);
    }
#endif
}

// A module set as `saved` says, before it plays: a bank first, then the preset, then each parameter.
void apply_saved(LoadedModule& loaded, const SynthFile& saved) {
    for (const auto& [key, value] : saved.values)
        if (ModuleParam* param = loaded.param(key); param && param->selector) loaded.set_now(key, value);
    if (saved.preset >= 0 && !loaded.preset_param.empty()) loaded.set_now(loaded.preset_param, std::to_string(saved.preset));
    for (const auto& [key, value] : saved.values)
        if (ModuleParam* param = loaded.param(key); param && key != loaded.preset_param && !param->selector) loaded.set_now(key, value);
}

uint64_t pattern_bit(unsigned p) { return uint64_t(1) << (p % 64); }

} // namespace

bool changes_print(bindings::Job job) {
    using bindings::Job;
    switch (job) {
    case Job::TurnKnob: case Job::TurnKnobFine: case Job::OpenBrowser: case Job::SampleOptions: case Job::ModuleMenu:
    case Job::ToggleStep: case Job::NoteOnStep: case Job::NoteOnBar: case Job::ClearPad: case Job::CopyPad: case Job::StepVelocity:
    case Job::StepLength: case Job::StepLengthTo: case Job::TransposeStep: case Job::TransposeStepOctave: case Job::NudgeStep:
    case Job::NudgeStepFine: case Job::NudgeStepWhole: case Job::StepMotion: case Job::Record: case Job::Quantize: case Job::DoubleLoop:
    case Job::ClearBar: case Job::CopyStep: case Job::CopyBar: case Job::BarLength: case Job::BarVelocity: case Job::TransposeBar:
    case Job::NudgeBar: case Job::LoopLength: case Job::LoopLengthFine:
        return true;
    default:
        return false;
    }
}

// The job as the print thread takes it: copies of what the prints are made of, as it was when it started.
struct Printer::Work {
    Job job;
    uint32_t serial = 0;
    std::string project_path;
    std::unique_ptr<melodee::Project> project;
    std::unique_ptr<melodee::PatternBank> bank;
    PluginStates states;
    melodee::PrintTable known;
    std::array<const melodee::SampleData*, melodee::sample_slots> samples {}; // the studio's, kept for the run
    bool synth = false;
    ModuleInfo module;  // a synth track's
    SynthFile values;   // and how it is set
};

Printer::Printer(Studio& owner) : studio(owner) { thread = std::thread([this] { run(); }); }
Printer::~Printer() { stop(); }

void Printer::stop() {
    {
        std::lock_guard<std::mutex> guard(lock);
        if (stopping) return;
        stopping = true;
    }
    cancelled.store(true);
    wake.notify_all();
    if (thread.joinable()) thread.join();
    std::lock_guard<std::mutex> guard(lock);
    for (auto& r : ready) delete r.pack;
    ready.clear();
}

bool Printer::printable(unsigned t) const {
    auto kind = studio.kind(t % melodee::track_count);
    if (kind == TrackKind::Kit) return true;
    if (kind != TrackKind::Synth) return false;
    auto* module = const_cast<Studio&>(studio).module_track(t);
    return module && ((module->module && !module->module->source) || deep(t)); // a stream takes no notes: nothing to print
}

std::array<uint64_t, 2> Printer::with_notes(unsigned t) const {
    std::array<uint64_t, 2> bits {};
    const auto& patterns = studio.patterns().at[t % melodee::track_count];
    for (unsigned p = 0; p < melodee::pattern_count; ++p)
        if (!patterns[p].empty()) bits[p / 64] |= pattern_bit(p);
    return bits;
}

std::string Printer::start(Job job) {
    unsigned t = job.track % melodee::track_count;
    if (busy()) return "PRINTING ALREADY";
    if (!printable(t)) return studio.kind(t) == TrackKind::Midi ? "MIDI TRACKS: LATER" : "NOTHING TO PRINT";
    if (studio.project_path().empty()) return "SAVE FIRST";
    auto with = with_notes(t);
    std::vector<uint8_t> patterns;
    for (uint8_t p : job.patterns)
        if (with[p / 64] & pattern_bit(p)) patterns.push_back(p);
    if (job.deep) { // the whole track, every pattern with notes, at Post FX
        patterns.clear();
        for (unsigned p = 0; p < melodee::pattern_count; ++p) if (with[p / 64] & pattern_bit(p)) patterns.push_back(uint8_t(p));
        job.tap = PrintTap::PostFx;
    }
    if (patterns.empty()) return "NO PATTERN WITH NOTES";
    job.patterns = patterns;
    job.track = t;
    auto work = std::make_unique<Work>();
    work->serial = ++serial;
    work->project_path = studio.project_path();
    work->project = std::make_unique<melodee::Project>(studio.project());
    work->bank = std::make_unique<melodee::PatternBank>(studio.patterns());
    work->states = studio.plugin_states();
    work->known = prints;
    for (unsigned slot = 0; slot < melodee::sample_slots; ++slot)
        if (const Sample* sample = studio.sample_in(slot); sample && sample->data) work->samples[slot] = sample->data.get();
    if (studio.kind(t) == TrackKind::Synth) {
        auto* module = studio.module_track(t);
        work->synth = true;
        work->values = studio.synth_file(t);
        auto found = std::find_if(studio.catalog.begin(), studio.catalog.end(), [&](const ModuleInfo& m) { return m.id == work->values.module; });
        if (found == studio.catalog.end() || !module) return "MODULE MISSING";
        work->module = *found;
    }
    work->job = std::move(job);
    {
        std::lock_guard<std::mutex> guard(lock);
        shown = Progress {};
        shown.busy = true;
        shown.track = t;
        shown.freeze = work->job.freeze;
        shown.count = uint16_t(work->job.patterns.size());
        waiting = std::move(work);
        job_waiting = true;
    }
    cancelled.store(false);
    wake.notify_all();
    return {};
}

void Printer::cancel() {
    cancelled.store(true);
    std::lock_guard<std::mutex> guard(lock);
    waiting.reset();
    job_waiting = false;
}

std::string Printer::unfreeze(unsigned t) {
    t %= melodee::track_count;
    auto& f = prints.freeze[t];
    if (!f.frozen()) return "NOT FROZEN";
    if (f.state == melodee::FreezeState::Deep) { // its module back first, as it was; the prints play meanwhile
        auto* module = studio.module_track(t);
        if (module && !module->module) {
            module->job = module->saved.module;
            module->job_restores = true;
            thaw_mask |= 1u << t;
            thaw_since[t] = Clock::now();
            deep_pending &= ~(1u << t);
            f.state = melodee::FreezeState::Frozen; // frozen while it thaws
            studio.changed();
            return {};
        }
    }
    f = melodee::TrackFreeze {};
    deep_pending &= ~(1u << t);
    studio.send({Type::Freeze, uint8_t(t)}); // none: live again at once
    studio.changed();
    return {};
}

Printer::Progress Printer::progress() const {
    std::lock_guard<std::mutex> guard(lock);
    return shown;
}

unsigned Printer::cpu_percent(unsigned t) const {
    return unsigned(std::lround(std::clamp(studio.host().track_load(t), 0.0f, 1.0f) * 100));
}

uint64_t Printer::room(unsigned t, const std::vector<uint8_t>& patterns, PrintTail tail) const {
    t %= melodee::track_count;
    const auto& track = studio.project().tracks[t];
    double tempo = std::max(30.0, studio.project().tempo / 10.0);
    uint64_t bytes = 0;
    unsigned channels = 2 + 2 * unsigned(__builtin_popcount(melodee::print_sends(track))); // the print's two, a pair a bus its pads send into
    for (uint8_t p : patterns) {
        uint32_t ticks = melodee::played_pattern(studio.patterns().at[t][p], track.speed).length;
        double seconds = double(ticks) / melodee::ticks_per_quarter * 60.0 / tempo;
        if (tail == PrintTail::Ring) seconds += 10;
        bytes += uint64_t(seconds * 44100.0) * channels * 4 + print_data_at;
    }
    return bytes;
}

// ---------------------------------------------------------------- the MIDI thread's side

void Printer::send_pack(Ready& r) {
    unsigned t = r.track % melodee::track_count;
    melodee::Command c {Type::Freeze, uint8_t(t)};
    c.freeze = r.pack;
    c.value = studio.playing() ? 1 : 0; // on a bar line while playing
    if (!studio.send(c)) { delete r.pack; r.pack = nullptr; studio.say("BUSY, TRY AGAIN"); return; }
    sent.push_back(r.pack);
    prints.freeze[t] = r.freeze;
    if (r.freeze.state == melodee::FreezeState::Deep) {
        deep_pending |= 1u << t;
        deep_since[t] = Clock::now();
    }
    ++froze;
    studio.changed();
    char text[24];
    std::snprintf(text, sizeof text, "TRACK %u FROZEN", t + 1);
    studio.say(text);
}

void Printer::tick() {
    // Results first: each print's record into the table.
    std::deque<Result> got;
    std::deque<Ready> packs;
    {
        std::lock_guard<std::mutex> guard(lock);
        got.swap(results);
        packs.swap(ready);
    }
    for (auto& r : got) {
        if (!r.error.empty()) continue;
        prints.put(r.record);
        if (r.made) ++printed; else ++skipped;
        studio.changed();
    }
    for (auto& r : packs) {
        if (r.serial != serial || !r.pack) { // called off, or another freeze came after it
            if (!r.error.empty()) studio.say(r.error.substr(0, 16));
            delete r.pack;
            continue;
        }
        send_pack(r);
    }
    // The packs the engine has finished with.
    melodee::FreezePack* done = nullptr;
    while (studio.host().take_done_freeze(done)) {
        sent.erase(std::remove(sent.begin(), sent.end(), done), sent.end());
        delete done;
    }
    // A deep freeze lets its module go once its print plays and the engine has slept (or after 8 s).
    auto now = Clock::now();
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        if (deep_pending >> t & 1) {
            bool asleep = studio.host().sleeping_tracks() >> t & 1;
            if (!asleep && now - deep_since[t] < deep_wait) continue;
            deep_pending &= ~(1u << t);
            if (auto* module = studio.module_track(t); module && module->module) {
                module->saved = studio.synth_file(t); // as it was, for the thaw
                if (module->player.set_module(nullptr)) module->leaving.push_back(std::move(module->module));
            }
        }
        if (thaw_mask >> t & 1) {
            auto* module = studio.module_track(t);
            bool open = module && module->module && !module->loading;
            bool failed = module && !module->module && !module->loading && module->job.empty();
            if (open && now - thaw_since[t] < thaw_settle) continue;
            if (!open && !failed && now - thaw_since[t] < std::chrono::duration<double>(module_wait_seconds)) continue;
            thaw_mask &= ~(1u << t);
            prints.freeze[t] = melodee::TrackFreeze {};
            studio.send({Type::Freeze, uint8_t(t)});
            studio.changed();
            char text[24];
            std::snprintf(text, sizeof text, open ? "TRACK %u LIVE" : "TRACK %u LIVE: NO MODULE", t + 1);
            studio.say(text);
        }
    }
}

void Printer::opened() {
    std::vector<Ready> made;
    std::string project_path;
    std::unique_ptr<Work> work;
    melodee::PrintTable table;
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        table = prints;
        project_path = studio.project_path();
        work = std::make_unique<Work>();
        work->project_path = project_path;
        work->project = std::make_unique<melodee::Project>(studio.project());
        work->bank = std::make_unique<melodee::PatternBank>(studio.patterns());
    }
    // Each frozen track's prints read (outside the lock), then sent.
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        const auto& f = table.freeze[t];
        if (!f.frozen()) continue;
        work->job.track = t;
        work->job.tap = f.tap;
        work->job.tail = f.tail;
        work->job.deep = f.state == melodee::FreezeState::Deep;
        work->job.patterns.clear();
        for (unsigned p = 0; p < melodee::pattern_count; ++p) if (f.plays(p)) work->job.patterns.push_back(uint8_t(p));
        made.push_back(freeze_pack(*work, table));
    }
    std::lock_guard<std::mutex> guard(studio.lock);
    for (auto& r : made) {
        if (!r.pack) {
            prints.freeze[r.track] = melodee::TrackFreeze {}; // its prints are gone: it plays live
            studio.say(("TRACK " + std::to_string(r.track + 1) + " LIVE: NO PRINTS").substr(0, 16));
            continue;
        }
        r.serial = serial;
        send_pack(r);
        if (prints.freeze[r.track].state == melodee::FreezeState::Deep) deep_since[r.track] = Clock::now() - deep_wait; // as opened: no module
    }
}

// ---------------------------------------------------------------- the print thread

void Printer::run() {
    lowest_priority();
    for (;;) {
        std::unique_ptr<Work> work;
        {
            std::unique_lock<std::mutex> guard(lock);
            wake.wait(guard, [&] { return stopping || job_waiting; });
            if (stopping) return;
            work = std::move(waiting);
            job_waiting = false;
        }
        if (!work) continue;
        running.store(true, std::memory_order_release);
        melodee::PrintTable known = work->known;
        bool failed = false;
        for (uint8_t p : work->job.patterns) {
            if (cancelled.load()) break;
            {
                std::lock_guard<std::mutex> guard(lock);
                shown.pattern = p;
                shown.fraction = 0;
            }
            Result r = print_one(*work, p);
            std::lock_guard<std::mutex> guard(lock);
            if (r.error.empty()) {
                known.put(r.record);
                shown.done[p / 64] |= pattern_bit(p);
            } else {
                shown.failed[p / 64] |= pattern_bit(p);
                shown.error = r.error;
                failed = true;
            }
            ++shown.through;
            char line[200];
            std::snprintf(line, sizeof line, "track=%u pattern=%s %s seconds=%.2f times_real=%.1f%s%s", r.record.track + 1u,
                          melodee::print_pattern_name(p).c_str(), !r.error.empty() ? "failed" : r.made ? "made" : "unchanged", r.seconds,
                          r.times_real, r.error.empty() ? "" : " error=", r.error.c_str());
            lines.push_back(line);
            if (lines.size() > 8) lines.pop_front();
            results.push_back(std::move(r));
            if (failed) break;
        }
        if (work->job.freeze && !failed && !cancelled.load()) {
            Ready r = freeze_pack(*work, known);
            r.serial = work->serial;
            std::lock_guard<std::mutex> guard(lock);
            if (!r.error.empty()) shown.error = r.error;
            ready.push_back(std::move(r));
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            shown.busy = false;
            shown.pattern = -1;
            if (cancelled.load()) shown.error = "called off";
        }
        running.store(false, std::memory_order_release);
    }
}

Printer::Result Printer::print_one(const Work& work, unsigned pattern) {
    Result r;
    const unsigned t = work.job.track % melodee::track_count;
    r.record.track = uint8_t(t);
    r.record.pattern = uint8_t(pattern % melodee::pattern_count);
    r.record.tap = work.job.tap;
    r.record.tail = work.job.tail;
    r.record.file = melodee::print_file_name(t, pattern);
    r.record.sends = melodee::print_sends(work.project->tracks[t]);
    auto t0 = Clock::now();
    auto project = std::make_unique<melodee::Project>(*work.project);
    auto bank = std::make_unique<melodee::PatternBank>(*work.bank);
    melodee::print_project(*project, *bank, t, pattern);
    const melodee::Track& track = project->tracks[t];
    if (!track.used() || track.engine == melodee::EngineKind::Midi) { r.error = "nothing to print"; return r; }
    const float rate = melodee::dsp::sample_rate();
    r.record.rate = unsigned(std::lround(rate));
    r.record.hash = melodee::print_hash(*project, *bank, work.states, work.job.tap, work.job.tail, r.record.rate);
    const std::string folder = melodee::prints_folder(work.project_path);
    const fs::path path = fs::path(folder) / r.record.file;
    std::error_code e;
    if (const auto* known = work.known.find(t, pattern); known && known->hash == r.record.hash && known->file == r.record.file &&
                                                        known->sends == r.record.sends && fs::exists(path, e)) {
        r.record = *known; // unchanged: never printed twice
        return r;
    }

    // The print host: Melodee's lean engine host, this one track in its place.
    auto host = std::make_unique<melodee::EngineHost>(melodee::EngineHost::Lean {delay_blocks_for(track)});
    host->load(*project, *bank);
    host->set_audio_format(rate, melodee::EngineHost::block);
    melodee::Command reload {Type::Reload};
    reload.pack = new melodee::ProjectPack(*project, *bank);
    host->send(reload);
    std::vector<int32_t> out(melodee::EngineHost::block * 2);
    auto idle = [&] { // a silent block: offers taken, packs handed back
        host->render(out.data(), melodee::EngineHost::block, 1.0f);
        const melodee::ProjectPack* done = nullptr;
        while (host->take_done_pack(done)) delete done;
    };
    for (unsigned n = 0; n < melodee::sample_slots; ++n) {
        if (!project->samples[n].used() || !work.samples[n]) continue;
        while (!host->offer_sample(n, work.samples[n])) idle();
    }
    idle();
    // A synth track: its own instance of the module, set as the live one.
    std::unique_ptr<LoadedModule> module;
    std::unique_ptr<SynthPlayer> player;
    std::unique_ptr<SchwungRunner> runner;
    if (work.synth) {
        std::string error;
        module = LoadedModule::load(work.module, error);
        if (!module) { r.error = "module would not load: " + error; return r; }
        apply_saved(*module, work.values);
        player = std::make_unique<SynthPlayer>();
        runner = std::make_unique<SchwungRunner>(*player);
        player->set_module(module.get());
        if (track.plugin == melodee::no_plugin) { r.error = "no plug-in entry"; return r; }
        host->offer_plugin(track.plugin, runner.get());
        idle();
        idle();
    }
    uint32_t ticks = std::max<uint32_t>(1, melodee::played_pattern(bank->at[t][pattern], track.speed).length);
    auto r0 = Clock::now();
    uint64_t blocks = 0, total = 0;
    auto audio = melodee::render_print(*host, t, ticks, work.job.tap, work.job.tail, [&] {
        if (blocks == 1) {
            uint64_t pass = host->tick_frame(ticks);
            total = pass * (work.job.tail == PrintTail::Loop ? 2 : 1) + (work.job.tail == PrintTail::Ring ? uint64_t(rate) : 0);
        }
        if (total && (blocks & 7) == 0) {
            double rendered = double(blocks) * melodee::EngineHost::block, wall = since(r0);
            double speed = wall > 0 ? rendered / rate / wall : 0;
            std::lock_guard<std::mutex> guard(lock);
            shown.fraction = float(std::min(1.0, rendered / double(total)));
            shown.times_real = float(speed);
            unsigned left = shown.count > shown.through + 1u ? shown.count - shown.through - 1u : 0;
            shown.seconds_left = speed > 0 ? float((std::max(0.0, double(total) - rendered) + double(left) * double(total)) / rate / speed) : 0;
        }
        ++blocks;
        return !cancelled.load();
    }, r.record.sends);
    double rendering = since(r0);
    host.reset(); // it renders no more: the module can go
    if (player) player->stop();
    runner.reset();
    player.reset();
    module.reset();
    if (!audio.error.empty()) { r.error = audio.error; r.seconds = since(t0); return r; }

    // Written beside, then put in place: the print's two channels, then a pair for each bus its pads send into.
    r.record.frames = uint32_t(audio.left.size());
    r.record.pass = melodee::model_frames_of(audio.pass, rate);
    fs::create_directories(folder, e);
    const fs::path part = fs::path(folder) / (r.record.file + ".part");
    const unsigned channels = r.record.channels();
    WavOut wav;
    bool written = wav.open(part.string(), r.record.rate, 32, channels, print_data_at);
    std::vector<float> interleaved(size_t(melodee::EngineHost::block) * channels);
    for (size_t at = 0; written && at < audio.left.size(); at += melodee::EngineHost::block) {
        unsigned n = unsigned(std::min<size_t>(melodee::EngineHost::block, audio.left.size() - at));
        for (unsigned f = 0; f < n; ++f) {
            float* frame = interleaved.data() + size_t(channels) * f;
            frame[0] = audio.left[at + f];
            frame[1] = audio.right[at + f];
            unsigned pair = 2;
            for (unsigned b = 0; b < 4; ++b) {
                if (!(r.record.sends >> b & 1)) continue;
                frame[pair] = audio.send_left[b][at + f];
                frame[pair + 1] = audio.send_right[b][at + f];
                pair += 2;
            }
        }
        written = wav.add(interleaved.data(), n);
    }
    written = wav.close() && written;
    if (written) fs::rename(part, path, e);
    if (!written || e) { fs::remove(part, e); r.error = "could not write " + r.record.file; r.seconds = since(t0); return r; }
    give_to_owner_of(folder, studio.home_folder());
    give_to_owner_of(path.string(), studio.home_folder());
    r.made = true;
    r.seconds = since(t0);
    r.times_real = rendering > 0 ? double(audio.rendered) / rate / rendering : 0;
    return r;
}

// The prints read whole into memory, a clip a pattern, for the audio thread to play as it plays a sample.
Printer::Ready Printer::freeze_pack(const Work& work, const melodee::PrintTable& known) {
    Ready f;
    unsigned t = work.job.track % melodee::track_count;
    f.track = t;
    auto pack = std::make_unique<melodee::FreezePack>();
    pack->track = uint8_t(t);
    pack->tap = work.job.tap;
    pack->deep = work.job.deep;
    melodee::Mix m = melodee::print_mix(work.project->tracks[t]);
    pack->level = m.level;
    pack->pan = m.pan;
    const unsigned rate = unsigned(std::lround(melodee::dsp::sample_rate()));
    const std::string folder = melodee::prints_folder(work.project_path);
    f.freeze.state = work.job.deep ? melodee::FreezeState::Deep : melodee::FreezeState::Frozen;
    f.freeze.tap = work.job.tap;
    f.freeze.tail = work.job.tail;
    for (uint8_t p : work.job.patterns) {
        const melodee::PrintRecord* rec = known.find(t, p);
        if (!rec) { f.error = "NO PRINT OF " + melodee::print_pattern_name(p); return f; }
        std::vector<float> frames;
        unsigned file_rate = 0, channels = 0;
        if (!read_float_wav((fs::path(folder) / rec->file).string(), frames, file_rate, channels) || file_rate != rate ||
            channels != rec->channels()) {
            f.error = "PRINT UNREADABLE";
            return f;
        }
        auto clip = std::make_unique<melodee::PrintClip>();
        uint32_t pass = uint32_t(std::llround(double(rec->pass) * rate / melodee::dsp::model_rate));
        clip->hold_whole(std::move(frames), rate, pass, rec->tail, rec->sends);
        pack->clips[p] = clip.get();
        pack->owned.push_back(std::move(clip));
        f.freeze.set(p, true);
    }
    f.pack = pack.release();
    return f;
}

// ---------------------------------------------------------------- remote

// melodee-print TRACK [patterns] [pre|post] [loop|ring|cut]; melodee-print status|cancel;
// melodee-freeze TRACK [patterns] [pre|post] [loop|ring|cut] [deep]; melodee-freeze [status];
// melodee-unfreeze TRACK. Patterns: names (A1 B16) or numbers 1-128, comma or space apart; none: all with notes.
std::string Printer::command(const std::string& name, const std::string& words) {
    std::istringstream in(words);
    std::vector<std::string> args;
    for (std::string w; in >> w;) {
        std::stringstream split(w);
        for (std::string part; std::getline(split, part, ',');) if (!part.empty()) args.push_back(part);
    }
    bool print = name == "melodee-print", freeze = name == "melodee-freeze", thaw = name == "melodee-unfreeze";
    if (!print && !freeze && !thaw) return "unknown command";
    if (args.empty() || args[0] == "status") {
        if (print) return status();
        std::string line = "frozen";
        for (unsigned t = 0; t < melodee::track_count; ++t)
            if (frozen(t)) line += " " + std::to_string(t + 1) + (deep(t) ? "deep" : "") + (thawing(t) ? "thawing" : "");
        return line;
    }
    if (args[0] == "cancel") { cancel(); return "cancelled"; }
    int track = std::atoi(args[0].c_str());
    if (track < 1 || track > int(melodee::track_count)) return "no track " + args[0];
    if (thaw) { auto why = unfreeze(unsigned(track - 1)); return why.empty() ? "unfrozen " + args[0] : why; }
    Job job;
    job.track = unsigned(track - 1);
    job.freeze = freeze;
    for (size_t n = 1; n < args.size(); ++n) {
        const auto& a = args[n];
        if (a == "pre") job.tap = PrintTap::PreFx;
        else if (a == "post") job.tap = PrintTap::PostFx;
        else if (a == "loop") job.tail = PrintTail::Loop;
        else if (a == "ring") job.tail = PrintTail::Ring;
        else if (a == "cut") job.tail = PrintTail::Cut;
        else if (a == "deep") job.deep = true;
        else if (a.size() >= 2 && std::isalpha(static_cast<unsigned char>(a[0]))) {
            int bank = std::toupper(static_cast<unsigned char>(a[0])) - 'A', pad = std::atoi(a.c_str() + 1);
            if (bank < 0 || bank >= int(melodee::pattern_count / 16) || pad < 1 || pad > 16) return "no pattern " + a;
            job.patterns.push_back(uint8_t(bank * 16 + pad - 1));
        } else {
            int p = std::atoi(a.c_str());
            if (p < 1 || p > int(melodee::pattern_count)) return "no pattern " + a;
            job.patterns.push_back(uint8_t(p - 1));
        }
    }
    if (job.patterns.empty() && !job.deep) {
        auto with = with_notes(job.track);
        for (unsigned p = 0; p < melodee::pattern_count; ++p) if (with[p / 64] & pattern_bit(p)) job.patterns.push_back(uint8_t(p));
    }
    auto why = start(std::move(job));
    return why.empty() ? std::string(freeze ? "freezing " : "printing ") + args[0] : why;
}

std::string Printer::status() const {
    unsigned frozen_count = 0, deep_count = 0;
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        frozen_count += frozen(t);
        deep_count += deep(t);
    }
    std::lock_guard<std::mutex> guard(lock);
    char text[240];
    std::snprintf(text, sizeof text, " printing=%d print_track=%u print_through=%u/%u prints=%zu printed=%u print_skipped=%u frozen=%u deep=%u thawing=%u",
                  int(shown.busy), shown.track + 1, unsigned(shown.through), unsigned(shown.count), prints.records.size(), printed, skipped,
                  frozen_count, deep_count, unsigned(__builtin_popcount(thaw_mask)));
    std::string line = text;
    if (!shown.error.empty()) {
        std::string error = shown.error;
        std::replace(error.begin(), error.end(), ' ', '_');
        line += " print_error=" + error;
    }
    return line;
}

} // namespace app
