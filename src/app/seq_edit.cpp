// SPDX-License-Identifier: AGPL-3.0-only
#include "app/seq_edit.h"
#include "melodee/model/note_edits.h"
#include "melodee/model/trig.h"
#include "melodee/shell/record_place.h"
#include <algorithm>
#include <cstdio>

namespace app::seq {
namespace {

using melodee::Event;
using melodee::Pattern;

bool on_lane(const Event& e, int lane) { return lane < 0 || e.key == unsigned(lane); }

// Removes the events at `at` (indexes in order).
void remove_at(Pattern& p, const std::vector<size_t>& at) {
    for (size_t n = at.size(); n-- > 0;) p.events.erase(p.events.begin() + std::ptrdiff_t(at[n]));
}

template <class Change> void each(Pattern& p, Span span, int lane, Change&& change) {
    for (size_t n : notes_in(p, span, lane)) change(p.events[n]);
}

} // namespace

unsigned page_steps(const Pattern& p) { return melodee::division_triplet(p.division) ? 12 : 16; }

Span step_span(const Pattern& p, unsigned step) {
    uint32_t q = p.q(), at = step * q, half = q / 2;
    return {at >= half ? at - half : 0, at + (q - half)};
}

uint32_t step_tick(const Pattern& p, unsigned step) { return step * p.q(); }

Span bar_span(unsigned bar, uint32_t bar_ticks) { return {bar * bar_ticks, (bar + 1) * bar_ticks}; }

std::vector<size_t> notes_in(const Pattern& p, Span span, int lane) {
    std::vector<size_t> found;
    for (size_t n = p.lower(span.from); n < p.events.size() && p.events[n].tick < span.to; ++n)
        if (on_lane(p.events[n], lane)) found.push_back(n);
    return found;
}

bool any_in(const Pattern& p, Span span, int lane) {
    for (size_t n = p.lower(span.from); n < p.events.size() && p.events[n].tick < span.to; ++n)
        if (on_lane(p.events[n], lane)) return true;
    return false;
}

bool toggle_step(Pattern& p, unsigned step, int lane, const std::vector<uint8_t>& keys, uint8_t velocity) {
    Span span = step_span(p, step);
    if (any_in(p, span, lane)) {
        remove_at(p, notes_in(p, span, lane));
        return false;
    }
    bool added = false;
    for (uint8_t key : keys) {
        Event e;
        e.tick = step_tick(p, step);
        e.length = uint16_t(p.q());
        e.key = key;
        e.velocity = std::clamp<uint8_t>(velocity, 1, 127);
        added = p.add(e) || added;
    }
    return added;
}

bool toggle_key(Pattern& p, unsigned step, uint8_t key, uint8_t velocity) {
    Span span = step_span(p, step);
    if (any_in(p, span, key)) {
        remove_at(p, notes_in(p, span, key));
        return false;
    }
    // A chord's new note takes the step's length, as the Move adds it beside the others.
    auto first = notes_in(p, span, -1);
    Event e;
    e.tick = first.empty() ? step_tick(p, step) : p.events[first.front()].tick;
    e.length = first.empty() ? uint16_t(p.q()) : p.events[first.front()].length;
    e.key = key;
    e.velocity = std::clamp<uint8_t>(velocity, 1, 127);
    return p.add(e);
}

bool toggle_key_every_step(Pattern& p, Span span, uint8_t key, uint8_t velocity) {
    uint32_t q = p.q();
    unsigned first = (span.from + q - 1) / q, last = span.to ? (span.to - 1) / q : 0;
    bool every = true;
    for (unsigned s = first; s <= last && s * q < span.to; ++s)
        if (!any_in(p, step_span(p, s), key)) { every = false; break; }
    bool added = false;
    for (unsigned s = first; s <= last && s * q < span.to; ++s) {
        Span at = step_span(p, s);
        if (every) remove_at(p, notes_in(p, at, key));
        else if (!any_in(p, at, key)) added = toggle_key(p, s, key, velocity) || added;
    }
    return added;
}

void add_velocity(Pattern& p, Span span, int lane, int delta) {
    each(p, span, lane, [&](Event& e) { e.velocity = uint8_t(std::clamp(int(e.velocity) + delta, 1, 127)); });
}

void add_length(Pattern& p, Span span, int lane, int ticks) {
    each(p, span, lane, [&](Event& e) { e.length = uint16_t(std::clamp(int(e.length) + ticks, 1, 65535)); });
}

void length_to(Pattern& p, Span span, int lane, uint32_t end) {
    each(p, span, lane, [&](Event& e) { e.length = uint16_t(std::clamp<int64_t>(int64_t(end) - int64_t(e.tick), 1, 65535)); });
}

void transpose(Pattern& p, Span span, int lane, int semitones, bool kit) {
    each(p, span, lane, [&](Event& e) {
        if (kit) e.pitch = int8_t(std::clamp(int(e.pitch) + semitones, -melodee::max_pitch_ramp, melodee::max_pitch_ramp));
        else e.key = uint8_t(std::clamp(int(e.key) + semitones, 0, 127));
    });
    p.sort();
}

void nudge(Pattern& p, Span span, int lane, int ticks) {
    each(p, span, lane, [&](Event& e) { e.tick = uint32_t(std::max<int64_t>(0, int64_t(e.tick) + ticks)); });
    p.sort();
}

Held held(const Pattern& p, Span span, int lane, uint32_t at) {
    Held h;
    for (size_t n : notes_in(p, span, lane)) {
        const Event& e = p.events[n];
        if (!h.any) {
            h.any = true;
            h.velocity = e.velocity;
            h.length = e.length;
            h.offset = int(int64_t(e.tick) - int64_t(at));
            h.chance = e.chance;
            h.condition = e.condition;
            h.ratchet = e.ratchet;
            h.flags = e.flags;
            h.ramp = e.ramp;
            h.pitch_ramp = e.pitch_ramp;
        }
        h.length = std::max<uint32_t>(h.length, e.length);
        h.keys.push_back(e.key);
        ++h.count;
    }
    return h;
}

void set_trig(Pattern& p, Span span, int lane, Trig which, int delta) {
    each(p, span, lane, [&](Event& e) {
        auto flag = [&](uint8_t bit) { e.flags = uint8_t(delta > 0 ? e.flags | bit : e.flags & ~bit); };
        switch (which) {
        case Trig::Chance: e.chance = uint8_t(std::clamp(int(e.chance) + delta, 0, 100)); break;
        case Trig::Condition: e.condition = uint8_t(std::clamp(int(e.condition) + delta, 0, int(melodee::trig_count) - 1)); break;
        case Trig::Ratchet: e.ratchet = uint8_t(std::clamp(int(e.ratchet) + delta, 1, int(melodee::max_ratchet))); break;
        case Trig::Ramp: e.ramp = int8_t(std::clamp(int(e.ramp) + delta * 5, -100, 100)); break;
        case Trig::PitchRamp: e.pitch_ramp = int8_t(std::clamp(int(e.pitch_ramp) + delta, -melodee::max_pitch_ramp, melodee::max_pitch_ramp)); break;
        case Trig::Accent: flag(melodee::step_accent); break;
        case Trig::Slide: flag(melodee::step_slide); break;
        case Trig::Tie: flag(melodee::step_tie); break;
        case Trig::Count: break;
        }
    });
}

const char* trig_value(const Held& h, Trig which, char* text, size_t size) {
    switch (which) {
    case Trig::Chance: std::snprintf(text, size, "%u%%", unsigned(h.chance)); break;
    case Trig::Condition: std::snprintf(text, size, "%s", h.condition ? melodee::trig_name(h.condition) : "-"); break;
    case Trig::Ratchet: std::snprintf(text, size, "%u", unsigned(h.ratchet)); break;
    case Trig::Ramp: std::snprintf(text, size, "%+d", int(h.ramp)); break;
    case Trig::PitchRamp: std::snprintf(text, size, "%+d", int(h.pitch_ramp)); break;
    case Trig::Accent: std::snprintf(text, size, "%s", h.flags & melodee::step_accent ? "ON" : "-"); break;
    case Trig::Slide: std::snprintf(text, size, "%s", h.flags & melodee::step_slide ? "ON" : "-"); break;
    case Trig::Tie: std::snprintf(text, size, "%s", h.flags & melodee::step_tie ? "ON" : "-"); break;
    case Trig::Count: std::snprintf(text, size, "-"); break;
    }
    return text;
}

void copy_span(Pattern& p, Span from, Span to, int lane) {
    std::vector<Event> notes;
    for (size_t n : notes_in(p, from, lane)) notes.push_back(p.events[n]);
    remove_at(p, notes_in(p, to, lane));
    int64_t shift = int64_t(to.from) - int64_t(from.from);
    for (auto e : notes) {
        e.tick = uint32_t(std::max<int64_t>(0, int64_t(e.tick) + shift));
        p.add(e);
    }
}

void clear_span(Pattern& p, Span span, int lane) { remove_at(p, notes_in(p, span, lane)); }

bool clear_lane(Pattern& p, uint8_t key) {
    size_t before = p.events.size();
    p.events.erase(std::remove_if(p.events.begin(), p.events.end(), [&](const Event& e) { return e.key == key; }), p.events.end());
    return p.events.size() != before;
}

void quantize(Pattern& p, int lane, unsigned percent) {
    std::vector<size_t> at;
    for (size_t n = 0; n < p.events.size(); ++n)
        if (on_lane(p.events[n], lane) && p.events[n].tick < p.length) at.push_back(n);
    if (at.empty()) return;
    auto notes = melodee::take_events(p, at);
    melodee::quantize_events(notes, p.q(), p.length, std::min(percent, 100u));
    melodee::put_events(p, notes);
}

void lock(Pattern& p, Span span, int lane, unsigned param, unsigned pad, int value) {
    uint32_t longest = 0, at = span.from + (span.to - span.from) / 2;
    bool first = true;
    for (size_t n : notes_in(p, span, lane)) {
        if (first) { at = p.events[n].tick; first = false; }
        longest = std::max<uint32_t>(longest, p.events[n].length);
    }
    melodee::MotionPoint m;
    m.tick = at;
    m.param = uint8_t(param);
    m.pad = uint8_t(pad);
    m.value = int16_t(value);
    m.hold = uint16_t(std::clamp<uint32_t>(std::max(longest, p.q()), 1, 65535));
    // One lock a lane a step: an earlier one on the step (the note moved since) goes.
    p.motion.erase(std::remove_if(p.motion.begin(), p.motion.end(),
                                  [&](const melodee::MotionPoint& o) { return o.lock() && o.on(param, pad) && span.holds(o.tick) && o.tick != at; }),
                   p.motion.end());
    p.write_motion(m);
}

bool lock_of(const Pattern& p, Span span, unsigned param, unsigned pad, int& value) {
    for (size_t n = p.motion_lower(span.from); n < p.motion.size() && p.motion[n].tick < span.to; ++n)
        if (p.motion[n].lock() && p.motion[n].on(param, pad)) { value = p.motion[n].value; return true; }
    return false;
}

unsigned locks_in(const Pattern& p, Span span) {
    unsigned count = 0;
    for (size_t n = p.motion_lower(span.from); n < p.motion.size() && p.motion[n].tick < span.to; ++n) count += p.motion[n].lock();
    return count;
}

uint32_t record_tick(const Pattern& p, int position, bool quantize) {
    uint32_t at = melodee::wrap_tick(position, p.length);
    return quantize ? melodee::quantise_tick(p, at) : at;
}

unsigned bars_of(const Pattern& p, uint32_t bar_ticks) {
    bar_ticks = std::max<uint32_t>(bar_ticks, 1);
    return std::max(1u, unsigned((p.length + bar_ticks - 1) / bar_ticks));
}

void set_length(Pattern& p, uint32_t ticks, uint32_t most) {
    p.length = std::clamp<uint32_t>(ticks, melodee::sixteenth_ticks, std::max<uint32_t>(most, melodee::sixteenth_ticks));
    p.free = false;
    p.fit_lanes();
}

} // namespace app::seq
