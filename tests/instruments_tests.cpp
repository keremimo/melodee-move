// SPDX-License-Identifier: AGPL-3.0-only
// Melodee's instruments on the Move (docs/UI.md, "Instruments"): the
// instrument files read by Melodee's own readers into a keygroup's zones,
// their files landed and read in place, a sample's note found, a keygroup
// track playing them from the pads.
#include "app/auto_sampler.h"
#include "app/instruments.h"
#include "app/pad_layout.h"
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
#include "move/display.h"
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
bool has_suffix(const std::string& text, const std::string& end) { return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0; }
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

// A SoundFont of one preset ("Test Piano") playing one sound, a sine at
// middle C with a loop, written as the format lays it out.
bool write_soundfont(const std::string& path) {
    std::string out;
    auto u16 = [](std::string& s, unsigned v) { s += char(v & 0xff); s += char((v >> 8) & 0xff); };
    auto u32 = [&](std::string& s, uint32_t v) { u16(s, v & 0xffff); u16(s, v >> 16); };
    auto name = [](std::string& s, const char* text) { std::string n(text); n.resize(20, '\0'); s += n; };
    auto chunk = [&](const char* id, const std::string& body) { std::string c(id, 4); u32(c, uint32_t(body.size())); return c + body + (body.size() % 2 ? std::string(1, '\0') : ""); };
    const uint32_t frames = 22050;
    std::string smpl;
    for (uint32_t n = 0; n < frames + 46; ++n) u16(smpl, uint16_t(int16_t(n < frames ? std::lround(12000 * std::sin(2 * M_PI * 261.63 * n / 44100)) : 0)));
    std::string phdr, pbag, pmod(10, '\0'), pgen, inst, ibag, imod(10, '\0'), igen, shdr;
    name(phdr, "Test Piano"); u16(phdr, 0); u16(phdr, 0); u16(phdr, 0); u32(phdr, 0); u32(phdr, 0); u32(phdr, 0);
    name(phdr, "EOP"); u16(phdr, 0); u16(phdr, 0); u16(phdr, 1); u32(phdr, 0); u32(phdr, 0); u32(phdr, 0);
    u16(pbag, 0); u16(pbag, 0); u16(pbag, 1); u16(pbag, 0);
    u16(pgen, 41); u16(pgen, 0); u16(pgen, 0); u16(pgen, 0); // instrument 0
    name(inst, "Inst"); u16(inst, 0); name(inst, "EOI"); u16(inst, 1);
    u16(ibag, 0); u16(ibag, 0); u16(ibag, 1); u16(ibag, 0);
    u16(igen, 54); u16(igen, 1); u16(igen, 53); u16(igen, 0); u16(igen, 0); u16(igen, 0); // loop on, sample 0
    name(shdr, "Sine"); u32(shdr, 0); u32(shdr, frames); u32(shdr, 100); u32(shdr, frames - 100); u32(shdr, 44100);
    shdr += char(60); shdr += char(0); u16(shdr, 0); u16(shdr, 1);
    name(shdr, "EOS"); u32(shdr, 0); u32(shdr, 0); u32(shdr, 0); u32(shdr, 0); u32(shdr, 0); shdr += char(0); shdr += char(0); u16(shdr, 0); u16(shdr, 0);
    // igen's bag 1 starts after both gens
    ibag.replace(4, 2, std::string("\x02\x00", 2));
    std::string ifil; u16(ifil, 2); u16(ifil, 1);
    std::string info = "INFO" + chunk("ifil", ifil);
    std::string sdta = "sdta" + chunk("smpl", smpl);
    std::string pdta = "pdta" + chunk("phdr", phdr) + chunk("pbag", pbag) + chunk("pmod", pmod) + chunk("pgen", pgen) + chunk("inst", inst)
                     + chunk("ibag", ibag) + chunk("imod", imod) + chunk("igen", igen) + chunk("shdr", shdr);
    std::string body = "sfbk" + chunk("LIST", info) + chunk("LIST", sdta) + chunk("LIST", pdta);
    out = chunk("RIFF", body);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << out;
    return bool(file);
}

void test_soundfont() {
    // A SoundFont opens as a folder of its presets; a preset is an instrument whose sounds are decoded, laid out in
    // the cache as WAVs and read in place.
    const std::string folder = "build/test-instruments";
    fs::create_directories(folder);
    CHECK(write_soundfont(folder + "/gm.sf2"));
    CHECK(app::soundfont_file(folder + "/gm.sf2") && !app::instrument_file(folder + "/gm.sf2"));
    std::string error;
    auto presets = app::soundfont_presets(folder + "/gm.sf2", error);
    CHECK(error.empty() && presets.size() == 1 && presets[0].name == "Test Piano");
    auto path = app::preset_path(folder + "/gm.sf2", 0);
    CHECK(app::instrument_file(path) && app::instrument_format(path) == "SF2");
    auto preset = app::read_instrument_sample(path, error);
    CHECK(preset && error.empty() && preset->name == "Test Piano");
    if (!preset) return;
    fs::remove_all(folder + "/Cache");
    app::set_instrument_cache(folder + "/Cache");
    CHECK(app::land_part(*preset->instrument, *preset->data, 0, nullptr).empty());
    const auto& multi = *preset->data->multi;
    CHECK(multi.parts[0].frames.load() == 22050 && multi.parts[0].source != nullptr); // read in place from the cache
    CHECK(multi.zones[0].loop != melodee::ZoneLoop::Off && multi.zones[0].loop_start == 100);
    unsigned laid = 0;
    for (const auto& entry : fs::directory_iterator(folder + "/Cache")) laid += entry.path().extension() == ".wav";
    CHECK(laid == 1);
    // Again: the cache's file is used, not made twice.
    auto again = app::read_instrument_sample(path, error);
    CHECK(again && app::land_part(*again->instrument, *again->data, 0, nullptr).empty());
    laid = 0;
    for (const auto& entry : fs::directory_iterator(folder + "/Cache")) laid += entry.path().extension() == ".wav";
    CHECK(laid == 1);
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
        for (int block = 0, quiet = 0; block < 8000 && quiet < 100; ++block) quiet = loudest(melodee, 1) ? 0 : quiet + 1; // rung out
        // A pad with another job (a step held, Shift on a kit...): worked out, not sounded.
        melodee.studio().quiet_pads(true);
        melodee.audio_event(make_event(0x90, pad_note(9), 110));
        CHECK(loudest(melodee, 20) == 0);
        melodee.audio_event(make_event(0x80, pad_note(9), 0));
        melodee.studio().quiet_pads(false);
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
    // Two MPC banks (Kerem's reading (a)): In-Key and Chromatic run up in pad order, 1-16 the left 4x4, 17-32 the
    // right; Drums numbered the same. Physical pad 4 (bottom row, fifth column) is pad 17.
    app::set_pad_layout(app::PadLayout::Banks);
    app::set_keys_mode(track, app::KeysMode::InKey, false);
    CHECK(note(0) == 48 && note(1) == 50 && note(8) == 55 && note(4) == 48 + 24 + 4); // pad 5 (the left bank's second row) the fifth note, G; pad 17 the 17th, E
    app::set_keys_mode(track, app::KeysMode::Chromatic, true);
    CHECK(note(8) == 52 && note(4) == 64);
    app::set_keys_mode(track, app::KeysMode::Drums, false);
    CHECK(note(8) == 40 && note(4) == 52);
    app::set_pad_layout(app::PadLayout::Rows);
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

void test_instrument_keys() {
    // The Move's keys this area answers (docs/MOVE_CONTROLS.md): Shift + Step 10 Full Velocity, Shift + Plus / Minus
    // a note of the key on a note track; macros, wheel and bend kept on the track.
    const std::string home = "build/test-home-instrument-keys";
    fs::remove_all(home);
    fs::create_directories(home + "/Projects");
    app::MelodeeApp melodee("build/test-instruments", home, "build/test-no-modules");
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    CHECK(melodee.restore_now().empty());
    auto& studio = melodee.studio();
    auto button = [&](Button b, bool down) { melodee.surface(make_event(0xb0, button_cc(b), down ? 127 : 0)); };
    auto step = [&](unsigned n) { melodee.surface(make_event(0x90, step_note(n), 127)); melodee.surface(make_event(0x80, step_note(n), 0)); };
    button(Button::Shift, true);
    step(9); // Step 10
    button(Button::Shift, false);
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.full_velocity());
    }
    button(Button::Shift, true);
    step(9);
    button(Button::Shift, false);
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(!studio.full_velocity());
        CHECK(studio.new_track(4, app::TrackKind::Keys));
        studio.select(4);
    }
    button(Button::Shift, true);
    button(Button::Up, true); button(Button::Up, false); // Plus
    button(Button::Shift, false);
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.key_shift(4) == 1);
        studio.set_key(2, 1); // D minor, the project's
        CHECK(studio.project().root == 2 && studio.project().scale == 1);
        studio.set_keys(4, app::KeysMode::Chromatic, true);
        CHECK(studio.project().tracks[4].layout == melodee::PadLayout::Chromatic);
        studio.set_transpose(4, 30);
        CHECK(studio.project().tracks[4].transpose == 12);
    }
    // A chord stored into a progression: saved as Mine 1, the track on Prog.
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.store_chord(4, 0, {60, 64, 67, 71}).empty());
        const auto& track = studio.project().tracks[4];
        CHECK(track.layout == melodee::PadLayout::Prog && track.prog.chords[0].count == 4 && std::string(track.prog.file) == "Mine 1");
        CHECK(fs::exists(home + "/Progressions/Mine 1.progression"));
        auto found = studio.progressions();
        CHECK(!found.empty() && has_suffix(found.front(), "Mine 1.progression"));
        CHECK(studio.store_chord(4, 1, {62, 65, 69}).empty()); // the same file again
        CHECK(!fs::exists(home + "/Progressions/Mine 2.progression"));
    }
    {   // Read back into another track: both chords there.
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.new_track(6, app::TrackKind::Keys));
        CHECK(studio.load_progression(6, home + "/Progressions/Mine 1.progression").empty());
        const auto& track = studio.project().tracks[6];
        CHECK(track.layout == melodee::PadLayout::Prog && track.prog.count == 2 && track.prog.chords[1].count == 3);
    }
}

void test_auto_sampler() {
    // The auto sampler on a module of its own (the tests' stand-in, a square whose pitch does not follow the keys:
    // drums to the check), into an MPC keygroup program Melodee's reader plays back.
    auto modules = app::scan_modules("build/test-modules");
    const app::ModuleInfo* fake = nullptr;
    for (const auto& info : modules) if (info.id == "fake") fake = &info;
    CHECK(fake != nullptr);
    if (!fake) return;
    app::SynthFile state;
    state.values.push_back({"level", "0.9"});
    std::string error;
    auto source = app::module_source(*fake, state, error);
    CHECK(source && error.empty());
    if (!source) return;
    melodee::AutoSampleSettings settings;
    settings.low = 36;
    settings.high = 40;
    settings.step = 1;
    std::vector<std::string> rows;
    auto found = app::check_source(*source, settings, &rows);
    CHECK(found.heard && found.kind == melodee::AutoKind::Drums && found.sounding() == 5);
    CHECK(!rows.empty() && rows[0] == "DRUMS, 5 SOUNDS");
    CHECK(found.takes == 1 && found.speed > 1.0f); // faster than real time
    // Sampled whole: five one-shot keygroups.
    const std::string folder = "build/test-auto/Keygroups";
    fs::remove_all("build/test-auto");
    app::AutoSampler sampler;
    app::AutoSampler::Job job;
    job.track = 1; job.name = "Fake Kit"; job.folder = folder; job.sample = true;
    CHECK(sampler.start(job, settings, [&](std::string& why) { return app::module_source(*fake, state, why); }));
    for (int n = 0; n < 3000 && sampler.running(); ++n) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(sampler.phase_now() == app::AutoSampler::Phase::Done);
    CHECK(sampler.done() == 5 && sampler.total() == 5);
    auto program = sampler.program();
    CHECK(fs::exists(program) && has_suffix(program, "Fake Kit/Fake Kit.xpm"));
    CHECK(!fs::exists(folder + "/Fake Kit/.parts"));
    auto read = app::read_instrument_sample(program, error);
    CHECK(read && error.empty());
    if (read) {
        const auto& multi = *read->data->multi;
        CHECK(multi.zones.size() == 5 && multi.zones[0].low_key == 36 && multi.zones[0].high_key == 36 && multi.zones[4].root == 40);
        CHECK(app::land_part(*read->instrument, *read->data, 0, nullptr).empty() && multi.parts[0].frames.load() > 4000);
    }
    {   // From a module track in the studio: the program on the first empty track, a keygroup with its patterns.
        const std::string home = "build/test-home-auto";
        fs::remove_all(home);
        fs::create_directories(home + "/Projects");
        app::MelodeeApp melodee("build/test-instruments", home, "build/test-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        auto& studio = melodee.studio();
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            studio.select(1); // a new project's synth track
            melodee::Event e;
            e.key = 37; e.length = 4;
            studio.patterns().at[1][0].events.push_back(e);
        }
        CHECK(melodee.load_module_now("fake").empty());
        melodee::AutoSampleSettings few;
        few.low = 36; few.high = 38; few.step = 1;
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            CHECK(studio.auto_sample(1, few, true).empty());
            CHECK(studio.auto_sample(0, few, true) == "NO MODULE");
        }
        for (int n = 0; n < 3000 && studio.auto_sampled < 0; ++n) { melodee.service(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.auto_sampled == 2);
        CHECK(studio.kind(2) == app::TrackKind::Keys && studio.keys_sample(2) && studio.keys_sample(2)->instrument);
        CHECK(studio.patterns().at[2][0].events.size() == 1 && studio.patterns().at[2][0].events[0].key == 37);
        CHECK(fs::exists(studio.keygroups_folder()));
    }
    {   // melodee-autosample's words, as Melodee takes them.
        app::AutoCommand command;
        std::string why;
        CHECK(app::parse_autosample("3 c2-c5 step 4 layers 2 hold 3 tail 6 post loop peak check", command, why) && why.empty());
        CHECK(command.track == 2 && command.check && command.settings.low == 36 && command.settings.high == 72 && command.settings.step == 4);
        CHECK(command.settings.layers == 2 && !command.settings.hold_auto && command.settings.hold == 3.0f && command.settings.tail == 6.0f);
        CHECK(command.settings.post_fx && command.settings.loop && !command.settings.loop_auto && command.settings.normalize);
        CHECK(app::parse_autosample("2 40-50", command, why) && command.settings.low == 40 && command.settings.high == 50);
        CHECK(!app::parse_autosample("17", command, why) && !why.empty());
        CHECK(!app::parse_autosample("1 step 99", command, why));
        CHECK(!app::parse_autosample("1 sideways", command, why) && why == "not known: sideways");
    }
    // Called off: what was made goes.
    job.name = "Gone";
    settings.high = 60;
    CHECK(sampler.start(job, settings, [&](std::string& why) { return app::module_source(*fake, state, why); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sampler.cancel();
    CHECK(!fs::exists(folder + "/Gone/Gone.xpm"));
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void test_screens() {
    // The instrument screens through the Move's own controls (docs/MOVE_CONTROLS.md, docs/UI.md "Instruments").
    const std::string home = "build/test-home-screens";
    fs::remove_all(home);
    fs::create_directories(home + "/Projects");
    app::MelodeeApp melodee("build/test-instruments", home, "build/test-modules");
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    CHECK(melodee.restore_now().empty());
    auto& studio = melodee.studio();
    auto cc = [&](uint8_t number, uint8_t value) { melodee.surface(make_event(0xb0, number, value)); };
    auto button = [&](Button b) { cc(button_cc(b), 127); cc(button_cc(b), 0); };
    auto shifted = [&](auto&& what) { cc(button_cc(Button::Shift), 127); what(); cc(button_cc(Button::Shift), 0); std::this_thread::sleep_for(std::chrono::milliseconds(320)); };
    auto wheel = [&](int detents) { cc(main_knob_cc, uint8_t(detents > 0 ? detents : 128 + detents)); };
    auto knob = [&](unsigned n, int detents) { cc(uint8_t(knob_first_cc + n), uint8_t(detents > 0 ? detents : 128 + detents)); };
    auto step = [&](unsigned n) { melodee.surface(make_event(0x90, step_note(n), 127)); melodee.surface(make_event(0x80, step_note(n), 0)); };
    auto pad = [&](unsigned n, bool down) { melodee.surface(make_event(down ? 0x90 : 0x80, pad_note(n), down ? 100 : 0)); };
    // A keygroup on track 3, the SFZ on it.
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.new_track(2, app::TrackKind::Keys));
        studio.select(2);
        studio.jobs.push_back({"build/test-instruments/piano.sfz", 2, 0});
    }
    melodee.service();
    CHECK(has(melodee.status(), "page=keys-play") && has(melodee.status(), "keys_page=1"));
    // Shift + the wheel: its pages. PLAY: knob 1 the tune.
    shifted([&] { wheel(1); });
    CHECK(has(melodee.status(), "keys_page=2"));
    knob(0, 2);
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.project().tracks[2].pads[0].pitch == 2);
    }
    // To CHORD, the last page: knob 1 turns chords on.
    shifted([&] { wheel(1); wheel(1); wheel(1); wheel(1); wheel(1); });
    CHECK(has(melodee.status(), "keys_page=6"));
    knob(0, 1);
    CHECK(has(melodee.status(), "chord=1"));
    knob(0, -1);
    // The wheel's push on the instrument: the instrument browser, one place (--samples): straight in.
    button(Button::MainPush);
    CHECK(has(melodee.status(), "page=load-instruments") && !has(melodee.status(), "browse=0"));
    button(Button::Back);
    CHECK(has(melodee.status(), "page=keys-play"));
    // Shift + Step 9: Keys & Scale. MODE pushed, turned to Chromatic, pushed; Back out.
    shifted([&] { step(8); });
    CHECK(has(melodee.status(), "page=keys-scale") && has(melodee.status(), "keys_row=0"));
    button(Button::MainPush);
    CHECK(has(melodee.status(), "keys_editing=1"));
    wheel(1);
    button(Button::MainPush);
    CHECK(has(melodee.status(), "keys=CHROMATIC") && has(melodee.status(), "keys_editing=0"));
    // KEY: D; SCALE: the next of the Move's (Minor).
    wheel(1); // KEY (Chromatic has no ROWS)
    button(Button::MainPush); wheel(2); button(Button::MainPush);
    wheel(1); // SCALE
    button(Button::MainPush); wheel(1); button(Button::MainPush);
    CHECK(has(melodee.status(), "key=2/1"));
    // STORE CHORD with two pads held: the store sheet, a pad takes it, saved as Mine 1.
    wheel(1); wheel(1); // TRANSPOSE, STORE CHORD
    pad(0, true); pad(4, true);
    button(Button::MainPush);
    pad(0, false); pad(4, false);
    CHECK(has(melodee.status(), "page=store-chord"));
    pad(3, true); pad(3, false);
    CHECK(fs::exists(home + "/Progressions/Mine 1.progression"));
    CHECK(has(melodee.status(), "keys=PROG"));
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.project().tracks[2].prog.chords[3].count == 2);
    }
    button(Button::Back); // out of Keys & Scale
    CHECK(has(melodee.status(), "page=keys-play"));
    // Prog: Shift + Plus the next 16 chords; Drums: Plus the next 16 drums.
    shifted([&] { button(Button::Up); });
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.project().tracks[2].bank == 1);
        studio.set_keys(2, app::KeysMode::Drums, false);
        studio.current().bank = 0;
    }
    button(Button::Up);
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.project().tracks[2].bank == 1 && app::pad_notes(studio.project(), studio.current(), 0).keys[0] == 52);
        studio.set_keys(2, app::KeysMode::InKey, false);
    }
    // A module track: the module's page first, knob 1 its level, no
    // FILTER (Kerem, 2026-11-20). Then its menu's AUTO SAMPLE, the check,
    // sampling, the new track.
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        studio.select(1);
    }
    CHECK(melodee.load_module_now("fake").empty());
    auto level = [&] {
        std::lock_guard<std::mutex> guard(studio.lock);
        auto* track = studio.module_track(1);
        auto* param = track && track->module ? track->module->param("level") : nullptr;
        return param ? param->value : -1.0;
    };
    double before = level();
    knob(0, -10);
    CHECK(before >= 0.1 && std::abs(level() - (before - 0.1)) < 1e-6); // ten steps of 0.01 down
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.project().tracks[1].params[melodee::plugin_track::Macro1] == 0);
    }
    shifted([&] { button(Button::MainPush); }); // the module's menu
    wheel(40);                                   // its last row: AUTO SAMPLE
    button(Button::MainPush);
    CHECK(has(melodee.status(), "page=auto-sample"));
    for (int n = 0; n < 2000 && !has(melodee.status(), "auto_sheet=CHECKED"); ++n) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(has(melodee.status(), "auto_sheet=CHECKED"));
    knob(0, 36 - 36); // LOW stays C1 (36)
    knob(1, -55);      // HIGH down to 41
    knob(2, -2);       // STEP 1
    button(Button::MainPush);
    for (int n = 0; n < 3000 && studio.auto_sampled < 0; ++n) { melodee.service(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    CHECK(studio.auto_sampled == 2 + 1); // track 4: the first empty one
    button(Button::MainPush); // go to it
    CHECK(has(melodee.status(), "track=4 kind=keys"));
}

} // namespace

// The screen as the Move shows it, into build/instruments-screens/NAME.png (four times its size), to look at.
void snap(app::MelodeeApp& app, const std::string& name) {
    juce::Image image(juce::Image::ARGB, move::screen_width, move::screen_height, true);
    {
        juce::Graphics g(image);
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        app.draw(g);
    }
    fs::create_directories("build/instruments-screens");
    auto file = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String("build/instruments-screens/" + name + ".png"));
    file.deleteFile();
    juce::FileOutputStream output(file);
    if (output.openedOk()) juce::PNGImageFormat().writeImageToStream(move::frame_image(move::pack(image), 4), output);
}

void test_kit_instrument() {
    // An instrument on a kit pad (Melodee's SFZ on a pad): loaded as a sample is, its files landing, a hit playing key 60
    // plus the pad's Semi (here the SFZ's second zone); the pad's TRIM page is ZONES there, a zone walked and trimmed on
    // that pad without touching the file; a layer refuses it; saved and opened again, the instrument and its trims back.
    const std::string folder = "build/test-instruments", home = "build/test-home-kit-instrument";
    fs::remove_all(home);
    fs::create_directories(home + "/Projects");
    AudioBlock in {}, out {};
    auto loudest = [&](app::MelodeeApp& melodee, int blocks) {
        int32_t peak = 0;
        for (int block = 0; block < blocks; ++block) { out.fill(0); melodee.render(in, out); peak = std::max(peak, peak_of(out)); }
        return peak;
    };
    auto landed = [](app::MelodeeApp& melodee) {
        for (int n = 0; n < 200; ++n) {
            melodee.service();
            {
                std::lock_guard<std::mutex> guard(melodee.studio().lock);
                const auto* sample = melodee.studio().sample_of(0, 0);
                if (sample && sample->instrument && sample->instrument->done()) return true;
            }
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
        auto cc = [&](uint8_t number, uint8_t value) { melodee.surface(make_event(0xb0, number, value)); };
        auto knob = [&](unsigned n, int detents) { cc(uint8_t(knob_first_cc + n), uint8_t(detents > 0 ? detents : 128 + detents)); };
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            CHECK(studio.kind(0) == app::TrackKind::Kit);
            studio.select(0);
            studio.jobs.push_back({folder + "/piano.sfz", 0, 0});
        }
        CHECK(landed(melodee));
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            const auto* sample = studio.sample_of(0, 0);
            CHECK(sample && sample->instrument && sample->instrument->landed.load() == 2 && sample->data->multi->zones.size() == 2);
            CHECK(studio.project().tracks[0].pads[0].used());
        }
        loudest(melodee, 4);
        melodee.audio_event(make_event(0x90, pad_note(0), 110));
        CHECK(loudest(melodee, 20) > to_s32(0.02f));
        melodee.audio_event(make_event(0x80, pad_note(0), 0));
        // Shift + the wheel: the pad pages, TRIM's place is ZONES.
        cc(button_cc(Button::Shift), 127);
        cc(main_knob_cc, 1);
        cc(button_cc(Button::Shift), 0);
        CHECK(has(melodee.status(), "kit_page=ZONES") && has(melodee.status(), "kit_zone=1"));
        knob(0, 1); // the second zone in hand
        CHECK(has(melodee.status(), "kit_zone=2"));
        knob(1, 7);  // ZONE TUNE +7 cents
        knob(2, -2); // ZONE LEVEL -1 dB
        std::this_thread::sleep_for(std::chrono::milliseconds(2500)); // the knob's big view and the message gone
        snap(melodee, "kit-zones");
        {
            std::lock_guard<std::mutex> guard(studio.lock);
            const auto& kit = studio.project().tracks[0];
            const auto* trim = melodee::find_trim(kit.zone_trims, 0, 1, kit.pads[0].sample);
            CHECK(trim && trim->tune == 7 && trim->level == -10);
            CHECK(!melodee::find_trim(kit.zone_trims, 0, 0, kit.pads[0].sample));
            // Onto a layer: refused.
            studio.layer_target = {0, 0, 1};
            std::string error;
            auto again = app::read_any_sample(folder + "/piano.sfz", error);
            CHECK(again && studio.install(std::move(again), 0, 0) == "NOT ON A LAYER");
            studio.layer_target = {};
        }
        CHECK(melodee.save_now().empty());
    }
    {   // Opened again: the pad plays the instrument, its trim kept.
        app::MelodeeApp melodee(folder, home, "build/test-no-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        CHECK(landed(melodee));
        auto& studio = melodee.studio();
        std::lock_guard<std::mutex> guard(studio.lock);
        const auto& kit = studio.project().tracks[0];
        const auto* trim = melodee::find_trim(kit.zone_trims, 0, 1, kit.pads[0].sample);
        CHECK(trim && trim->tune == 7 && trim->level == -10);
    }
}

int main() {
    test_readers();
    test_sample_note();
    test_soundfont();
    test_keys();
    test_instrument_keys();
    test_auto_sampler();
    test_screens();
    test_keygroup_track();
    test_kit_instrument();
    std::cout << "instruments-tests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
