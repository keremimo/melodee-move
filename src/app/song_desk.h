// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/engine/command.h"
#include "melodee/model/jam_log.h"
#include "melodee/model/project.h"
#include "melodee/model/smf.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Song (docs/UI.md, "Song"; Melodee's Song page and its export, melodee-sample
// pages/song_page.cpp and shell/song_export.cpp, done again on the Move): a
// lane per track, clips of patterns, markers, the loop brace, the tempo lane
// and the cursor, kept in the project (Project::song, saved with it) and sent
// to the engine host, which plays them (Command::SongClip, Play from a bar).
// The jobs here are the model's; the Song screen (screens/song.cpp) calls them
// with what the controls say. Every call is under the studio's lock, on the
// MIDI thread, unless it says otherwise.
//
// Record into the song (guide/song.md): with Rec on while the song plays,
// each pattern launched (Command::Pattern, wherever it is sent from) becomes a
// clip on its lane from the next bar, growing a bar at a time; past 128 bars a
// new clip carries on; Rec off or Stop ends each with the bar playing.
namespace app {

class Studio;
class Exports;
class Printer;

class SongDesk {
public:
    explicit SongDesk(Studio& studio);
    ~SongDesk();
    SongDesk(const SongDesk&) = delete;
    SongDesk& operator=(const SongDesk&) = delete;
    void stop(); // its threads end: before the engine host goes

    melodee::Song& song();
    const melodee::Song& song() const;

    // The view, as Song shows it: the cursor's bar and the lane's track, how
    // many bars across, and what Place puts down where the cursor has no clip.
    static constexpr unsigned zooms[] {4, 8, 16, 32, 64};
    static constexpr unsigned zoom_count = sizeof(zooms) / sizeof(zooms[0]);
    unsigned cursor = 0;
    unsigned zoom_index = 2; // 16 bars
    unsigned zoom() const { return zooms[zoom_index % zoom_count]; }
    unsigned first_bar() const; // the first bar in view: whole screens of `zoom`, the cursor in view
    uint8_t next_pattern = 0, next_bars = 4;

    // ---- the model's jobs (each sends the song on; false: nothing changed)
    int clip_at(unsigned track, unsigned bar) const; // -1: none
    int clip_here(unsigned track) const { return clip_at(track, cursor); }
    bool place(unsigned track, unsigned bar);         // next_pattern for next_bars
    bool place(const melodee::Clip& clip);
    bool duplicate(int index);                        // after itself; the cursor follows
    bool split(int index, unsigned bar);              // in two at `bar`
    bool own_pattern(int index);                      // its pattern copied to the first free number
    bool repeat_to_marker(int index);                 // again and again up to the next marker
    bool loop_clip(int index);                        // the brace on its bars; again (or -1) off
    bool set_loop(unsigned from, unsigned to);        // bars [from, to), either order, inclusive of both
    bool toggle_mute(int index);
    bool erase(int index);
    bool copy_to(int index, unsigned track, unsigned bar); // a copy of the clip there
    bool set_pattern(int index, int pattern);          // the clip's pattern (knob 1)
    bool set_bars(int index, int bars);                // the clip's length (knob 2), 1..128
    bool marker(unsigned bar);                         // a marker, or on one its next name (past the last: gone)
    int marker_jump(unsigned from, int way) const;     // the next marker's bar that way (clip edges with none); -1 none
    // The tempo lane (TMPL): a point on `bar` (tenths of a BPM; under 30 BPM none there), its shape.
    bool set_bar_tempo(unsigned bar, int tenths);
    bool set_bar_ramp(unsigned bar, bool ramp);
    unsigned tempo_at(unsigned bar) const;             // tenths, as the lane plays it there
    double seconds_at(unsigned bar) const;             // from the song's start, through the lane
    void send_song(bool edited = true);                // the whole song to the engine (edited: saved after)
    void sync_pattern(unsigned track, unsigned pattern); // a pattern's copy to the engine, as it plays

    // ---- transport
    void play_from(unsigned bar); // the song from `bar`
    bool song_playing() const;
    float playhead() const;       // bars from the start while the song plays, < 0 not
    // Rec on Song: launches written as clips while the song plays.
    bool recording() const { return record_on; }
    void set_recording(bool on);
    // Studio::send(): a launch while recording into the playing song is written as a clip (and sent on too).
    void launched(unsigned track, unsigned pattern);
    void launched_off(unsigned track); // a track off (sequence links): its clip being written ends with the bar playing
    void tick(); // MIDI or service thread, under the lock, often: follows the song's bar

    // Jam capture (melodee-sample's, after Melodee FM-1's jam to song;
    // model/jam_log.h): from Play outside Song to Stop, each bar is logged as
    // it plays (tick()): the pattern each used track plays, which tracks are not
    // heard, the tempo, and a sequence that took over on it. Capture on Song
    // lays the log after the song's end; Play outside Song starts a new one.
    void scene_launched(unsigned n) { jam_scene = int(n % melodee::pattern_count); } // Sequencer::launch_scene()
    unsigned jam_bars() const { return unsigned(jam.bars.size()); }
    bool take_jam(std::string& said); // false: nothing laid (`said` says why)

    // ---- exports that take no time (exports.h has the ones that play the song)
    // The song laid end to end into the first pattern number free on every track (and used by no clip), as long as
    // the song, 128 bars at most; the number, or -1 when there is none or no notes.
    int to_pattern(std::string& said);
    // The song as a MIDI file (format 1, 960 ticks, the tempo and time signature, the lane's changes); with no song,
    // each track's pattern once. The bytes; empty when there are no notes.
    std::vector<uint8_t> midi_file() const;
    // A MIDI file's notes onto `track`'s pattern playing (a kit takes notes 36 on as its pads), as many bars as they
    // take (128 at most). Empty on success.
    std::string load_midi(const std::vector<uint8_t>& bytes, unsigned track);

    Exports& exports() { return *exporter; }
    Printer& printer() { return *prints; }
    const Exports& exports() const { return *exporter; }
    const Printer& printer() const { return *prints; }

    std::string status() const; // " song_clips=... song_bar=..."

private:
    Studio& studio;
    bool record_on = false;
    uint16_t recording_tracks = 0;          // the lanes being written
    std::array<uint16_t, melodee::track_count> recording_from {};
    unsigned seen_bar = ~0u;
    void finish_launches(uint16_t tracks = 0xffff); // the clips being written on these lanes end with the bar playing
    void jam_tick();
    melodee::JamLog jam;
    melodee::JamBar jam_now;      // the bar playing, logged when the next begins
    bool jam_running = false;
    uint32_t jam_bar_seen = 0;
    int jam_scene = -1;           // a sequence launched, for the bar it takes over on
    std::unique_ptr<Exports> exporter;
    std::unique_ptr<Printer> prints;
};

// The pattern's name as the pages give it (A1 .. H16).
std::string pattern_name(unsigned pattern);

} // namespace app
