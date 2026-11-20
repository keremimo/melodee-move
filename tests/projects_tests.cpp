// SPDX-License-Identifier: AGPL-3.0-only
// Tests for undo, projects and the system (docs/PARITY.md's "MIDI & sync &
// undo & projects" rows that are this area's): the history alone, then
// through the Move app with the Move's own keys.
#include "app/history.h"
#include "app/melodee_app.h"
#include "move/controls.h"
#include "move/tools.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

using namespace move;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void press(App& app, Button button) {
    app.surface(make_event(0xb0, button_cc(button), 127));
    app.surface(make_event(0xb0, button_cc(button), 0));
}
void down(App& app, Button button) { app.surface(make_event(0xb0, button_cc(button), 127)); }
void up(App& app, Button button) { app.surface(make_event(0xb0, button_cc(button), 0)); }
template <class Do> void with(App& app, Button held, Do&& what) { down(app, held); what(); up(app, held); }
void turn_knob(App& app, unsigned knob, int detents) {
    app.surface(make_event(0xb0, uint8_t(knob_first_cc + knob), uint8_t(detents < 0 ? 128 + detents : detents)));
}
void turn_wheel(App& app, int detents) { app.surface(make_event(0xb0, main_knob_cc, uint8_t(detents < 0 ? 128 + detents : detents))); }
void tap_pad(App& app, unsigned pad) {
    app.surface(make_event(0x90, pad_note(pad), 100));
    app.surface(make_event(0x80, pad_note(pad), 0));
}
// Time passing for the gestures: holds come due, a knob left still closes its step.
void later(App& app, std::chrono::milliseconds by) {
    Lights lights;
    app.lights(lights, App::Clock::now() + by);
}

// ---------------------------------------------------------------- the history alone

void test_history() {
    auto project = std::make_unique<melodee::Project>();
    auto bank = std::make_unique<melodee::PatternBank>();
    app::MoveExtras extras;
    project->tracks[0].engine = melodee::EngineKind::Sampler;
    app::Live live {*project, *bank, extras};
    app::History history;
    history.reset(live);
    auto now = app::History::Clock::now();
    CHECK(!history.close(live, "NOTHING", -1, now)); // nothing changed: no step

    // A pad tuned: one step, on track 1, its bytes alone.
    project->tracks[0].pads[4].pitch = 7;
    CHECK(history.close(live, "TUNE A05", -1, now));
    CHECK(history.size() == 1 && history.kept(0).track == 0 && history.kept(0).what == "TUNE A05");
    CHECK(history.kept(0).tracks.test(0) && !history.kept(0).globals);
    size_t bytes = 0;
    for (const auto& range : history.kept(0).project) bytes += range.before.size();
    CHECK(bytes > 0 && bytes <= 64);

    // A mute is no step, and no undo touches it later.
    project->tracks[0].mix.mute = true;
    CHECK(!history.close(live, "MUTE", -1, now, false));
    CHECK(history.size() == 1);

    // The tempo: a global, the tempo alone.
    project->tempo = 1300;
    CHECK(history.close(live, "TEMPO", -1, now));
    CHECK(history.kept(1).globals && history.kept(1).tempo_only && history.kept(1).track == -1);

    // A pattern and a sample slot.
    bank->at[2][5].events.push_back({});
    project->samples.edit(40).path[0] = 'x';
    CHECK(history.close(live, "NOTES", -1, now));
    CHECK(history.kept(2).patterns.size() == 1 && history.kept(2).samples.size() == 1);

    // Undo three times, then redo one.
    CHECK(history.undo(live) && bank->at[2][5].events.empty() && !project->samples[40].path[0]);
    CHECK(history.undo(live) && project->tempo == 1200);
    CHECK(history.undo(live) && project->tracks[0].pads[4].pitch == 0);
    CHECK(project->tracks[0].mix.mute); // the mute stays
    CHECK(!history.undo(live) && history.redos() == 3);
    CHECK(history.redo(live) && project->tracks[0].pads[4].pitch == 7);
    CHECK(!history.close(live, "NONE", -1, now)); // undo and redo leave the baseline as the project is
    // A new step forgets the steps undone.
    project->tracks[3].name[0] = 'Z';
    CHECK(history.close(live, "NAME", -1, now));
    CHECK(history.redos() == 0 && history.size() == 2);

    // Module values: only the keys the screens set, the rest moving with the module.
    live.modules[1].present = true;
    live.modules[1].file.module = "obxd";
    live.modules[1].file.values = {{"cutoff", "0.5"}, {"reso", "0.1"}};
    CHECK(!history.close(live, "LOADED", -1, now, false));
    live.modules[1].file.values = {{"cutoff", "0.7"}, {"reso", "0.3"}};
    live.edited[1] = {"cutoff"};
    CHECK(history.close(live, "CUTOFF", -1, now));
    CHECK(history.kept(2).modules.size() == 1 && history.kept(2).modules[0].keys.size() == 1 && history.kept(2).modules[0].keys[0].before == "0.5");
    CHECK(history.kept(2).track == 1);
    live.edited[1].clear();
    // Another module picked over it: the whole state, both sides.
    live.modules[1].file.module = "surge";
    CHECK(history.close(live, "SYNTH SURGE", -1, now));
    CHECK(history.kept(3).modules[0].whole && history.kept(3).modules[0].before.file.module == "obxd");

    // Thirty-two steps at most.
    for (int n = 0; n < 40; ++n) {
        project->tracks[5].pads[0].level = uint8_t(n);
        history.close(live, "LEVEL", -1, now);
    }
    CHECK(history.size() == app::History::depth);
    // Every slot a step could bring back.
    std::vector<bool> used(melodee::sample_slots);
    history.slots_in_use(used);
    CHECK(!used[40]); // undone, then forgotten by the steps after
}

// ---------------------------------------------------------------- undo through the app

void test_undo_app() {
    std::string folder = "build/test-samples", loop = folder + "/loop.wav", home = "build/test-home-undo";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    CHECK(write_test_loop(loop));
    app::MelodeeApp melodee(folder, home);
    Session session;
    melodee.attach(session);
    CHECK(melodee.load_now(loop, 0).empty());
    melodee.studio().close_step("LOAD A01"); // as the service thread closes a load
    auto pitch = [&] { return int(melodee.studio().project().tracks[0].pads[0].pitch); };
    auto status = [&] { return melodee.status(); };
    CHECK(has(status(), " undo=1 "));

    // Knob 1's turns are one step, closed by another control.
    turn_knob(melodee, 0, 1);
    turn_knob(melodee, 0, 1);
    turn_knob(melodee, 0, 1);
    CHECK(pitch() == 3);
    press(melodee, Button::Undo); // Undo (a tap) closes the turns' step, then takes it back
    CHECK(pitch() == 0);
    CHECK(has(status(), "undo_last=TUNE_A01"));
    with(melodee, Button::Shift, [&] { press(melodee, Button::Undo); }); // Shift + Undo: redo
    CHECK(pitch() == 3);

    // A knob left still two seconds closes its step: two steps.
    turn_knob(melodee, 1, -10);
    later(melodee, std::chrono::milliseconds(2100));
    turn_knob(melodee, 1, -10);
    press(melodee, Button::Undo);
    CHECK(melodee.studio().project().tracks[0].pads[0].level == 100 - 10);
    press(melodee, Button::Undo);
    CHECK(melodee.studio().project().tracks[0].pads[0].level == 100);

    // Playing a pad is no step; Undo takes back the tune again.
    tap_pad(melodee, 0);
    press(melodee, Button::Undo);
    CHECK(pitch() == 0);

    // Undo held: the list. The wheel moves; push goes there.
    down(melodee, Button::Undo);
    later(melodee, std::chrono::milliseconds(500));
    up(melodee, Button::Undo);
    CHECK(has(status(), "page=undo-list"));
    CHECK(has(status(), "undo_cursor=3")); // three steps undone above the state in hand
    turn_wheel(melodee, -3);                // up to the newest undone (redo all three)
    press(melodee, Button::MainPush);
    CHECK(!has(status(), "page=undo-list"));
    CHECK(pitch() == 3 && melodee.studio().project().tracks[0].pads[0].level == 80);
    CHECK(has(status(), " redo=0 "));

    // Track 2 (a synth with no module yet) deleted, then undone: back as it was.
    auto& studio = melodee.studio();
    CHECK(studio.kind(1) == app::TrackKind::Synth);
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        studio.delete_track(1);
    }
    studio.close_step("DELETE TRACK");
    CHECK(studio.kind(1) == app::TrackKind::Empty);
    press(melodee, Button::Undo);
    CHECK(studio.kind(1) == app::TrackKind::Synth);
    with(melodee, Button::Shift, [&] { press(melodee, Button::Undo); });
    CHECK(studio.kind(1) == app::TrackKind::Empty);
    press(melodee, Button::Undo);

    // The tempo: Shift + Step 5, the wheel; undone as a global.
    with(melodee, Button::Shift, [&] { melodee.surface(make_event(0x90, step_note(4), 127)); melodee.surface(make_event(0x80, step_note(4), 0)); });
    turn_wheel(melodee, 5);
    press(melodee, Button::Back);
    CHECK(studio.tempo() == 1250);
    press(melodee, Button::Undo);
    CHECK(studio.tempo() == 1200);
}

} // namespace

int main() {
    test_history();
    test_undo_app();
    std::cout << "projects-tests: " << checks - failures << "/" << checks << " checks passed\n";
    return failures ? 1 : 0;
}
