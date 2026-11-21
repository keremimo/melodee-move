// SPDX-License-Identifier: AGPL-3.0-only
#include "app/loop_desk.h"
#include "app/studio.h"
#include "melodee/engine/sample_data.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <thread>

namespace app {
namespace {

namespace fs = std::filesystem;
namespace looper = melodee::looper;
using melodee::Command;
using Type = melodee::Command::Type;
using S = melodee::Looper::State;
using Do = looper::Do;

std::string loop_name(unsigned loop) { return "L" + std::to_string(loop % LoopDesk::loops + 1); }

// A loop's frames at `rate` as the project counts them (44.1 kHz frames).
uint32_t to_model(uint32_t frames, unsigned rate) { return uint32_t((uint64_t(frames) * 44100 + rate / 2) / std::max(1u, rate)); }
uint32_t from_model(uint32_t frames, unsigned rate) { return uint32_t((uint64_t(frames) * rate + 22050) / 44100); }

std::string loop_file(const std::string& dir, unsigned loop) { return dir + "/Loop " + std::to_string(loop + 1) + ".wav"; }

// What the audio thread refused, as the screen says it.
const char* refusal(looper::Error e) {
    switch (e) {
    case looper::Error::NoMemory: return "NO MEMORY YET";
    case looper::Error::Locked: return "IT RECORDS";
    case looper::Error::TooLong: return "TOO LONG";
    case looper::Error::Tempo: return "CANNOT FIT";
    case looper::Error::Busy: return "BUSY, AGAIN";
    default: return "";
    }
}

// The sources a loop can record on the Move, in the order the knob turns
// through them: the input (Input Config's: the Move's line in or mic), its
// left and right sides, the tracks there are, the other loops, the mix.
std::vector<uint8_t> sources_for(const melodee::Project& project, unsigned loop) {
    std::vector<uint8_t> out {looper::source_input, looper::source_direct(1), looper::source_direct(2)}; // Rear L, Rear R: the input's sides
    for (unsigned t = 0; t < melodee::track_count; ++t)
        if (project.tracks[t].used()) out.push_back(looper::source_track(t));
    for (unsigned n = 0; n < LoopDesk::loops; ++n)
        if (n != loop) out.push_back(looper::source_loop(n));
    out.push_back(looper::source_mix);
    uint8_t own = project.looper.loops[loop].source;
    if (std::find(out.begin(), out.end(), own) == out.end()) out.insert(out.begin(), own);
    return out;
}

std::string lower(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

} // namespace

LoopDesk::LoopDesk(Studio& owner) : studio(owner) {
    // The project just read: the loops it keeps come back from their files on the first service pass.
    const auto& lp = studio.project().looper;
    for (unsigned n = 0; n < loops; ++n)
        if (lp.on && lp.loops[n].frames) load_mask |= uint8_t(1u << n);
}

melodee::Looper& LoopDesk::looper() { return studio.host().looper(); }
const melodee::Looper& LoopDesk::looper() const { return studio.host().looper(); }

std::string LoopDesk::loop_dir() const {
    fs::path p(studio.project_path());
    return (p.parent_path() / (p.stem().string() + " Loops")).string();
}

// ---------------------------------------------------------------- under the lock

bool LoopDesk::on() const { return studio.project().looper.on; }
unsigned LoopDesk::memory_made() const { return std::min(looper().buffers_out(), 2 * loops); }
LoopDesk::View LoopDesk::view(unsigned loop) const { return looper().view(loop % loops); }

unsigned LoopDesk::holding() const {
    unsigned n = 0;
    for (unsigned loop = 0; loop < loops; ++loop) n += view(loop).state != S::Empty;
    return n;
}

bool LoopDesk::ask(Do what, unsigned loop, int value, uint32_t position) {
    loop %= loops;
    auto& lp = studio.project().looper;
    auto v = view(loop);
    bool taking = v.state == S::Recording || v.state == S::Overdub;
    bool keeps = true; // a value the project keeps: saved
    switch (what) {
    case Do::Enable: lp.on = value != 0; break;
    case Do::Length:
        if (v.state != S::Empty && v.state != S::Armed) { studio.say(loop_name(loop) + ": BEFORE A TAKE"); return false; }
        lp.loops[loop].bars = uint8_t(std::clamp(value, 0, 16));
        break;
    case Do::Feedback: lp.loops[loop].feedback = uint8_t(std::clamp(value, 0, 100)); break;
    case Do::Source:
        if (taking) { studio.say(loop_name(loop) + ": BETWEEN TAKES"); return false; }
        if (value < 0 || value >= int(looper::source_count)) return false;
        lp.loops[loop].source = uint8_t(value);
        break;
    case Do::Quantize: lp.quantize = uint8_t(std::clamp(value, 0, int(looper::Quantize::Count) - 1)); break;
    case Do::Then: lp.then = uint8_t(std::clamp(value, 0, int(looper::Then::Count) - 1)); break;
    case Do::Transport: lp.transport = uint8_t(std::clamp(value, 0, int(looper::Transport::Count) - 1)); break;
    case Do::Latency: keeps = false; break; // the Move's own: kept in its settings (Studio)
    case Do::Clear: lp.loops[loop].frames = 0; break; // nothing kept for the file any more
    default:
        keeps = false;
        if (!lp.on) { studio.say("LOOPER IS OFF"); return false; }
        break;
    }
    Command command {Type::Looper, uint8_t(loop), 0, uint16_t(what), int16_t(value)};
    command.position = position;
    if (!studio.send(command)) return false;
    if (keeps) studio.changed();
    return true;
}

void LoopDesk::set_on(bool turn_on) {
    ask(Do::Enable, 0, turn_on);
    if (!turn_on) {
        for (auto& l : studio.project().looper.loops) l.frames = 0;
        clear_loop = -1;
    }
    studio.say(turn_on ? "LOOPER ON" : "LOOPER OFF");
}

void LoopDesk::step_length(unsigned loop, int delta) {
    loop %= loops;
    const auto& lengths = looper::lengths;
    unsigned bars = studio.project().looper.loops[loop].bars;
    auto at = std::find(lengths.begin(), lengths.end(), bars);
    int index = at == lengths.end() ? 0 : int(at - lengths.begin());
    int next = std::clamp(index + delta, 0, int(lengths.size()) - 1);
    if (next != index) ask(Do::Length, loop, lengths[size_t(next)]);
}

void LoopDesk::step_source(unsigned loop, int delta) {
    loop %= loops;
    auto list = sources_for(studio.project(), loop);
    uint8_t own = studio.project().looper.loops[loop].source;
    int index = int(std::find(list.begin(), list.end(), own) - list.begin());
    int next = std::clamp(index + delta, 0, int(list.size()) - 1);
    if (next != index) ask(Do::Source, loop, list[size_t(next)]);
}

void LoopDesk::set_feedback(unsigned loop, int percent) { ask(Do::Feedback, loop, std::clamp(percent, 0, 100)); }
void LoopDesk::set_quantize(int quantize) { ask(Do::Quantize, 0, quantize); }
void LoopDesk::set_then(int then) { ask(Do::Then, 0, then); }

const melodee::Mix& LoopDesk::mix(unsigned loop) const { return studio.project().loop_mix[loop % loops]; }

void LoopDesk::set_level(unsigned loop, int level) {
    auto& m = studio.project().loop_mix[loop % loops];
    m.level = uint8_t(std::clamp(level, 0, 127));
    studio.send({Type::Level, uint8_t(melodee::track_count + loop % loops), 0, 0, int16_t(m.level)});
    studio.changed();
}
void LoopDesk::set_pan(unsigned loop, int pan) {
    auto& m = studio.project().loop_mix[loop % loops];
    m.pan = int8_t(std::clamp(pan, -50, 50));
    studio.send({Type::Pan, uint8_t(melodee::track_count + loop % loops), 0, 0, int16_t(m.pan)});
    studio.changed();
}
void LoopDesk::set_send(unsigned loop, int send) {
    auto& m = studio.project().loop_mix[loop % loops];
    m.send = uint8_t(std::clamp(send, 0, 100));
    studio.send({Type::Send, uint8_t(melodee::track_count + loop % loops), 0, 0, int16_t(m.send)});
    studio.changed();
}
void LoopDesk::set_bus(unsigned loop, int bus) {
    auto& m = studio.project().loop_mix[loop % loops];
    m.bus = uint8_t(std::clamp(bus, 0, int(melodee::bus_count) - 1));
    studio.send({Type::SendBus, uint8_t(melodee::track_count + loop % loops), 0, 0, int16_t(m.bus)});
    studio.changed();
}
void LoopDesk::set_mute(unsigned loop, bool mute) {
    studio.project().loop_mix[loop % loops].mute = mute;
    studio.send({Type::Mute, uint8_t(melodee::track_count + loop % loops), 0, 0, int16_t(mute)});
    studio.changed();
}
void LoopDesk::set_solo(unsigned loop, bool solo) {
    studio.project().loop_mix[loop % loops].solo = solo;
    studio.send({Type::Solo, uint8_t(melodee::track_count + loop % loops), 0, 0, int16_t(solo)});
    studio.changed();
}

std::string LoopDesk::source_name(uint8_t source) {
    if (source == looper::source_input) return "INPUT";
    if (source == looper::source_mix) return "MIX";
    if (looper::source_is_track(source)) return "TRACK " + std::to_string(source);
    if (looper::source_is_loop(source)) return loop_name(source - looper::source_loop(0));
    if (source == looper::source_direct(1)) return "INPUT L";
    if (source == looper::source_direct(2)) return "INPUT R";
    if (looper::source_is_direct(source)) return "INPUT";
    return "INPUT";
}

std::string LoopDesk::length_name(unsigned bars) {
    if (!bars) return "FREE";
    return std::to_string(bars) + (bars == 1 ? " BAR" : " BARS");
}

void LoopDesk::set_latency(int frames) {
    input_latency = std::clamp(frames, 0, 48000);
    ask(Do::Latency, 0, 0, uint32_t(input_latency));
}

void LoopDesk::measure() {
    if (measuring_now()) { measuring.store(false, std::memory_order_relaxed); studio.say("MEASURE STOPPED"); return; }
    measure_seen = meter.count();
    measure_missed = meter.missed();
    measure_got = 0;
    measure_best = -1;
    measuring.store(true, std::memory_order_relaxed);
    studio.say("CABLE OUT TO IN");
}

void LoopDesk::pad(unsigned loop, unsigned job, bool down, Clock::time_point now) {
    loop %= loops;
    if (!down) {
        if (job == ClearPad && clear_loop == int(loop)) clear_loop = -1; // let go early: nothing
        return;
    }
    select(loop);
    if (!on()) { studio.say("LOOPER IS OFF"); return; }
    if (job == ClearPad && view(loop).state != S::Empty) {
        clear_loop = int(loop);
        clear_at = now;
    }
}

float LoopDesk::clear_progress(Clock::time_point now) const {
    if (clear_loop < 0) return 0;
    return std::clamp(float(std::chrono::duration<double>(now - clear_at).count() / std::chrono::duration<double>(clear_time).count()), 0.0f, 1.0f);
}

void LoopDesk::tick(Clock::time_point now) {
    if (measuring_now()) {
        if (meter.count() != measure_seen) {
            measure_seen = meter.count();
            int frames = meter.last();
            if (frames > 0 && (measure_best < 0 || frames < measure_best)) measure_best = frames;
            if (++measure_got >= 4) {
                measuring.store(false, std::memory_order_relaxed);
                set_latency(measure_best);
                studio.changed(); // kept in the Move's settings
                studio.say("IN DELAY " + std::to_string(measure_best));
            }
        }
        if (meter.missed() - measure_missed >= 4) {
            measuring.store(false, std::memory_order_relaxed);
            studio.say("NO CLICK CAME IN");
        }
    }
    if (clear_loop >= 0 && now - clear_at >= clear_time) {
        unsigned loop = unsigned(clear_loop);
        clear_loop = -1;
        if (ask(Do::Clear, loop)) studio.say(loop_name(loop) + " CLEARED");
    }
    for (unsigned n = 0; n < loops; ++n) {
        auto v = view(n);
        if (v.errors != errors_seen[n]) {
            errors_seen[n] = v.errors;
            if (*refusal(v.error)) studio.say(loop_name(n) + ": " + refusal(v.error));
        }
        observe(n, v);
    }
}

void LoopDesk::observe(unsigned n, const View& v) {
    // A loop's audio changed (a take, a pass, an undo, a fit, a clear): saved for it.
    Seen now_seen;
    now_seen.empty = v.state == S::Empty;
    now_seen.length = v.length;
    now_seen.layers = v.layers;
    now_seen.undo = uint8_t(v.undo);
    now_seen.tempo = v.tempo;
    now_seen.fitting = v.fitting;
    if (now_seen != seen[n]) {
        seen[n] = now_seen;
        ++edits[n];
        studio.changed();
    }
    if (adopting[n] && !now_seen.empty && v.length) { written[n] = edits[n]; adopting[n] = false; } // its file is the one read
    bool stopped = v.state == S::Stopped;
    if (stopped != was_stopped[n]) { was_stopped[n] = stopped; if (!now_seen.empty) studio.changed(); }
}

int LoopDesk::commit_pad() const {
    const auto& track = studio.current();
    if (track.engine != melodee::EngineKind::Sampler) return -1;
    for (unsigned k = 1; k <= pad_count; ++k) {
        unsigned at = (track.last_pad + k) % pad_count;
        if (!track.pads[at].used()) return int(at);
    }
    return -1;
}

bool LoopDesk::commit(unsigned loop, bool note) {
    loop %= loops;
    auto v = view(loop);
    if (studio.kind() != TrackKind::Kit) { studio.say("COMMIT: A KIT"); return false; }
    if (!v.length || (v.state != S::Playing && v.state != S::Stopped)) { studio.say(loop_name(loop) + ": NOT NOW"); return false; }
    int at = commit_pad();
    if (at < 0) { studio.say("NO EMPTY PAD"); return false; }
    for (const auto& job : commits)
        if (job.track == studio.track() && int(job.pad) == at) { studio.say("BUSY, AGAIN"); return false; }
    commits.push_back({loop, studio.track(), unsigned(at), note, v.ticks});
    return true;
}

void LoopDesk::keep_values() {
    auto& lp = studio.project().looper;
    FileJob job;
    job.dir = loop_dir();
    for (unsigned n = 0; n < loops; ++n) {
        auto v = view(n);
        observe(n, v); // whatever tick() has not seen yet
        auto& l = lp.loops[n];
        uint8_t bit = uint8_t(1u << n);
        if (!lp.on || v.state == S::Empty || v.state == S::Armed || ((!v.length || v.ticks <= 0) && v.state != S::Recording)) {
            l.frames = 0;
            job.remove |= bit;
            continue;
        }
        if (v.state == S::Recording || v.state == S::Overdub || v.fitting) {
            // Still being written: the project keeps what its file has; saved again once it is done.
            if (files_due && (files.write & bit)) { job.write |= bit; job.edits[n] = files.edits[n]; }
            continue;
        }
        l.frames = to_model(v.length, v.rate);
        l.ticks = v.ticks;
        l.tempo = v.tempo;
        l.layers = v.layers;
        l.stopped = v.state == S::Stopped;
        if (edits[n] != written[n]) { job.write |= bit; job.edits[n] = edits[n]; }
    }
    if (job.write || job.remove || files_due) {
        files = std::move(job);
        files_due = true;
    }
}

// ---------------------------------------------------------------- remote

std::string LoopDesk::remote(const std::string& arguments) {
    std::istringstream in(arguments);
    std::string verb, a, b, d;
    in >> verb >> a >> b >> d;
    verb = lower(verb); a = lower(a); b = lower(b); d = lower(d);
    int loop = -1;
    auto loop_of = [&](const std::string& text) {
        int n = std::atoi(text.c_str());
        if (n < 1 || n > int(loops)) return false;
        loop = n - 1;
        return true;
    };
    const std::string usage =
        "melodee-looper status | on | off | rec|toggle|halt|now|undo|clear N | length N 1-16|free | feedback N 0-1 | "
        "level|pan|send|bus N V | mute|solo N 0|1 | quantize bar|beat|off | then play|overdub|stop | "
        "source N input|input l|input r|track M|loop M|mix | latency FRAMES|auto | transport follow|free | commit N [note]";
    if (verb.empty() || verb == "status") return status();
    if (verb == "on" || verb == "off") { set_on(verb == "on"); return "ok"; }
    struct Simple { const char* name; Do what; };
    static const Simple simple[] {{"rec", Do::Primary}, {"toggle", Do::Toggle}, {"halt", Do::Halt}, {"now", Do::Now}, {"undo", Do::Undo}, {"clear", Do::Clear}};
    for (const auto& s : simple)
        if (verb == s.name) {
            if (!loop_of(a)) return "error: loops are 1 to 4";
            return ask(s.what, unsigned(loop)) ? "ok" : "error: refused";
        }
    if (verb == "length") {
        if (!loop_of(a)) return "error: loops are 1 to 4";
        int bars = b == "free" ? 0 : std::atoi(b.c_str());
        if (b != "free" && std::find(looper::lengths.begin(), looper::lengths.end(), bars) == looper::lengths.end()) return "error: length 1, 2, 4, 8, 16 or free";
        return ask(Do::Length, unsigned(loop), bars) ? "ok" : "error: before a take";
    }
    if (verb == "feedback") {
        if (!loop_of(a)) return "error: loops are 1 to 4";
        set_feedback(unsigned(loop), int(std::lround(std::atof(b.c_str()) * 100)));
        return "ok";
    }
    if (verb == "level" || verb == "pan" || verb == "send" || verb == "bus") {
        if (!loop_of(a)) return "error: loops are 1 to 4";
        int value = verb == "bus" && !b.empty() && std::isalpha(static_cast<unsigned char>(b[0])) ? b[0] - 'a' : std::atoi(b.c_str());
        if (verb == "level") set_level(unsigned(loop), value);
        else if (verb == "pan") set_pan(unsigned(loop), value);
        else if (verb == "send") set_send(unsigned(loop), value);
        else set_bus(unsigned(loop), value);
        return "ok";
    }
    if (verb == "mute" || verb == "solo") {
        if (!loop_of(a)) return "error: loops are 1 to 4";
        bool value = b != "0" && b != "off";
        if (verb == "mute") set_mute(unsigned(loop), value);
        else set_solo(unsigned(loop), value);
        return "ok";
    }
    if (verb == "quantize") {
        int q = a == "bar" ? 0 : a == "beat" ? 1 : a == "off" ? 2 : -1;
        if (q < 0) return "error: quantize bar, beat or off";
        set_quantize(q);
        return "ok";
    }
    if (verb == "then") {
        int t = a == "play" ? 0 : a == "overdub" ? 1 : a == "stop" ? 2 : -1;
        if (t < 0) return "error: then play, overdub or stop";
        set_then(t);
        return "ok";
    }
    if (verb == "transport") {
        int t = a == "follow" ? 0 : a == "free" ? 1 : -1;
        if (t < 0) return "error: transport follow or free";
        ask(Do::Transport, 0, t);
        return "ok";
    }
    if (verb == "latency") {
        if (a == "auto") { set_latency(default_latency); studio.changed(); return "ok"; }
        if (a.empty() || !std::isdigit(static_cast<unsigned char>(a[0]))) return "error: latency FRAMES or auto";
        set_latency(std::atoi(a.c_str()));
        studio.changed(); // kept in the Move's settings
        return "ok";
    }
    if (verb == "source") {
        if (!loop_of(a)) return "error: loops are 1 to 4";
        int m = std::atoi(d.c_str());
        int source = -1;
        if (b == "input" && d.empty()) source = looper::source_input;
        else if (b == "input" && d == "l") source = looper::source_direct(1);
        else if (b == "input" && d == "r") source = looper::source_direct(2);
        else if (b == "mix") source = looper::source_mix;
        else if (b == "track" && m >= 1 && m <= int(melodee::track_count)) source = looper::source_track(unsigned(m - 1));
        else if (b == "loop" && m >= 1 && m <= int(loops) && m - 1 != loop) source = looper::source_loop(unsigned(m - 1));
        if (source < 0) return "error: source input [l|r], track 1-16, loop 1-4 (another) or mix";
        return ask(Do::Source, unsigned(loop), source) ? "ok" : "error: between takes";
    }
    if (verb == "commit") {
        if (!loop_of(a)) return "error: loops are 1 to 4";
        return commit(unsigned(loop), b == "note") ? "ok" : "error: refused";
    }
    return "error: " + usage;
}

std::string LoopDesk::status() const {
    static const char* const pending[] {"", "start", "close", "play", "stop"};
    const auto& lp = looper();
    std::string out = std::string(" looper=") + (on() ? "on" : "off") + " loop_measuring=" + (measuring_now() ? "1" : "0") +
                      " loop_quantize=" + looper::quantize_name(lp.quantize()) +
                      " loop_then=" + looper::then_name(lp.then()) + " loop_latency=" + std::to_string(input_latency) +
                      " loop_memory=" + std::to_string(lp.buffers_out()) + " loop_selected=" + std::to_string(selected + 1);
    for (unsigned n = 0; n < loops; ++n) {
        auto v = view(n);
        const char* word = looper::state_word(v.state);
        std::string source = lower(source_name(v.source));
        source.erase(std::remove(source.begin(), source.end(), ' '), source.end());
        out += " loop" + std::to_string(n + 1) + "=" + (*word ? word : "EMPTY") + ",len=" + std::to_string(v.length) +
               ",bars=" + std::to_string(v.bars) + ",layers=" + std::to_string(v.layers) + ",src=" + source +
               ",tempo=" + std::to_string(v.tempo);
        if (v.pending != melodee::Looper::Pending::None) out += std::string(",pending=") + pending[unsigned(v.pending) % 5];
    }
    return out;
}

// ---------------------------------------------------------------- service thread

bool LoopDesk::hold_wait(unsigned loop, std::chrono::milliseconds wait) {
    auto until = Clock::now() + wait;
    while (!looper().hold(loop)) {
        if (Clock::now() >= until) { looper().release(loop); return false; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void LoopDesk::read_files() {
    std::string dir;
    std::array<melodee::LoopTrack, loops> values {};
    uint8_t mask = 0;
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        if (loaded) return;
        loaded = true;
        mask = load_mask;
        dir = loop_dir();
        values = studio.project().looper.loops;
    }
    const float device = melodee::dsp::sample_rate();
    for (unsigned n = 0; n < loops; ++n) {
        if (!((mask >> n) & 1)) continue;
        melodee::SampleData data;
        auto failed = melodee::read_wav(loop_file(dir, n), data, 70);
        if (!failed.empty() || !data.frames) {
            std::lock_guard<std::mutex> guard(studio.lock);
            studio.say(loop_name(n) + ": FILE MISSING");
            continue;
        }
        uint32_t frames = data.frames;
        if (values[n].frames) frames = std::min(frames, from_model(values[n].frames, data.rate) + 2);
        auto* buffer = new melodee::LoopBuffer(std::max(frames, looper::frames_at(device)));
        std::copy(data.left.begin(), data.left.begin() + frames, buffer->left.get());
        std::copy(data.right.begin(), data.right.begin() + frames, buffer->right.get());
        melodee::LoopLoad item;
        item.buffer = buffer;
        item.loop = uint8_t(n);
        item.frames = frames;
        item.rate = data.rate;
        item.ticks = values[n].ticks;
        item.tempo = values[n].tempo;
        item.layers = std::max<uint8_t>(1, values[n].layers);
        item.stopped = values[n].stopped;
        std::lock_guard<std::mutex> guard(studio.lock);
        adopting[n] = true;
        if (!looper().give_load(item)) { delete buffer; adopting[n] = false; studio.say(loop_name(n) + ": NOT READ"); }
    }
}

void LoopDesk::write_files(FileJob job, std::chrono::milliseconds wait) {
    std::error_code error;
    uint8_t retry = 0;
    for (unsigned n = 0; n < loops; ++n) {
        std::string file = loop_file(job.dir, n);
        if ((job.remove >> n) & 1) { fs::remove(file, error); continue; }
        if (!((job.write >> n) & 1)) continue;
        if (!hold_wait(n, wait)) { retry |= uint8_t(1u << n); continue; }
        auto v = view(n);
        uint32_t frames = std::min<uint32_t>(v.length, looper().held_frames(n));
        std::vector<float> left(looper().held_left(n), looper().held_left(n) + frames);
        std::vector<float> right(looper().held_right(n), looper().held_right(n) + frames);
        looper().release(n);
        melodee::SampleData data;
        melodee::adopt(data, std::move(left), std::move(right), v.rate); // at the loop's own rate
        fs::create_directories(job.dir, error);
        auto failed = melodee::write_wav(file + ".new", data);
        if (failed.empty()) fs::rename(file + ".new", file, error);
        std::lock_guard<std::mutex> guard(studio.lock);
        if (!failed.empty() || error) { studio.say(loop_name(n) + ": NOT SAVED"); retry |= uint8_t(1u << n); continue; }
        written[n] = job.edits[n];
    }
    // No loops left: no folder either.
    if (job.remove == (1u << loops) - 1 && fs::is_directory(job.dir, error) && fs::is_empty(job.dir, error)) fs::remove(job.dir, error);
    if (retry) {
        std::lock_guard<std::mutex> guard(studio.lock);
        if (!files_due) { files = job; files.write = retry; files.remove = 0; files_due = true; }
        else files.write |= uint8_t(retry & ~files.remove);
    }
}

void LoopDesk::run_commit(const CommitJob& job) {
    if (!hold_wait(job.loop, std::chrono::milliseconds(1000))) {
        std::lock_guard<std::mutex> guard(studio.lock);
        studio.say(loop_name(job.loop) + ": BUSY, AGAIN");
        return;
    }
    auto v = view(job.loop);
    uint32_t frames = std::min<uint32_t>(v.length, looper().held_frames(job.loop));
    std::vector<float> left(looper().held_left(job.loop), looper().held_left(job.loop) + frames);
    std::vector<float> right(looper().held_right(job.loop), looper().held_right(job.loop) + frames);
    looper().release(job.loop);
    melodee::SampleData data;
    melodee::adopt(data, std::move(left), std::move(right), v.rate);
    // Melodee/Samples/Looper/L<n> Loop <m>.wav, m the first free number.
    std::error_code error;
    fs::path folder = fs::path(studio.home_folder()) / "Samples" / "Looper";
    fs::create_directories(folder, error);
    fs::path file;
    for (unsigned m = 1; m < 10000; ++m) {
        file = folder / (loop_name(job.loop) + " Loop " + std::to_string(m) + ".wav");
        if (!fs::exists(file, error)) break;
    }
    std::string failed = frames ? melodee::write_wav(file.string(), data) : "nothing there";
    std::unique_ptr<Sample> sample;
    if (failed.empty()) sample = read_sample(file.string(), failed);
    std::lock_guard<std::mutex> guard(studio.lock);
    if (!sample) { studio.say("COMMIT: NOT SAVED"); return; }
    auto& track = studio.project().tracks[job.track];
    if (track.engine != melodee::EngineKind::Sampler || track.pads[job.pad].used()) { studio.say("COMMIT: PAD TAKEN"); return; }
    if (auto refused = studio.install(std::move(sample), job.track, job.pad); !refused.empty()) { studio.say(refused); return; }
    if (job.note) {
        // One note at bar 1 as long as the loop (the pattern made long enough), and the loop stops: the pad plays it now.
        auto& pattern = studio.patterns().at[job.track][track.pattern % melodee::pattern_count];
        unsigned ticks = unsigned(std::max(1.0, std::round(job.ticks))), bar = studio.project().bar_ticks();
        uint32_t want = std::min<uint32_t>((ticks + bar - 1) / bar * bar, studio.project().max_length());
        if (pattern.length < want) pattern.length = want;
        melodee::Event e;
        e.tick = 0;
        e.length = uint16_t(std::min<unsigned>(ticks, 65535));
        e.key = uint8_t(melodee::sampler_first_note + job.pad);
        e.velocity = 100;
        pattern.add(e);
        Command command {Type::PatternData, uint8_t(job.track), uint8_t(track.pattern % melodee::pattern_count)};
        command.data = new melodee::Pattern(pattern); // handed back to be freed (Studio::take_back)
        if (!studio.send(command)) delete command.data;
        ask(Do::Halt, job.loop);
    }
    char text[24];
    std::snprintf(text, sizeof text, "%s TO %c%02u", loop_name(job.loop).c_str(), 'A' + int(job.pad / 16), job.pad % 16 + 1);
    studio.say(text);
    studio.changed();
}

void LoopDesk::service() {
    looper().service(); // memory in and out, fits (thread-safe against the audio thread)
    if (!loaded) read_files();
    std::vector<CommitJob> todo;
    FileJob job;
    bool write = false;
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        todo.swap(commits);
        if (files_due) { job = std::move(files); files = {}; files_due = false; write = true; }
    }
    for (const auto& each : todo) run_commit(each);
    if (write) write_files(std::move(job), std::chrono::milliseconds(20));
}

void LoopDesk::flush(std::chrono::milliseconds wait) {
    FileJob job;
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        keep_values();
        if (!files_due) return;
        job = std::move(files);
        files = {};
        files_due = false;
    }
    write_files(std::move(job), wait);
}

} // namespace app
