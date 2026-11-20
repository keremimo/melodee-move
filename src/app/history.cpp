// SPDX-License-Identifier: AGPL-3.0-only
#include "app/history.h"
#include <algorithm>
#include <cstring>

namespace app {
namespace {

using melodee::Project;

struct Layout {
    size_t samples = 0, chops = 0, tracks = 0, tempo = 0, swing = 0;
};
const Layout& layout() {
    static const Layout at = [] {
        auto p = std::make_unique<Project>();
        auto offset = [&](const void* field) { return size_t(static_cast<const char*>(field) - reinterpret_cast<const char*>(p.get())); };
        Layout l;
        l.samples = offset(&p->samples);
        l.chops = offset(&p->chops);
        l.tracks = offset(&p->tracks);
        l.tempo = offset(&p->tempo);
        l.swing = offset(&p->swing);
        return l;
    }();
    return at;
}

uint8_t* bytes_of(Project& p) { return reinterpret_cast<uint8_t*>(&p); }

// The ranges of [from, to) where `now` differs from `base`: aligned to eight
// bytes, a run going on until 32 bytes in a row are the same.
void diff_bytes(const uint8_t* base, const uint8_t* now, size_t from, size_t to, std::vector<Step::Bytes>& out) {
    size_t at = from;
    while (at < to) {
        size_t block = std::min<size_t>(64, to - at);
        if (std::memcmp(base + at, now + at, block) == 0) { at += block; continue; }
        size_t start = at;
        while (start < to && base[start] == now[start]) ++start;
        if (start >= to) break;
        start = std::max(from, start & ~size_t(7));
        size_t last = start, same = 0;
        for (size_t n = start; n < to; ++n) {
            if (base[n] != now[n]) { last = n; same = 0; }
            else if (++same >= 32) break;
        }
        size_t end = std::min(to, (last + 8) & ~size_t(7));
        Step::Bytes range;
        range.at = uint32_t(start);
        range.before.assign(base + start, base + end);
        range.after.assign(now + start, now + end);
        out.push_back(std::move(range));
        at = end;
    }
}

template <class T> bool same(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }

} // namespace

std::vector<ProjectSpan> project_spans() {
    const auto& l = layout();
    using SamplesTable = decltype(Project::samples);
    using ChopsTable = decltype(Project::chops);
    return {{0, l.samples}, {l.samples + sizeof(SamplesTable), l.chops}, {l.chops + sizeof(ChopsTable), sizeof(Project)}};
}

bool span_holds(const std::vector<ProjectSpan>& spans, size_t at) {
    for (const auto& span : spans)
        if (at >= span.from && at < span.to) return true;
    return false;
}

int track_of_byte(size_t at) {
    const auto& l = layout();
    if (at < l.tracks || at >= l.tracks + sizeof(melodee::Track) * melodee::track_count) return -1;
    return int((at - l.tracks) / sizeof(melodee::Track));
}

bool tempo_byte(size_t at) {
    const auto& l = layout();
    return (at >= l.tempo && at < l.tempo + sizeof(Project::tempo)) || (at >= l.swing && at < l.swing + sizeof(Project::swing));
}

History::History() : base(std::make_unique<Project>()), base_bank(std::make_unique<melodee::PatternBank>()) {}
History::~History() = default;

void History::take(const Live& now) {
    base_bytes.assign(bytes_of(now.project), bytes_of(now.project) + sizeof(Project));
    for (const auto& span : {layout().samples, layout().chops}) // the tables' pointers are never compared
        std::fill(base_bytes.begin() + long(span), base_bytes.begin() + long(span + sizeof(Project::samples)), uint8_t(0));
    base->samples = now.project.samples;
    base->chops = now.project.chops;
    *base_bank = now.bank;
    base_extras = now.extras;
    base_modules = now.modules;
}

void History::reset(const Live& now) {
    done.clear();
    undone.clear();
    take(now);
}

void History::diff(const Live& now, Step& step) const {
    const auto* live = bytes_of(now.project);
    for (const auto& span : project_spans()) diff_bytes(base_bytes.data(), live, span.from, span.to, step.project);
    diff_bytes(reinterpret_cast<const uint8_t*>(&base_extras), reinterpret_cast<const uint8_t*>(&now.extras), 0, sizeof(MoveExtras), step.extras);
    size_t slots = std::max(base->samples.span(), now.project.samples.span());
    for (size_t slot = 0; slot < slots; ++slot)
        if (!same(base->samples[slot], now.project.samples[slot])) step.samples.push_back({uint16_t(slot), base->samples[slot], now.project.samples[slot]});
    slots = std::max(base->chops.span(), now.project.chops.span());
    for (size_t slot = 0; slot < slots; ++slot)
        if (!same(base->chops[slot], now.project.chops[slot])) step.chops.push_back({uint16_t(slot), base->chops[slot], now.project.chops[slot]});
    for (unsigned t = 0; t < melodee::track_count; ++t)
        for (unsigned p = 0; p < melodee::pattern_count; ++p)
            if (base_bank->at[t][p] != now.bank.at[t][p]) step.patterns.push_back({uint8_t(t), uint8_t(p), base_bank->at[t][p], now.bank.at[t][p]});
    // The modules: the whole state where the module or its preset changed,
    // else the values the screens set.
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        const auto& was = base_modules[t];
        const auto& is = now.modules[t];
        if (!is.settled || !was.settled) continue;
        if (was.present != is.present || was.file.module != is.file.module || was.file.preset != is.file.preset) {
            if (!was.present && !is.present) continue;
            Step::Module change;
            change.track = uint8_t(t);
            change.whole = true;
            change.before = was;
            change.after = is;
            step.modules.push_back(std::move(change));
            continue;
        }
        Step::Module change;
        change.track = uint8_t(t);
        for (const auto& key : now.edited[t]) {
            auto find = [&](const SynthFile& file) {
                for (const auto& [k, v] : file.values)
                    if (k == key) return v;
                return std::string();
            };
            std::string before = find(was.file), after = find(is.file);
            if (before != after && std::none_of(change.keys.begin(), change.keys.end(), [&](const Step::Key& k) { return k.key == key; }))
                change.keys.push_back({key, before, after});
        }
        if (!change.keys.empty()) step.modules.push_back(std::move(change));
    }
    // What it touched.
    for (const auto& range : step.project)
        for (size_t at = range.at; at < range.at + range.before.size(); ++at) {
            if (range.before[at - range.at] == range.after[at - range.at]) continue; // in the range, not changed
            int t = track_of_byte(at);
            if (t >= 0) step.tracks.set(size_t(t));
            else {
                step.globals = true;
                if (!tempo_byte(at)) step.tempo_only = false;
            }
        }
    for (const auto& change : step.patterns) step.tracks.set(change.track);
    for (const auto& change : step.modules) step.tracks.set(change.track);
    if (!step.samples.empty() || !step.chops.empty()) { step.globals = true; step.tempo_only = false; }
}

bool History::close(const Live& now, const std::string& what, int track, Clock::time_point at, bool keep) {
    Step step;
    diff(now, step);
    if (step.empty()) {
        for (unsigned t = 0; t < melodee::track_count; ++t)
            if (now.modules[t].settled) base_modules[t] = now.modules[t]; // the modules' answers, taken in
        return false;
    }
    // The baseline made `now`, part by part.
    for (const auto& range : step.project) std::memcpy(base_bytes.data() + range.at, range.after.data(), range.after.size());
    for (const auto& range : step.extras)
        std::memcpy(reinterpret_cast<uint8_t*>(&base_extras) + range.at, range.after.data(), range.after.size());
    for (const auto& change : step.samples) base->samples.edit(change.slot) = change.after;
    for (const auto& change : step.chops) base->chops.edit(change.slot) = change.after;
    for (const auto& change : step.patterns) base_bank->at[change.track][change.pattern] = change.after;
    for (unsigned t = 0; t < melodee::track_count; ++t)
        if (now.modules[t].settled) base_modules[t] = now.modules[t];
    if (!keep) return false;
    step.what = what;
    step.track = track;
    if (track < 0 && step.tracks.count() == 1)
        for (unsigned t = 0; t < melodee::track_count; ++t)
            if (step.tracks.test(t)) step.track = int(t); // one track's: whose it is
    step.at = at;
    undone.clear();
    done.push_back(std::move(step));
    if (done.size() > depth) done.pop_front();
    return true;
}

void History::apply(Live& now, const Step& step, bool forward) {
    auto* live = bytes_of(now.project);
    for (const auto& range : step.project) {
        const auto& value = forward ? range.after : range.before;
        std::memcpy(live + range.at, value.data(), value.size());
        std::memcpy(base_bytes.data() + range.at, value.data(), value.size());
    }
    for (const auto& range : step.extras) {
        const auto& value = forward ? range.after : range.before;
        std::memcpy(reinterpret_cast<uint8_t*>(&now.extras) + range.at, value.data(), value.size());
        std::memcpy(reinterpret_cast<uint8_t*>(&base_extras) + range.at, value.data(), value.size());
    }
    for (const auto& change : step.samples) {
        const auto& value = forward ? change.after : change.before;
        now.project.samples.edit(change.slot) = value;
        base->samples.edit(change.slot) = value;
    }
    for (const auto& change : step.chops) {
        const auto& value = forward ? change.after : change.before;
        now.project.chops.edit(change.slot) = value;
        base->chops.edit(change.slot) = value;
    }
    for (const auto& change : step.patterns) {
        const auto& value = forward ? change.after : change.before;
        now.bank.at[change.track][change.pattern] = value;
        base_bank->at[change.track][change.pattern] = value;
    }
    // The modules: the baseline says what they will be once the studio has
    // set them (it does the setting).
    for (const auto& change : step.modules) {
        auto& kept = base_modules[change.track];
        if (change.whole) { kept = forward ? change.after : change.before; continue; }
        for (const auto& key : change.keys)
            for (auto& [k, v] : kept.file.values)
                if (k == key.key) v = forward ? key.after : key.before;
    }
}

const Step* History::undo(Live& now) {
    if (done.empty()) return nullptr;
    undone.push_back(std::move(done.back()));
    done.pop_back();
    apply(now, undone.back(), false);
    return &undone.back();
}

const Step* History::redo(Live& now) {
    if (undone.empty()) return nullptr;
    done.push_back(std::move(undone.back()));
    undone.pop_back();
    apply(now, done.back(), true);
    return &done.back();
}

void History::slots_in_use(std::vector<bool>& used) const {
    auto mark_track = [&](const melodee::Track& track) {
        for (const auto& pad : track.pads) {
            if (pad.used() && pad.sample < used.size()) used[pad.sample] = true;
            for (const auto& layer : pad.layers)
                if (layer.used() && layer.sample < used.size()) used[layer.sample] = true;
        }
    };
    // Each step's tracks, before and after: the track's bytes as the step
    // leaves them laid over the baseline's (a kit pad's sample is a field
    // of its track).
    auto scratch = std::make_unique<melodee::Track>();
    const auto& l = layout();
    auto each = [&](const Step& step) {
        for (int side = 0; side < 2; ++side) {
            std::bitset<melodee::track_count> seen;
            for (const auto& range : step.project) {
                int t = track_of_byte(range.at);
                if (t < 0 || seen.test(size_t(t))) continue;
                seen.set(size_t(t));
                size_t track_at = l.tracks + size_t(t) * sizeof(melodee::Track);
                std::memcpy(scratch.get(), base_bytes.data() + track_at, sizeof(melodee::Track));
                for (const auto& r : step.project) { // every range of this track, laid over
                    size_t from = std::max<size_t>(r.at, track_at), to = std::min<size_t>(r.at + r.before.size(), track_at + sizeof(melodee::Track));
                    if (from >= to) continue;
                    const auto& value = side ? r.after : r.before;
                    std::memcpy(reinterpret_cast<uint8_t*>(scratch.get()) + (from - track_at), value.data() + (from - r.at), to - from);
                }
                mark_track(*scratch);
            }
        }
        for (const auto& change : step.samples) {
            if (change.slot < used.size() && (change.before.path[0] || change.after.path[0])) used[change.slot] = true;
        }
    };
    for (const auto& step : done) each(step);
    for (const auto& step : undone) each(step);
}

} // namespace app
