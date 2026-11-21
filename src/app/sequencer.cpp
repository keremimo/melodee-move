// SPDX-License-Identifier: AGPL-3.0-only
#include "app/sequencer.h"
#include "app/screen.h"
#include "app/studio.h"
#include "move/display.h"
#include "move/font.h"
#include "ui/seq_widgets.h"
#include "ui/track_style.h"
#include "ui/widgets.h"
#include "melodee/engine/arp.h"
#include "melodee/engine/engine_info.h"
#include "melodee/model/lane_value.h"
#include "melodee/model/generators.h"
#include "melodee/model/pad_params.h"
#include "melodee/model/scales.h"
#include "melodee/shell/free_length.h"
#include "melodee/shell/record_place.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace app {
namespace {

using melodee::Command;
using melodee::Pattern;
using Type = melodee::Command::Type;
using bindings::Job;
constexpr auto lock_linger = std::chrono::milliseconds(900);
constexpr auto tap_window = std::chrono::milliseconds(2000);

uint64_t lane_id(unsigned track, unsigned number, unsigned param, unsigned pad) {
    return uint64_t(track) << 32 | uint64_t(number) << 16 | uint64_t(param) << 8 | pad;
}

std::string number_text(int value) { return std::to_string(value); }

// A kit page's knob by its name, as a lane of the last pad (kit_play's TUNE,
// LEVEL ... and whatever page names its knobs the same).
int pad_lane_named(const std::string& name) {
    using P = melodee::PadParam;
    struct Named { const char* name; P param; };
    static const Named named[] {{"TUNE", P::Pitch}, {"PITCH", P::Pitch}, {"FINE", P::Fine}, {"LEVEL", P::Level}, {"PAN", P::Pan},
                                {"ATTACK", P::Attack}, {"DECAY", P::Decay}, {"SUSTAIN", P::Sustain}, {"RELEASE", P::Release},
                                {"CUTOFF", P::FilterCutoff}, {"RESO", P::FilterResonance}, {"RESONANCE", P::FilterResonance}};
    for (const auto& n : named)
        if (name == n.name) return int(melodee::motion_pad_param(n.param));
    return -1;
}

} // namespace

Sequencer::Sequencer(Studio& owner) : studio(owner), ring(std::make_unique<melodee::CaptureRing>()) {
    pad_keys.fill(-1);
    pad_tracks.fill(-1);
    loop_sent.fill(-1);
}

Sequencer::~Sequencer() = default;

uint32_t Sequencer::ms_now() const {
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
}

// ---------------------------------------------------------------- patterns

Pattern& Sequencer::pattern(unsigned track, unsigned n) { return studio.patterns().at[track % melodee::track_count][n % melodee::pattern_count]; }
Pattern& Sequencer::pattern() { return pattern(studio.track(), number(studio.track())); }
unsigned Sequencer::number(unsigned track) const { return studio.project().tracks[track % melodee::track_count].pattern % melodee::pattern_count; }
uint32_t Sequencer::bar_ticks() const { return std::max(1u, studio.project().bar_ticks()); }
bool Sequencer::kit() const { return studio.current().engine == melodee::EngineKind::Sampler; }
int Sequencer::lane_key() const { return kit() ? int(studio.current().step_note()) : -1; }

Pattern Sequencer::copy_for(unsigned track, unsigned n) const {
    const auto& source = studio.patterns().at[track % melodee::track_count][n % melodee::pattern_count];
    Pattern played = melodee::played_pattern(source, studio.project().tracks[track % melodee::track_count].speed);
    if (!lanes_off.empty())
        played.motion.erase(std::remove_if(played.motion.begin(), played.motion.end(),
                                           [&](const melodee::MotionPoint& m) { return lane_off(track, n, m.param, m.pad); }),
                            played.motion.end());
    return played;
}

void Sequencer::sync(unsigned track, unsigned n) {
    Command c {Type::PatternData, uint8_t(track), uint8_t(n)};
    c.data = new Pattern(copy_for(track, n)); // freed once the audio thread hands it back (Studio::take_back)
    if (!studio.send(c)) delete c.data;
    studio.changed();
}

void Sequencer::sync_track(unsigned track) {
    for (unsigned n = 0; n < melodee::pattern_count; ++n)
        if (!pattern(track, n).events.empty() || !pattern(track, n).motion.empty() || n == number(track)) sync(track, n);
}

int Sequencer::next_empty(unsigned track, unsigned after) const {
    const auto& bank = studio.patterns().at[track % melodee::track_count];
    for (unsigned k = 1; k <= melodee::pattern_count; ++k) {
        unsigned n = (after + k) % melodee::pattern_count;
        if (bank[n].empty() && bank[n].motion.empty()) return int(n);
    }
    return -1;
}

void Sequencer::launch(unsigned track, unsigned n) {
    track %= melodee::track_count;
    n %= melodee::pattern_count;
    auto& t = studio.project().tracks[track];
    auto& p = pattern(track, n);
    if (studio.playing() && p.is_free()) { // an empty one plays as long as the one playing (Melodee's)
        const auto& now = pattern(track, studio.host().pattern_of(track));
        if (!now.is_free()) { seq::set_length(p, now.length, studio.project().max_length()); sync(track, n); }
    }
    if (t.chained()) {
        t.chain_count = 0;
        studio.send({Type::Chained, uint8_t(track), 0, 0, 0});
    }
    t.pattern = uint8_t(n);
    studio.send({Type::Pattern, uint8_t(track), 0, 0, int16_t(n)});
    if (track == studio.track()) pages[track] = 0;
    if (studio.project().seq_tempo && studio.project().pattern_tempo[n])
        studio.send({Type::SeqTempo, 0, 0, uint16_t(n), 1});
    studio.changed();
}

void Sequencer::chain(unsigned track, const std::vector<uint8_t>& numbers) {
    track %= melodee::track_count;
    if (numbers.size() < 2) { if (!numbers.empty()) launch(track, numbers.front()); return; }
    launch(track, numbers.front());
    auto& t = studio.project().tracks[track];
    t.chain_count = uint8_t(std::min<size_t>(numbers.size(), melodee::max_chain));
    for (unsigned k = 0; k < t.chain_count; ++k) t.chain[k] = numbers[k];
    t.chain_at = 0;
    studio.send({Type::Chained, uint8_t(track), 0, 0, 1});
    studio.changed();
}

void Sequencer::launch_scene(unsigned n) {
    n %= melodee::pattern_count;
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        if (!studio.project().tracks[t].used()) continue;
        if (t == studio.track() || !pattern(t, n).empty()) launch(t, n);
    }
    if (auto tempo = studio.project().scenes[n].tempo) studio.send({Type::SceneTempo, 0, 0, 0, int16_t(tempo)});
}

bool Sequencer::copy_pattern(unsigned track, unsigned from, unsigned to_track, unsigned to) {
    if (track == to_track && from == to) return false;
    pattern(to_track, to) = pattern(track, from);
    sync(to_track, to);
    return true;
}

void Sequencer::clear_pattern(unsigned track, unsigned n) {
    auto& p = pattern(track, n);
    p.clear_notes_and_motion();
    p.free = true;
    sync(track, n);
}

// ---------------------------------------------------------------- pages and the loop

unsigned Sequencer::page_steps() const { return seq::page_steps(const_cast<Sequencer*>(this)->pattern()); }

unsigned Sequencer::page() const {
    unsigned t = studio.track();
    if (follow && studio.playing() && steps_down.empty()) {
        int position = studio.host().position_of(t);
        if (position >= 0) {
            auto& p = const_cast<Sequencer*>(this)->pattern();
            return unsigned(position) / std::max(1u, p.q()) / page_steps();
        }
    }
    return unsigned(std::max(0, pages[t]));
}

void Sequencer::set_page(int to) {
    auto& p = pattern();
    unsigned steps = page_steps();
    int last = int((p.length + p.q() * steps - 1) / (p.q() * steps)); // one page past the end: spare steps
    pages[studio.track()] = std::clamp(to, 0, std::min(last, int(melodee::max_bars * bar_ticks() / (p.q() * steps)) - 1));
    follow = false;
}

unsigned Sequencer::step_of(unsigned index) const { return page() * page_steps() + index; }

unsigned Sequencer::loop_from(unsigned track, unsigned n) const {
    auto at = loop_starts.find(track * melodee::pattern_count + n);
    return at == loop_starts.end() ? 0 : at->second;
}

void Sequencer::set_loop(unsigned from_bar, unsigned to_bar) {
    if (to_bar <= from_bar) std::swap(from_bar, to_bar);
    if (to_bar == from_bar) ++to_bar;
    unsigned t = studio.track(), n = number(t);
    auto& p = pattern();
    uint32_t bar = bar_ticks();
    if (from_bar == 0) {
        loop_starts.erase(t * melodee::pattern_count + n);
        seq::set_length(p, to_bar * bar, studio.project().max_length());
    } else {
        loop_starts[t * melodee::pattern_count + n] = from_bar;
        if (p.length < to_bar * bar) seq::set_length(p, to_bar * bar, studio.project().max_length());
        // The loop's end beyond its start, kept as the length while it loops from a later bar.
        loop_starts[t * melodee::pattern_count + n] = from_bar | (to_bar << 16);
    }
    loop_sent[t] = -1; // tick() sends the editor's loop
    sync(t, n);
}

// ---------------------------------------------------------------- the keys

std::vector<uint8_t> Sequencer::step_keys() const {
    if (kit()) return {uint8_t(studio.current().step_note())};
    if (!keys_down.empty()) return keys_down;
    return {studio.current().last_note};
}

int Sequencer::held_step() const { return steps_down.empty() ? -1 : int(step_of(steps_down.front())); }

bool Sequencer::on_tempo() const { return studio.top() && studio.top()->id() == "tempo"; }

void Sequencer::tap(Clock::time_point now) {
    if (!taps.empty() && now - taps.back() > tap_window) taps.clear();
    taps.push_back(now);
    if (taps.size() > 5) taps.erase(taps.begin());
    if (taps.size() < 2) { studio.say("TAP AGAIN"); return; }
    double ms = std::chrono::duration<double, std::milli>(taps.back() - taps.front()).count() / double(taps.size() - 1);
    if (ms <= 0) return;
    studio.set_tempo(unsigned(std::lround(600000.0 / ms)));
}

void Sequencer::fill(bool on) {
    if (fill_on == on) return;
    fill_on = on;
    studio.send({Type::Fill, 0, 0, 0, int16_t(on)});
}

bool Sequencer::job(Job job, const bindings::Use& use) {
    auto& p = pattern();
    unsigned t = studio.track(), n = number(t);
    int lane = lane_key();
    uint32_t q = p.q();
    auto held_span = [&] { return seq::step_span(p, unsigned(held_step())); };
    auto bar_span = [&] { return seq::bar_span(unsigned(held_step()), bar_ticks()); };
    switch (job) {
    // Steps (11): a tap sets or clears; a held step's notes take the turns.
    case Job::ToggleStep: {
        if (on_tempo()) { tap(Clock::now()); return true; }
        unsigned step = step_of(unsigned(use.index));
        if (seq::step_tick(p, step) >= p.length) { // past the end: a spare step lengthens it to its bar (Melodee's)
            uint32_t bar = bar_ticks();
            seq::set_length(p, (seq::step_tick(p, step) / bar + 1) * bar, studio.project().max_length());
        }
        bool added = seq::toggle_step(p, step, lane, step_keys(), 100);
        if (added && p.is_free()) p.free = false;
        sync(t, n);
        return true;
    }
    case Job::StepVelocity: seq::add_velocity(p, held_span(), lane, use.delta); sync(t, n); return true;
    case Job::StepLength: seq::add_length(p, held_span(), lane, use.delta * std::max<int>(1, int(q) / 10)); sync(t, n); return true;
    case Job::StepLengthTo: {
        unsigned to = step_of(unsigned(use.index));
        if (int(to) <= held_step()) return true;
        seq::length_to(p, held_span(), lane, seq::step_tick(p, to) + q);
        sync(t, n);
        return true;
    }
    case Job::TransposeStep: case Job::TransposeStepOctave: {
        int by = (job == Job::TransposeStepOctave ? 12 : 1) * (use.control == bindings::Control::Plus ? 1 : -1);
        seq::transpose(p, held_span(), lane, by, kit());
        sync(t, n);
        return true;
    }
    case Job::NudgeStep: case Job::NudgeStepFine: case Job::NudgeStepWhole: {
        int sign = use.control == bindings::Control::Right ? 1 : -1;
        int by = job == Job::NudgeStepWhole ? int(q) : job == Job::NudgeStepFine ? 1 : std::max(1, int(q) / 10);
        seq::nudge(p, held_span(), lane, sign * by);
        sync(t, n);
        return true;
    }
    case Job::StepMotion: lock_turn(unsigned(use.index), use.delta); return true;
    case Job::StepTrig: trig_page = !trig_page; knob_shown = -1; return true;
    case Job::NoteOnStep: {
        int key = studio.stamp(unsigned(use.index)).key.load(std::memory_order_acquire);
        if (key < 0 || held_step() < 0) return true;
        seq::toggle_key(p, unsigned(held_step()), uint8_t(key), 100); // the steps stay on their lane
        sync(t, n);
        return true;
    }
    case Job::QuantizePad: {
        int key = studio.stamp(unsigned(use.index)).key.load(std::memory_order_acquire);
        if (key < 0) return true;
        seq::quantize(p, key, workflow.quantize);
        sync(t, n);
        studio.say("QUANTIZED " + std::to_string(workflow.quantize) + "%");
        return true;
    }
    case Job::ClearPad: {
        int key = studio.stamp(unsigned(use.index)).key.load(std::memory_order_acquire);
        if (key < 0) return true;
        if (seq::clear_lane(p, uint8_t(key))) { sync(t, n); studio.say("NOTES DELETED"); }
        else { studio.set_pad(t, unsigned(use.index), melodee::PadSound {}); studio.say("PAD EMPTIED"); }
        return true;
    }
    case Job::CopyStep: {
        unsigned step = step_of(unsigned(use.index));
        if (copy_from < 0) { copy_from = int(step); copy_to = int(step); studio.say("STEP COPIED"); return true; }
        if (use.held_step >= 0 && !steps_down.empty() && copy_to == copy_from && step_of(steps_down.front()) == unsigned(copy_from)) {
            copy_to = int(step); // the first held, the last pressed: a range
            studio.say("STEPS COPIED");
            return true;
        }
        int from = std::min(copy_from, copy_to), to = std::max(copy_from, copy_to);
        seq::Span source {seq::step_span(p, unsigned(from)).from, seq::step_span(p, unsigned(to)).to};
        uint32_t shift = seq::step_tick(p, step) - seq::step_tick(p, unsigned(from)) * (step >= unsigned(from) ? 1 : 0);
        if (step < unsigned(from)) { // pasted earlier: as far back
            uint32_t back = seq::step_tick(p, unsigned(from)) - seq::step_tick(p, step);
            seq::Span target {source.from >= back ? source.from - back : 0, source.to - back};
            seq::copy_span(p, source, target, lane);
            sync(t, n);
            studio.say("PASTED");
            return true;
        }
        seq::Span target {source.from + shift, source.to + shift};
        seq::copy_span(p, source, target, lane);
        sync(t, n);
        studio.say("PASTED");
        return true;
    }
    // Patterns (12.3, 12.4, Shift + Steps 14-16).
    case Job::DuplicatePattern: {
        int to = next_empty(t, n);
        if (to < 0) { studio.say("NO EMPTY PATTERN"); return true; }
        copy_pattern(t, n, t, unsigned(to));
        launch(t, unsigned(to));
        studio.say("PATTERN " + std::to_string(to + 1));
        return true;
    }
    case Job::DeleteCurrentPattern:
        clear_pattern(t, n);
        studio.say("PATTERN " + std::to_string(n + 1) + " CLEARED");
        return true;
    case Job::NewPattern: {
        int to = next_empty(t, n);
        if (to < 0) { studio.say("NO EMPTY PATTERN"); return true; }
        launch(t, unsigned(to));
        studio.say("NEW PATTERN " + std::to_string(to + 1));
        return true;
    }
    case Job::DoubleLoop:
        if (!melodee::double_pattern(p, studio.project().max_length())) { studio.say("128 BARS AT MOST"); return true; }
        sync(t, n);
        studio.say(std::to_string(seq::bars_of(p, bar_ticks())) + " BARS");
        return true;
    case Job::Quantize:
        seq::quantize(p, -1, workflow.quantize);
        sync(t, n);
        studio.say("QUANTIZED " + std::to_string(workflow.quantize) + "%");
        return true;
    // Bars (9.5): Left / Right a page, Shift sixteen bars.
    case Job::Bar:
        if (on_tempo()) { tap_tempo_job(use.control == bindings::Control::Left, use.control == bindings::Control::Right); return true; }
        set_page(int(page()) + (use.control == bindings::Control::Right ? 1 : -1));
        studio.say("BAR " + std::to_string(page() * page_steps() * q / bar_ticks() + 1));
        return true;
    case Job::Bar16: {
        unsigned per_bar = std::max(1u, bar_ticks() / (q * page_steps()));
        set_page(int(page()) + (use.control == bindings::Control::Right ? 16 : -16) * int(per_bar));
        studio.say("BAR " + std::to_string(page() * page_steps() * q / bar_ticks() + 1));
        return true;
    }
    case Job::LoopLength: case Job::LoopLengthFine: {
        uint32_t by = job == Job::LoopLengthFine ? melodee::sixteenth_ticks : bar_ticks();
        int64_t length = int64_t(p.length) + int64_t(use.delta) * by;
        if (job == Job::LoopLength) length = (length + by / 2) / by * by;
        seq::set_length(p, uint32_t(std::max<int64_t>(length, by)), studio.project().max_length());
        sync(t, n);
        length_until = Clock::now() + lock_linger;
        return true;
    }
    // Loop Mode's own bars, from a Play screen (the Loop screen has them first).
    case Job::NoteOnBar: {
        int key = studio.stamp(unsigned(use.index)).key.load(std::memory_order_acquire);
        if (key >= 0 && held_step() >= 0) { seq::toggle_key_every_step(p, bar_span(), uint8_t(key), 100); sync(t, n); }
        return true;
    }
    // Recording (14.1) and Capture (14.3).
    case Job::Record: record(!rec && !rec_waiting); return true;
    case Job::Recall:
        if (!capture()) studio.say("NOTHING TO CAPTURE");
        return true;
    case Job::ClearRecall: clear_capture(); studio.say("CAPTURE CLEARED"); return true;
    // Motion lanes (14.2.1-3).
    case Job::DeleteMotion: {
        Lane lane_of;
        if (!knob_lane(unsigned(use.index), lane_of)) { studio.say("NO LANE"); return true; }
        if (p.clear_lane(lane_of.param, lane_of.pad)) { sync(t, n); studio.say(lane_of.name + " CLEARED"); }
        else studio.say("NO MOTION");
        return true;
    }
    case Job::ToggleMotion: {
        Lane lane_of;
        if (!knob_lane(unsigned(use.index), lane_of)) { studio.say("NO LANE"); return true; }
        toggle_lane(t, n, lane_of.param, lane_of.pad);
        studio.say(lane_of.name + (lane_off(t, n, lane_of.param, lane_of.pad) ? " OFF" : " ON"));
        return true;
    }
    // The sequencer's screens.
    case Job::LoopMode: studio.open("loop"); return true;
    case Job::ToggleSession: studio.open("session"); return true;
    case Job::PeekSession: studio.open("session"); peek_session = true; return true;
    case Job::Fill: if (studio.playing()) fill(true); return true;
    default: return false;
    }
}

void Sequencer::tap_tempo_job(bool halve, bool doubled) {
    unsigned tempo = studio.tempo();
    if (halve) studio.set_tempo(tempo / 2);
    else if (doubled) studio.set_tempo(tempo * 2);
}

void Sequencer::lock_turn(unsigned knob, int delta) {
    if (held_step() < 0) return;
    auto& p = pattern();
    unsigned t = studio.track(), n = number(t);
    auto span = seq::step_span(p, unsigned(held_step()));
    if (trig_page) {
        if (knob < unsigned(seq::Trig::Count)) seq::set_trig(p, span, lane_key(), seq::Trig(knob), delta);
        knob_shown = int(knob);
        knob_until = Clock::now() + lock_linger;
        sync(t, n);
        return;
    }
    Lane lane;
    if (!knob_lane(knob, lane)) { studio.say("NO LOCK HERE"); return; }
    int value = 0;
    if (!seq::lock_of(p, span, lane.param, lane.pad, value)) value = melodee::lane_own(studio.project(), t, lane.param, lane.pad);
    value = std::clamp(value + delta, std::min(lane.low, lane.high), std::max(lane.low, lane.high));
    if (!seq::any_in(p, span, lane_key())) { studio.say("STEP IS EMPTY"); return; }
    seq::lock(p, span, lane_key(), lane.param, lane.pad, value);
    knob_shown = int(knob);
    knob_until = Clock::now() + lock_linger;
    sync(t, n);
}

void Sequencer::after(Job job, const bindings::Use& use) {
    // Seq Record: a knob moved while recording is written as Motion where the playhead is (14.2).
    if (!rec || !studio.playing() || studio.host().count_in()) return;
    unsigned t = studio.track();
    Lane lane;
    bool knob = (job == Job::TurnKnob || job == Job::TurnKnobFine) && knob_lane(unsigned(use.index), lane);
    if (job == Job::TrackLevel) { lane.param = melodee::motion_level; lane.pad = 0; knob = true; }
    if (!knob) return;
    int position = studio.host().position_of(t);
    if (position < 0) return;
    auto& p = pattern();
    melodee::MotionPoint m;
    m.tick = melodee::wrap_tick(position, p.length);
    m.param = uint8_t(lane.param);
    m.pad = uint8_t(lane.pad);
    m.value = int16_t(melodee::lane_own(studio.project(), t, lane.param, lane.pad));
    if (p.write_motion(m)) sync(t, number(t));
}

bool Sequencer::knob_lane(unsigned knob, Lane& lane) const {
    unsigned t = studio.track();
    auto* top = studio.top();
    if (!top) return false;
    unsigned param = 0, pad = 0;
    bool found = top->knob_lane(knob, param, pad);
    Knob shown = top->knob(knob);
    if (!found && kit()) { // a kit page's knob by its name: the last pad's value
        int named = pad_lane_named(shown.name);
        if (named >= 0) { param = unsigned(named); pad = studio.current().last_pad; found = true; }
    }
    if (!found) return false;
    lane.param = param;
    lane.pad = pad;
    lane.name = shown.name.empty() ? "LANE" : shown.name;
    if (int which = melodee::pad_param_of(param); which >= 0) {
        auto range = melodee::pad_param_range(melodee::PadParam(which));
        lane.low = range.min;
        lane.high = range.max;
    } else if (param == melodee::motion_pan) {
        lane.low = -64; lane.high = 63;
    } else {
        lane.low = 0; lane.high = 127;
    }
    (void)t;
    return true;
}

void Sequencer::set_lane(unsigned t, unsigned param, unsigned pad, int value) {
    auto& track = studio.project().tracks[t % melodee::track_count];
    if (int which = melodee::pad_param_of(param); which >= 0) {
        auto sound = track.pads[pad % melodee::sampler_pads];
        melodee::set_pad_param(sound, melodee::PadParam(which), value);
        studio.set_pad(t, pad % melodee::sampler_pads, sound);
    } else if (param == melodee::motion_level) {
        studio.set_level(t, value);
    } else if (param == melodee::motion_pan) {
        track.mix.pan = int8_t(std::clamp(value, -64, 63));
        studio.send({Type::Pan, uint8_t(t), 0, 0, int16_t(track.mix.pan)});
        studio.changed();
    } else if (param < melodee::max_params) {
        track.params[param] = int16_t(value);
        studio.send({Type::Param, uint8_t(t), 0, uint16_t(param), int16_t(value)});
        studio.changed();
    }
}

bool Sequencer::lane_off(unsigned track, unsigned n, unsigned param, unsigned pad) const {
    return lanes_off.count(lane_id(track, n, param, pad)) != 0;
}

void Sequencer::toggle_lane(unsigned track, unsigned n, unsigned param, unsigned pad) {
    auto id = lane_id(track, n, param, pad);
    if (!lanes_off.erase(id)) lanes_off[id] = true;
    sync(track, n);
}

// ---------------------------------------------------------------- Seq Record

bool Sequencer::counting_in() const { return rec_waiting || (rec && studio.host().count_in() != 0); }

void Sequencer::record(bool on) {
    if (on == (rec || rec_waiting)) return;
    if (on) {
        take_track = studio.track();
        take_number = number(take_track);
        any_written = false;
        if (!studio.playing()) {
            // From stopped: a bar's count-in first (14.1.1), then the take from the top.
            Command play {Type::Play, uint8_t(workflow.count_in ? 1 : 0)};
            studio.send(play);
        }
        rec = true;
        studio.send({Type::SongRecord, 0, 0, 0, 1}); // the engine writes repeats and arp notes as they play
        start_take();
        return;
    }
    end_take();
    rec = rec_waiting = false;
    studio.send({Type::SongRecord, 0, 0, 0, 0});
    for (auto& h : held_notes) h.key = -1;
}

void Sequencer::start_take() {
    auto& p = pattern(take_track, take_number);
    take_free = p.is_free();
    if (!take_free) return;
    // Free length: the bar playing and the next, counted from the bar the take begins in.
    uint32_t bar = bar_ticks();
    uint32_t most = workflow.max_bars ? workflow.max_bars * bar : studio.project().max_length();
    p.length = melodee::free_grown(0, bar, most);
    if (studio.playing()) studio.send({Type::Rebase, uint8_t(take_track), uint8_t(take_number), 255, 0});
    sync(take_track, take_number);
}

void Sequencer::grow_take() {
    if (!take_free || !rec) return;
    auto& p = pattern(take_track, take_number);
    int position = studio.host().position_of(take_track);
    if (position < 0) return;
    uint32_t bar = bar_ticks();
    uint32_t most = workflow.max_bars ? workflow.max_bars * bar : studio.project().max_length();
    uint32_t want = melodee::free_grown(uint32_t(position), bar, most);
    if (want > p.length) { p.length = want; sync(take_track, take_number); }
}

void Sequencer::end_take() {
    if (!take_free) return;
    take_free = false;
    auto& p = pattern(take_track, take_number);
    if (!any_written || p.events.empty()) { // nothing taken: free again
        p.length = bar_ticks();
        p.free = true;
        sync(take_track, take_number);
        return;
    }
    int position = std::max(0, studio.host().position_of(take_track));
    uint32_t bar = bar_ticks();
    uint32_t most = workflow.max_bars ? workflow.max_bars * bar : studio.project().max_length();
    melodee::free_close(p, melodee::free_end(uint32_t(position), bar, bar / 8, most));
    sync(take_track, take_number);
    studio.say(std::to_string(seq::bars_of(p, bar)) + " BARS");
}

void Sequencer::write_note(int key, int velocity, int position, uint32_t ticks, bool early) {
    unsigned t = take_track;
    unsigned n = take_number;
    auto& p = pattern(t, n);
    if (position < 0 && !early) return;
    uint32_t heard = early ? 0 : melodee::wrap_tick(position, p.length);
    uint32_t at = early ? 0 : seq::record_tick(p, position, workflow.rec_quantize);
    bool ahead = early || (workflow.rec_quantize && melodee::still_ahead(heard, at, position, p.length));
    melodee::Event e;
    e.tick = at;
    e.length = uint16_t(p.q());
    e.key = uint8_t(key & 0xff);
    e.pitch = int8_t(key >> 8);
    e.velocity = uint8_t(std::clamp(velocity, 1, 127));
    if (!p.add(e)) { studio.say("PATTERN FULL"); return; }
    if (p.free) p.free = take_free; // a take on a fixed pattern keeps it fixed
    any_written = true;
    sync(t, n);
    if (ahead) { Command c {Type::Recorded, uint8_t(t), uint8_t(n), uint16_t(key)}; c.position = at; studio.send(c); }
    for (auto& h : held_notes)
        if (h.key < 0) { h = {key, t, n, at, ticks}; break; }
}

void Sequencer::release_note(int key, uint32_t ticks) {
    for (auto& h : held_notes) {
        if (h.key != key) continue;
        h.key = -1;
        auto& p = pattern(h.track, h.number);
        for (size_t at = p.lower(h.tick); at < p.events.size() && p.events[at].tick == h.tick; ++at)
            if (p.events[at].key == uint8_t(key & 0xff)) {
                p.events[at].length = uint16_t(std::clamp<int64_t>(int64_t(ticks) - int64_t(h.since), 1, 65535));
                sync(h.track, h.number);
                break;
            }
        return;
    }
}

void Sequencer::set_repeat(unsigned t, bool on, unsigned style, unsigned rate) {
    auto& track = studio.project().tracks[t % melodee::track_count];
    track.repeat = on && style == 0;
    track.arp = on && style > 0;
    track.repeat_rate = uint8_t(rate % melodee::arp_rates);
    track.arp_rate = track.repeat_rate;
    static const uint8_t modes[] {0, 0, 1, 3}; // the arp's Up, Down, Random (engine/arp.h ArpMode)
    if (style > 0) track.arp_mode = modes[std::min(style, 3u)];
    studio.send({Type::Repeat, uint8_t(t), 0, 2, int16_t(track.repeat_rate)});
    studio.send({Type::Repeat, uint8_t(t), 0, 0, int16_t(track.repeat)});
    studio.send({Type::Arp, uint8_t(t), 0, 1, int16_t(track.arp_mode)});
    studio.send({Type::Arp, uint8_t(t), 0, 2, int16_t(track.arp_rate)});
    studio.send({Type::Arp, uint8_t(t), 0, 0, int16_t(track.arp)});
    studio.pads_repeat(t == studio.track() ? (track.repeat ? 1 : track.arp ? 2 : 0) : 0);
    studio.changed();
}

// ---------------------------------------------------------------- Capture (Melodee's Recall)

bool Sequencer::can_capture() const { return ring->written > capture_since; }

void Sequencer::clear_capture() { capture_since = ring->written; }

bool Sequencer::capture() {
    if (!can_capture()) return false;
    // What was played since Capture was last cleared, alone.
    auto view = std::make_unique<melodee::CaptureRing>();
    for (uint32_t k = std::max(capture_since, ring->written > melodee::capture_size ? ring->written - melodee::capture_size : 0u); k < ring->written; ++k)
        view->notes[view->written++ % melodee::capture_size] = ring->notes[k % melodee::capture_size];
    melodee::CaptureAsk ask;
    ask.bar = bar_ticks();
    ask.grid = workflow.rec_quantize ? pattern().q() : 1;
    ask.tick_ms = 60000.0 / (studio.tempo() / 10.0) / melodee::ticks_per_quarter;
    ask.now = ms_now();
    ask.ticks = studio.host().ticks();
    auto window = melodee::capture_window(*view, ask);
    if (!window.any) return false;
    guesses.clear();
    if (!window.run) {
        // Played stopped (14.3): three tempos the phrase fits, as one, two or four bars.
        uint32_t first = UINT32_MAX, last = 0;
        for (unsigned k = 0; k < view->count(); ++k) {
            const auto& note = view->newest(k);
            if (note.run) continue;
            first = std::min(first, note.at);
            last = std::max(last, note.at + (note.length ? note.length : 1));
        }
        double phrase = std::max(1.0, double(last) - double(first));
        unsigned quarters = bar_ticks() / melodee::ticks_per_quarter;
        for (unsigned bars : {1u, 2u, 4u, 8u}) {
            double bpm = bars * quarters * 60000.0 / phrase;
            if (bpm >= 60 && bpm <= 200) guesses.push_back({unsigned(std::lround(bpm * 10)), bars});
        }
        std::sort(guesses.begin(), guesses.end(), [&](const TempoGuess& a, const TempoGuess& b) {
            return std::abs(int(a.tenths) - int(studio.tempo())) < std::abs(int(b.tenths) - int(studio.tempo()));
        });
        if (guesses.size() > 3) guesses.resize(3);
    }
    unsigned kept = 0, first_track = studio.track();
    int shown = -1;
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        if (!studio.project().tracks[t].used()) continue;
        Pattern laid;
        if (!melodee::lay_capture(*view, t, window, ask, laid)) continue;
        int to = pattern(t, number(t)).empty() ? int(number(t)) : next_empty(t, number(t));
        if (to < 0) continue;
        pattern(t, unsigned(to)) = laid;
        sync(t, unsigned(to));
        launch(t, unsigned(to));
        if (t == first_track) shown = to;
        ++kept;
    }
    if (!kept) return false;
    clear_capture();
    if (!window.run && !studio.playing()) {
        studio.play(); // Capture from stopped starts the transport (14.3)
        if (!guesses.empty()) {
            studio.set_tempo(guesses.front().tenths);
            studio.open("capture");
        }
    }
    captured = shown;
    if (window.run) captured_until = Clock::now() + std::chrono::milliseconds(1500); // playing: the notes kept, a moment
    else studio.say(shown >= 0 ? "CAPTURED: PATTERN " + std::to_string(shown + 1) : "CAPTURED");
    return true;
}

void Sequencer::take_guess(unsigned which) {
    if (which >= guesses.size()) return;
    studio.set_tempo(guesses[which].tenths);
    // The phrase's bars at that tempo: the pattern as long as the guess says.
    auto& p = pattern();
    seq::set_length(p, guesses[which].bars * bar_ticks(), studio.project().max_length());
    sync(studio.track(), number(studio.track()));
}

// ---------------------------------------------------------------- Melodee's SEQ pages

void Sequencer::half_seq() {
    auto& p = pattern();
    melodee::half_pattern(p);
    p.free = false;
    sync(studio.track(), number(studio.track()));
    studio.say(std::to_string(seq::bars_of(p, bar_ticks())) + " BARS");
}

void Sequencer::half_speed(bool slower) {
    auto& p = pattern();
    melodee::scale_pattern(p, slower);
    sync(studio.track(), number(studio.track()));
    studio.say(slower ? "HALF SPEED" : "DOUBLE SPEED");
}

int Sequencer::vary(unsigned variation, double amount) {
    unsigned t = studio.track(), n = number(t);
    auto v = melodee::Variation(variation % unsigned(melodee::Variation::Count));
    if (kit() && !melodee::variation_on_kit(v)) { studio.say("NOT ON A KIT"); return -1; }
    int to = next_empty(t, n);
    if (to < 0) { studio.say("NO EMPTY PATTERN"); return -1; }
    melodee::VaryPlace place;
    place.kit = kit();
    place.bar = bar_ticks();
    place.beat = studio.project().beat_ticks();
    place.root = studio.project().root % 12;
    place.degree_count = melodee::scale_degrees(studio.project().scale, place.degrees);
    static uint64_t seed = 0x5eed;
    pattern(t, unsigned(to)) = melodee::vary_pattern(pattern(), v, std::clamp(amount, 0.0, 1.0), ++seed, place);
    sync(t, unsigned(to));
    launch(t, unsigned(to));
    studio.say(std::string(melodee::variation_name(v)) + ": P" + std::to_string(to + 1));
    return to;
}

void Sequencer::set_division(int delta) {
    auto& p = pattern();
    int d = std::clamp(int(p.division) + delta, 0, int(melodee::Division::Count) - 1);
    p.division = melodee::Division(d);
    pages[studio.track()] = 0;
    sync(studio.track(), number(studio.track()));
}

void Sequencer::set_signature(int delta) {
    auto& project = studio.project();
    project.signature = uint8_t(std::clamp(int(project.signature) + delta, 0, int(melodee::time_signature_count) - 1));
    auto sig = project.time_signature();
    studio.send({Type::Signature, 0, 0, uint16_t(project.beat_ticks()), int16_t(project.bar_ticks())});
    studio.signature_to_modules(sig.beats, sig.unit);
    studio.changed();
}

void Sequencer::set_speed(int delta) {
    unsigned t = studio.track();
    auto& track = studio.project().tracks[t];
    track.speed = uint8_t(std::clamp(int(track.speed) + delta, 0, int(melodee::speed_count) - 1));
    studio.send({Type::TrackSpeed, uint8_t(t), 0, 0, int16_t(track.speed)});
    sync_track(t); // its patterns come scaled
}

void Sequencer::set_pattern_tempo(int delta) {
    auto& project = studio.project();
    unsigned n = number(studio.track());
    int now = project.pattern_tempo[n] ? int(project.pattern_tempo[n]) : int(project.tempo);
    int to = now + delta * 10;
    project.pattern_tempo[n] = uint16_t(to < 300 ? 0 : std::min(to, 3000));
    studio.send({Type::PatternTempo, 0, 0, uint16_t(n), int16_t(project.pattern_tempo[n])});
    studio.changed();
}

void Sequencer::set_seq_tempo(bool on) {
    studio.project().seq_tempo = on;
    studio.send({Type::SeqTempo, 0, 0, uint16_t(number(studio.track())), int16_t(on)});
    studio.changed();
}

void Sequencer::set_scene_tempo(unsigned n, int delta) {
    auto& scene = studio.project().scenes[n % melodee::pattern_count];
    int now = scene.tempo ? int(scene.tempo) : int(studio.project().tempo);
    int to = now + delta * 10;
    scene.tempo = uint16_t(to < 300 ? 0 : std::min(to, 3000));
    studio.changed();
}

void Sequencer::feel_all() {
    unsigned t = studio.track(), n = number(t);
    const auto feel = pattern().feel;
    unsigned laid = 0;
    for (unsigned k = 0; k < melodee::pattern_count; ++k) {
        if (k == n || pattern(t, k).empty()) continue;
        pattern(t, k).feel = feel;
        sync(t, k);
        ++laid;
    }
    studio.say("FEEL ON " + std::to_string(laid) + " MORE");
}

void Sequencer::set_groove_from(int delta) {
    unsigned t = studio.track(), n = number(t);
    auto& feel = pattern().feel;
    // The grooves to take: this track's other patterns with notes, then none.
    std::vector<uint16_t> from {0};
    for (unsigned k = 0; k < melodee::pattern_count; ++k)
        if (k != n && !pattern(t, k).empty()) from.push_back(melodee::groove_source(t, k));
    auto at = std::find(from.begin(), from.end(), feel.groove_from);
    int index = at == from.end() ? 0 : int(at - from.begin());
    index = std::clamp(index + delta, 0, int(from.size()) - 1);
    feel.groove_from = from[size_t(index)];
    feel.groove_offsets.clear();
    feel.groove_levels.clear();
    if (feel.groove_from) melodee::groove_of(pattern(melodee::groove_track(feel.groove_from), melodee::groove_pattern(feel.groove_from)), feel);
    sync(t, n);
}

void Sequencer::follow_column() {
    unsigned n = number(studio.track());
    const auto follow = pattern().follow;
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        if (!studio.project().tracks[t].used() || pattern(t, n).empty()) continue;
        pattern(t, n).follow = follow;
        sync(t, n);
    }
    studio.say("FOLLOW ON THE COLUMN");
}

void Sequencer::lay_euclid(unsigned hits, unsigned steps, unsigned rotate) {
    if (!kit()) return;
    auto& p = pattern();
    if (!melodee::lay_euclid(p, studio.current().step_note(), hits, steps, rotate)) { studio.say("PATTERN FULL"); return; }
    p.free = false;
    sync(studio.track(), number(studio.track()));
}

void Sequencer::set_lane_steps(int delta) {
    if (!kit()) return;
    auto& p = pattern();
    uint8_t key = studio.current().step_note();
    unsigned steps = p.own_lane(key) ? p.lane_length(key) / p.q() : p.steps();
    int to = std::clamp(int(steps) + delta, 1, int(p.steps()));
    p.set_lane_length(key, unsigned(to) >= p.steps() ? 0 : uint32_t(to) * p.q());
    sync(studio.track(), number(studio.track()));
}

std::vector<Sequencer::Lane> Sequencer::lanes_of(unsigned t) const {
    std::vector<Lane> lanes;
    const auto& track = studio.project().tracks[t % melodee::track_count];
    if (track.engine == melodee::EngineKind::Sampler) {
        using P = melodee::PadParam;
        const struct { P param; const char* name; } pad_lanes[] {{P::Pitch, "TUNE"}, {P::Level, "LEVEL"}, {P::Pan, "PAN"}, {P::FilterCutoff, "CUTOFF"},
                                                                  {P::FilterResonance, "RESO"}, {P::Attack, "ATTACK"}, {P::Decay, "DECAY"}};
        for (const auto& lane : pad_lanes) {
            auto range = melodee::pad_param_range(lane.param);
            lanes.push_back({melodee::motion_pad_param(lane.param), track.last_pad, range.min, range.max, lane.name});
        }
    }
    lanes.push_back({melodee::motion_level, 0, 0, 127, "LEVEL"});
    lanes.push_back({melodee::motion_pan, 0, -64, 63, "PAN"});
    return lanes;
}

void Sequencer::send_modulator(unsigned n) {
    n %= melodee::modulator_count;
    auto& m = studio.project().modulators[n];
    studio.send(melodee::pack_modulator(n, m));
    for (unsigned i = 0; i < melodee::modulator_targets; ++i) {
        auto target = m.targets[i];
        if (target.used()) target.own = int16_t(melodee::lane_own(studio.project(), target.track, target.param, target.pad));
        studio.send(melodee::pack_mod_target(n, i, target));
    }
    studio.changed();
}

void Sequencer::turn_macro(unsigned n, int delta) {
    auto& macro = studio.project().macros[n % melodee::macro_count];
    macro.value = uint8_t(std::clamp(int(macro.value) + delta, 0, 127));
    for (const auto& target : macro.targets)
        if (target.used()) set_lane(target.track, target.param, target.pad, melodee::Macro::at(target, macro.value));
    studio.changed();
}

void Sequencer::entry_rest() {
    caret = std::min<unsigned>(caret + entry_length, pattern().steps() - 1 + entry_length);
}

void Sequencer::entry_erase() {
    auto& p = pattern();
    if (caret == 0) return;
    caret = caret >= entry_length ? caret - entry_length : 0;
    seq::clear_span(p, seq::step_span(p, caret), -1);
    sync(studio.track(), number(studio.track()));
}

void Sequencer::slices_to_pattern(unsigned first_pad, const std::vector<uint32_t>& starts, uint32_t frames) {
    auto& p = pattern();
    if (!frames || starts.empty()) return;
    for (size_t k = 0; k < starts.size(); ++k) seq::clear_lane(p, uint8_t(melodee::sampler_first_note + first_pad + k));
    for (size_t k = 0; k < starts.size(); ++k) {
        uint32_t next = k + 1 < starts.size() ? starts[k + 1] : frames;
        melodee::Event e;
        e.tick = uint32_t(uint64_t(starts[k]) * p.length / frames);
        e.length = uint16_t(std::clamp<uint64_t>(uint64_t(next - std::min(next, starts[k])) * p.length / frames, 1, 65535));
        e.key = uint8_t(melodee::sampler_first_note + first_pad + k);
        p.add(e);
    }
    p.free = false;
    sync(studio.track(), number(studio.track()));
}

void Sequencer::move_playhead(int steps) {
    auto& p = pattern();
    unsigned t = studio.track();
    if (studio.playing()) {
        int position = std::max(0, studio.host().position_of(t));
        int64_t to = (int64_t(position) / p.q() + steps) * p.q();
        to = ((to % int64_t(p.length)) + int64_t(p.length)) % int64_t(p.length);
        Command c {Type::Locate};
        c.position = uint32_t(to);
        studio.send(c);
        return;
    }
    int64_t to = int64_t(play_from) + int64_t(steps) * p.q();
    play_from = uint32_t(std::clamp<int64_t>(to, 0, int64_t(p.length) - 1));
}

uint32_t Sequencer::take_play_from() {
    uint32_t from = play_from;
    play_from = 0;
    return from;
}

// ---------------------------------------------------------------- what comes in

void Sequencer::input(const move::SurfaceEvent& event) {
    using Kind = move::Input::Kind;
    const auto& in = event.input;
    if (in.kind == Kind::Step) {
        if (in.pressed) {
            if (std::find(steps_down.begin(), steps_down.end(), unsigned(in.index)) == steps_down.end()) steps_down.push_back(in.index);
        } else {
            steps_down.erase(std::remove(steps_down.begin(), steps_down.end(), unsigned(in.index)), steps_down.end());
            if (steps_down.empty()) { trig_page = false; knob_shown = -1; }
        }
        return;
    }
    if (in.kind == Kind::Button) {
        auto button = move::Button(in.index);
        if (button == move::Button::Copy) {
            copy_down = in.pressed;
            if (!in.pressed) copy_from = copy_to = -1;
        }
        if (button == move::Button::Loop && !in.pressed) fill(false);
        if (button == move::Button::Menu && !in.pressed && studio.top() && studio.top()->id() == "session" && peek_session) {
            peek_session = false;
            studio.close();
        }
        return;
    }
    if (in.kind != Kind::Pad || in.index >= move::pad_count) return;
    const auto& stamp = studio.stamp(in.index);
    if (in.pressed) {
        if (!stamp.sounded.load(std::memory_order_acquire)) { pad_keys[in.index] = -1; return; }
        int key = stamp.key.load(std::memory_order_relaxed), t = stamp.track.load(std::memory_order_relaxed);
        int velocity = stamp.velocity.load(std::memory_order_relaxed), position = stamp.position.load(std::memory_order_relaxed);
        uint32_t ticks = stamp.ticks.load(std::memory_order_relaxed);
        pad_keys[in.index] = key;
        pad_tracks[in.index] = t;
        if (t == int(studio.track()) && !kit()) {
            if (std::find(keys_down.begin(), keys_down.end(), uint8_t(key)) == keys_down.end()) keys_down.push_back(uint8_t(key));
            studio.current().last_note = uint8_t(key & 0x7f);
        }
        ring->press(unsigned(t), key, velocity, ms_now(), studio.playing() ? run : 0, ticks);
        if (entry && t == int(studio.track())) { // step entry (Melodee's Step Edit PLAY): written at the caret
            auto& p = pattern();
            melodee::Event e;
            e.tick = seq::step_tick(p, caret);
            e.length = uint16_t(std::min<uint32_t>(entry_length * p.q(), 65535));
            e.key = uint8_t(key & 0xff);
            e.pitch = int8_t(key >> 8);
            e.velocity = uint8_t(entry_velocity ? entry_velocity : std::clamp(velocity, 1, 127));
            e.chance = entry_chance;
            if (e.tick >= p.length) seq::set_length(p, (e.tick / bar_ticks() + 1) * bar_ticks(), studio.project().max_length());
            p.add(e);
            p.free = false;
            entry_written = true;
            sync(studio.track(), number(studio.track()));
            return;
        }
        const auto& track = studio.project().tracks[unsigned(t) % melodee::track_count];
        bool repeats = track.repeat || track.arp; // the engine writes what it plays
        if (rec && !repeats && unsigned(t) == take_track && studio.playing()) {
            bool early = studio.host().count_in() != 0;
            if (early && studio.host().count_in_ticks() > melodee::sixteenth_ticks) return; // counting in: not yet
            write_note(key, velocity, position, ticks, early);
        }
        return;
    }
    int key = pad_keys[in.index], t = pad_tracks[in.index];
    pad_keys[in.index] = -1;
    if (key < 0) return;
    keys_down.erase(std::remove(keys_down.begin(), keys_down.end(), uint8_t(key)), keys_down.end());
    if (entry && entry_written && std::all_of(pad_keys.begin(), pad_keys.end(), [](int k) { return k < 0; })) {
        entry_written = false; // the chord let go: the caret moves on
        caret += entry_length;
    }
    ring->release(unsigned(t), key, ms_now(), studio.host().ticks());
    if (rec) release_note(key, studio.host().ticks());
}

// ---------------------------------------------------------------- every lights() call

void Sequencer::tick(Clock::time_point now) {
    (void)now;
    bool playing = studio.playing();
    if (playing != was_playing) {
        // The transport started or stopped: Capture starts afresh (14.3), Seq Record ends with Stop (14.1).
        clear_capture();
        if (playing) { ++run; follow = true; }
        else {
            if (rec) record(false);
            fill(false);
        }
        was_playing = playing;
    }
    if (studio.track() != last_track) {
        // Track select clears Capture (14.3); the pads repeat on the track they play.
        last_track = studio.track();
        const auto& track = studio.current();
        studio.pads_repeat(track.repeat ? 1 : track.arp ? 2 : 0);
        keys_down.clear();
    }
    grow_take();
    // Repeats and arp notes the engine played while recording, written (Melodee's take_arp_notes).
    melodee::EngineHost::ArpWritten written;
    while (studio.host().take_arp_written(written)) {
        if (!rec) continue;
        auto& p = pattern(written.track, written.pattern);
        melodee::Event e;
        e.tick = written.tick % std::max<uint32_t>(p.length, 1);
        e.length = written.length;
        e.key = uint8_t(written.key & 0xff);
        e.pitch = int8_t(written.key >> 8);
        e.velocity = written.velocity;
        if (p.add(e)) { any_written = true; sync(written.track, written.pattern); }
    }
    // The metronome on Rec only (Workflow): on while recording.
    bool click = studio.click() && (!workflow.click_rec_only || rec);
    if (click != click_now) {
        click_now = click;
        studio.send({Type::Click, 0, 0, uint16_t(studio.click_level()), int16_t(click)});
    }
    // Pattern chains (Melodee's CHAN): the next queued in the last bar of the one playing.
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        auto& track = studio.project().tracks[t];
        if (!track.chained() || !playing) continue;
        unsigned now_playing = studio.host().pattern_of(t);
        uint8_t next = track.chain_ahead(1);
        if (now_playing == track.chain_ahead(1) && now_playing != track.chain_ahead(0)) track.chain_at = uint8_t((track.chain_at + 1) % track.chain_count);
        const auto& p = pattern(t, now_playing);
        if (melodee::chain_due(studio.host().position_of(t), p.length, bar_ticks()) && track.pattern != next) {
            track.pattern = next;
            studio.send({Type::Pattern, uint8_t(t), 0, 0, int16_t(next)});
        }
    }
    // Loop Mode's loop from a later bar: the editor's loop for the pattern playing.
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        unsigned n = studio.host().pattern_of(t);
        unsigned packed = loop_from(t, n);
        int want = int(packed);
        if (loop_sent[t] == want) continue;
        loop_sent[t] = want;
        uint32_t bar = bar_ticks(), from = (packed & 0xffff) * bar, to = (packed >> 16) * bar;
        uint32_t span = packed ? to - std::min(to, from) : 0;
        Command c {Type::EditLoop, uint8_t(t), 0, uint16_t(span & 0xffff), int16_t(uint16_t(span >> 16))};
        c.position = packed ? from : 0;
        studio.send(c);
    }
}

// ---------------------------------------------------------------- lights

void Sequencer::transport_lights(move::Lights& lights, Clock::time_point now) {
    using namespace move;
    bool blink = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() / 250 % 2;
    // Rec (14.1): dim when it can record, flashing through the count-in, red recording.
    lights.button(Button::Rec, counting_in() ? (blink ? colour::red : colour::off) : rec ? colour::red : colour::dark_grey);
    lights.button(Button::Capture, can_capture() ? white_bright : 0);
}

void Sequencer::lights(move::Lights& lights, Clock::time_point now) {
    using namespace move;
    lights_steps(lights, now);
    lights.button(Button::Loop, white_dim);
    lights.button(Button::Menu, white_dim);
    lights.button(Button::Copy, white_dim);
    lights.button(Button::Delete, white_dim);
    auto& p = pattern();
    unsigned pages_used = std::max(1u, (p.length + p.q() * page_steps() - 1) / (p.q() * page_steps()));
    lights.button(Button::Left, page() > 0 ? white_dim : 0);
    lights.button(Button::Right, page() + 1 <= pages_used ? white_dim : 0);
}

void Sequencer::lights_steps(move::Lights& lights, Clock::time_point) {
    using namespace move;
    auto& p = pattern();
    unsigned steps = page_steps(), first = page() * steps;
    int lane = lane_key();
    auto colour = ui::track_colour(studio.track());
    int position = studio.playing() ? studio.host().position_of(studio.track()) : -1;
    int playing_step = position >= 0 ? int(unsigned(position) / std::max(1u, p.q())) : -1;
    // A held step's locks: red from it to where they end (14.2.4).
    uint32_t lock_from = 0, lock_to = 0;
    if (held_step() >= 0) {
        auto span = seq::step_span(p, unsigned(held_step()));
        for (size_t k = p.motion_lower(span.from); k < p.motion.size() && p.motion[k].tick < span.to; ++k)
            if (p.motion[k].lock()) { lock_from = p.motion[k].tick; lock_to = std::max(lock_to, p.motion[k].tick + p.motion[k].hold); }
    }
    // Notes held over later steps: those steps brighter (9.5).
    for (unsigned s = 0; s < step_count; ++s) {
        if (s >= steps) { lights.step(s, colour::off); continue; }
        unsigned step = first + s;
        auto span = seq::step_span(p, step);
        uint32_t at = seq::step_tick(p, step);
        bool notes = seq::any_in(p, span, lane);
        bool covered = false;
        if (!notes)
            for (size_t k = 0; k < p.events.size() && p.events[k].tick < at; ++k)
                if ((lane < 0 || p.events[k].key == unsigned(lane)) && p.events[k].tick + p.events[k].length > at) { covered = true; break; }
        uint8_t shown = at >= p.length ? (notes ? colour::light_grey : colour::dark_grey) : notes ? colour::white : covered ? colour.bright : colour.dim;
        if (lock_to > lock_from && at >= lock_from && at < lock_to) shown = colour::red;
        if (int(step) == playing_step) shown = colour::green;
        lights.step(s, shown);
    }
}

// ---------------------------------------------------------------- the screen

bool Sequencer::draw(juce::Graphics& g) {
    auto now = Clock::now();
    if (held_step() < 0) {
        auto& p = pattern();
        uint32_t bar = bar_ticks();
        std::string number = std::to_string(this->number(studio.track()) + 1);
        if (now < length_until) { // Loop held + the wheel (12.1)
            unsigned bars = seq::bars_of(p, bar);
            uint32_t rest = p.length % bar;
            std::string value = std::to_string(p.length / bar) + (rest ? "." + std::to_string(rest / melodee::sixteenth_ticks) : "") + (bars == 1 && !rest ? " BAR" : " BARS");
            ui::rail(g, "LOOP", std::to_string(bars), "BARS");
            ui::big_parameter(g, ui::content(4, 44), "PATTERN " + number + " LENGTH", value, float(p.length) / float(16 * bar));
            ui::detail(g, "SHIFT: 1/16S");
            return true;
        }
        if (counting_in()) { // Rec from stopped (14.1.1): the count-in's beat
            unsigned beats = std::max(1u, bar / std::max(1u, studio.project().beat_ticks()));
            ui::rail(g, "REC", "", "CNT");
            g.setColour(juce::Colours::black);
            ui::dot(g, 12, 32, 6);
            g.setColour(juce::Colours::white);
            ui::seq::count_in(g, ui::content(0, 52), std::max(1u, studio.host().count_in()), beats);
            ui::detail(g, "PATTERN " + number, "1 BAR");
            return true;
        }
        if (rec && take_free && studio.track() == take_track) { // a free take growing
            ui::rail(g, "REC", std::to_string(seq::bars_of(p, bar)), "BARS");
            ui::seq::title(g, "PATTERN " + number, "FREE");
            ui::seq::Bars bars;
            bars.count = seq::bars_of(p, bar);
            bars.loop_to = bars.count;
            bars.selected = -1;
            int position = studio.host().position_of(studio.track());
            bars.playing = position < 0 ? -1 : int(unsigned(position) / bar);
            for (unsigned k = 0; k < 16; ++k) bars.notes[k] = uint8_t(std::min<size_t>(seq::notes_in(p, seq::bar_span(k, bar)).size(), 255));
            ui::seq::bar_blocks(g, ui::content(13, 38), bars);
            unsigned ticks = position < 0 ? 0 : unsigned(position);
            ui::detail(g, "REC: ENDS BAR", std::to_string(ticks / bar + 1) + "." + std::to_string(ticks % bar / std::max(1u, studio.project().beat_ticks()) + 1));
            return true;
        }
        if (now < captured_until && captured >= 0) { // Capture while playing (14.3): the notes kept
            const auto& kept = pattern(studio.track(), unsigned(captured));
            std::vector<ui::seq::RollNote> notes;
            int low = 127, high = 0;
            for (const auto& e : kept.events) { notes.push_back({e.tick, e.length, e.key, false}); low = std::min(low, int(e.key)); high = std::max(high, int(e.key)); }
            if (low > high) { low = 36; high = 47; }
            if (high - low < 11) high = low + 11;
            ui::rail(g, "CAP", std::to_string(seq::bars_of(kept, bar)), "BARS");
            ui::seq::roll(g, ui::content(1, 50), notes, 0, kept.length, low, high, -1, -1, bar);
            ui::detail(g, "TO PATTERN " + std::to_string(captured + 1), std::to_string(kept.events.size()));
            return true;
        }
        return false;
    }
    auto& p = pattern();
    unsigned step = unsigned(held_step());
    auto span = seq::step_span(p, step);
    int lane = lane_key();
    auto held = seq::held(p, span, lane, seq::step_tick(p, step));
    std::string bar = "B" + std::to_string(seq::step_tick(p, step) / bar_ticks() + 1);
    std::string number = std::to_string(step % page_steps() + 1);
    std::string what = kit() ? screens_pad_name() : std::string();
    if (!kit()) {
        for (uint8_t key : held.keys) {
            static const char* names[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            what += std::string(what.empty() ? "" : " ") + names[key % 12] + std::to_string(key / 12 - 2);
        }
        if (what.empty()) what = "NOTE";
    }
    if (knob_shown >= 0 && Clock::now() < knob_until && !trig_page) {
        Lane lane_of;
        if (knob_lane(unsigned(knob_shown), lane_of)) {
            int value = 0;
            if (!seq::lock_of(p, span, lane_of.param, lane_of.pad, value)) value = melodee::lane_own(studio.project(), studio.track(), lane_of.param, lane_of.pad);
            ui::rail(g, "LOCK", number, kit() ? what.substr(0, 3) : bar);
            float fraction = float(value - lane_of.low) / float(std::max(1, lane_of.high - lane_of.low));
            ui::big_parameter(g, ui::content(4, 44), lane_of.name + " ON STEP " + number, number_text(value), fraction);
            ui::detail(g, "DELETE+TOUCH: CLR");
            return true;
        }
    }
    if (trig_page) {
        ui::rail(g, "TRIG", number, bar);
        ui::seq::title(g, what, "TRIG");
        std::array<std::string, 8> names {"CHN", "CND", "RTC", "RMP", "PIT", "ACC", "SLD", "TIE"}, values;
        char text[16];
        for (unsigned k = 0; k < 8; ++k) values[k] = held.any ? seq::trig_value(held, seq::Trig(k), text, sizeof text) : "-";
        if (!kit()) names[4] = "";
        ui::seq::value_cells(g, ui::content(11, 40), names, values, now < knob_until ? knob_shown : -1);
        ui::detail(g, "PUSH: LOCKS");
        return true;
    }
    ui::rail(g, "STEP", number, bar);
    ui::seq::StepCard card;
    card.what = what;
    card.empty = !held.any;
    if (held.any) {
        char text[16];
        card.velocity = held.velocity;
        std::snprintf(text, sizeof text, "%.2f", double(held.length) / double(p.q()));
        card.length = text;
        std::snprintf(text, sizeof text, "%+.2f", double(held.offset) / double(p.q()));
        card.nudge = text[1] == '0' ? std::string(1, text[0]) + (text + 2) : text; // "+.10"
        if (held.flags & melodee::step_accent) card.marks.push_back("ACC");
        if (held.condition) card.marks.push_back(melodee::trig_name(held.condition));
        if (held.ratchet > 1) card.marks.push_back("R" + std::to_string(held.ratchet));
        if (held.chance < 100) card.marks.push_back(std::to_string(held.chance) + "%");
        if (held.flags & melodee::step_slide) card.marks.push_back("SLD");
        if (held.flags & melodee::step_tie) card.marks.push_back("TIE");
    }
    ui::seq::step_card(g, ui::content(0, 52), card);
    unsigned locks = seq::locks_in(p, span);
    ui::detail(g, held.any ? "PUSH: TRIG" : "WHEEL: LENGTH", locks ? std::to_string(locks) + " LCK" : "");
    return true;
}

std::string Sequencer::screens_pad_name() const {
    unsigned pad = studio.current().last_pad;
    char label[8];
    std::snprintf(label, sizeof label, "%c%02u", 'A' + int(pad / 16), pad % 16 + 1);
    std::string name = label;
    if (const Sample* sample = studio.sample_of_pad()) {
        std::string file = sample->name.substr(0, sample->name.rfind('.'));
        for (auto& c : file) c = char(std::toupper(static_cast<unsigned char>(c)));
        name += " " + file;
    }
    return name;
}

// ---------------------------------------------------------------- settings and status

std::string Sequencer::settings_text() const {
    std::ostringstream out;
    out << "seq_quantize " << workflow.quantize << '\n' << "seq_count_in " << int(workflow.count_in) << '\n'
        << "seq_rec_quantize " << int(workflow.rec_quantize) << '\n' << "seq_max_bars " << workflow.max_bars << '\n'
        << "seq_click_rec " << int(workflow.click_rec_only) << '\n';
    for (const auto& [key, packed] : loop_starts) out << "seq_loop " << key << ' ' << packed << '\n';
    return out.str();
}

bool Sequencer::setting_line(const std::string& line) {
    std::istringstream fields(line);
    std::string word;
    unsigned value = 0, more = 0;
    if (!(fields >> word >> value) || word.rfind("seq_", 0) != 0) return false;
    if (word == "seq_quantize") workflow.quantize = std::min(value, 100u);
    else if (word == "seq_count_in") workflow.count_in = value != 0;
    else if (word == "seq_rec_quantize") workflow.rec_quantize = value != 0;
    else if (word == "seq_max_bars") workflow.max_bars = std::min(value, melodee::max_bars);
    else if (word == "seq_click_rec") workflow.click_rec_only = value != 0;
    else if (word == "seq_loop" && (fields >> more) && value < melodee::track_count * melodee::pattern_count && (more & 0xffff))
        loop_starts[value] = more;
    return true;
}

std::string Sequencer::status() const {
    const auto& p = const_cast<Sequencer*>(this)->pattern();
    char text[256];
    std::snprintf(text, sizeof text, " seq_pattern=%u seq_bars=%u seq_notes=%zu seq_page=%u rec=%d count_in=%d capture=%u held_step=%d fill=%d click_heard=%d",
                  number(studio.track()) + 1, seq::bars_of(p, bar_ticks()), p.events.size(), page() + 1, int(rec), int(counting_in()),
                  unsigned(ring->written - capture_since), held_step(), int(fill_on), int(click_now));
    return text;
}

} // namespace app
