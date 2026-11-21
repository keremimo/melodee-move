// SPDX-License-Identifier: AGPL-3.0-only
#include "app/song_desk.h"
#include "app/exports.h"
#include "app/prints.h"
#include "app/studio.h"
#include "melodee/model/pattern.h"
#include "melodee/model/song_flatten.h"
#include <algorithm>
#include <cstdio>

namespace app {

using melodee::Clip;
using Type = melodee::Command::Type;

std::string pattern_name(unsigned pattern) {
    char text[8];
    std::snprintf(text, sizeof text, "%c%u", 'A' + int(pattern % melodee::pattern_count / 16), pattern % 16 + 1);
    return text;
}

SongDesk::SongDesk(Studio& owner)
    : studio(owner), exporter(std::make_unique<Exports>(owner)), prints(std::make_unique<Printer>(owner)) {}
SongDesk::~SongDesk() { stop(); }

void SongDesk::stop() {
    if (prints) prints->stop();
    if (exporter) exporter->stop();
}

melodee::Song& SongDesk::song() { return studio.project().song; }
const melodee::Song& SongDesk::song() const { return studio.project().song; }

unsigned SongDesk::first_bar() const { return cursor / zoom() * zoom(); }

int SongDesk::clip_at(unsigned track, unsigned bar) const { return song().clip_at(track % melodee::track_count, bar); }

// ---------------------------------------------------------------- sending

void SongDesk::send_song(bool edited) {
    const auto& s = song();
    for (unsigned n = 0; n < s.count; ++n) {
        melodee::Command c {Type::SongClip, 0, 0, uint16_t(n)};
        c.clip = s.clips[n];
        studio.send(c);
    }
    studio.send({Type::SongCount, 0, 0, 0, int16_t(s.count)});
    studio.send({Type::SongLoop, 0, 0, s.loop_start, int16_t(s.loop_end)});
    for (unsigned n = 0; n < s.tempo.count; ++n) { // the tempo lane
        const auto& p = s.tempo.points[n];
        melodee::Command c {Type::SongTempo, 0, uint8_t(p.ramp), uint16_t(n), int16_t(p.tempo)};
        c.position = p.bar;
        studio.send(c);
    }
    studio.send({Type::SongTempoCount, 0, 0, 0, int16_t(s.tempo.count)});
    if (edited) studio.changed();
}

void SongDesk::sync_pattern(unsigned t, unsigned p) {
    t %= melodee::track_count;
    p %= melodee::pattern_count;
    melodee::Command c {Type::PatternData, uint8_t(t), uint8_t(p)};
    c.data = new melodee::Pattern(melodee::played_pattern(studio.patterns().at[t][p], studio.project().tracks[t].speed)); // as it plays
    if (!studio.send(c)) delete c.data; // else handed back to be freed (Studio::take_back)
    studio.changed();
}

// ---------------------------------------------------------------- clips

bool SongDesk::place(const Clip& clip) {
    if (clip.bar >= melodee::song_bars || !studio.project().tracks[clip.track % melodee::track_count].used()) return false;
    Clip c = clip;
    c.bars = uint16_t(std::clamp<unsigned>(c.bars, 1, melodee::song_bars - c.bar));
    if (!song().place(c)) { studio.say("SONG FULL"); return false; }
    send_song();
    return true;
}

bool SongDesk::place(unsigned track, unsigned bar) {
    Clip c;
    c.track = uint8_t(track % melodee::track_count);
    c.pattern = uint8_t(next_pattern % melodee::pattern_count);
    c.bar = uint16_t(bar);
    c.bars = std::max<uint8_t>(next_bars, 1);
    return place(c);
}

bool SongDesk::duplicate(int index) {
    if (index < 0 || index >= int(song().count)) return false;
    Clip copy = song().clips[size_t(index)];
    copy.bar = copy.end();
    if (!place(copy)) return false;
    cursor = copy.bar;
    return true;
}

bool SongDesk::split(int index, unsigned bar) {
    auto& s = song();
    if (index < 0 || index >= int(s.count)) return false;
    Clip clip = s.clips[size_t(index)];
    if (bar <= clip.bar || bar >= clip.end()) { studio.say("SPLIT: INSIDE A CLIP"); return false; }
    if (s.count >= s.clips.size()) { studio.say("SONG FULL"); return false; } // the tail would be lost
    s.clips[size_t(index)].bars = uint16_t(bar - clip.bar);
    Clip tail = clip;
    tail.bar = uint16_t(bar);
    tail.bars = uint16_t(clip.end() - bar);
    s.place(tail);
    send_song();
    return true;
}

// A copy of its pattern in the first number its track leaves empty (and no clip
// plays), so editing it changes this clip alone.
bool SongDesk::own_pattern(int index) {
    auto& s = song();
    if (index < 0 || index >= int(s.count)) return false;
    Clip clip = s.clips[size_t(index)];
    auto& patterns = studio.patterns().at[clip.track];
    for (unsigned n = 0; n < melodee::pattern_count; ++n) {
        bool placed = false;
        for (unsigned k = 0; k < s.count; ++k) placed |= s.clips[k].track == clip.track && s.clips[k].pattern == n;
        if (placed || !patterns[n].empty() || n == clip.pattern) continue;
        patterns[n] = patterns[clip.pattern];
        sync_pattern(clip.track, n);
        s.clips[size_t(index)].pattern = uint8_t(n);
        send_song();
        studio.say("OWN PATTERN " + pattern_name(n));
        return true;
    }
    studio.say("NO FREE PATTERN");
    return false;
}

bool SongDesk::repeat_to_marker(int index) {
    auto& s = song();
    if (index < 0 || index >= int(s.count)) return false;
    Clip clip = s.clips[size_t(index)];
    unsigned until = s.next_marker(clip.bar);
    if (until <= clip.end()) { studio.say("REPEAT: A MARKER AHEAD"); return false; }
    for (unsigned at = clip.end(); at < until;) {
        Clip copy = clip;
        copy.bar = uint16_t(at);
        copy.bars = uint16_t(std::min<unsigned>(clip.bars, until - at));
        if (!s.place(copy)) break;
        at += copy.bars;
    }
    send_song();
    return true;
}

bool SongDesk::loop_clip(int index) {
    auto& s = song();
    if (index < 0 || index >= int(s.count)) {
        if (!s.looping()) return false;
        s.loop_start = s.loop_end = 0;
        send_song();
        return true;
    }
    const Clip& clip = s.clips[size_t(index)];
    bool same = s.loop_start == clip.bar && s.loop_end == clip.end();
    if (same) s.loop_start = s.loop_end = 0;
    else { s.loop_start = clip.bar; s.loop_end = clip.end(); }
    send_song();
    return true;
}

bool SongDesk::set_loop(unsigned from, unsigned to) {
    if (from > to) std::swap(from, to);
    to = std::min<unsigned>(to + 1, melodee::song_bars);
    auto& s = song();
    if (s.loop_start == from && s.loop_end == to) return false;
    s.loop_start = uint16_t(from);
    s.loop_end = uint16_t(to);
    send_song();
    return true;
}

bool SongDesk::toggle_mute(int index) {
    auto& s = song();
    if (index < 0 || index >= int(s.count)) return false;
    s.clips[size_t(index)].muted = !s.clips[size_t(index)].muted;
    send_song();
    return true;
}

bool SongDesk::erase(int index) {
    auto& s = song();
    if (index < 0 || index >= int(s.count)) return false;
    s.erase(unsigned(index));
    send_song();
    return true;
}

bool SongDesk::copy_to(int index, unsigned track, unsigned bar) {
    auto& s = song();
    if (index < 0 || index >= int(s.count)) return false;
    Clip copy = s.clips[size_t(index)];
    copy.track = uint8_t(track % melodee::track_count);
    copy.bar = uint16_t(bar);
    if (copy.track != s.clips[size_t(index)].track && studio.patterns().at[copy.track][copy.pattern].empty())
        studio.say("EMPTY THERE: " + pattern_name(copy.pattern));
    return place(copy);
}

bool SongDesk::set_pattern(int index, int pattern) {
    auto& s = song();
    if (index < 0 || index >= int(s.count)) return false;
    uint8_t p = uint8_t(std::clamp(pattern, 0, int(melodee::pattern_count) - 1));
    if (s.clips[size_t(index)].pattern == p) return false;
    s.clips[size_t(index)].pattern = p;
    send_song();
    return true;
}

bool SongDesk::set_bars(int index, int bars) {
    auto& s = song();
    if (index < 0 || index >= int(s.count)) return false;
    Clip clip = s.clips[size_t(index)];
    unsigned want = unsigned(std::clamp(bars, 1, 16));
    if (want == clip.bars || clip.bar + want > melodee::song_bars) return false;
    s.erase(unsigned(index)); // placed again: a longer clip cuts what it now covers
    clip.bars = uint16_t(want);
    s.place(clip);
    send_song();
    return true;
}

// ---------------------------------------------------------------- markers and tempo

bool SongDesk::marker(unsigned bar) {
    if (!song().step_marker(bar)) { studio.say("MARKERS FULL"); return false; }
    studio.changed();
    return true;
}

int SongDesk::marker_jump(unsigned from, int way) const {
    const auto& s = song();
    int best = -1;
    auto consider = [&](int bar) {
        if (way > 0 ? bar > int(from) && (best < 0 || bar < best) : bar < int(from) && (best < 0 || bar > best)) best = bar;
    };
    for (unsigned m = 0; m < s.marker_count; ++m) consider(s.markers[m].bar);
    if (!s.marker_count)
        for (unsigned n = 0; n < s.count; ++n) { consider(s.clips[n].bar); consider(s.clips[n].end()); }
    return best;
}

bool SongDesk::set_bar_tempo(unsigned bar, int tenths) {
    auto& lane = song().tempo;
    bool done;
    if (tenths < 300) done = lane.erase(bar);
    else {
        int at = lane.at_bar(bar);
        bool ramp = at >= 0 && lane.points[size_t(at)].ramp;
        done = lane.set(bar, unsigned(std::min(tenths, 3000)), ramp);
        if (!done) studio.say("TEMPO LANE FULL");
    }
    if (done) send_song();
    return done;
}

bool SongDesk::set_bar_ramp(unsigned bar, bool ramp) {
    auto& lane = song().tempo;
    int at = lane.at_bar(bar);
    if (at < 0 || lane.points[size_t(at)].ramp == ramp) return false;
    lane.points[size_t(at)].ramp = ramp;
    send_song();
    return true;
}

unsigned SongDesk::tempo_at(unsigned bar) const {
    return unsigned(std::lround(song().tempo.at(double(bar), studio.project().tempo)));
}

double SongDesk::seconds_at(unsigned bar) const {
    double quarters = double(studio.project().bar_ticks()) / melodee::ticks_per_quarter;
    return song().tempo.seconds(double(bar), quarters, std::max<unsigned>(studio.project().tempo, 300));
}

// ---------------------------------------------------------------- transport

void SongDesk::play_from(unsigned bar) {
    if (studio.playing()) studio.stop();
    send_song();
    melodee::Command c {Type::Play, 0, 0, uint16_t(std::min<unsigned>(bar, melodee::song_bars - 1)), 1};
    studio.send(c);
}

bool SongDesk::song_playing() const { return studio.playing() && studio.host().song_playing(); }

float SongDesk::playhead() const {
    if (!song_playing()) return -1;
    unsigned bar_ticks = std::max(studio.project().bar_ticks(), 1u);
    // The bar the song is in, and how far into it the clock is.
    unsigned bar = studio.host().song_bar();
    float into = float(studio.host().ticks() % bar_ticks) / float(bar_ticks);
    return float(bar) + into;
}

void SongDesk::set_recording(bool on) {
    if (on == record_on) return;
    record_on = on;
    studio.send({Type::SongRecord, 0, 0, 0, int16_t(on)});
    if (!on) tick(); // the clips being written end
}

// The launch starts a clip on its lane at the next bar, as a launch plays.
void SongDesk::launched(unsigned t, unsigned p) {
    if (!record_on || !song_playing()) return;
    t %= melodee::track_count;
    unsigned bar = studio.host().song_bar() + 1;
    if (bar >= melodee::song_bars) return;
    song().place({uint8_t(t), uint8_t(p % melodee::pattern_count), uint16_t(bar), 1, false});
    recording_from[t] = uint16_t(bar);
    recording_tracks |= uint16_t(1u << t);
    seen_bar = studio.host().song_bar();
    send_song();
}

// Each bar the clips being written grow by one, so the song always holds the
// bar after the one playing; past 16 bars a new clip carries on.
void SongDesk::tick() {
    bool on = record_on && song_playing();
    if (!on) {
        if (recording_tracks) finish_launches();
        seen_bar = ~0u;
        return;
    }
    unsigned bar = studio.host().song_bar();
    if (bar == seen_bar) return;
    if (seen_bar != ~0u && bar < seen_bar) { finish_launches(); seen_bar = bar; return; } // the loop came round
    seen_bar = bar;
    if (!recording_tracks) return;
    auto& s = song();
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        if (!(recording_tracks & (1u << t))) continue;
        int index = -1;
        for (unsigned n = 0; n < s.count; ++n) if (s.clips[n].track == t && s.clips[n].bar == recording_from[t]) index = int(n);
        if (index < 0) { recording_tracks &= uint16_t(~(1u << t)); continue; }
        Clip clip = s.clips[size_t(index)];
        if (clip.end() != bar + 1 || bar + 1 >= melodee::song_bars) continue;
        if (clip.bars >= 16) { // a new clip carries on
            s.place({clip.track, clip.pattern, uint16_t(bar + 1), 1, false});
            recording_from[t] = uint16_t(bar + 1);
            continue;
        }
        s.cut(t, bar + 1, bar + 2); // what was written after it gives way
        for (unsigned n = 0; n < s.count; ++n)
            if (s.clips[n].track == t && s.clips[n].bar == recording_from[t]) { ++s.clips[n].bars; break; }
    }
    send_song();
}

void SongDesk::finish_launches() {
    auto& s = song();
    unsigned bar = seen_bar == ~0u ? 0 : seen_bar;
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        if (!(recording_tracks & (1u << t))) continue;
        for (unsigned n = 0; n < s.count; ++n) {
            auto& c = s.clips[n];
            if (c.track != t || c.bar != recording_from[t]) continue;
            unsigned end = std::max<unsigned>(bar + 1, c.bar + 1u);
            if (c.end() > end) c.bars = uint16_t(end - c.bar);
            break;
        }
    }
    recording_tracks = 0;
    send_song();
}

// ---------------------------------------------------------------- exports that take no time

int SongDesk::to_pattern(std::string& said) {
    const auto& s = song();
    unsigned bars = s.length();
    if (!bars) { said = "SONG EMPTY"; return -1; }
    bool cut = bars > melodee::max_bars;
    bars = std::min(bars, melodee::max_bars);
    uint32_t bar = studio.project().bar_ticks();
    auto& bank = studio.patterns();
    int column = -1;
    for (unsigned p = 0; p < melodee::pattern_count && column < 0; ++p) {
        bool free = true;
        for (unsigned t = 0; t < melodee::track_count && free; ++t) free = bank.at[t][p].empty();
        for (unsigned n = 0; n < s.count && free; ++n) free = s.clips[n].pattern != p;
        if (free) column = int(p);
    }
    if (column < 0) { said = "NO EMPTY PATTERN"; return -1; }
    bool full = false, any = false;
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        melodee::Pattern out;
        if (!melodee::flatten_lane(s, t, bank.at[t], bar, bars, out, full)) continue;
        bank.at[t][size_t(column)] = std::move(out);
        sync_pattern(t, unsigned(column));
        any = true;
    }
    if (!any) { said = "SONG: NO NOTES"; return -1; }
    said = "SONG TO " + pattern_name(unsigned(column)) + (cut ? " 128 BARS" : full ? " NOTES FULL" : "");
    return column;
}

std::vector<uint8_t> SongDesk::midi_file() const {
    const auto& project = studio.project();
    auto tracks = melodee::smf_tracks(project, studio.patterns());
    if (tracks.empty()) return {};
    auto ts = project.time_signature();
    return melodee::write_smf(tracks, project.tempo, ts.beats, ts.unit, melodee::smf_tempos(project));
}

std::string SongDesk::load_midi(const std::vector<uint8_t>& bytes, unsigned t) {
    if (bytes.size() > (8u << 20)) return "MIDI FILE TOO BIG";
    std::vector<melodee::SmfTrack> tracks;
    std::string why;
    if (!melodee::read_smf(bytes, tracks, why)) return "NOT A MIDI FILE";
    t %= melodee::track_count;
    auto& track = studio.project().tracks[t];
    if (!track.used()) return "PICK A TRACK WITH A SOUND";
    melodee::Pattern laid;
    if (!melodee::smf_pattern(tracks, track, studio.project().bar_ticks(), laid)) return "NO NOTES FOR THIS TRACK";
    unsigned p = track.pattern % melodee::pattern_count;
    auto& pattern = studio.patterns().at[t][p];
    pattern.events = laid.events;
    pattern.length = laid.length;
    pattern.free = false;
    pattern.fit_lanes();
    sync_pattern(t, p);
    return {};
}

std::string SongDesk::status() const {
    const auto& s = song();
    char text[200];
    std::snprintf(text, sizeof text, " song_clips=%u song_markers=%u song_loop=%u-%u song_tempo=%u song_cursor=%u song_playing=%d song_bar=%u song_rec=%d",
                  unsigned(s.count), unsigned(s.marker_count), unsigned(s.loop_start), unsigned(s.loop_end), unsigned(s.tempo.count), cursor + 1,
                  int(song_playing()), song_playing() ? studio.host().song_bar() + 1 : 0u, int(record_on));
    return std::string(text) + exporter->status() + prints->status();
}

} // namespace app
