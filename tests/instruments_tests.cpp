// SPDX-License-Identifier: AGPL-3.0-only
// Melodee's instruments on the Move (docs/UI.md, "Instruments"): the
// instrument files read by Melodee's own readers into a keygroup's zones,
// their files landed and read in place, a sample's note found, a keygroup
// track playing them from the pads.
#include "app/instruments.h"
#include "app/melodee_app.h"
#include "app/studio.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/sample_data.h"
#include "melodee/model/scales.h"
#include "melodee/engine/engine_info.h"
#include "move/lights.h"
#include "melodee/shell/xpm_write.h"
#include "move/controls.h"
#include "move/app.h"
#include "move/audio.h"
#include "move/tools.h"
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace move;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)


// A 16-bit mono WAV of a sine at `hz`, `seconds` long.
bool write_sine(const std::string& path, double hz, double seconds, unsigned rate = 44100) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    const uint32_t frames = uint32_t(seconds * rate), data = frames * 2;
    auto u32 = [&](uint32_t v) { char b[4] {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; file.write(b, 4); };
    auto u16 = [&](uint16_t v) { char b[2] {char(v), char(v >> 8)}; file.write(b, 2); };
    file.write("RIFF", 4); u32(36 + data); file.write("WAVE", 4);
    file.write("fmt ", 4); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    file.write("data", 4); u32(data);
    for (uint32_t n = 0; n < frames; ++n) u16(uint16_t(int16_t(std::lround(12000.0 * std::sin(2 * M_PI * hz * n / rate)))));
    return bool(file);
}
bool write_text(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    return bool(file);
}
int32_t peak_of(const AudioBlock& block) {
    int32_t peak = 0;
    for (auto s : block) peak = std::max(peak, std::abs(int32_t(s)));
    return peak;
}

void test_readers() {
    const std::string folder = "build/test-instruments";
    fs::remove_all(folder);
    fs::create_directories(folder + "/Samples");
    CHECK(write_sine(folder + "/Samples/a3.wav", 220.0, 1.0));
    CHECK(write_sine(folder + "/Samples/e4.wav", 329.63, 0.5, 48000));

    // An SFZ: two regions, one loop, the second file named in another case.
    CHECK(write_text(folder + "/piano.sfz",
        "// a test\n<group> ampeg_release=0.4 amp_veltrack=80\n"
        "<region> sample=Samples/a3.wav lokey=40 hikey=59 pitch_keycenter=57\n"
        "<region> sample=samples/E4.WAV lokey=60 hikey=80 pitch_keycenter=64 loop_mode=loop_continuous loop_start=100 loop_end=20000\n"));
    CHECK(app::instrument_file(folder + "/piano.sfz") && app::instrument_format(folder + "/piano.sfz") == "SFZ");
    CHECK(!app::instrument_file(folder + "/Samples/a3.wav") && app::instrument_format(folder + "/Samples/a3.wav").empty());
    std::string error;
    auto sfz = app::read_instrument_sample(folder + "/piano.sfz", error);
    CHECK(sfz && error.empty());
    if (sfz) {
        auto& multi = *sfz->data->multi;
        CHECK(multi.zones.size() == 2 && multi.part_count == 2 && sfz->instrument->parts.size() == 2);
        CHECK(multi.zones[0].low_key == 40 && multi.zones[0].high_key == 59 && multi.zones[0].root == 57);
        CHECK(sfz->instrument->read.envelope.release && std::fabs(*sfz->instrument->read.envelope.release - 0.4) < 1e-6);
        CHECK(multi.parts[0].frames.load() == 0); // silent until landed
        CHECK(app::land_part(*sfz->instrument, *sfz->data, 0, nullptr).empty());
        CHECK(app::land_part(*sfz->instrument, *sfz->data, 1, nullptr).empty()); // found whatever the case
        CHECK(multi.parts[0].frames.load() == 44100 && multi.parts[0].rate == 44100.0f);
        CHECK(multi.parts[1].frames.load() == 24000 && multi.parts[1].rate == 48000.0f); // read in place at its own rate
        CHECK(multi.parts[0].source != nullptr);                                         // a WAV: read in place, nothing laid out
        CHECK(multi.zones[1].loop != melodee::ZoneLoop::Off && multi.zones[1].loop_start == 100 && multi.zones[1].loop_end == 20001); // SFZ names the last frame in the loop
        CHECK(sfz->name == "piano");
    }

    // A DecentSampler preset, its sample beside it.
    CHECK(write_text(folder + "/cello.dspreset",
        "<?xml version=\"1.0\"?>\n<DecentSampler><groups><group>"
        "<sample path=\"Samples/a3.wav\" rootNote=\"57\" loNote=\"36\" hiNote=\"84\"/>"
        "</group></groups></DecentSampler>\n"));
    auto decent = app::read_instrument_sample(folder + "/cello.dspreset", error);
    CHECK(decent && error.empty() && app::instrument_format(folder + "/cello.dspreset") == "DECENT");
    if (decent) CHECK(decent->data->multi->zones.size() == 1 && decent->data->multi->zones[0].root == 57);
    // One whose sample has gone: refused as Melodee refuses it.
    CHECK(write_text(folder + "/gone.dspreset",
        "<DecentSampler><groups><group><sample path=\"Samples/none.wav\" rootNote=\"60\"/></group></groups></DecentSampler>"));
    CHECK(!app::read_instrument_sample(folder + "/gone.dspreset", error) && error == "Files missing");

    // An MPC keygroup program, as Melodee's auto sampler writes one (shell/xpm_write.h), read back by Melodee's reader.
    {
        melodee::XpmProgram program;
        program.name = "Test";
        melodee::XpmKeygroup low, high;
        low.low = 0; low.high = 59; low.layers.push_back({"a3", 57, 0, 127, 44100});
        high.low = 60; high.high = 127; high.layers.push_back({"e4", 64, 0, 127, 24000});
        program.keygroups = {low, high};
        program.mono = true; program.legato = true; program.tied = true; program.glide = 0.2;
        fs::copy_file(folder + "/Samples/a3.wav", folder + "/a3.wav", fs::copy_options::overwrite_existing);
        fs::copy_file(folder + "/Samples/e4.wav", folder + "/e4.wav", fs::copy_options::overwrite_existing);
        CHECK(melodee::write_xpm(folder + "/Test.xpm", program).empty());
        auto xpm = app::read_instrument_sample(folder + "/Test.xpm", error);
        CHECK(xpm && error.empty() && app::instrument_format(folder + "/Test.xpm") == "MPC");
        if (xpm) {
            const auto& multi = *xpm->data->multi;
            CHECK(multi.zones.size() == 2 && multi.zones[1].low_key == 60 && multi.zones[1].root == 64);
            // Keygroup glide: the program's Mono and Portamento reach the sampler.
            CHECK(multi.mono && multi.legato && multi.tied && std::fabs(multi.glide - 0.2f) < 0.05f);
        }
    }

    // Not an instrument at all.
    CHECK(write_text(folder + "/broken.xpm", "nothing here"));
    CHECK(!app::read_instrument_sample(folder + "/broken.xpm", error) && !error.empty());

    // A file that is not a PCM WAV is laid out in the cache and read in place too.
    {
        app::set_instrument_cache(folder + "/Cache");
        // a WAV of three channels is no WAV read_in_place() takes: Melodee's read_audio decodes it.
        CHECK(write_text(folder + "/odd.sfz", "<region> sample=Samples/a3.wav key=60\n"));
        auto odd = app::read_instrument_sample(folder + "/odd.sfz", error);
        CHECK(odd && odd->data->multi->zones[0].low_key == 60 && odd->data->multi->zones[0].high_key == 60);
    }
}

void test_sample_note() {
    // A sample's note, found as it is read: E (329.63 Hz) is pitch class 4; a noise has none.
    const std::string folder = "build/test-instruments";
    fs::create_directories(folder);
    CHECK(write_sine(folder + "/note-e.wav", 329.63, 1.0));
    std::string error;
    auto sample = app::read_sample(folder + "/note-e.wav", error);
    CHECK(sample && sample->root.found() && sample->root.pitch_class == 4);
    {
        std::ofstream file(folder + "/noise.wav", std::ios::binary | std::ios::trunc);
        const uint32_t frames = 44100, data = frames * 2;
        auto u32 = [&](uint32_t v) { char b[4] {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; file.write(b, 4); };
        auto u16 = [&](uint16_t v) { char b[2] {char(v), char(v >> 8)}; file.write(b, 2); };
        file.write("RIFF", 4); u32(36 + data); file.write("WAVE", 4);
        file.write("fmt ", 4); u32(16); u16(1); u16(1); u32(44100); u32(88200); u16(2); u16(16);
        file.write("data", 4); u32(data);
        uint32_t seed = 1;
        for (uint32_t n = 0; n < frames; ++n) { seed = seed * 1664525u + 1013904223u; u16(uint16_t(int16_t((seed >> 16) & 0x3fff) - 0x2000)); }
    }
    auto noise = app::read_sample(folder + "/noise.wav", error);
    CHECK(noise && !noise->root.found());
}

void test_keygroup_track() {
    // A keygroup track: an SFZ loaded onto it through the studio's jobs, its files landing on a thread of their own,
    // the pads playing it; saved and opened again with the instrument read back. A keygroup of one sample plays from
    // the note found in it (KeyedRoot).
    const std::string folder = "build/test-instruments", home = "build/test-home-instruments";
    fs::remove_all(home);
    fs::create_directories(home + "/Projects");
    AudioBlock in {}, out {};
    auto loudest = [&](app::MelodeeApp& melodee, int blocks) {
        int32_t peak = 0;
        for (int block = 0; block < blocks; ++block) { out.fill(0); melodee.render(in, out); peak = std::max(peak, peak_of(out)); }
        return peak;
    };
    auto pad = [](app::MelodeeApp& melodee, unsigned at, bool down) {
        auto event = make_event(down ? 0x90 : 0x80, pad_note(at), down ? 110 : 0);
        melodee.surface(event);
        melodee.audio_event(event);
    };
    auto wait_landed = [](app::MelodeeApp& melodee) {
        for (int n = 0; n < 200; ++n) {
            melodee.service();
            bool done = true;
            {
                auto& studio = melodee.studio();
                std::lock_guard<std::mutex> guard(studio.lock);
                const auto* sample = studio.keys_sample(2);
                done = sample && (!sample->instrument || sample->instrument->done());
            }
            if (done) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    {
        app::MelodeeApp melodee(folder, home, "build/test-no-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        auto& studio = melodee.studio();
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            CHECK(studio.new_track(2, app::TrackKind::Keys));
            studio.select(2);
            studio.jobs.push_back({folder + "/piano.sfz", 2, 0});
        }
        CHECK(wait_landed(melodee));
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            const auto* sample = studio.keys_sample(2);
            CHECK(sample && sample->instrument && sample->instrument->landed.load() == 2);
            const auto& ref = studio.project().samples[studio.project().tracks[2].pads[0].sample];
            CHECK(ref.instrument() && ref.zones == 2 && ref.root == melodee::root_none);
            CHECK(studio.project().tracks[2].pads[0].play == melodee::PadPlay::Gate);
        }
        loudest(melodee, 4);
        pad(melodee, 9, true); // a note in the instrument's range
        CHECK(loudest(melodee, 20) > to_s32(0.02f));
        pad(melodee, 9, false);
        CHECK(melodee.save_now().empty());
    }
    {   // Opened again: the instrument read back and landed, and it plays.
        app::MelodeeApp melodee(folder, home, "build/test-no-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        CHECK(wait_landed(melodee));
        auto& studio = melodee.studio();
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            studio.select(2);
            const auto* sample = studio.keys_sample(2);
            CHECK(sample && sample->instrument);
        }
        loudest(melodee, 4);
        pad(melodee, 9, true);
        CHECK(loudest(melodee, 20) > to_s32(0.02f));
        pad(melodee, 9, false);
        // One sample instead: E, played from E (its note), its keyed root sent.
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            studio.jobs.push_back({folder + "/note-e.wav", 2, 0});
        }
        melodee.service();
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            const auto& track = studio.project().tracks[2];
            const auto& ref = studio.project().samples[track.pads[0].sample];
            CHECK(!ref.instrument() && ref.root == 4);
            CHECK(melodee::keyed_root(studio.project(), track) == 64); // E4 in Melodee's names, the nearest E to C4
        }
    }
}

void test_keys() {
    // The Move's Keys & Scale on 32 pads (app/keys.h), C major, the bottom-left pad C2 (48).
    auto project = std::make_unique<melodee::Project>();
    project->root = 0;
    project->scale = 0; // Major
    auto& track = project->tracks[1];
    melodee::load_sound(track, melodee::EngineKind::Plugin, 0);
    track.octave = 3;
    auto note = [&](unsigned pad, int shift = 0) { auto n = app::pad_notes(*project, track, pad, shift); return n.count ? int(n.keys[0]) : -1; };
    // In-Key in octaves (Melodee's Scale): a row an octave, the eighth column the next root.
    app::set_keys_mode(track, app::KeysMode::InKey, false);
    CHECK(track.layout == melodee::PadLayout::Scale && app::keys_mode(track) == app::KeysMode::InKey && !app::keys_fourths(track));
    CHECK(note(0) == 48 && note(1) == 50 && note(2) == 52 && note(3) == 53 && note(7) == 60 && note(8) == 60 && note(24) == 84);
    CHECK(app::pad_notes(*project, track, 0).root && app::pad_notes(*project, track, 7).root && !app::pad_notes(*project, track, 1).root);
    CHECK(note(0, 1) == 50 && note(0, -1) == 47); // Shift + Plus / Minus: a note of the key
    // In-Key in 4ths (Melodee's Chords): up a row three notes of the key, a fourth.
    app::set_keys_mode(track, app::KeysMode::InKey, true);
    CHECK(track.layout == melodee::PadLayout::Chords && app::keys_fourths(track));
    CHECK(note(8) == 53 && note(16) == 59 && note(9) == 55);
    // Chromatic: a semitone right, a fourth up; out of the key not lit.
    app::set_keys_mode(track, app::KeysMode::Chromatic, true);
    CHECK(note(1) == 49 && note(8) == 53 && note(31) == 48 + 7 + 15);
    auto sharp = app::pad_notes(*project, track, 1);
    CHECK(!sharp.in_key && !sharp.root);
    CHECK(app::pad_light(sharp, track, 15, 97, false, false) == move::colour::off);
    CHECK(app::pad_light(app::pad_notes(*project, track, 0), track, 15, 97, false, false) == 15);
    CHECK(app::pad_light(app::pad_notes(*project, track, 2), track, 15, 97, false, false) == move::colour::light_grey);
    CHECK(app::pad_light(sharp, track, 15, 97, true, false) == move::colour::white);
    // A pentatonic in octaves: fewer than eight notes go on into the next octave on the row.
    project->scale = 9; // Major pent
    app::set_keys_mode(track, app::KeysMode::InKey, false);
    CHECK(note(4) == 57 && note(5) == 60 && note(8) == 60);
    project->scale = 0;
    // The key's root moves the pads; the transpose too.
    project->root = 2;
    CHECK(note(0) == 50 && note(1) == 52);
    project->root = 0;
    track.transpose = -12;
    CHECK(note(0) == 36);
    track.transpose = 0;
    // CHORD on: the pad's note the root, the key's thirds stacked (a triad, close).
    track.chord = true;
    track.chord_type = 0;
    auto chord = app::pad_notes(*project, track, 0);
    CHECK(chord.count == 3 && chord.keys[0] == 48 && chord.keys[1] == 52 && chord.keys[2] == 55);
    chord = app::pad_notes(*project, track, 1); // D minor, in C major
    CHECK(chord.count == 3 && chord.keys[0] == 50 && chord.keys[1] == 53 && chord.keys[2] == 57);
    track.chord_type = 1; // Dia 7
    CHECK(app::pad_notes(*project, track, 0).count == 4 && app::pad_notes(*project, track, 0).keys[3] == 59);
    track.chord = false;
    // Prog: a chord of the progression a pad, at the file's notes on the Move's octave 0.
    auto& chord0 = track.prog.chords[0];
    chord0.count = 3; chord0.notes[0] = 60; chord0.notes[1] = 64; chord0.notes[2] = 67; chord0.root = true;
    track.prog.count = 1;
    app::set_keys_mode(track, app::KeysMode::Prog, false);
    auto prog = app::pad_notes(*project, track, 0);
    CHECK(prog.count == 3 && prog.keys[0] == 60 && prog.keys[2] == 67 && prog.root);
    CHECK(app::pad_notes(*project, track, 1).count == 0); // no chord there: silent, dark
    CHECK(app::pad_light(app::pad_notes(*project, track, 1), track, 15, 97, false, false) == move::colour::off);
    CHECK(app::keys_bank_count(track) == 2);
    // Drums: notes from 36 as a kit's pads, 16 a bank.
    app::set_keys_mode(track, app::KeysMode::Drums, false);
    CHECK(note(0) == 36 && note(31) == 67);
    track.bank = 1;
    CHECK(note(0) == 52);
    track.bank = 0;
    auto& midi = project->tracks[3];
    melodee::load_sound(midi, melodee::EngineKind::Midi, 0);
    app::set_keys_mode(midi, app::KeysMode::Drums, false);
    midi.midi.drum_note[2] = 40;
    CHECK(app::pad_notes(*project, midi, 2).keys[0] == 40); // a MIDI track's drum map
    // The pad map: a note track's route; a kit has none.
    app::PadMap map;
    track.arp = true;
    app::fill_pad_map(map, *project, 1, 0, true);
    CHECK(map.track == 1 && map.route == app::PadMap::Route::Arp && map.full_velocity && map.pads[0].count == 1);
    track.arp = false;
    app::fill_pad_map(map, *project, 1, 0, false);
    CHECK(map.route == app::PadMap::Route::Notes);
    melodee::load_sound(project->tracks[0], melodee::EngineKind::Sampler, 0);
    app::fill_pad_map(map, *project, 0, 0, false);
    CHECK(map.route == app::PadMap::Route::None);
    // The arp's settings, on last.
    track.arp = true; track.arp_mode = 2; track.arp_rate = 6;
    auto settings = app::arp_settings(track);
    CHECK(settings[4].first == 0 && settings[4].second == 1 && settings[0].second == 2 && settings[1].second == 6);
}

} // namespace

int main() {
    test_readers();
    test_sample_note();
    test_keys();
    test_keygroup_track();
    std::cout << "instruments-tests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
