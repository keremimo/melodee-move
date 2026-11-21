// SPDX-License-Identifier: AGPL-3.0-only
// Song, its exports, print and freeze (docs/PARITY.md's `song & export`
// rows), against the whole app as the Move runs it: the song's model jobs,
// playing from a bar, Rec into the song, the MIDI file both ways, the song
// into a pattern, a mixdown, stems and an MPC 3 project written while the
// song plays, prints made by the print host and a track frozen, unfrozen and
// opened frozen again. The tests drive the audio thread themselves (render)
// and the service thread (service), as the runtime would.
#include "app/exports.h"
#include "app/melodee_app.h"
#include "app/pad_layout.h"
#include "app/prints.h"
#include "app/project_store.h"
#include "app/song_desk.h"
#include "app/wav_out.h"
#include "move/audio.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/tools.h"
#include "melodee/engine/engine_info.h"
#include "melodee/model/project_file.h"
#include "melodee/model/smf.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace move;
namespace fs = std::filesystem;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
int32_t peak_of(const AudioBlock& block) {
    int32_t peak = 0;
    for (auto v : block) peak = std::max(peak, v == INT32_MIN ? INT32_MAX : std::abs(v));
    return peak;
}

// A project as the MPC would leave it: a kit on track 1, A01 the test loop,
// pattern A1 a beat on A01 every quarter of one bar, pattern A2 two bars of
// eighths; a second kit on track 2 with A1.
void write_project(const std::string& home, const std::string& loop) {
    fs::remove_all(home);
    fs::create_directories(home + "/Projects");
    auto project = std::make_unique<melodee::Project>();
    auto patterns = std::make_unique<melodee::PatternBank>();
    std::snprintf(project->name, sizeof project->name, "Tune");
    for (unsigned t = 0; t < 2; ++t) {
        melodee::load_sound(project->tracks[t], melodee::EngineKind::Sampler, 0);
        std::snprintf(project->tracks[t].name, sizeof project->tracks[t].name, t ? "PERC" : "DRUMS");
        project->tracks[t].pads[0].sample = 0;
    }
    auto& ref = project->samples.edit(0);
    std::snprintf(ref.path, sizeof ref.path, "%s", loop.c_str());
    std::snprintf(ref.name, sizeof ref.name, "loop.wav");
    auto add = [&](unsigned t, unsigned p, uint32_t tick) {
        melodee::Event e;
        e.tick = tick;
        e.key = uint8_t(melodee::sampler_first_note);
        e.length = 12;
        patterns->at[t][p].events.push_back(e);
    };
    uint32_t bar = project->bar_ticks();
    patterns->at[0][0].length = bar;
    for (uint32_t q = 0; q < 4; ++q) add(0, 0, q * melodee::ticks_per_quarter);
    patterns->at[0][1].length = 2 * bar;
    for (uint32_t e = 0; e < 16; ++e) add(0, 1, e * melodee::ticks_per_quarter / 2);
    patterns->at[1][0].length = bar;
    add(1, 0, 0);
    app::PluginStates none;
    auto bytes = app::project_file_bytes(*project, *patterns, none);
    app::write_whole(home + "/Projects/Move.mldp", bytes.data(), bytes.size());
}

// The Move's controls as a hand uses them.
void press(App& app, Button button) {
    app.surface(make_event(0xb0, button_cc(button), 127));
    app.surface(make_event(0xb0, button_cc(button), 0));
}
void hold(App& app, Button button, bool down) { app.surface(make_event(0xb0, button_cc(button), down ? 127 : 0)); }
template <class Do> void with(App& app, Button button, Do&& what) {
    hold(app, button, true);
    what();
    hold(app, button, false);
}
void tap_pad(App& app, unsigned pad) {
    app.surface(make_event(0x90, pad_note(pad), 100));
    app.surface(make_event(0x80, pad_note(pad), 0));
}
void tap_step(App& app, unsigned step) {
    app.surface(make_event(0x90, step_note(step), 127));
    app.surface(make_event(0x80, step_note(step), 0));
}
void turn_wheel(App& app, int detents) { app.surface(make_event(0xb0, main_knob_cc, uint8_t(detents < 0 ? 128 + detents : detents))); }
void turn_knob(App& app, unsigned knob, int detents) {
    app.surface(make_event(0x90, uint8_t(knob), 127)); // touched
    app.surface(make_event(0xb0, uint8_t(knob_first_cc + knob), uint8_t(detents < 0 ? 128 + detents : detents)));
    app.surface(make_event(0x80, uint8_t(knob), 0));
}
// The app's screen as it is now, four times size, into build/song-screens/
// (to set beside docs/ui/song.png).
void snap(app::MelodeeApp& app, const std::string& name) {
    juce::Image image(juce::Image::ARGB, move::screen_width, move::screen_height, true);
    {
        juce::Graphics g(image);
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        app.draw(g);
    }
    fs::create_directories("build/song-screens");
    auto file = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String("build/song-screens/" + name + ".png"));
    file.deleteFile();
    juce::FileOutputStream output(file);
    if (output.openedOk()) juce::PNGImageFormat().writeImageToStream(move::frame_image(move::pack(image), 4), output);
}

// The pad of the Song's lane (0 the bank's first, the top row) and bar (0-7 of the page).
unsigned song_pad(unsigned lane, unsigned bar) { return (3 - lane) * 8 + bar; }

struct Rig {
    app::MelodeeApp app;
    Session session;
    AudioBlock in {}, out {};
    Rig(const std::string& samples, const std::string& home, const std::string& modules = "build/test-no-modules") : app(samples, home, modules) {
        app.attach(session);
        app.audio_opened(AudioFormat {});
        app.service(); // the samples read, the frozen tracks sent
    }
    app::Studio& studio() { return app.studio(); }
    app::SongDesk& desk() { return app.studio().song_desk(); }
    int32_t render(int blocks) {
        int32_t peak = 0;
        for (int b = 0; b < blocks; ++b) { out.fill(0); app.render(in, out); peak = std::max(peak, peak_of(out)); }
        return peak;
    }
    // Rendering and serving until `done` says so, at most `seconds` of audio.
    template <class Done> bool until(Done done, double seconds = 60) {
        int blocks = int(seconds * 44100 / audio_frames);
        for (int b = 0; b < blocks; ++b) {
            render(1);
            if ((b & 7) == 0) app.service();
            if (done()) return true;
            // The other threads' turn: about 20 times real time, so the exports' rings drain as they do on a Move.
            if ((b & 15) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return done();
    }
};

void test_song_model() {
    std::string folder = "build/test-samples", home = "build/test-home-song", loop = folder + "/loop.wav";
    fs::create_directories(folder);
    CHECK(write_test_loop(loop));
    write_project(home, loop);
    Rig rig(folder, home);
    auto& desk = rig.desk();
    std::unique_lock<std::mutex> guard(rig.studio().lock);
    auto& song = desk.song();
    // Place: A1 for 4 bars at bar 1, then A2 at bar 5 for 4.
    desk.next_pattern = 0; desk.next_bars = 4;
    CHECK(desk.place(0, 0));
    desk.next_pattern = 1;
    CHECK(desk.place(0, 4));
    CHECK(song.count == 2 && song.length() == 8);
    CHECK(desk.clip_at(0, 3) == 0 && desk.clip_at(0, 4) == 1 && desk.clip_at(1, 0) < 0);
    CHECK(!desk.place(2, 0)); // an empty track takes no clip
    // Duplicate after itself; the cursor follows.
    CHECK(desk.duplicate(desk.clip_at(0, 4)) && desk.cursor == 8 && desk.clip_at(0, 9) >= 0 && song.length() == 12);
    // Split at bar 10: two clips, 8-10 and 10-12.
    CHECK(desk.split(desk.clip_at(0, 9), 10));
    CHECK(song.clips[size_t(desk.clip_at(0, 9))].bars == 2 && song.clips[size_t(desk.clip_at(0, 10))].bar == 10);
    CHECK(!desk.split(desk.clip_at(0, 10), 10)); // not inside
    // Mute, erase.
    int at = desk.clip_at(0, 10);
    CHECK(desk.toggle_mute(at) && song.clips[size_t(at)].muted);
    CHECK(desk.erase(desk.clip_at(0, 10)) && desk.clip_at(0, 10) < 0 && song.length() == 10);
    // Own pattern: the clip's pattern copied to the first free number.
    at = desk.clip_at(0, 8);
    CHECK(desk.own_pattern(at));
    unsigned own = song.clips[size_t(at)].pattern;
    CHECK(own == 2 && rig.studio().patterns().at[0][2].events.size() == rig.studio().patterns().at[0][1].events.size());
    // Markers: added with ready names, stepped, jumped between; repeat to the next.
    CHECK(!desk.repeat_to_marker(desk.clip_at(0, 4))); // no marker ahead yet
    CHECK(desk.marker(0) && desk.marker(16) && song.marker_count == 2);
    CHECK(std::string(song.markers[0].name) == "INTRO" && std::string(song.markers[1].name) == "VERSE");
    CHECK(desk.marker(16) && std::string(song.markers[1].name) == "CHORUS");
    CHECK(desk.marker_jump(3, 1) == 16 && desk.marker_jump(16, -1) == 0 && desk.marker_jump(16, 1) < 0);
    desk.erase(desk.clip_at(0, 8));
    CHECK(desk.repeat_to_marker(desk.clip_at(0, 4)) && desk.clip_at(0, 15) >= 0 && desk.clip_at(0, 16) < 0);
    // Loop: on the clip's bars, again off; a range from two bars (the steps).
    at = desk.clip_at(0, 4);
    CHECK(desk.loop_clip(at) && song.loop_start == 4 && song.loop_end == 8);
    CHECK(desk.loop_clip(at) && !song.looping());
    CHECK(desk.set_loop(11, 8) && song.loop_start == 8 && song.loop_end == 12);
    CHECK(desk.loop_clip(-1) && !song.looping());
    // Copy to another lane, pattern and length on the knobs.
    CHECK(desk.copy_to(desk.clip_at(0, 0), 1, 2) && desk.clip_at(1, 2) >= 0);
    at = desk.clip_at(1, 2);
    CHECK(desk.set_bars(at, 2) && song.clips[size_t(desk.clip_at(1, 2))].bars == 2);
    CHECK(desk.set_pattern(desk.clip_at(1, 2), 5) && song.clips[size_t(desk.clip_at(1, 2))].pattern == 5);
    // The tempo lane: a point, its shape, taken away under 30 BPM; the clock through it.
    unsigned base = rig.studio().tempo();
    CHECK(desk.tempo_at(4) == base);
    CHECK(desk.set_bar_tempo(4, 1500) && desk.tempo_at(4) == 1500 && desk.tempo_at(3) == base);
    CHECK(desk.set_bar_ramp(4, true) && desk.tempo_at(2) > base && desk.tempo_at(2) < 1500);
    double before = desk.seconds_at(8);
    CHECK(desk.set_bar_tempo(4, 200) && song.tempo.empty() && desk.seconds_at(8) > before);
    CHECK(has(rig.studio().status(), "song_clips=") && has(rig.studio().status(), "song_markers=2")); // under the lock: the studio's own
    // The song into a new pattern: the first number free everywhere.
    std::string said;
    int column = desk.to_pattern(said);
    CHECK(column >= 0 && has(said, "SONG TO"));
    CHECK(column >= 0 && rig.studio().patterns().at[0][size_t(column)].length == 16 * rig.studio().project().bar_ticks());
    guard.unlock();
    // Saved with the project and opened again.
    CHECK(rig.app.save_now().empty());
    Rig again(folder, home);
    std::lock_guard<std::mutex> lock(again.studio().lock);
    CHECK(again.desk().song().count == song.count && again.desk().song().marker_count == 2);
}

void test_song_plays() {
    std::string folder = "build/test-samples", home = "build/test-home-song-play", loop = folder + "/loop.wav";
    write_project(home, loop);
    Rig rig(folder, home);
    auto& desk = rig.desk();
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        desk.next_pattern = 0; desk.next_bars = 2;
        desk.place(0, 0);
        desk.place(0, 4); // bars 2-3 empty
        desk.play_from(4);
    }
    CHECK(rig.render(4) > 0);
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(desk.song_playing() && rig.studio().host().song_bar() == 4 && desk.playhead() >= 4);
        rig.studio().stop();
    }
    int quiet = 0;
    for (int block = 0; block < 8000 && quiet < 200; ++block) quiet = rig.render(1) ? 0 : quiet + 1;
    CHECK(quiet >= 200); // stopped, rung out
    // Rec into the song: a launch while the song plays is written from the next bar and grows.
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        desk.erase(desk.clip_at(0, 4));
        desk.play_from(0);
        desk.set_recording(true);
    }
    rig.render(8);
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        rig.studio().send({melodee::Command::Type::Pattern, 1, 0, 0, 0}); // track 2 launches A1
        CHECK(desk.clip_at(1, 1) >= 0 && desk.song().clips[size_t(desk.clip_at(1, 1))].bars == 1);
    }
    double bar_seconds = 60.0 / (rig.studio().tempo() / 10.0) * 4;
    CHECK(rig.until([&] { std::lock_guard<std::mutex> g(rig.studio().lock); return rig.studio().host().song_bar() >= 3; }, bar_seconds * 4));
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        int at = desk.clip_at(1, 1);
        CHECK(at >= 0 && desk.song().clips[size_t(at)].bars >= 2); // grown a bar at a time
        desk.set_recording(false);
        at = desk.clip_at(1, 1);
        CHECK(at >= 0 && desk.song().clips[size_t(at)].end() <= rig.studio().host().song_bar() + 1u);
        rig.studio().stop();
    }
}

void test_midi_file() {
    std::string folder = "build/test-samples", home = "build/test-home-song-midi", loop = folder + "/loop.wav";
    write_project(home, loop);
    Rig rig(folder, home);
    std::unique_lock<std::mutex> guard(rig.studio().lock);
    auto& desk = rig.desk();
    // No song: each track's pattern once.
    auto bytes = desk.midi_file();
    std::vector<melodee::SmfTrack> tracks;
    std::string why;
    CHECK(!bytes.empty() && melodee::read_smf(bytes, tracks, why));
    CHECK(tracks.size() == 2 && tracks[0].notes.size() == 4 && tracks[0].channel == 10 && tracks[0].notes[0].key == 36);
    // With a song: as it plays.
    desk.next_pattern = 0; desk.next_bars = 3;
    desk.place(0, 0);
    bytes = desk.midi_file();
    tracks.clear();
    CHECK(melodee::read_smf(bytes, tracks, why) && tracks.size() == 1 && tracks[0].notes.size() == 12);
    // Exported into MIDI/, named after the project.
    CHECK(desk.exports().start(app::Exports::Kind::Midi).empty());
    CHECK(fs::exists(home + "/MIDI/Tune.mid"));
    CHECK(desk.exports().start(app::Exports::Kind::Midi).empty() && fs::exists(home + "/MIDI/Tune 2.mid"));
    CHECK(has(rig.studio().status(), "export=MIDI/Tune_2.mid") && has(rig.studio().status(), "export_done=1"));
    // Loaded onto track 2's pattern playing: twelve notes over three bars.
    std::ifstream in(home + "/MIDI/Tune.mid", std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(desk.load_midi(file, 1).empty());
    const auto& pattern = rig.studio().patterns().at[1][0];
    CHECK(pattern.events.size() == 12 && pattern.length == 3 * rig.studio().project().bar_ticks());
    CHECK(!desk.load_midi({1, 2, 3}, 1).empty());
    CHECK(desk.load_midi(file, 5) == "PICK A TRACK WITH A SOUND");
}

// A WAV file's format and frames, as its header says.
struct WavInfo { unsigned format = 0, channels = 0, rate = 0, bits = 0; uint64_t frames = 0; };
WavInfo wav_info(const std::string& path) {
    WavInfo w;
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4)) return w;
    auto u16 = [&](size_t at) { return unsigned(b[at] | b[at + 1] << 8); };
    auto u32 = [&](size_t at) { return uint32_t(b[at] | b[at + 1] << 8 | b[at + 2] << 16 | uint32_t(b[at + 3]) << 24); };
    for (size_t at = 12; at + 8 <= b.size();) {
        uint32_t size = u32(at + 4);
        if (!std::memcmp(&b[at], "fmt ", 4)) { w.format = u16(at + 8); w.channels = u16(at + 10); w.rate = u32(at + 12); w.bits = u16(at + 22); }
        if (!std::memcmp(&b[at], "data", 4) && w.channels && w.bits) w.frames = size / (w.channels * w.bits / 8);
        at += 8 + size + (size & 1);
    }
    return w;
}

void test_exports() {
    std::string folder = "build/test-samples", home = "build/test-home-song-export", loop = folder + "/loop.wav";
    write_project(home, loop);
    Rig rig(folder, home);
    auto& desk = rig.desk();
    auto& exports = desk.exports();
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(exports.start(app::Exports::Kind::Mix16) == "SONG EMPTY");
        desk.next_pattern = 0; desk.next_bars = 1;
        desk.place(0, 0);
        desk.place(1, 0);
        CHECK(exports.start(app::Exports::Kind::Mix16).empty());
        CHECK(exports.busy() && !exports.start(app::Exports::Kind::Mix32).empty());
    }
    // The song plays once through, then its tail, into Mixdowns/Tune.wav.
    CHECK(rig.until([&] { return !exports.busy(); }, 20));
    auto shown = exports.shown();
    CHECK(shown.done && shown.error.empty() && shown.file == "Mixdowns/Tune.wav");
    auto mix = wav_info(home + "/Mixdowns/Tune.wav");
    double bar = 60.0 / (rig.studio().tempo() / 10.0) * 4;
    CHECK(mix.format == 1 && mix.bits == 16 && mix.channels == 2 && mix.rate == 44100);
    CHECK(mix.frames >= uint64_t(bar * 44100 * 0.98) && mix.frames <= uint64_t((bar + 10.5) * 44100));
    CHECK(!fs::exists(home + "/Mixdowns/Tune.wav.part"));
    // Stems: the mix and each track, the same frames, 32-bit float.
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(exports.start(app::Exports::Kind::Stems).empty());
    }
    CHECK(rig.until([&] { return !exports.busy(); }, 20));
    auto all = wav_info(home + "/Stems/Tune/Mix.wav"), drums = wav_info(home + "/Stems/Tune/01 DRUMS.wav"), perc = wav_info(home + "/Stems/Tune/02 PERC.wav");
    CHECK(all.format == 3 && all.bits == 32 && all.frames > 0);
    CHECK(drums.frames == all.frames && perc.frames == all.frames && drums.format == 3);
    // Stopped part way (Play, as the Move's Stop): nothing kept.
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        desk.next_bars = 8;
        desk.place(0, 0);
        CHECK(exports.start(app::Exports::Kind::Mix32).empty());
    }
    rig.render(100);
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        exports.cancel();
    }
    CHECK(rig.until([&] { return !exports.busy(); }, 5));
    CHECK(!exports.shown().done && !fs::exists(home + "/Mixdowns/Tune 2.wav") && !fs::exists(home + "/Mixdowns/Tune 2.wav.part"));
    // The MPC 3 project: its file and its data folder.
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(exports.start(app::Exports::Kind::Mpc3).empty());
    }
    for (int wait = 0; wait < 1200 && exports.busy(); ++wait) std::this_thread::sleep_for(std::chrono::milliseconds(50)); // its own thread: no song plays
    CHECK(exports.shown().done && exports.shown().error.empty());
    CHECK(fs::exists(home + "/MPC 3/Tune.xpj") && fs::is_directory(home + "/MPC 3/Tune_[ProjectData]"));
    CHECK(fs::exists(home + "/MPC 3/Tune_[ProjectData]/loop.wav"));
    // Song to pattern from the export list.
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(exports.start(app::Exports::Kind::ToPattern).empty() && has(exports.shown().said, "SONG TO"));
    }
}

void test_print_and_freeze() {
    std::string folder = "build/test-samples", home = "build/test-home-song-print", loop = folder + "/loop.wav";
    write_project(home, loop);
    std::string prints_folder = home + "/Projects/Move Prints";
    {
        Rig rig(folder, home);
        auto& printer = rig.desk().printer();
        {
            std::lock_guard<std::mutex> guard(rig.studio().lock);
            CHECK(printer.printable(0) && !printer.printable(5));
            CHECK(printer.with_notes(0)[0] == 3);
            app::Printer::Job job;
            job.track = 0;
            job.patterns = {0, 1};
            job.freeze = true;
            CHECK(printer.start(job).empty());
            CHECK(!printer.start(job).empty()); // one at a time
        }
        CHECK(rig.until([&] { std::lock_guard<std::mutex> g(rig.studio().lock); return printer.frozen(0); }, 30));
        {
            std::lock_guard<std::mutex> guard(rig.studio().lock);
            auto p = printer.progress();
            CHECK(p.is_done(0) && p.is_done(1) && p.error.empty());
            CHECK(printer.table().find(0, 0) && printer.table().find(0, 1) && printer.table().freeze[0].plays(1));
            CHECK(has(rig.studio().status(), "frozen=1") && has(rig.studio().status(), "printed=2"));
        }
        auto a1 = wav_info(prints_folder + "/" + melodee::print_file_name(0, 0));
        CHECK(a1.format == 3 && a1.bits == 32 && a1.channels == 2 && a1.frames > 0);
        // The frozen track plays its print: Play sounds, as before.
        rig.render(20);
        CHECK((rig.studio().host().frozen_tracks() & 1) != 0);
        {
            std::lock_guard<std::mutex> guard(rig.studio().lock);
            rig.studio().play();
        }
        CHECK(rig.render(80) > to_s32(0.02f));
        CHECK((rig.studio().host().printing_tracks() & 1) != 0); // the print plays, not the engine
        {
            std::lock_guard<std::mutex> guard(rig.studio().lock);
            rig.studio().stop();
            // Printed again with nothing changed: both unchanged, none made.
            app::Printer::Job job;
            job.track = 0;
            job.patterns = {0, 1};
            job.freeze = false;
            CHECK(printer.start(job).empty());
        }
        CHECK(rig.until([&] { return !printer.busy(); }, 10));
        rig.app.service();
        CHECK(has(rig.app.status(), "printed=2 print_skipped=2"));
        // busy() holds from start() until the job's results are in, never false while the print thread takes the job:
        // watched as closely as a core can, each time.
        for (int round = 0; round < 40; ++round) {
            {
                std::lock_guard<std::mutex> guard(rig.studio().lock);
                app::Printer::Job job;
                job.track = 0;
                job.patterns = {0, 1};
                job.freeze = false;
                CHECK(printer.start(job).empty());
            }
            auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (printer.busy() && std::chrono::steady_clock::now() < give_up) {}
            auto p = printer.progress();
            CHECK(!printer.busy() && !p.busy && p.through == 2 && p.error.empty());
        }
        rig.app.service();
        CHECK(has(rig.app.status(), "printed=2 print_skipped=82"));
        CHECK(rig.app.save_now().empty());
    }
    {   // Opened again: frozen as it was, its prints read and playing.
        Rig rig(folder, home);
        rig.render(20);
        rig.app.service();
        auto& printer = rig.desk().printer();
        std::unique_lock<std::mutex> guard(rig.studio().lock);
        CHECK(printer.frozen(0) && printer.table().records.size() == 2);
        guard.unlock();
        rig.render(4);
        CHECK((rig.studio().host().frozen_tracks() & 1) != 0);
        guard.lock();
        // melodee-unfreeze: live again at once, the prints kept.
        CHECK(printer.command("melodee-unfreeze", "1") == "unfrozen 1" && !printer.frozen(0));
        CHECK(printer.command("melodee-freeze", "status") == "frozen");
        CHECK(printer.command("melodee-print", "0") == "no track 0");
        CHECK(printer.command("melodee-freeze", "1 A1 cut") == "freezing 1");
        guard.unlock();
        CHECK(rig.until([&] { std::lock_guard<std::mutex> g(rig.studio().lock); return printer.frozen(0); }, 30));
        guard.lock();
        CHECK(printer.table().freeze[0].plays(0) && !printer.table().freeze[0].plays(1) && printer.table().freeze[0].tail == melodee::PrintTail::Cut);
        CHECK(fs::exists(prints_folder + "/" + melodee::print_file_name(0, 1))); // kept from before
    }
}

// Another project opened while a print runs: the print is called off, what it made is dropped (it is the last
// project's), and the samples it reads stay in memory until it is done with them.
void test_open_while_printing() {
    std::string folder = "build/test-samples", home = "build/test-home-song-print-open", loop = folder + "/loop.wav";
    write_project(home, loop);
    Rig rig(folder, home);
    auto& printer = rig.desk().printer();
    printer.hold(true); // a long print: it waits before its first block
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        app::Printer::Job job;
        job.track = 0;
        job.patterns = {0, 1};
        job.freeze = true;
        CHECK(printer.start(job).empty());
    }
    CHECK(rig.until([&] { return printer.progress().pattern == 0; }, 10));
    CHECK(rig.studio().open_project(home + "/Projects/Other.mldp", true).empty());
    CHECK(rig.until([&] { return has(rig.app.status(), "samples_printing=1"); }, 5)); // the engine let it go; kept
    CHECK(printer.busy());
    printer.hold(false);
    CHECK(rig.until([&] { return !printer.busy(); }, 10));
    rig.app.service();
    auto status = rig.app.status();
    CHECK(has(status, "samples_printing=0") && has(status, "project=Other"));
    CHECK(has(status, "printed=0 print_skipped=0") && has(status, "frozen=0"));
    std::lock_guard<std::mutex> guard(rig.studio().lock);
    CHECK(printer.table().records.empty() && !printer.frozen(0) && printer.progress().error == "called off");
}

void test_wav_out() {
    std::string path = "build/test-wav-out.wav";
    {
        app::WavOut wav;
        CHECK(wav.open(path, 44100, 32, 2, 4096));
        std::vector<float> frames {0.5f, -0.5f, 0.25f, -0.25f};
        CHECK(wav.add(frames.data(), 2) && wav.close());
    }
    std::vector<float> back;
    unsigned rate = 0, channels = 0;
    CHECK(app::read_float_wav(path, back, rate, channels) && rate == 44100 && channels == 2 && back.size() == 4 && back[3] == -0.25f);
    CHECK(fs::file_size(path) == 4096 + 16);
    {   // 24-bit, dithered: within a step of the value, both ways.
        app::WavOut wav;
        CHECK(wav.open(path, 48000, 24, 1));
        std::vector<float> frames(1000, 0.5f);
        CHECK(wav.add(frames.data(), frames.size()) && wav.close());
    }
    auto info = wav_info(path);
    CHECK(info.format == 1 && info.bits == 24 && info.frames == 1000 && info.rate == 48000);
}

// A synth track (a Schwung module, the tests' stand-in) printed by its own
// instance of the module in the print host; a deep freeze lets the live
// module go once the print plays, and unfreezing thaws it back as it was.
void test_synth_freeze() {
    std::string folder = "build/test-samples", home = "build/test-home-song-synth", loop = folder + "/loop.wav";
    write_project(home, loop);
    Rig rig(folder, home, "build/test-modules");
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(rig.studio().new_track(2, app::TrackKind::Synth));
    }
    CHECK(rig.studio().load_module_now(2, "fake").empty());
    auto& printer = rig.desk().printer();
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        auto& pattern = rig.studio().patterns().at[2][0];
        pattern.length = rig.studio().project().bar_ticks();
        melodee::Event e;
        e.key = 60;
        e.length = 48;
        e.velocity = 110;
        pattern.events.push_back(e);
        rig.desk().sync_pattern(2, 0);
        CHECK(printer.printable(2));
        app::Printer::Job job;
        job.track = 2;
        job.deep = true;
        CHECK(printer.start(job).empty());
    }
    CHECK(rig.until([&] { std::lock_guard<std::mutex> g(rig.studio().lock); return printer.frozen(2); }, 60));
    auto a1 = wav_info(home + "/Projects/Move Prints/" + melodee::print_file_name(2, 0));
    CHECK(a1.format == 3 && a1.frames > 0);
    {   // The print is the module's sound: not silent.
        std::vector<float> frames;
        unsigned rate = 0, channels = 0;
        CHECK(app::read_float_wav(home + "/Projects/Move Prints/" + melodee::print_file_name(2, 0), frames, rate, channels));
        float loudest = 0;
        for (float f : frames) loudest = std::max(loudest, std::fabs(f));
        CHECK(loudest > 0.01f);
    }
    // Deep: the module let go once the print plays and the engine sleeps.
    CHECK(rig.until([&] {
        std::lock_guard<std::mutex> g(rig.studio().lock);
        auto* module = rig.studio().module_track(2);
        return module && !module->module;
    }, 60));
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(printer.deep(2) && has(rig.studio().status(), "deep=1"));
        CHECK(rig.studio().module_track(2)->saved.module == "fake");
        CHECK(printer.unfreeze(2).empty() && printer.thawing(2));
    }
    // Thawed: the module loads again, then the track plays live.
    CHECK(rig.until([&] { std::lock_guard<std::mutex> g(rig.studio().lock); return !printer.frozen(2); }, 200));
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        auto* module = rig.studio().module_track(2);
        CHECK(module && module->module && module->module->info().id == "fake");
        // MPC 3: the synth track goes as its prints, made first (A1's unchanged: kept).
        CHECK(rig.desk().exports().start(app::Exports::Kind::Mpc3).empty());
    }
    for (int wait = 0; wait < 1500 && rig.desk().exports().busy(); ++wait) {
        rig.render(8);
        rig.app.service();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto shown = rig.desk().exports().shown();
    CHECK(shown.done && shown.error.empty() && fs::exists(home + "/MPC 3/Tune.xpj"));
    CHECK(has(rig.app.status(), "print_skipped=1"));
}

// The pad layouts (pad_layout.h): PRINT's patterns are numbered pads, so
// they follow Rows or Banks; Song's lanes and bars are where the pads lie,
// as Session's grid, so they stay physical in both (Kerem, 2026-11-20).
void test_pad_layouts() {
    std::string folder = "build/test-samples", home = "build/test-home-song-pads", loop = folder + "/loop.wav";
    write_project(home, loop);
    Rig rig(folder, home);
    auto& app = rig.app;
    auto status = [&] { return app.status(); };
    {   // A9 has notes too: pattern 8.
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        auto& pattern = rig.studio().patterns().at[0][8];
        pattern.length = rig.studio().project().bar_ticks();
        melodee::Event e;
        e.key = uint8_t(melodee::sampler_first_note);
        e.length = 12;
        pattern.events.push_back(e);
        rig.desk().sync_pattern(0, 8);
    }
    app::set_pad_layout(app::PadLayout::Banks);
    hold(app, Button::Row1, true);
    press(app, Button::Sample);
    hold(app, Button::Row1, false);
    CHECK(has(status(), "page=print") && has(status(), "print_chosen=3")); // A1, A2, A9
    // Banks: pad 9 (A9) is the left bank's third row, first column: physical 16.
    tap_pad(app, 16);
    CHECK(has(status(), "print_chosen=2"));
    tap_pad(app, 16);
    CHECK(has(status(), "print_chosen=3"));
    // Rows: physical 16 is pad 17 (no notes); A9 is physical 8, the second row's first.
    app::set_pad_layout(app::PadLayout::Rows);
    tap_pad(app, 16);
    CHECK(has(status(), "print_chosen=3"));
    tap_pad(app, 8);
    CHECK(has(status(), "print_chosen=2"));
    press(app, Button::Back);
    // Song in Banks: the top row is still the bank's first lane, the third pad its third bar.
    app::set_pad_layout(app::PadLayout::Banks);
    with(app, Button::Shift, [&] { tap_step(app, 12); });
    tap_pad(app, song_pad(1, 2));
    CHECK(has(status(), "page=song ") && has(status(), "track=2 ") && has(status(), "song_cursor=3"));
    tap_pad(app, song_pad(0, 6));
    CHECK(has(status(), "track=1 ") && has(status(), "song_cursor=7"));
    app::set_pad_layout(app::PadLayout::Rows);
}

// Song, EXPORT and PRINT driven by the Move's own controls, through the
// bindings table (docs/MOVE_CONTROLS.md, "Song", "Print and freeze").
void test_song_screens() {
    std::string folder = "build/test-samples", home = "build/test-home-song-keys", loop = folder + "/loop.wav";
    write_project(home, loop);
    Rig rig(folder, home);
    auto& app = rig.app;
    auto status = [&] { return app.status(); };
    // Shift + Step 13: Song; again: back.
    with(app, Button::Shift, [&] { tap_step(app, 12); });
    CHECK(has(status(), "page=song "));
    with(app, Button::Shift, [&] { tap_step(app, 12); });
    CHECK(has(status(), "page=kit-play"));
    with(app, Button::Shift, [&] { tap_step(app, 12); });
    // A pad: the lane and the bar; the wheel's push places A1 for four bars there.
    tap_pad(app, song_pad(0, 2));
    CHECK(has(status(), "song_cursor=3"));
    press(app, Button::MainPush);
    CHECK(has(status(), "song_clips=1"));
    CHECK(rig.render(1) == 0); // the pads on Song are the song, not sounds
    // Copy: duplicated after itself, the cursor on it; Delete: gone again.
    press(app, Button::Copy);
    CHECK(has(status(), "song_clips=2") && has(status(), "song_cursor=7"));
    press(app, Button::Delete);
    CHECK(has(status(), "song_clips=1"));
    // The wheel back onto the clip; Loop: the brace on its bars.
    turn_wheel(app, -4);
    CHECK(has(status(), "song_cursor=3"));
    press(app, Button::Loop);
    CHECK(has(status(), "song_loop=2-6"));
    // Shift + push: a marker.
    with(app, Button::Shift, [&] { press(app, Button::MainPush); });
    CHECK(has(status(), "song_markers=1"));
    snap(app, "song-marker");
    // Knob 2: the clip's length; knob 1 its pattern.
    turn_knob(app, 1, 2);
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(rig.desk().song().clips[size_t(rig.desk().clip_here(0))].bars == 6);
    }
    turn_knob(app, 0, 1);
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(rig.desk().song().clips[size_t(rig.desk().clip_here(0))].pattern == 1);
    }
    // Mute held + the clip's pad: muted.
    with(app, Button::Mute, [&] { tap_pad(app, song_pad(0, 2)); });
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(rig.desk().song().clips[size_t(rig.desk().clip_here(0))].muted);
        CHECK(!rig.studio().current().mix.mute); // not the track
    }
    // Copy held + the clip's pad, then lane 2's bar 1: a copy there.
    with(app, Button::Copy, [&] { tap_pad(app, song_pad(0, 2)); tap_pad(app, song_pad(1, 0)); });
    CHECK(has(status(), "song_clips=2"));
    // Delete held + that pad: gone.
    with(app, Button::Delete, [&] { tap_pad(app, song_pad(1, 0)); });
    CHECK(has(status(), "song_clips=1"));
    // A step held and another: the loop from the first bar to the second.
    app.surface(make_event(0x90, step_note(0), 127));
    tap_step(app, 3);
    app.surface(make_event(0x80, step_note(0), 0));
    CHECK(has(status(), "song_loop=0-4"));
    tap_step(app, 9);
    CHECK(has(status(), "song_cursor=10"));
    // Shift + Left: back to the marker; Left / Right a page of eight.
    with(app, Button::Shift, [&] { press(app, Button::Left); });
    CHECK(has(status(), "song_cursor=3"));
    press(app, Button::Right);
    CHECK(has(status(), "song_cursor=11"));
    // Plus and Minus: the bank of four lanes.
    press(app, Button::Up);
    CHECK(has(status(), "track=5 "));
    press(app, Button::Down);
    CHECK(has(status(), "track=1 "));
    // The wheel's push on a clip: its jobs; the seventh unmutes it.
    turn_wheel(app, -8);
    press(app, Button::MainPush);
    CHECK(has(status(), "page=song-clip"));
    snap(app, "clip-sheet");
    turn_wheel(app, 6);
    press(app, Button::MainPush);
    CHECK(has(status(), "page=song "));
    {
        std::lock_guard<std::mutex> guard(rig.studio().lock);
        CHECK(!rig.desk().song().clips[size_t(rig.desk().clip_here(0))].muted);
    }
    // Play: the song from the cursor; again: stopped.
    std::this_thread::sleep_for(std::chrono::milliseconds(700)); // the touched knob let go
    snap(app, "song");
    press(app, Button::Play);
    rig.render(4);
    CHECK(has(status(), "song_playing=1 song_bar=3"));
    snap(app, "song-playing");
    press(app, Button::Play);
    rig.render(2);
    CHECK(has(status(), "playing=0"));
    // Rec on Song: the song plays, launches written.
    press(app, Button::Rec);
    rig.render(2);
    CHECK(has(status(), "song_playing=1") && has(status(), "song_rec=1"));
    press(app, Button::Rec);
    press(app, Button::Play);
    rig.render(2);
    // Sampling on Song: EXPORT; a 16-bit mixdown, then where it went.
    press(app, Button::Sample);
    CHECK(has(status(), "page=song-export"));
    snap(app, "export");
    turn_wheel(app, 2);
    press(app, Button::MainPush);
    rig.render(200);
    snap(app, "exporting");
    CHECK(rig.until([&] { return !rig.desk().exports().busy(); }, 30));
    CHECK(has(status(), "export=Mixdowns/Tune.wav") && has(status(), "export_done=1"));
    CHECK(wav_info(home + "/Mixdowns/Tune.wav").bits == 16);
    snap(app, "exported");
    press(app, Button::MainPush);
    CHECK(has(status(), "page=song "));
    press(app, Button::Back);
    CHECK(has(status(), "page=kit-play"));
    // A track button held + Sampling: PRINT for track 2 (the second button).
    hold(app, Button::Row2, true);
    press(app, Button::Sample);
    hold(app, Button::Row2, false);
    CHECK(has(status(), "page=print") && has(status(), "print_sheet=2") && has(status(), "track=2 "));
    snap(app, "print");
    press(app, Button::MainPush); // FREEZE
    snap(app, "printing");
    CHECK(rig.until([&] { std::lock_guard<std::mutex> g(rig.studio().lock); return rig.desk().printer().frozen(1); }, 30));
    rig.render(4);
    CHECK(has(status(), "frozen=1"));
    press(app, Button::Back);
    CHECK(has(status(), "page=kit-play"));
    snap(app, "frozen");
    // A knob on the frozen track asks first; push: live again.
    turn_knob(app, 0, 3);
    CHECK(has(status(), "page=unfreeze"));
    snap(app, "unfreeze");
    press(app, Button::MainPush);
    CHECK(has(status(), "frozen=0") && has(status(), "page=kit-play"));
    // Note/Session held + Sampling, from the patterns grid (Melodee's Seq held + Sample Record): PRINT for the selected
    // track, its patterns on the pads; Note/Session let go leaves it open.
    hold(app, Button::Menu, true);
    press(app, Button::Sample);
    hold(app, Button::Menu, false);
    CHECK(has(status(), "page=print") && has(status(), "print_sheet=2") && has(status(), "track=2 "));
    press(app, Button::Back);
    CHECK(has(status(), "page=kit-play"));
}

} // namespace

int main() {
    std::cerr << "test_wav_out\n";
    test_wav_out();
    std::cerr << "test_song_model\n";
    test_song_model();
    std::cerr << "test_song_plays\n";
    test_song_plays();
    std::cerr << "test_midi_file\n";
    test_midi_file();
    std::cerr << "test_exports\n";
    test_exports();
    std::cerr << "test_print_and_freeze\n";
    test_print_and_freeze();
    std::cerr << "test_open_while_printing\n";
    test_open_while_printing();
    std::cerr << "test_synth_freeze\n";
    test_synth_freeze();
    std::cerr << "test_song_screens\n";
    test_song_screens();
    std::cerr << "test_pad_layouts\n";
    test_pad_layouts();
    std::cout << "song-tests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
