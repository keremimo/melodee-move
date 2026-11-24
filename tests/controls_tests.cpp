// SPDX-License-Identifier: AGPL-3.0-only
// The controls map: docs/MOVE_CONTROLS.md against src/app/bindings.*, row
// for row, every row answering where it says, the decisions Kerem took, and
// the gestures (tap, hold, modifier, double press, Shift lock).
// Run from the repository's root (./build.sh does), or give the doc's path.
#include "app/bindings.h"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace app::bindings;
using Clock = Gestures::Clock;
using namespace std::chrono_literals;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)
#define CHECK_SAID(condition, said) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition ": " << said << "\n"; } } while (0)

std::string trim(const std::string& text) {
    size_t from = text.find_first_not_of(" \t"), to = text.find_last_not_of(" \t");
    return from == std::string::npos ? std::string() : text.substr(from, to - from + 1);
}

struct DocRow { std::string control, status; };

// The rows of every table under "## The map", up to "## Lights".
std::vector<DocRow> read_map(const std::string& path) {
    std::ifstream file(path);
    std::vector<DocRow> rows;
    std::string line;
    bool in_map = false;
    while (std::getline(file, line)) {
        if (line.rfind("## ", 0) == 0) in_map = line == "## The map";
        if (!in_map || line.rfind("| ", 0) != 0 || line.rfind("| Control |", 0) == 0 || line.rfind("| ---", 0) == 0) continue;
        std::vector<std::string> cells;
        std::stringstream split(line);
        std::string cell;
        while (std::getline(split, cell, '|')) cells.push_back(trim(cell));
        if (cells.size() < 5) continue;
        rows.push_back({cells[1], cells[4]});
    }
    return rows;
}

// Doc row and table agree: the same controls, the same status and area.
void test_doc_and_table(const std::string& doc) {
    auto rows = read_map(doc);
    CHECK_SAID(rows.size() > 140, "read " << rows.size() << " rows from " << doc);
    std::map<std::string, std::string> status_of;
    for (const auto& row : rows) {
        CHECK_SAID(!status_of.count(row.control), "twice in the doc: " << row.control);
        status_of[row.control] = row.status;
    }
    std::map<std::string, int> bound;
    for (const auto& binding : table()) ++bound[binding.doc];
    unsigned done = 0, next = 0;
    for (const auto& row : rows) {
        if (row.status == "none" || row.status == "runtime") { // runtime: answered outside the table (the power button)
            CHECK_SAID(!bound.count(row.control), "a " << row.status << " row bound: " << row.control);
            continue;
        }
        CHECK_SAID(bound.count(row.control), "not in the table: " << row.control);
        for (const auto& binding : table()) {
            if (row.control != binding.doc) continue;
            std::string want = status_name(binding.status);
            if (binding.status == Status::Wave2) want += std::string(": ") + binding.area;
            CHECK_SAID(row.status == want, row.control << ": the doc says '" << row.status << "', the table '" << want << "'");
        }
        done += row.status == "done";
        next += row.status == "next";
    }
    for (const auto& [control, count] : bound) CHECK_SAID(status_of.count(control), "not in the doc: " << control);
    std::cout << "controls: " << rows.size() << " doc rows, " << table().size() << " bindings, " << done << " done, " << next << " next\n";
}

// Every row answers its own use somewhere it applies, with its job.
void test_every_row_answers() {
    const Screen real[] {Screen::Play, Screen::Loop, Screen::Session, Screen::Overview, Screen::Browser, Screen::List,
                         Screen::Chop, Screen::ToPads, Screen::Sampling, Screen::Typing, Screen::Canvas, Screen::Tracks,
                         Screen::Mixer, Screen::KnobFx, Screen::PadFx, Screen::Flex};
    for (const auto& row : table()) {
        std::vector<Screen> screens;
        if (row.screen == Screen::Any || row.screen == Screen::Everywhere) {
            for (Screen s : real)
                if (s != Screen::Typing || row.screen == Screen::Everywhere) screens.push_back(s);
        } else screens.push_back(row.screen);
        std::vector<Track> tracks;
        if (row.track == Track::Any) tracks = {Track::Kit, Track::Synth, Track::Audio};
        else tracks = {row.track};
        Use use;
        use.control = row.control;
        use.gesture = row.gesture;
        use.held = row.held;
        use.index = 0;
        for (int bit = 0; bit < 32; ++bit)
            if (row.only & (1u << bit)) { use.index = bit; break; }
        use.held_step = row.step_held >= 0 ? row.step_held : (row.held & held::step) ? 1 : -1;
        bool answered = false;
        for (Screen s : screens)
            for (Track t : tracks)
                if (find({s, t}, use) == &row) {
                    answered = true;
                    CHECK(resolve({s, t}, use) == row.job);
                }
        CHECK_SAID(answered, "never answers: " << row.doc << " (" << job_name(row.job) << ")");
        // A row for one screen answers there whatever the track.
        if (row.screen != Screen::Any && row.screen != Screen::Everywhere && row.track == Track::Any) {
            const Binding* found = find({row.screen, Track::Kit}, use);
            CHECK_SAID(found && found->screen == row.screen, row.doc << " is taken on its own screen by " << (found ? found->doc : "nothing"));
        }
    }
}

// One place, one control: never a Press job and a Tap job together, never two rows alike.
void test_no_clash() {
    const auto& rows = table();
    for (size_t a = 0; a < rows.size(); ++a)
        for (size_t b = a + 1; b < rows.size(); ++b) {
            const auto& x = rows[a];
            const auto& y = rows[b];
            if (x.screen != y.screen || x.track != y.track || x.held != y.held || x.control != y.control || x.step_held != y.step_held) continue;
            if (x.only && y.only && !(x.only & y.only)) continue;
            bool same = x.gesture == y.gesture;
            bool press_and_tap = (x.gesture == Gesture::Press && y.gesture == Gesture::Tap) || (x.gesture == Gesture::Tap && y.gesture == Gesture::Press);
            CHECK_SAID(!same || x.only != y.only, "two rows alike: " << x.doc << " / " << y.doc);
            CHECK_SAID(!press_and_tap, "a Press and a Tap job at once: " << x.doc << " / " << y.doc);
        }
}

Use use_of(Control control, Gesture gesture = Gesture::Press, uint16_t held_bits = held::none, int index = 0, int held_step = -1) {
    Use use;
    use.control = control;
    use.gesture = gesture;
    use.held = held_bits;
    use.index = index;
    use.held_step = held_step;
    return use;
}

// The decisions (Kerem, 2026-11-20) and the Move's own jobs that replaced Melodee's.
void test_decisions() {
    const Where kit {Screen::Play, Track::Kit}, synth {Screen::Play, Track::Synth}, pitches {Screen::Play, Track::Pitches};
    const Where typing {Screen::Typing, Track::Kit}, browser {Screen::Browser, Track::Kit};
    // Loop is Loop Mode; Chop is Shift + push on the sample.
    CHECK(resolve(kit, use_of(Control::Loop, Gesture::Tap)) == Job::LoopMode);
    CHECK(resolve(kit, use_of(Control::Loop)) == Job::Nothing);
    CHECK(resolve(kit, use_of(Control::Loop, Gesture::Press, held::shift)) == Job::Nothing);
    // The sequencer's (Kerem, 2026-11-20): Shift + Loop tapped Melodee's SEQ pages, Loop held Fill,
    // a step held + push Melodee's TRIG on the knobs.
    CHECK(resolve(kit, use_of(Control::Loop, Gesture::Tap, held::shift)) == Job::SeqPages);
    CHECK(resolve(kit, use_of(Control::Loop, Gesture::Hold)) == Job::Fill);
    CHECK(resolve(kit, use_of(Control::WheelTurn, Gesture::Turn, held::shift | held::loop)) == Job::LoopLengthFine);
    CHECK(resolve(kit, use_of(Control::WheelPush, Gesture::Press, held::step, 0, 3)) == Job::StepTrig);
    CHECK(resolve(kit, use_of(Control::Knob, Gesture::Turn, held::step, 0, 3)) == Job::StepMotion);
    CHECK(resolve(kit, use_of(Control::WheelPush, Gesture::Press, held::shift)) == Job::SampleOptions);
    CHECK(resolve(synth, use_of(Control::WheelPush, Gesture::Press, held::shift)) == Job::ModuleMenu);
    // Note/Session switches Play and Session; Load is the wheel's push; pages Shift + turn.
    CHECK(resolve(kit, use_of(Control::NoteSession, Gesture::Tap)) == Job::ToggleSession);
    CHECK(resolve(kit, use_of(Control::NoteSession)) == Job::Nothing);
    CHECK(resolve(kit, use_of(Control::NoteSession, Gesture::Hold)) == Job::PeekSession);
    CHECK(resolve(kit, use_of(Control::WheelPush)) == Job::OpenBrowser);
    CHECK(resolve(kit, use_of(Control::WheelTurn, Gesture::Turn)) == Job::Focus);
    CHECK(resolve(kit, use_of(Control::WheelTurn, Gesture::Turn, held::shift)) == Job::Page);
    CHECK(resolve(kit, use_of(Control::Left)) == Job::Bar);
    // Track buttons: the bank's four; held + a step any of 16; Shift + Note/Session all 16.
    CHECK(resolve(kit, use_of(Control::TrackButton, Gesture::Tap, held::none, 2)) == Job::SelectTrack);
    CHECK(resolve(kit, use_of(Control::TrackButton, Gesture::Press, held::none, 2)) == Job::Nothing);
    CHECK(resolve(kit, use_of(Control::TrackButton, Gesture::Hold, held::none, 2)) == Job::PeekTrack);
    CHECK(resolve(kit, use_of(Control::Step, Gesture::Press, held::track, 8)) == Job::PickTrack);
    CHECK(resolve({Screen::Chop, Track::Kit}, use_of(Control::Step, Gesture::Press, held::track, 8)) == Job::PickTrack);
    CHECK(resolve(kit, use_of(Control::Step, Gesture::Press, held::track | held::copy, 4)) == Job::CopyTrack);
    CHECK(resolve(kit, use_of(Control::Step, Gesture::Press, held::track | held::del, 4)) == Job::ClearTrack);
    CHECK(resolve(kit, use_of(Control::NoteSession, Gesture::Press, held::shift)) == Job::TracksOverview);
    CHECK(resolve(kit, use_of(Control::TrackButton, Gesture::Press, held::shift)) == Job::TrackOptions);
    CHECK(resolve({Screen::Session, Track::Kit}, use_of(Control::Plus)) == Job::TrackBank);
    // Capture is Recall; Shift + Sampling Audio Recall.
    CHECK(resolve(kit, use_of(Control::Capture)) == Job::Recall);
    CHECK(resolve(kit, use_of(Control::Capture, Gesture::Press, held::shift)) == Job::ClearRecall);
    CHECK(resolve(kit, use_of(Control::Sampling)) == Job::SamplingMode);
    CHECK(resolve(kit, use_of(Control::Sampling, Gesture::Press, held::shift)) == Job::AudioRecall);
    // Note/Session held + Sampling: PRINT, from the patterns grid (Melodee's Seq held + Sample Record).
    CHECK(resolve(kit, use_of(Control::Sampling, Gesture::Press, held::note)) == Job::Print);
    CHECK(resolve({Screen::Session, Track::Kit}, use_of(Control::Sampling, Gesture::Press, held::note)) == Job::Print);
    // Kit pads: 32, Plus and Minus 16 more; Shift + Step 8, the right 16 pitches.
    CHECK(resolve(kit, use_of(Control::Plus)) == Job::PadBank);
    CHECK(resolve(synth, use_of(Control::Plus)) == Job::Octave);
    CHECK(resolve(pitches, use_of(Control::Pad, Gesture::Press, held::none, 4)) == Job::PlayPitch);
    CHECK(resolve(pitches, use_of(Control::Pad, Gesture::Press, held::none, 3)) == Job::PlayPad);
    CHECK(resolve(pitches, use_of(Control::Pad, Gesture::Press, held::none, 31)) == Job::PlayPitch);
    CHECK(resolve(pitches, use_of(Control::Plus)) == Job::PitchesOctave);
    CHECK(resolve(kit, use_of(Control::Step, Gesture::Press, held::shift, 7)) == Job::SixteenPitches);
    // Shift + steps: the Move's, and Melodee's 4, 12, 13; Tempo on Step 5, Shift + Play restarts.
    const Job jobs[16] {Job::Projects, Job::Setup, Job::Workflow, Job::Mixer, Job::Tempo, Job::Metronome, Job::Groove, Job::SixteenPitches,
                        Job::KeysAndScales, Job::FullVelocity, Job::Repeat, Job::Looper, Job::Song, Job::NewPattern, Job::DoubleLoop, Job::Quantize};
    // Shift + Step 16 quantizes on a tap: held, a pad picks the one lane (the Move's 1.4.0).
    for (int n = 0; n < 16; ++n) CHECK(resolve(kit, use_of(Control::Step, n == 15 ? Gesture::Tap : Gesture::Press, held::shift, n)) == jobs[n]);
    CHECK(resolve(kit, use_of(Control::Play, Gesture::Press, held::shift)) == Job::Restart);
    CHECK(resolve(kit, use_of(Control::Play)) == Job::PlayStop);
    // Shift held: nothing on the screen but the steps (the Shift card was dropped).
    CHECK(resolve(kit, use_of(Control::Shift, Gesture::Hold)) == Job::Nothing);
    CHECK(resolve(kit, use_of(Control::Shift)) == Job::ShiftSteps);
    CHECK(resolve(browser, use_of(Control::Shift, Gesture::Hold)) == Job::BrowserOptions);
    // Undo, quit.
    CHECK(resolve(kit, use_of(Control::Undo, Gesture::Tap)) == Job::Undo);
    CHECK(resolve(kit, use_of(Control::Undo, Gesture::Hold)) == Job::UndoList);
    CHECK(resolve(kit, use_of(Control::Undo, Gesture::Press, held::shift)) == Job::Redo);
    CHECK(resolve(kit, use_of(Control::Back, Gesture::Press, held::shift)) == Job::Quit);
    CHECK(resolve(kit, use_of(Control::Back)) == Job::Back);
    CHECK(resolve(kit, use_of(Control::Back, Gesture::Hold)) == Job::BackToPlay);
    // Typing takes all but the Volume knob and Shift + Back; Back deletes, held cancels.
    CHECK(resolve(typing, use_of(Control::Back, Gesture::Press, held::shift)) == Job::Quit);
    CHECK(resolve(typing, use_of(Control::Volume, Gesture::Turn)) == Job::MainVolume);
    CHECK(resolve(typing, use_of(Control::Step, Gesture::Press, held::shift, 4)) == Job::TypeDigit); // a digit, never Shift + Step's own job
    CHECK(resolve(typing, use_of(Control::Pad, Gesture::Press, held::shift)) == Job::TypeKey);       // a capital (Shift held or locked)
    CHECK(resolve(typing, use_of(Control::Back)) == Job::TypeBackspace);
    CHECK(resolve(typing, use_of(Control::Back, Gesture::Hold)) == Job::TypeCancel);
    CHECK(resolve(typing, use_of(Control::NoteSession, Gesture::Tap)) == Job::Nothing);
    CHECK(resolve(typing, use_of(Control::Pad)) == Job::TypeKey);
    // Screens over Play keep its controls; To Pads keeps Chop's.
    CHECK(resolve({Screen::List, Track::Synth}, use_of(Control::Pad)) == Job::PlayNote);
    CHECK(resolve({Screen::List, Track::Synth}, use_of(Control::WheelTurn, Gesture::Turn)) == Job::ListTurn);
    CHECK(resolve({Screen::Canvas, Track::Synth}, use_of(Control::Knob, Gesture::Turn)) == Job::TurnKnob);
    CHECK(resolve({Screen::ToPads, Track::Kit}, use_of(Control::Pad)) == Job::PlaySlice);
    CHECK(resolve({Screen::Chop, Track::Kit}, use_of(Control::WheelTurn, Gesture::Turn)) == Job::ChopAmount);
    CHECK(resolve({Screen::Chop, Track::Kit}, use_of(Control::Back)) == Job::LeaveChop);
    // Shift + Step 16 held + a pad quantizes it; another step held, nothing.
    CHECK(resolve(kit, use_of(Control::Pad, Gesture::Press, held::shift | held::step, 3, 15)) == Job::QuantizePad);
    CHECK(resolve(kit, use_of(Control::Pad, Gesture::Press, held::shift | held::step, 3, 14)) == Job::Nothing);
}

// The audio thread's flag: a pad sounds unless something gives it another job.
void test_pads_sound() {
    const Where kit {Screen::Play, Track::Kit}, synth {Screen::Play, Track::Synth};
    CHECK(pad_sounds(kit, held::none, 0));
    CHECK(pad_sounds(synth, held::none, 31));
    CHECK(pad_sounds({Screen::Chop, Track::Kit}, held::none, 5));
    CHECK(pad_sounds({Screen::Browser, Track::Kit}, held::none, 5));
    CHECK(pad_sounds({Screen::List, Track::Synth}, held::none, 5));
    CHECK(pad_sounds({Screen::Play, Track::Pitches}, held::none, 7));
    CHECK(pad_sounds(synth, held::shift, 0));      // no job for Shift + a note: it plays
    CHECK(pad_sounds(kit, held::track, 0));        // a track button held: the pads still play
    CHECK(pad_sounds({Screen::Play, Track::Any}, held::none, 0)); // an empty track: nothing stops it
    for (uint16_t bits : {held::shift, held::copy, held::del, held::mute, held::step})
        CHECK_SAID(!pad_sounds(kit, bits, 0), "a pad sounds with " << bits << " held");
    CHECK(!pad_sounds({Screen::Typing, Track::Kit}, held::none, 0));
    CHECK(!pad_sounds({Screen::Sampling, Track::Kit}, held::none, 0));
    CHECK(!pad_sounds({Screen::Session, Track::Kit}, held::none, 0));
    CHECK(!pad_sounds({Screen::Overview, Track::Kit}, held::none, 0));
}

// Pads held are played: every other control does its own job meanwhile, but
// where a row names "Pad held" (the Move's pad gain, Session's chain).
void test_pads_played() {
    const Where kit {Screen::Play, Track::Kit}, synth {Screen::Play, Track::Synth}, pitches {Screen::Play, Track::Pitches};
    const Where canvas {Screen::Canvas, Track::Synth}, session {Screen::Session, Track::Kit}, overview {Screen::Overview, Track::Kit};
    for (Where at : {kit, synth, canvas}) {
        CHECK(resolve(at, use_of(Control::Knob, Gesture::Turn, held::pad, 3)) == Job::TurnKnob);
        CHECK(resolve(at, use_of(Control::Knob, Gesture::Turn, held::pad | held::shift, 3)) == Job::TurnKnobFine);
        CHECK(resolve(at, use_of(Control::Play, Gesture::Press, held::pad)) == Job::PlayStop);
        CHECK(resolve(at, use_of(Control::KnobTouch, Gesture::Press, held::pad, 2)) == Job::ShowKnob);
    }
    // The Volume knob: the main volume, but the pad's gain on a kit (the Move's) and the project's on Overview.
    CHECK(resolve(synth, use_of(Control::Volume, Gesture::Turn, held::pad)) == Job::MainVolume);
    CHECK(resolve(canvas, use_of(Control::Volume, Gesture::Turn, held::pad)) == Job::MainVolume);
    CHECK(resolve({Screen::Typing, Track::Kit}, use_of(Control::Volume, Gesture::Turn, held::pad)) == Job::MainVolume);
    CHECK(resolve(kit, use_of(Control::Volume, Gesture::Turn, held::pad)) == Job::PadLevel);
    CHECK(resolve(pitches, use_of(Control::Volume, Gesture::Turn, held::pad)) == Job::PadLevel);
    CHECK(resolve(overview, use_of(Control::Volume, Gesture::Turn, held::pad)) == Job::ProjectVolume);
    // A step pressed with notes held puts them on it; a step held turns its knobs to motion.
    CHECK(resolve(synth, use_of(Control::Step, Gesture::Press, held::pad, 4)) == Job::ToggleStep);
    CHECK(resolve(synth, use_of(Control::Step, Gesture::Release, held::pad, 4)) == Job::ClearStep);
    // A screen's own step tap keeps Play's press and let-go from it: Loop Mode's steps are bars.
    for (Screen s : {Screen::Loop, Screen::Mixer, Screen::KnobFx, Screen::Looper}) {
        CHECK(resolve({s, Track::Kit}, use_of(Control::Step, Gesture::Press, held::none, 4)) == Job::Nothing);
        CHECK(resolve({s, Track::Kit}, use_of(Control::Step, Gesture::Release, held::none, 4)) == Job::Nothing);
    }
    CHECK(resolve({Screen::Browser, Track::Kit}, use_of(Control::Step, Gesture::Press, held::none, 4)) == Job::ToggleStep);
    CHECK(resolve(kit, use_of(Control::Knob, Gesture::Turn, held::pad | held::step, 0, 3)) == Job::StepMotion);
    // Another pad: its own job (the next note, Chop's next slice), Session's chain where it has one.
    CHECK(resolve(kit, use_of(Control::Pad, Gesture::Press, held::pad, 5)) == Job::PlayPad);
    CHECK(resolve({Screen::Chop, Track::Kit}, use_of(Control::Pad, Gesture::Press, held::pad, 5)) == Job::PlaySlice);
    CHECK(resolve(session, use_of(Control::Pad, Gesture::Press, held::pad, 5)) == Job::ChainPattern);
    CHECK(pad_sounds(kit, held::pad, 0));
    CHECK(!pad_sounds(kit, held::pad | held::copy, 0)); // Copy held: the pad is copied, not played
    // A pad's pressure stays the audio thread's: no job.
    CHECK(resolve(synth, use_of(Control::PadPressure, Gesture::Turn, held::pad, 5)) == Job::Nothing);
}

move::Input button(move::Button which, bool pressed) {
    move::Input input;
    input.kind = move::Input::Kind::Button;
    input.index = uint8_t(which);
    input.pressed = pressed;
    return input;
}
move::Input step_input(unsigned index, bool pressed) {
    move::Input input;
    input.kind = move::Input::Kind::Step;
    input.index = uint8_t(index);
    input.pressed = pressed;
    return input;
}
move::Input turn(move::Input::Kind kind, int delta, unsigned index = 0) {
    move::Input input;
    input.kind = kind;
    input.index = uint8_t(index);
    input.delta = delta;
    return input;
}
bool has(const std::vector<Use>& uses, Gesture gesture) {
    for (const auto& use : uses) if (use.gesture == gesture) return true;
    return false;
}

void test_gestures() {
    using move::Button;
    const Where kit {Screen::Play, Track::Kit};
    auto t0 = Clock::time_point {} + 10s;
    {   // A tap: Undo let go within the hold time.
        Gestures g;
        auto down = g.feed(button(Button::Undo, true), t0);
        CHECK(down.size() == 1 && down[0].gesture == Gesture::Press && down[0].control == Control::Undo);
        CHECK(g.tick(t0 + 100ms).empty());
        auto up = g.feed(button(Button::Undo, false), t0 + 150ms);
        CHECK(up.size() == 1 && up[0].gesture == Gesture::Tap && resolve(kit, up[0]) == Job::Undo);
    }
    {   // A hold: Undo held past it, once; no tap after.
        Gestures g;
        g.feed(button(Button::Undo, true), t0);
        auto due = g.tick(t0 + 450ms);
        CHECK(due.size() == 1 && due[0].gesture == Gesture::Hold && resolve(kit, due[0]) == Job::UndoList);
        CHECK(g.tick(t0 + 900ms).empty());
        CHECK(g.feed(button(Button::Undo, false), t0 + 1s).empty());
    }
    {   // Note/Session held counts for Sampling only: its peek's pads launch as Session's do.
        Gestures g;
        g.feed(button(Button::Menu, true), t0);
        auto due = g.tick(t0 + 450ms);
        CHECK(due.size() == 1 && resolve(kit, due[0]) == Job::PeekSession);
        auto sampling = g.feed(button(Button::Sample, true), t0 + 500ms);
        CHECK(sampling.size() == 1 && sampling[0].held == held::note && resolve({Screen::Session, Track::Kit}, sampling[0]) == Job::Print);
        g.feed(button(Button::Sample, false), t0 + 550ms);
        auto step = g.feed(button(Button::Undo, true), t0 + 600ms);
        CHECK(step.size() == 1 && step[0].held == held::none);
    }
    {   // A modifier: Track 1 held, step 9 picks track 9; Track 1 neither selects nor peeks.
        Gestures g;
        g.feed(button(Button::Row1, true), t0);
        auto pick = g.feed(step_input(8, true), t0 + 50ms);
        CHECK(pick.size() == 1 && pick[0].held == held::track && pick[0].index == 8 && resolve(kit, pick[0]) == Job::PickTrack);
        CHECK(g.tick(t0 + 600ms).empty());
        g.feed(step_input(8, false), t0 + 700ms);
        CHECK(g.feed(button(Button::Row1, false), t0 + 800ms).empty());
        CHECK(g.held() == held::none);
    }
    {   // Track 3 tapped selects it; held, peeks.
        Gestures g;
        g.feed(button(Button::Row3, true), t0);
        auto up = g.feed(button(Button::Row3, false), t0 + 100ms);
        CHECK(up.size() == 1 && up[0].index == 2 && resolve(kit, up[0]) == Job::SelectTrack);
        g.feed(button(Button::Row3, true), t0 + 1s);
        auto due = g.tick(t0 + 1500ms);
        CHECK(due.size() == 1 && resolve(kit, due[0]) == Job::PeekTrack);
    }
    {   // A turn makes a held button a modifier: Track 2 held + the Volume knob, its level.
        Gestures g;
        g.feed(button(Button::Row2, true), t0);
        auto level = g.feed(turn(move::Input::Kind::MasterKnob, 3), t0 + 50ms);
        CHECK(level.size() == 1 && level[0].delta == 3 && resolve(kit, level[0]) == Job::TrackLevel);
        CHECK(g.feed(button(Button::Row2, false), t0 + 100ms).empty());
    }
    {   // Loop held + the wheel: the length, and Loop Mode is not toggled.
        Gestures g;
        g.feed(button(Button::Loop, true), t0);
        auto length = g.feed(turn(move::Input::Kind::MainKnob, -1), t0 + 50ms);
        CHECK(length.size() == 1 && resolve(kit, length[0]) == Job::LoopLength);
        CHECK(g.feed(button(Button::Loop, false), t0 + 100ms).empty());
    }
    {   // A double press: step 3 twice in Loop Mode, a one-bar loop.
        Gestures g;
        const Where loop {Screen::Loop, Track::Kit};
        g.feed(step_input(2, true), t0);
        auto first = g.feed(step_input(2, false), t0 + 60ms); // a Tap, then its Release, which Loop Mode keeps from Play
        CHECK(first.size() == 2 && resolve(loop, first[0]) == Job::SelectBar && resolve(loop, first[1]) == Job::Nothing);
        auto second = g.feed(step_input(2, true), t0 + 200ms);
        CHECK(has(second, Gesture::Double));
        for (const auto& use : second)
            if (use.gesture == Gesture::Double) CHECK(resolve(loop, use) == Job::OneBarLoop);
        g.feed(step_input(2, false), t0 + 260ms);
        auto third = g.feed(step_input(2, true), t0 + 400ms); // a third press starts afresh
        CHECK(!has(third, Gesture::Double));
    }
    {   // Step held + Plus: a tap a semitone, held an octave.
        Gestures g;
        g.feed(step_input(1, true), t0);
        g.feed(button(Button::Up, true), t0 + 50ms);
        auto up = g.feed(button(Button::Up, false), t0 + 150ms);
        CHECK(up.size() == 1 && up[0].held == held::step && resolve(kit, up[0]) == Job::TransposeStep);
        g.feed(button(Button::Up, true), t0 + 1s);
        auto due = g.tick(t0 + 1500ms);
        CHECK(due.size() == 1 && resolve(kit, due[0]) == Job::TransposeStepOctave);
        CHECK(g.feed(button(Button::Up, false), t0 + 1600ms).empty());
        // The step was a modifier: no step tap when it is let go, its Release no tap.
        auto let_go = g.feed(step_input(1, false), t0 + 1700ms);
        CHECK(let_go.size() == 1 && let_go[0].gesture == Gesture::Release && !let_go[0].tap);
    }
    {   // Shift: a double press locks it; held for all but Play and Back; a press unlocks.
        Gestures g;
        g.feed(button(Button::Shift, true), t0);
        g.feed(button(Button::Shift, false), t0 + 50ms);
        auto second = g.feed(button(Button::Shift, true), t0 + 150ms);
        CHECK(has(second, Gesture::Double));
        for (const auto& use : second)
            if (use.gesture == Gesture::Double) CHECK(resolve(kit, use) == Job::ShiftLock);
        g.feed(button(Button::Shift, false), t0 + 200ms);
        CHECK(g.shift_locked());
        CHECK(g.held() == held::shift);
        auto play = g.feed(button(Button::Play, true), t0 + 1s);
        CHECK(play.size() == 1 && play[0].held == held::none && resolve(kit, play[0]) == Job::PlayStop);
        g.feed(button(Button::Play, false), t0 + 1100ms);
        auto five = g.feed(step_input(4, true), t0 + 1200ms);
        CHECK(five.size() == 1 && resolve(kit, five[0]) == Job::Tempo);
        g.feed(step_input(4, false), t0 + 1250ms);
        auto back = g.feed(button(Button::Back, true), t0 + 1300ms); // Back stays a way up, not Shift + Back (quit)
        CHECK(!back.empty() && back[0].held == held::none && resolve(kit, back[0]) == Job::Back);
        g.feed(button(Button::Back, false), t0 + 1350ms);
        g.feed(button(Button::Shift, true), t0 + 2s);
        CHECK(!g.shift_locked());
        g.feed(button(Button::Shift, false), t0 + 2100ms);
        CHECK(g.held() == held::none);
        // With Shift down, Play restarts.
        g.feed(button(Button::Shift, true), t0 + 3s);
        auto restart = g.feed(button(Button::Play, true), t0 + 3100ms);
        CHECK(restart.size() == 1 && resolve(kit, restart[0]) == Job::Restart);
    }
    {   // Touches and turns: Delete held + a knob touched deletes its motion, and Delete does not tap.
        Gestures g;
        g.feed(button(Button::Delete, true), t0);
        move::Input touch;
        touch.kind = move::Input::Kind::Touch;
        touch.index = 2;
        touch.pressed = true;
        auto touched = g.feed(touch, t0 + 50ms);
        CHECK(touched.size() == 1 && touched[0].control == Control::KnobTouch && touched[0].index == 2 &&
              resolve(kit, touched[0]) == Job::DeleteMotion);
        touch.pressed = false;
        CHECK(g.feed(touch, t0 + 100ms).empty());
        CHECK(g.feed(button(Button::Delete, false), t0 + 150ms).empty());
    }
    {   // Playing: a pad held, the knobs, the Volume knob and Play still do their jobs.
        const Where synth {Screen::Play, Track::Synth};
        Gestures g;
        move::Input pad;
        pad.kind = move::Input::Kind::Pad;
        pad.index = 9;
        pad.pressed = true;
        pad.value = 100;
        g.feed(pad, t0);
        auto knob = g.feed(turn(move::Input::Kind::Knob, 2, 4), t0 + 50ms);
        CHECK(knob.size() == 1 && knob[0].held == held::pad && resolve(synth, knob[0]) == Job::TurnKnob && knob[0].index == 4);
        auto volume = g.feed(turn(move::Input::Kind::MasterKnob, -1), t0 + 100ms);
        CHECK(volume.size() == 1 && resolve(synth, volume[0]) == Job::MainVolume);
        CHECK(resolve(kit, volume[0]) == Job::PadLevel); // the Move's: a kit pad held, its gain
        auto play = g.feed(button(Button::Play, true), t0 + 150ms);
        CHECK(play.size() == 1 && resolve(synth, play[0]) == Job::PlayStop);
        g.feed(button(Button::Play, false), t0 + 200ms);
        auto step_down = g.feed(step_input(2, true), t0 + 250ms);
        CHECK(step_down.size() == 1 && resolve(synth, step_down[0]) == Job::ToggleStep); // a step acts on its press
        auto step_up = g.feed(step_input(2, false), t0 + 300ms);
        CHECK(step_up.size() == 2 && step_up[0].gesture == Gesture::Tap && resolve(synth, step_up[0]) == Job::Nothing);
        CHECK(step_up[1].gesture == Gesture::Release && step_up[1].tap && resolve(synth, step_up[1]) == Job::ClearStep);
        // Held past hold_time, or used, its let-go is no tap: the step keeps its notes.
        g.feed(step_input(2, true), t0 + 1000ms);
        auto held_up = g.feed(step_input(2, false), t0 + 1500ms);
        CHECK(held_up.size() == 1 && held_up[0].gesture == Gesture::Release && !held_up[0].tap);
        pad.pressed = false;
        pad.value = 0;
        auto up = g.feed(pad, t0 + 400ms);
        CHECK(up.size() == 1 && up[0].gesture == Gesture::Release && g.held() == held::none);
    }
    {   // The controls map from inputs: the code's names to the Move's.
        CHECK(map(button(Button::Menu, true)).control == Control::NoteSession);
        CHECK(map(button(Button::Up, true)).control == Control::Plus);
        CHECK(map(button(Button::Down, true)).control == Control::Minus);
        CHECK(map(button(Button::Sample, true)).control == Control::Sampling);
        CHECK(map(button(Button::Row4, true)).control == Control::TrackButton && map(button(Button::Row4, true)).index == 3);
        CHECK(map(button(Button::MainPush, true)).control == Control::WheelPush);
        move::Input touch;
        touch.kind = move::Input::Kind::Touch;
        touch.index = move::touch_main;
        CHECK(map(touch).control == Control::WheelTouch);
        touch.index = move::touch_master;
        CHECK(map(touch).control == Control::VolumeTouch);
        CHECK(map(turn(move::Input::Kind::AudioInSense, 0)).control == Control::Count);
    }
}

// The Looper (Shift + Step 12): the Move's keys for the loop tracks, a pad row a loop.
void test_looper() {
    const Where looper {Screen::Looper, Track::Kit}, synth {Screen::Looper, Track::Synth};
    CHECK(resolve({Screen::Play, Track::Kit}, use_of(Control::Step, Gesture::Press, held::shift, 11)) == Job::Looper);
    CHECK(resolve(looper, use_of(Control::Step, Gesture::Press, held::shift, 11)) == Job::Looper); // again: back to Play
    // The left 16: a loop's row, whatever the track; the right 16 play the track.
    for (int pad : {0, 3, 8, 11, 16, 19, 24, 27}) {
        CHECK_SAID(resolve(looper, use_of(Control::Pad, Gesture::Press, held::none, pad)) == Job::LoopPad, "pad " << pad);
        CHECK_SAID(resolve(synth, use_of(Control::Pad, Gesture::Press, held::shift, pad)) == Job::LoopPadNow, "pad " << pad);
        CHECK(!pad_sounds(looper, held::none, unsigned(pad)));
    }
    for (int pad : {4, 7, 12, 15, 20, 23, 28, 31}) {
        CHECK(resolve(looper, use_of(Control::Pad, Gesture::Press, held::none, pad)) == Job::PlayPad);
        CHECK(resolve(synth, use_of(Control::Pad, Gesture::Press, held::none, pad)) == Job::PlayNote);
        CHECK(pad_sounds(looper, held::none, unsigned(pad)));
    }
    // The track buttons are L1-L4: select, level, mute, solo; held, still the sixteen tracks.
    CHECK(resolve(looper, use_of(Control::TrackButton, Gesture::Tap, held::none, 1)) == Job::SelectLoop);
    CHECK(resolve(looper, use_of(Control::TrackButton, Gesture::Hold, held::none, 1)) == Job::PeekTrack);
    CHECK(resolve(looper, use_of(Control::Step, Gesture::Press, held::track, 5)) == Job::PickTrack);
    CHECK(resolve(looper, use_of(Control::Volume, Gesture::Turn, held::track)) == Job::LoopLevel);
    CHECK(resolve(looper, use_of(Control::Volume, Gesture::Turn)) == Job::MainVolume);
    CHECK(resolve(looper, use_of(Control::Mute, Gesture::Tap)) == Job::MuteLoop);
    CHECK(resolve(looper, use_of(Control::TrackButton, Gesture::Press, held::mute, 2)) == Job::MuteLoop);
    CHECK(resolve(looper, use_of(Control::TrackButton, Gesture::Press, held::shift | held::mute, 2)) == Job::SoloLoop);
    // Rec records the selected loop; the wheel picks, its push opens the sheet; Shift + turn the page.
    CHECK(resolve(looper, use_of(Control::Rec)) == Job::LoopRecord);
    CHECK(resolve({Screen::Play, Track::Kit}, use_of(Control::Rec)) == Job::Record);
    CHECK(resolve(looper, use_of(Control::WheelTurn, Gesture::Turn)) == Job::PickLoop);
    CHECK(resolve(looper, use_of(Control::WheelPush)) == Job::LoopSheet);
    CHECK(resolve(looper, use_of(Control::WheelTurn, Gesture::Turn, held::shift)) == Job::Page);
    CHECK(resolve(looper, use_of(Control::WheelPush, Gesture::Press, held::shift)) == Job::Nothing); // not Play's Sample Options
    // The rest as on Play: transport, knobs, Back, Plus and Minus; the steps do nothing.
    CHECK(resolve(looper, use_of(Control::Play)) == Job::PlayStop);
    CHECK(resolve(looper, use_of(Control::Play, Gesture::Press, held::shift)) == Job::Restart);
    CHECK(resolve(looper, use_of(Control::Knob, Gesture::Turn)) == Job::TurnKnob);
    CHECK(resolve(looper, use_of(Control::KnobTouch)) == Job::ShowKnob);
    CHECK(resolve(looper, use_of(Control::Back)) == Job::Back);
    CHECK(resolve(synth, use_of(Control::Plus)) == Job::Octave);
    CHECK(resolve(looper, use_of(Control::Step, Gesture::Tap, held::none, 3)) == Job::Nothing);
    CHECK(resolve(looper, use_of(Control::Back, Gesture::Press, held::shift)) == Job::Quit);
}

} // namespace

int main(int argc, char** argv) {
    std::string doc = argc > 1 ? argv[1] : "docs/MOVE_CONTROLS.md";
    test_doc_and_table(doc);
    test_every_row_answers();
    test_no_clash();
    test_decisions();
    test_pads_sound();
    test_pads_played();
    test_gestures();
    test_looper();
    std::cout << "controls-tests: " << checks - failures << '/' << checks << " checks passed\n";
    return failures ? 1 : 0;
}
