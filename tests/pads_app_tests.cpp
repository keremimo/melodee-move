// SPDX-License-Identifier: AGPL-3.0-only
// The pad layouts in Melodee (docs/UI.md, "Pads"): every pad job in both
// layouts, through the Move app as a hand uses it. A kit's pads and their
// lights, Chop's slices, a synth's notes (Kerem's reading (a): 1-32 up the
// semitones in Banks), the setting (Setup > PADS > LAYOUT) and
// Move.settings.
// Run from the repository's root (./build.sh does): it reads
// build/test-modules, the stand-in Schwung module.
#include "app/melodee_app.h"
#include "app/pad_layout.h"
#include "app/screens/edit_preview.h"
#include "move/audio.h"
#include "move/controls.h"
#include "move/lights.h"
#include "move/tools.h"
#include "ui/track_style.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

using namespace move;
using app::PadLayout;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void press_button(App& app, Button button) {
    app.surface(make_event(0xb0, button_cc(button), 127));
    app.surface(make_event(0xb0, button_cc(button), 0));
}
template <class Do> void with_shift(App& app, Do&& what) {
    app.surface(make_event(0xb0, button_cc(Button::Shift), 127));
    what();
    app.surface(make_event(0xb0, button_cc(Button::Shift), 0));
}
void turn_wheel(App& app, int detents) { app.surface(make_event(0xb0, main_knob_cc, uint8_t(detents < 0 ? 128 + detents : detents))); }

// A physical pad hit as the runtime hands it on: the MIDI thread's job,
// then the audio thread's sound.
void hit(App& app, unsigned physical, bool down) {
    auto event = make_event(down ? 0x90 : 0x80, pad_note(physical), down ? 110 : 0);
    app.surface(event);
    app.audio_event(event);
}

int32_t peak_of(const AudioBlock& block) {
    int32_t peak = 0;
    for (auto sample : block) peak = std::max(peak, std::abs(sample));
    return peak;
}

// Setup > PADS > LAYOUT: Setup's PADS list (move-projects') pushes
// `setup-pads` from its LAYOUT row; here it is pushed as that row does. The
// wheel turns the layout, push sets it and goes back.
void edit_layout(app::MelodeeApp& melodee) {
    std::lock_guard<std::mutex> guard(melodee.studio().lock);
    melodee.studio().push("setup-pads");
}
void choose_layout(app::MelodeeApp& melodee, PadLayout layout) {
    edit_layout(melodee);
    turn_wheel(melodee, layout == PadLayout::Banks ? 1 : -1);
    press_button(melodee, Button::MainPush);
}

// The pads' lights as the app sends them now: physical pad -> palette.
std::map<unsigned, uint8_t> pad_lights(app::MelodeeApp& melodee) {
    Lights lights;
    melodee.lights(lights, App::Clock::now());
    std::map<unsigned, uint8_t> pads;
    lights.update([&](const uint8_t* message) {
        if ((message[0] & 0xf0) == 0x90 && message[1] >= pad_first_note && message[1] < pad_first_note + pad_count)
            pads[message[1] - pad_first_note] = message[2];
        return true;
    }, 1000);
    return pads;
}

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

void test_kit_and_chop() {
    std::string folder = "build/test-samples", loop = folder + "/loop.wav", home = "build/test-home-pads";
    std::filesystem::create_directories(folder);
    CHECK(write_test_loop(loop));
    std::filesystem::remove_all(home);
    {
        app::MelodeeApp melodee(folder, home);
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(app::pad_layout() == PadLayout::Rows); // a new Move: the Move's own rows
        CHECK(has(melodee.status(), "layout=rows"));
        CHECK(melodee.load_now(loop, 16).empty()); // kit pad B01
        AudioBlock in {}, out {};
        auto loudest = [&](int blocks) {
            int32_t peak = 0;
            for (int block = 0; block < blocks; ++block) { out.fill(0); melodee.render(in, out); peak = std::max(peak, peak_of(out)); }
            return peak;
        };
        auto quiet = [&] {
            for (int block = 0; block < 4000; ++block) { out.fill(0); melodee.render(in, out); if (!peak_of(out)) return true; }
            return false;
        };
        auto play = [&](unsigned physical) {
            hit(melodee, physical, true);
            auto peak = loudest(20);
            hit(melodee, physical, false);
            CHECK(quiet());
            return peak;
        };

        // Rows: B01 is the third row's first pad (physical 16).
        CHECK(play(16) > to_s32(0.05f));
        CHECK(has(melodee.status(), "pad=B01"));
        CHECK(play(4) == 0); // A05, empty

        // Setup > PADS > LAYOUT turned to Banks, then Back: nothing changes,
        // but while it is turned the pads light Banks' numbering (pad 17 white
        // at the bottom row's fifth pad, pad 5 azure above pad 1).
        edit_layout(melodee);
        CHECK(has(melodee.status(), "page=setup-pads"));
        turn_wheel(melodee, 1);
        auto numbering = pad_lights(melodee);
        CHECK(numbering[0] == colour::white && numbering[4] == colour::white);
        CHECK(numbering[8] == colour::azure && numbering[28] == colour::violet);
        press_button(melodee, Button::Back);
        CHECK(app::pad_layout() == PadLayout::Rows);
        CHECK(has(melodee.status(), "page=kit-play"));
        // Banks, set: B01 is the right bank's first pad (physical 4).
        choose_layout(melodee, PadLayout::Banks);
        CHECK(app::pad_layout() == PadLayout::Banks);
        CHECK(has(melodee.status(), "layout=banks"));
        CHECK(has(melodee.status(), "page=kit-play"));
        CHECK(play(16) == 0); // the left bank's third row: A09, empty
        CHECK(play(4) > to_s32(0.05f));
        CHECK(has(melodee.status(), "pad=B01"));
        // The lights: B01, the last pad, bright in its colour where it lies,
        // white while held (a press flashes, then fades back to it); A01
        // (physical 0) dark.
        auto lit = pad_lights(melodee);
        CHECK(lit[4] == colour::white || !melodee.studio().sounding()[16]); // white while it still sounds
        for (int block = 0; block < 20000 && melodee.studio().sounding()[16]; ++block) { out.fill(0); melodee.render(in, out); }
        lit = pad_lights(melodee);
        uint8_t marker = ui::track_colour(melodee.studio().track()).bright;
        CHECK(lit[4] == marker && marker != colour::white);
        session.held_pads = 1u << 4;
        CHECK(pad_lights(melodee)[4] == colour::white);
        session.held_pads = 0;
        CHECK(pad_lights(melodee)[4] == marker);
        CHECK(lit[16] == colour::off);
        CHECK(lit[0] == colour::off);
        // A pad picked quietly (Shift + pad) by its number too: physical 9 is A06.
        with_shift(melodee, [&] { hit(melodee, 9, true); hit(melodee, 9, false); });
        CHECK(has(melodee.status(), "pad=A06"));
        CHECK(pad_lights(melodee)[9] == colour::dark_grey); // the last pad, empty
        hit(melodee, 4, true); hit(melodee, 4, false);       // back to B01
        CHECK(quiet());

        // END turned (knob 5), then START (knob 4): each heard once still
        // for 150 ms (edit_preview.h), not while the knob may still turn.
        auto knob = [&](unsigned n, int detents) { melodee.surface(make_event(0xb0, uint8_t(knob_first_cc + n), uint8_t(detents < 0 ? 128 + detents : detents))); };
        auto heard_once_still = [&] {
            pad_lights(melodee);
            bool early = loudest(20) == 0; // not while the knob may still turn
            std::this_thread::sleep_for(app::screens::EditPreview::still + std::chrono::milliseconds(50));
            pad_lights(melodee);                        // lights() hears it
            bool heard = loudest(200) > to_s32(0.02f);  // within 0.6 s: the loop has silent sixteenths
            loudest(2000);                              // played out: the region is 4 s at most
            pad_lights(melodee);
            return early && heard && loudest(400) == 0; // once: nothing repeats
        };
        knob(4, -20);
        CHECK(heard_once_still());
        knob(3, 20);
        CHECK(heard_once_still());
        knob(3, -20); // the region back, for Chop's
        knob(4, 20);
        CHECK(heard_once_still());
        // The same on the pad pages' TRIM (Shift + the wheel): END is knob 2.
        with_shift(melodee, [&] { turn_wheel(melodee, 1); });
        CHECK(has(melodee.status(), "kit_page=TRIM"));
        knob(1, -20);
        CHECK(heard_once_still());
        knob(1, 20);
        CHECK(heard_once_still());
        press_button(melodee, Button::Back);
        CHECK(has(melodee.status(), "page=kit-play"));

        // Chop in Banks: slice n on pad n; slice 5 is the left bank's second row (physical 8).
        with_shift(melodee, [&] { press_button(melodee, Button::MainPush); });
        press_button(melodee, Button::MainPush);
        CHECK(has(melodee.status(), "page=chop "));
        unsigned slices = melodee.slice_count();
        CHECK(slices >= 6 && slices <= 32);
        CHECK(play(8) > to_s32(0.02f));
        auto chop_lit = pad_lights(melodee);
        CHECK(chop_lit[8] == colour::white);  // slice 5 picked, white where it lies
        CHECK(chop_lit[0] != colour::off);    // slice 1
        CHECK(chop_lit[4] == (slices > 16 ? chop_lit[4] : colour::off)); // pad 17: a slice only past 16
        if (slices < 32) CHECK(play(app::physical_pad(31, PadLayout::Banks)) == 0); // pad 32: no slice
        press_button(melodee, Button::Back); // leaves Chop, the pads as the kit has them
        CHECK(has(melodee.status(), "page=kit-play"));
        melodee.save_now();
    }
    // The choice is the Move's, kept in Move.settings, back on the next start.
    CHECK(has(read_file(home + "/Move.settings"), "pads 1"));
    {
        app::set_pad_layout(PadLayout::Rows); // as a fresh process
        app::MelodeeApp melodee(folder, home);
        Session session;
        melodee.attach(session);
        CHECK(melodee.restore_now().empty());
        CHECK(app::pad_layout() == PadLayout::Banks);
        CHECK(has(melodee.status(), "layout=banks"));
        // Back to Rows: said, and saved.
        choose_layout(melodee, PadLayout::Rows);
        CHECK(app::pad_layout() == PadLayout::Rows);
        melodee.save_now();
    }
    CHECK(has(read_file(home + "/Move.settings"), "pads 0"));
}

// What a moved start or end plays of its region (Melodee's Chop): a start
// from 300 ms before it to the region's end; an end its last second, or the
// whole region when shorter.
void test_edit_preview_stretch() {
    using app::screens::EditPreview;
    auto start = EditPreview::stretch(44100, 441000, false);
    CHECK(start.from == 44100 - 13230 && start.to == 441000);
    start = EditPreview::stretch(1000, 441000, false);
    CHECK(start.from == 0 && start.to == 441000);
    auto end = EditPreview::stretch(44100, 441000, true);
    CHECK(end.from == 441000 - 44100 && end.to == 441000);
    end = EditPreview::stretch(44100, 66150, true);
    CHECK(end.from == 44100 && end.to == 66150);
}

// A keygroup's one sample: its TRIM (keys_page 1) START and END heard as a
// kit pad's are, once still (edit_preview.h).
void test_keys_trim_heard() {
    std::string folder = "build/test-samples", loop = folder + "/loop.wav", home = "build/test-home-pads-keys";
    std::filesystem::create_directories(folder);
    CHECK(write_test_loop(loop));
    std::filesystem::remove_all(home);
    app::MelodeeApp melodee(folder, home);
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    auto& studio = melodee.studio();
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.new_track(2, app::TrackKind::Keys));
        studio.select(2);
        studio.jobs.push_back({loop, 2, 0});
    }
    melodee.service();
    CHECK(has(melodee.status(), "page=keys-play") && has(melodee.status(), "keys_page=1"));
    AudioBlock in {}, out {};
    auto loudest = [&](int blocks) {
        int32_t peak = 0;
        for (int block = 0; block < blocks; ++block) { out.fill(0); melodee.render(in, out); peak = std::max(peak, peak_of(out)); }
        return peak;
    };
    auto knob = [&](unsigned n, int detents) { melodee.surface(make_event(0xb0, uint8_t(knob_first_cc + n), uint8_t(detents < 0 ? 128 + detents : detents))); };
    auto heard_once_still = [&] {
        pad_lights(melodee);
        bool early = loudest(20) == 0;
        std::this_thread::sleep_for(app::screens::EditPreview::still + std::chrono::milliseconds(50));
        pad_lights(melodee);
        bool heard = loudest(200) > to_s32(0.02f);
        loudest(2000);
        pad_lights(melodee);
        return early && heard && loudest(400) == 0;
    };
    loudest(400); // whatever the load played
    knob(1, -20); // END
    CHECK(heard_once_still());
    knob(0, 20);  // START
    CHECK(heard_once_still());
    knob(2, 1);   // LOOP: not a place, nothing heard
    pad_lights(melodee);
    std::this_thread::sleep_for(app::screens::EditPreview::still + std::chrono::milliseconds(50));
    pad_lights(melodee);
    CHECK(loudest(200) == 0);
}

void test_synth_notes() {
    std::string folder = "build/test-samples", home = "build/test-home-pads-synth", modules = "build/test-modules";
    std::filesystem::remove_all(home);
    app::MelodeeApp melodee(folder, home, modules);
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    CHECK(melodee.restore_now().empty());
    press_button(melodee, Button::Row2);           // track 2, a synth
    press_button(melodee, Button::MainPush);       // its modules
    press_button(melodee, Button::MainPush);       // the stand-in
    melodee.service();
    CHECK(has(melodee.status(), "synth=fake"));
    AudioBlock in {}, out {};
    // The module's note held, asked as the screen asks (the audio thread
    // answers between renders: here this thread renders while another asks).
    auto note = [&](unsigned physical) {
        hit(melodee, physical, true);
        out.fill(0);
        melodee.render(in, out);
        auto asked = std::async(std::launch::async, [&] { return melodee.ask_module("note"); });
        while (asked.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) { out.fill(0); melodee.render(in, out); }
        auto held = asked.get();
        hit(melodee, physical, false);
        out.fill(0);
        melodee.render(in, out);
        return held;
    };
    // Keys & Scale's Chromatic in 4ths (move-instruments; a new Set plays
    // In-Key in octaves), so the notes are the pad map's arithmetic alone.
    {
        std::lock_guard<std::mutex> guard(melodee.studio().lock);
        melodee.studio().set_keys(1, app::KeysMode::Chromatic, true);
    }
    // Rows: right a semitone, up a fourth, C2 (48) bottom left.
    CHECK(note(0) == "48");
    CHECK(note(4) == "52");
    CHECK(note(8) == "53");
    CHECK(note(31) == "70");
    // Banks, reading (a): 1-32 up the semitones, the right bank going on.
    choose_layout(melodee, PadLayout::Banks);
    CHECK(note(0) == "48");  // pad 1: C2
    CHECK(note(3) == "51");  // pad 4
    CHECK(note(8) == "52");  // pad 5: the left bank's second row
    CHECK(note(27) == "63"); // pad 16: the left bank's top right
    CHECK(note(4) == "64");  // pad 17: the right bank's bottom left, the next semitone
    CHECK(note(31) == "79"); // pad 32: G4
    // The lights: every C in the track's colour wherever it falls: pads 1, 13 and 25.
    auto lit = pad_lights(melodee);
    uint8_t c_colour = lit[0];
    CHECK(c_colour != colour::off && c_colour != colour::dark_grey);
    CHECK(lit[app::physical_pad(12, PadLayout::Banks)] == c_colour); // C3
    CHECK(lit[app::physical_pad(24, PadLayout::Banks)] == c_colour); // C4
    CHECK(lit[app::physical_pad(1, PadLayout::Banks)] == colour::off); // C#2: not a natural
    // An octave up (Plus) moves every pad, the layout kept.
    press_button(melodee, Button::Up);
    CHECK(note(4) == "76");
    choose_layout(melodee, PadLayout::Rows);
}

} // namespace

int main() {
    test_edit_preview_stretch();
    test_kit_and_chop();
    test_keys_trim_heard();
    test_synth_notes();
    std::cout << "pads-app-tests: " << checks - failures << '/' << checks << " passed\n";
    return failures ? 1 : 0;
}
