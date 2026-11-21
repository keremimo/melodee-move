// SPDX-License-Identifier: AGPL-3.0-only
// Song (Shift + Step 13; docs/UI.md "Song", docs/ui/song.png): Melodee's
// arrange view on the Move. A lane per track of the bank in view (the track
// buttons' four), clips of patterns over bars, markers, the loop brace, the
// tempo lane, the cursor. The pads are the four lanes over eight bars, as
// Session lays clips out (a row a track); the steps sixteen bars, as Loop
// Mode has them: a step moves the cursor there, a step held and another sets
// the loop. The wheel moves the cursor a bar; its push opens the clip's jobs
// (song_clip.cpp) or, on empty time, places a clip; Shift + push a marker.
// Copy, Delete and Loop act on the clip under the cursor, Copy held, Delete
// held and Mute held on the pad's clip. The pads are a grid of where they
// lie, a row a lane beside its track button, as Session's: they stay
// physical in both pad layouts (Kerem, 2026-11-20, docs/MOVE_CONTROLS.md
// "Pads": only numbered pad jobs follow Rows or Banks). Knobs: 1 the pattern, 2 the length
// (of the clip under the cursor, or the next one placed), 3 the zoom, 5 the
// cursor's bar's tempo (under 30 BPM: none), 6 its shape. Plus and Minus the
// bank, Left and Right the pads' page, Shift + Left / Right the markers. Play
// plays the song from the cursor, Rec writes launches into it, Sampling
// opens EXPORT (song_export.cpp). The model's jobs are SongDesk's.
#include "app/screens/common.h"
#include "app/exports.h"
#include "app/song_desk.h"
#include "ui/song_view.h"
#include <cmath>

namespace app::screens {
namespace {

using bindings::Job;

std::string clock_text(double seconds) {
    unsigned s = unsigned(std::lround(std::max(0.0, seconds)));
    char text[16];
    std::snprintf(text, sizeof text, "%u:%02u", s / 60, s % 60);
    return text;
}
std::string tempo_text(unsigned tenths) {
    char text[16];
    std::snprintf(text, sizeof text, "%u.%u", tenths / 10, tenths % 10);
    return text;
}

class Song final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::Song; }

    void enter() override { copy_from = -1; zoom_turned = 0; }

    void input(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Button && input.index == unsigned(move::Button::Copy) && !input.pressed) copy_from = -1;
    }

    bool job(Job job, const bindings::Use& use) override {
        auto& desk = studio.song_desk();
        unsigned track = studio.track(), bank = track / 4 * 4;
        switch (job) {
        case Job::Song: case Job::Back: studio.close(); return true; // Shift + Step 13 again, or Back
        case Job::SongPick: case Job::SongCopyClip: case Job::SongDeleteClip: case Job::SongMuteClip: {
            unsigned lane = lane_of(unsigned(use.index)), bar = pads_first() + move::pad_column(unsigned(use.index));
            unsigned at_track = bank + lane;
            int clip = desk.clip_at(at_track, bar);
            if (job == Job::SongPick) {
                if (at_track != track) pick(at_track);
                desk.cursor = bar;
            } else if (job == Job::SongCopyClip) {
                if (copy_from < 0) copy_from = clip; // the clip to copy, then where it goes
                else desk.copy_to(copy_from, at_track, bar);
            } else if (job == Job::SongDeleteClip) {
                desk.erase(clip);
            } else {
                desk.toggle_mute(clip);
            }
            return true;
        }
        case Job::SongBar: desk.cursor = steps_first() + unsigned(use.index); return true;
        case Job::SongLoopRange:
            if (use.held_step >= 0) desk.set_loop(steps_first() + unsigned(use.held_step), steps_first() + unsigned(use.index));
            return true;
        case Job::SongCursor: move_cursor(int(desk.cursor) + use.delta); return true;
        case Job::SongPage: move_cursor(int(desk.cursor) + (use.control == bindings::Control::Left ? -8 : 8)); return true;
        case Job::SongMarkerJump: {
            int bar = desk.marker_jump(desk.cursor, use.control == bindings::Control::Left ? -1 : 1);
            if (bar >= 0) desk.cursor = unsigned(bar);
            else move_cursor(use.control == bindings::Control::Left ? 0 : int(desk.cursor));
            return true;
        }
        case Job::SongPush:
            if (desk.clip_here(track) >= 0) studio.push("song-clip");
            else if (studio.kind() == TrackKind::Empty) studio.say("EMPTY TRACK");
            else desk.place(track, desk.cursor);
            return true;
        case Job::SongMarker: desk.marker(desk.cursor); return true;
        case Job::SongDuplicate: desk.duplicate(desk.clip_here(track)); return true;
        case Job::SongDelete: desk.erase(desk.clip_here(track)); return true;
        case Job::SongLoopClip: desk.loop_clip(desk.clip_here(track)); return true;
        case Job::TrackBank: { // the next or previous four, the same lane
            int to = int(track) + (use.control == bindings::Control::Plus ? 4 : -4);
            if (to >= 0 && to < int(melodee::track_count)) pick(unsigned(to));
            return true;
        }
        case Job::PlaySong:
            if (studio.playing()) { desk.set_recording(false); studio.stop(); }
            else desk.play_from(desk.cursor);
            return true;
        case Job::RestartSong: if (studio.playing()) desk.play_from(desk.cursor); return true;
        case Job::RecordSong:
            if (desk.recording()) desk.set_recording(false);
            else {
                if (!desk.song_playing()) desk.play_from(desk.cursor);
                desk.set_recording(true);
            }
            return true;
        case Job::SongExport: studio.push("song-export"); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: turn(use.index, use.delta, job == Job::TurnKnobFine); return true;
        default: return false;
        }
    }

    Knob knob(unsigned which) const override {
        const auto& desk = studio.song_desk();
        int clip = desk.clip_here(studio.track());
        const auto* c = clip >= 0 ? &desk.song().clips[size_t(clip)] : nullptr;
        switch (which) {
        case 0: { unsigned p = c ? c->pattern : desk.next_pattern; return {"PATTERN", pattern_name(p), p / 127.0f}; }
        case 1: { unsigned b = c ? c->bars : desk.next_bars; return {"BARS", std::to_string(b), (b - 1) / 15.0f}; }
        case 2: return {"ZOOM", std::to_string(desk.zoom()) + " BARS", desk.zoom_index / float(SongDesk::zoom_count - 1)};
        case 4: {
            unsigned tenths = desk.tempo_at(desk.cursor);
            bool point = desk.song().tempo.at_bar(desk.cursor) >= 0;
            return {"BAR " + std::to_string(desk.cursor + 1) + " TEMPO", point ? tempo_text(tenths) : "SONG " + tempo_text(tenths),
                    (tenths - 300) / 2700.0f};
        }
        case 5: {
            int at = desk.song().tempo.at_bar(desk.cursor);
            if (at < 0) return {"SHAPE", "NO POINT", 0};
            bool ramp = desk.song().tempo.points[size_t(at)].ramp;
            return {"SHAPE", ramp ? "RAMP" : "STEP", ramp ? 1.0f : 0.0f};
        }
        default: return {};
        }
    }
    std::string knob_owner() const override { return "SONG"; }

    void lights(move::Lights& lights) override {
        using namespace move;
        const auto& desk = studio.song_desk();
        const auto& song = desk.song();
        unsigned track = studio.track(), bank = track / 4 * 4;
        int play_bar = desk.song_playing() ? int(studio.host().song_bar()) : -1;
        // The pads: four lanes over eight bars, top row the bank's first.
        unsigned first = pads_first();
        for (unsigned pad = 0; pad < pad_count; ++pad) { // where it lies, in both pad layouts
            unsigned lane = lane_of(pad), bar = first + move::pad_column(pad), t = bank + lane;
            int clip = desk.clip_at(t, bar);
            uint8_t colour = colour::off;
            if (clip >= 0) {
                const auto& c = song.clips[size_t(clip)];
                colour = c.muted ? colour::dark_grey : bar == c.bar ? ui::track_colour(t).bright : ui::track_colour(t).dim;
            }
            if (int(bar) == play_bar && clip < 0) colour = colour::very_dark_green;
            if (int(bar) == play_bar && clip >= 0) colour = colour::green;
            if (t == track && bar == desk.cursor) colour = colour::white;
            lights.pad(pad, colour);
        }
        // The steps: sixteen bars; the loop's bright, the lane's clips dim, the cursor white, the playhead green.
        unsigned steps = steps_first();
        for (unsigned step = 0; step < step_count; ++step) {
            unsigned bar = steps + step;
            bool looped = song.looping() && bar >= song.loop_start && bar < song.loop_end;
            uint8_t colour = looped ? ui::track_colour(track).bright : desk.clip_at(track, bar) >= 0 ? ui::track_colour(track).dim : colour::off;
            if (int(bar) == play_bar) colour = colour::green;
            if (bar == desk.cursor) colour = colour::white;
            lights.step(step, colour);
        }
        bool on_clip = desk.clip_here(track) >= 0;
        lights.button(Button::Copy, on_clip ? white_dim : 0);
        lights.button(Button::Delete, on_clip ? white_dim : 0);
        lights.button(Button::Loop, on_clip || song.looping() ? white_dim : 0);
        for (auto button : {Button::Left, Button::Right, Button::Back, Button::Menu}) lights.button(button, white_dim);
        lights.button(Button::Up, bank + 4 < melodee::track_count ? white_dim : 0);
        lights.button(Button::Down, bank ? white_dim : 0);
        lights.button(Button::Rec, desk.recording() ? colour::red : colour::dark_grey);
        lights.button(Button::Sample, desk.exports().busy() ? colour::red : colour::dark_grey);
    }

    void draw(juce::Graphics& g) override {
        const auto& desk = studio.song_desk();
        const auto& song = desk.song();
        unsigned track = studio.track(), bank = track / 4 * 4;
        ui::SongView view;
        view.first = int(desk.first_bar());
        view.zoom = int(desk.zoom());
        view.cursor = int(desk.cursor);
        view.lane = int(track - bank);
        for (unsigned n = 0; n < song.count; ++n) {
            const auto& c = song.clips[n];
            if (c.track < bank || c.track >= bank + 4) continue;
            view.clips.push_back({int(c.track - bank), int(c.bar), int(c.bars), pattern_name(c.pattern), c.muted});
        }
        for (unsigned m = 0; m < song.marker_count; ++m) view.markers.push_back({int(song.markers[m].bar), song.markers[m].name});
        view.loop_from = song.loop_start;
        view.loop_to = song.loop_end;
        for (unsigned n = 0; n < song.tempo.count; ++n) view.tempo_points.push_back(song.tempo.points[n].bar);
        view.playhead = desk.playhead();
        view.recording = desk.recording();
        for (unsigned lane = 0; lane < 4; ++lane) view.lane_used[lane] = studio.kind(bank + lane) != TrackKind::Empty;
        ui::rail(g, "SONG", std::to_string(desk.cursor + 1), "T" + std::to_string(track + 1));
        ui::song_view(g, view);
        // The detail: the clip under the cursor (or what a push places), and the clock or the tempo.
        std::string left, right = song.tempo.empty() ? clock_text(desk.seconds_at(desk.cursor)) : tempo_text(desk.tempo_at(desk.cursor));
        int clip = desk.clip_here(track);
        if (view.playhead >= 0) {
            unsigned beats = std::max(1u, studio.project().bar_ticks() / std::max(1u, studio.project().beat_ticks()));
            float into = view.playhead - std::floor(view.playhead);
            left = "BAR " + std::to_string(unsigned(view.playhead) + 1) + "." + std::to_string(unsigned(into * float(beats)) + 1);
        } else if (int at = song.marker_at(desk.cursor); at >= 0) {
            left = "MARKER " + std::string(song.markers[size_t(at)].name);
        } else if (clip >= 0) {
            const auto& c = song.clips[size_t(clip)];
            left = pattern_name(c.pattern) + " " + std::to_string(c.bars) + (c.bars == 1 ? " BAR" : " BARS") + (c.muted ? " M" : "");
        } else if (studio.kind() != TrackKind::Empty) {
            left = "PLACE " + pattern_name(desk.next_pattern) + " X" + std::to_string(desk.next_bars);
        }
        if (left.size() + right.size() > 15) right.clear();
        ui::detail(g, left, right);
    }

    std::string status() const override {
        return " song_lane_first=" + std::to_string(pads_first() + 1);
    }

private:
    int copy_from = -1;   // Copy held: the clip pressed first
    int zoom_turned = 0;  // detents toward the next zoom
    unsigned pads_first() const { return studio.song_desk().cursor / 8 * 8; }
    // The lane of a pad where it lies: the top row the bank's first track, beside its track button.
    static unsigned lane_of(unsigned pad) { return move::pad_rows - 1 - move::pad_row(pad); }
    unsigned steps_first() const { return studio.song_desk().cursor / 16 * 16; }
    void move_cursor(int bar) { studio.song_desk().cursor = unsigned(std::clamp(bar, 0, int(melodee::song_bars) - 1)); }
    // Another lane: its track selected, Song still on top (a track button leaves for Play, as on the Move).
    void pick(unsigned t) {
        studio.select(t);
        studio.open("song");
    }

    void turn(int knob, int delta, bool fine) {
        auto& desk = studio.song_desk();
        int clip = desk.clip_here(studio.track());
        const auto* c = clip >= 0 ? &desk.song().clips[size_t(clip)] : nullptr;
        switch (knob) {
        case 0:
            if (c) desk.set_pattern(clip, int(c->pattern) + delta);
            else desk.next_pattern = uint8_t(std::clamp(int(desk.next_pattern) + delta, 0, int(melodee::pattern_count) - 1));
            break;
        case 1:
            if (c) desk.set_bars(clip, int(c->bars) + delta);
            else desk.next_bars = uint8_t(std::clamp(int(desk.next_bars) + delta, 1, 16));
            break;
        case 2:
            zoom_turned += delta;
            if (std::abs(zoom_turned) >= 3) {
                desk.zoom_index = unsigned(std::clamp(int(desk.zoom_index) + (zoom_turned > 0 ? 1 : -1), 0, int(SongDesk::zoom_count) - 1));
                zoom_turned = 0;
            }
            break;
        case 4: {
            int now = desk.song().tempo.at_bar(desk.cursor) >= 0 ? int(desk.tempo_at(desk.cursor)) : int(desk.tempo_at(desk.cursor));
            int to = now + delta * (fine ? 1 : 10);
            if (desk.song().tempo.at_bar(desk.cursor) < 0 && to < 300) break; // nothing there to take away
            desk.set_bar_tempo(desk.cursor, to < 300 ? 0 : to);
            break;
        }
        case 5: if (delta) desk.set_bar_ramp(desk.cursor, delta > 0); break;
        default: break;
        }
    }
};

RegisterScreen song("song", make<Song>, {{bindings::Job::Song}}); // Shift + Step 13

} // namespace
} // namespace app::screens
