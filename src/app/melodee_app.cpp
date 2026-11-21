// SPDX-License-Identifier: AGPL-3.0-only
#include "app/melodee_app.h"
#include <cmath>
#include "app/pad_layout.h"
#include "app/screen.h"
#include "app/sequencer.h"
#include "move/display.h"
#include "move/font.h"
#include "ui/track_style.h"
#include "ui/system_draw.h"
#include "ui/widgets.h"
#include "melodee/engine/engine_info.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace app {
namespace {

constexpr auto touch_linger = std::chrono::milliseconds(600);
constexpr auto hold_shown = bindings::Gestures::hold_time; // a track button held this long shows the sixteen (the Move's hold)
constexpr auto volume_linger = std::chrono::milliseconds(1200);

constexpr auto turn_linger = std::chrono::milliseconds(1200);
constexpr auto gesture_quiet = std::chrono::milliseconds(2000); // a control left this long: its next turn is a new undo step (Melodee's)

// Jobs whose change to the project is no undo step, as Melodee leaves them
// out (MELODEE.md, Undo): playing and picking pads, mutes and solos, the
// octave and bank in view, the pattern launched, the transport.
bool no_step(bindings::Job job) {
    using bindings::Job;
    switch (job) {
    case Job::PlayPad: case Job::PlayNote: case Job::PlayPitch: case Job::Aftertouch: case Job::SelectPadQuietly: case Job::PlaySlice:
    case Job::MuteSelected: case Job::MuteTrack: case Job::SoloTrack: case Job::MutePad: case Job::Octave: case Job::PadBank:
    case Job::PitchesOctave: case Job::ScaleStep: case Job::TrackBank: case Job::LaunchPattern: case Job::SelectBar: case Job::Bar:
    case Job::Bar16: case Job::PatternPage: case Job::PlayStop: case Job::Restart: case Job::SelectTrack: case Job::PickTrack:
    case Job::Focus: case Job::Page: case Job::PreviewProject: case Job::SelectProject:
        return true;
    default: return false;
    }
}

// A job's name as a step's: "ToPadsPlace" as "TO PADS PLACE".
std::string spoken(const char* name) {
    std::string text;
    for (const char* c = name; *c; ++c) {
        if (std::isupper(static_cast<unsigned char>(*c)) && !text.empty()) text += ' ';
        text += char(std::toupper(static_cast<unsigned char>(*c)));
    }
    return text;
}

// Typing, for a module's page: the pads a keyboard, top row first ('\b'
// backspace), the steps digits and marks.
constexpr const char* step_keys = "1234567890()/:,!";
// The pad map's keyboard (pad_layout.h): the physical pad's key in the layout chosen.
char key_for_pad(unsigned pad) { return typing_key(uint8_t(pad), pad_layout()); }
// The Move's own typing (Kerem's decision): the wheel walks this strip, its
// push types the one in the box; SPACE, DEL and OK end it.
const std::vector<std::string>& strip() {
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> made;
        for (char c : std::string("abcdefghijklmnopqrstuvwxyz0123456789-_.'&()!")) made.push_back(std::string(1, c));
        for (const char* key : {"SP", "DL", "OK"}) made.push_back(key);
        return made;
    }();
    return keys;
}

std::string upper(std::string text) {
    for (auto& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

constexpr move::Button track_buttons[4] {move::Button::Row1, move::Button::Row2, move::Button::Row3, move::Button::Row4};
int track_button(move::Button button) {
    for (int n = 0; n < 4; ++n)
        if (track_buttons[n] == button) return n;
    return -1;
}

} // namespace

MelodeeApp::MelodeeApp(std::string library, std::string home, std::string modules)
    : own(std::make_unique<Studio>(std::move(library), std::move(home), std::move(modules))) {
    started_unix = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    own->machine.version = __DATE__; // the build's day
}

MelodeeApp::~MelodeeApp() = default; // the studio saves what is not saved yet

// ---------------------------------------------------------------- audio thread

void MelodeeApp::render(const move::AudioBlock& input, move::AudioBlock& output) { own->render(input, output); }
void MelodeeApp::audio_opened(const move::AudioFormat& format) { own->audio_opened(format); }
void MelodeeApp::audio_event(const move::SurfaceEvent& event) { own->audio_event(event); }

// ---------------------------------------------------------------- MIDI thread

Screen* MelodeeApp::tracks() const { return own->screen("tracks"); }

unsigned MelodeeApp::held_track() const {
    for (unsigned row = 0; row < 4; ++row)
        if (rows_down.test(row)) return ui::bank_of(own->track()) * ui::tracks_a_bank + row;
    return own->track();
}

bindings::Where MelodeeApp::where() const {
    using bindings::Screen;
    using bindings::Track;
    const auto& studio = *own;
    bindings::Where at;
    if (studio.typing) at.screen = Screen::Typing;
    else if (auto* top = studio.top()) at.screen = top->screen_kind();
    switch (studio.kind()) {
    case TrackKind::Kit: at.track = studio.pitches() ? Track::Pitches : Track::Kit; break;
    case TrackKind::Synth: case TrackKind::Keys: case TrackKind::Midi: case TrackKind::Plugin: at.track = Track::Synth; break;
    case TrackKind::Audio: at.track = Track::Audio; break;
    default: at.track = Track::Any; break;
    }
    return at;
}

bool MelodeeApp::trigger(bindings::Job job) {
    auto& studio = *own;
    for (const auto& entry : registered_screens())
        for (const auto& when : entry.triggers) {
            if (when.job != job) continue;
            if (when.kinds && !(when.kinds & kind_bit(studio.kind()))) continue;
            studio.open(entry.id);
            return true;
        }
    return false;
}

void MelodeeApp::type(char c) {
    auto& studio = *own;
    if (c == '\b') { if (!studio.typed.empty()) studio.typed.pop_back(); return; }
    if (studio.typed.size() >= studio.typing_room) return;
    studio.typed += studio.shift ? char(std::toupper(static_cast<unsigned char>(c))) : c;
}

// The jobs that are the same on every screen.
bool MelodeeApp::global(bindings::Job job, const bindings::Use& use) {
    using bindings::Job;
    auto& studio = *own;
    unsigned bank = ui::bank_of(studio.track()) * ui::tracks_a_bank;
    switch (job) {
    // The track buttons: a tap selects, held the sixteen (drawn), a step picks.
    case Job::SelectTrack:
        // The overview's: a track button opens the project picked (the Move enters Note Mode with the Set).
        if (auto* top = studio.top(); top && top->screen_kind() == bindings::Screen::Overview && top->job(job, use)) return true;
        studio.select(bank + unsigned(use.index));
        return true;
    case Job::TrackOptions: studio.options_track = bank + unsigned(use.index); studio.open("track-options"); return true;
    case Job::PeekTrack: return true;
    case Job::PickTrack: case Job::CopyTrack: case Job::ClearTrack: case Job::MoveTrack:
        if (!holding()) return false; // the sixteen latched: the screen's
        if (auto* sixteen = tracks()) sixteen->job(job, use);
        return true;
    case Job::TrackLevel: {
        unsigned t = held_track();
        studio.set_level(t, int(studio.project().tracks[t].mix.level) + use.delta);
        volume_track = t;
        volume_loop = -1;
        volume_until = Clock::now() + volume_linger;
        return true;
    }
    case Job::LoopLevel: { // the Looper: a track button is a loop, L1 on top
        unsigned loop = 0;
        while (loop + 1 < 4 && !rows_down.test(loop)) ++loop;
        studio.loops().set_level(loop, int(studio.loops().mix(loop).level) + use.delta);
        volume_loop = int(loop);
        volume_until = Clock::now() + volume_linger;
        return true;
    }
    case Job::MuteSelected: studio.set_mute(studio.track(), !studio.current().mix.mute); return true;
    case Job::MuteTrack: {
        unsigned t = bank + unsigned(use.index);
        studio.set_mute(t, !studio.project().tracks[t].mix.mute);
        return true;
    }
    case Job::SoloTrack: {
        unsigned t = bank + unsigned(use.index);
        studio.set_solo(t, !studio.project().tracks[t].mix.solo);
        return true;
    }
    case Job::TracksOverview: studio.open("tracks"); return true;
    // A track by its step (the Mixer, the sixteen tracks): Mute held mutes it, Shift + Mute solos it.
    case Job::MuteStep: {
        unsigned t = unsigned(use.index) % melodee::track_count;
        studio.set_mute(t, !studio.project().tracks[t].mix.mute);
        return true;
    }
    case Job::SoloStep: {
        unsigned t = unsigned(use.index) % melodee::track_count;
        studio.set_solo(t, !studio.project().tracks[t].mix.solo);
        return true;
    }
    case Job::UnmuteAll: mixing::unmute_all(studio); studio.say("UNMUTED ALL"); return true;
    case Job::ShowMeter: meter_until = Clock::time_point::max(); return true;
    // Transport and volume.
    case Job::PlayStop:
        if (studio.playing()) studio.stop();
        else studio.play();
        return true;
    case Job::Restart: studio.restart(); return true; // the Move's retrigger: every playing pattern from its top
    case Job::MainVolume:
        studio.volume = std::clamp(studio.volume.load() + use.delta * 0.01f, 0.0f, 1.0f);
        if (meter_until != Clock::time_point::max()) meter_until = Clock::now() + turn_linger; // the Move's meter as it turns
        return true;
    case Job::Metronome: studio.set_click(!studio.click(), studio.click_level()); return false; // and its screen opens
    // Knobs: a touch or a turn shows the knob large; the turn is the screen's.
    case Job::ShowKnob: touched = use.index; touched_until = Clock::time_point::max(); return true;
    case Job::TurnKnob: case Job::TurnKnobFine:
        if (touched != use.index || touched_until != Clock::time_point::max()) {
            touched = use.index;
            touched_until = Clock::now() + turn_linger;
        }
        return false;
    case Job::BackToPlay: studio.home(); return true;
    // Undo, as on the Move; held, Melodee's undo list.
    case Job::Undo: studio.say(studio.undo() ? "UNDO " + studio.last_step : "NOTHING TO UNDO"); return true;
    case Job::Redo: studio.say(studio.redo() ? "REDO " + studio.last_step : "NOTHING TO REDO"); return true;
    case Job::UndoList:
        if (!studio.history.can_undo() && !studio.history.can_redo()) studio.say("NOTHING TO UNDO");
        else if (!studio.top() || studio.top()->id() != "undo-list") studio.push("undo-list");
        return true;
    case Job::Quit: return true; // the runtime's (Shift + Back never arrives)
    case Job::ShiftSteps: return true; // lights() shows them
    case Job::ShiftLock: studio.say(gestures.shift_locked() ? "SHIFT LOCKED" : "SHIFT"); return true;
    // Typing, for a module's page that asked: the pads and steps a keyboard.
    case Job::TypeKey: type(key_for_pad(unsigned(use.index))); return true;
    case Job::TypeDigit: type(step_keys[use.index]); return true;
    case Job::TypeBackspace: type('\b'); return true;
    case Job::TypeTurn: studio.typing_strip = std::clamp(studio.typing_strip + use.delta, 0, int(strip().size()) - 1); return true;
    case Job::TypeDone: { // the strip's key in the box; OK finishes
        const auto& key = strip()[size_t(std::clamp(studio.typing_strip, 0, int(strip().size()) - 1))];
        if (key == "OK") studio.finish_typing(false);
        else if (key == "DL") type('\b');
        else if (key == "SP") type(' ');
        else type(key[0]);
        return true;
    }
    case Job::TypeCancel: studio.finish_typing(true); return true;
    case Job::Uppercase: return true; // type() reads Shift
    default: return false;
    }
}

std::string MelodeeApp::step_name(bindings::Job job, const bindings::Use& use) const {
    using bindings::Job;
    const auto& studio = *own;
    Screen* top = studio.top();
    switch (job) {
    case Job::TurnKnob: case Job::TurnKnobFine:
        if (top) {
            auto shown = top->knob(unsigned(use.index));
            if (!shown.name.empty()) return shown.name + (top->knob_owner().empty() ? "" : " " + top->knob_owner());
        }
        return "KNOB " + std::to_string(use.index + 1);
    case Job::TrackLevel: return "LEVEL T" + std::to_string(held_track() + 1);
    case Job::BrowseTurn: return "PRESET";
    case Job::BrowsePush: return "LOAD";
    case Job::CopyTrack: return "COPY TRACK";
    case Job::ClearTrack: return "DELETE TRACK";
    case Job::ToPadsPlace: return "TO PADS";
    case Job::ListTurn: case Job::ListTurnFine: case Job::ListPush: case Job::ChopAmount:
        return top ? spoken(top->id().c_str()) : "EDIT";
    default: return spoken(bindings::job_name(job));
    }
}

void MelodeeApp::close_gesture() {
    if (!gesture_open) return;
    gesture_open = false;
    own->close_step(gesture_name);
}

void MelodeeApp::act(const bindings::Use& use) {
    using bindings::Control;
    using bindings::Gesture;
    auto& studio = *own;
    const bindings::Binding* row = bindings::find(where(), use);
    // Undo steps: a turn goes on while the same control turns; any other use
    // of a control closes the open one first (a pad's pressure is no use).
    bool turn = use.gesture == Gesture::Turn && use.control != Control::PadPressure;
    if (use.control != Control::PadPressure && gesture_open && !(turn && gesture_control == use.control && gesture_index == use.index))
        close_gesture();
    if (!row || row->job == bindings::Job::Nothing) return;
    auto job = row->job;
    last_job = job;
    std::string name = (turn && gesture_open) || job == bindings::Job::Undo || job == bindings::Job::Redo ? std::string() : step_name(job, use);
    if (studio.opening && job != bindings::Job::MainVolume) return; // a project opening: presses wait (the Loading sheet)
    // Basic (Setup's FEATURES): the Move's own pages, tracks, modules, the mixer and Song; Melodee's Looper waits for Full.
    if (!studio.settings.full && job == bindings::Job::Looper) { studio.say("FULL ONLY: SETUP"); return; }
    [&] {
        if (global(job, use)) return;
        Screen* top = studio.top();
        if (top && top->job(job, use)) return;
        // The track's Play screen under the one on top keeps what that one leaves.
        if (auto* play = studio.screen(home_screen(studio.kind())); play && play != top && play->job(job, use)) return;
        // Then the sequencer (sequencer.h): the steps, bars and patterns, Rec, Capture.
        if (studio.sequencer().job(job, use)) return;
        if (trigger(job)) return;
        // Delete + a knob's touch with no motion to delete: Melodee's Erase + pot, the value back to its start.
        if (job == bindings::Job::DeleteMotion && top) {
            if (top->reset_knob(unsigned(use.index))) studio.name_step("RESET " + top->knob(unsigned(use.index)).name);
            else if (auto* play = studio.screen(home_screen(studio.kind())); play && play != top && play->reset_knob(unsigned(use.index)))
                studio.name_step("RESET " + play->knob(unsigned(use.index)).name);
        }
    }();
    studio.sequencer().after(job, use); // a knob moved while recording, written
    if (no_step(job)) studio.not_a_step();
    if (studio.settings.usage_log && use.control != Control::PadPressure) { // what the hands do, no names (Setup's usage log)
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        usage += std::to_string(ms) + ' ' + (studio.top() ? studio.top()->id() : std::string("-")) + ' ' + bindings::job_name(job) + '\n';
    }
    if (use.control == Control::PadPressure) return;
    if (turn) {
        if (!gesture_open) { gesture_open = true; gesture_control = use.control; gesture_index = use.index; gesture_name = name; }
        gesture_at = Clock::now();
        return;
    }
    studio.close_step(name);
}

void MelodeeApp::settle() {
    auto& studio = *own;
    studio.shift = (gestures.held() & bindings::held::shift) != 0;
    auto at = where();
    // On the Looper the left 16 pads are the loops' rows (the audio thread's,
    // through loop_pads()); whether the rest sound is a right-16 pad's row.
    bool looping = at.screen == bindings::Screen::Looper;
    uint16_t held = gestures.held(bindings::Control::Pad);
    studio.loop_pads(looping, (held & ~(bindings::held::shift | bindings::held::pad)) == 0);
    studio.quiet_pads(!bindings::pad_sounds(at, held, looping ? 4 : 0));
    if (studio.pitches()) studio.refresh_pads(); // 16 Pitches follow the project's key, wherever it was set
}

void MelodeeApp::surface(const move::SurfaceEvent& event) {
    using Kind = move::Input::Kind;
    const auto& input = event.input;
    std::lock_guard<std::mutex> guard(own->lock);
    auto& studio = *own;
    auto now = Clock::now();
    // What the app and the screens show of controls held.
    if (input.kind == Kind::Button)
        if (int row = track_button(move::Button(input.index)); row >= 0) {
            if (input.pressed) {
                if (!holding()) {
                    held_at = now;
                    if (auto* sixteen = tracks()) sixteen->enter();
                }
                rows_down.set(size_t(row));
            } else rows_down.reset(size_t(row));
        }
    if (input.kind == Kind::Touch && !input.pressed && touched == int(input.index) && input.index < move::knob_count)
        touched_until = now + touch_linger;
    if (input.kind == Kind::Touch && !input.pressed && input.index == move::touch_master && meter_until == Clock::time_point::max())
        meter_until = now + touch_linger;
    for (auto* shown : std::vector<Screen*>(studio.stack)) shown->input(input);
    if (holding())
        if (auto* sixteen = tracks()) sixteen->input(input);
    if (!studio.typing) studio.sequencer().input(event); // pads into Seq Record and Capture, steps held
    // Then the jobs.
    studio.shift = (gestures.held() & bindings::held::shift) != 0;
    for (const auto& use : gestures.feed(input, now)) act(use);
    settle();
}

// MIDI from the Move's port (USB-A and USB-C): Melodee's MIDI in (midi.h).
void MelodeeApp::midi(const move::ExternalMidi& message) {
    std::lock_guard<std::mutex> guard(own->lock);
    own->midi.receive(*own, message);
}

unsigned MelodeeApp::midi_out(move::ExternalMidi* out, unsigned room) { return own->midi.take(out, room); }

void MelodeeApp::lights(move::Lights& lights, Clock::time_point now) {
    using namespace move;
    std::lock_guard<std::mutex> guard(own->lock);
    auto& studio = *own;
    studio.midi.pump(studio, now); // MIDI Config and the tracks' channels to the engine
    // How the Move is doing, for Setup's About: the audio cycle's share over the last second.
    if (now - machine_at >= std::chrono::seconds(1)) {
        const auto& run = session();
        uint64_t calls = run.process_calls.load(), ns = run.process_ns.load();
        double period_ns = run.rate ? 1e9 * run.period / run.rate : 2902000.0;
        if (calls > machine_calls) studio.machine.cpu_percent = float(100.0 * double(ns - machine_ns) / double(calls - machine_calls) / period_ns);
        studio.machine.peak_us = unsigned(run.process_ns_peak.load() / 1000);
        studio.machine.late = unsigned(run.xruns.load());
        machine_calls = calls;
        machine_ns = ns;
        machine_at = now;
    }
    // Holds that came due (a track button, Undo, Back held).
    for (const auto& use : gestures.tick(now)) act(use);
    if (gesture_open && now - gesture_at >= gesture_quiet) close_gesture(); // a knob left still: its turns are one step
    studio.loops().tick(now); // a clear pad held long enough, the Looper's refusals, its loops kept
    settle();
    studio.sequencer().tick(now);
    // The track buttons: the selected track's bank, each its colour, the selected one bright.
    // Soloed blue, as on the Move; muted grey.
    unsigned first = ui::bank_of(studio.track()) * ui::tracks_a_bank;
    for (unsigned n = 0; n < 4; ++n) {
        unsigned t = first + n;
        auto colour = studio.colour_of(t); // its own (track options) or its place's
        const auto& mix = studio.project().tracks[t].mix;
        bool selected = t == studio.track();
        uint8_t shown = studio.kind(t) == TrackKind::Empty ? (selected ? colour::dark_grey : colour::off)
                      : mix.solo ? colour::blue : mix.mute ? (selected ? colour::light_grey : colour::dark_grey)
                      : selected ? colour.bright : colour.dim;
        lights.button(track_buttons[n], shown);
    }
    lights.button(Button::Mute, studio.current().mix.mute ? white_bright : white_dim);
    lights.button(Button::Play, studio.playing() ? colour::green : colour::dark_grey);
    lights.button(Button::Shift, studio.shift ? white_bright : white_dim);
    lights.button(Button::Delete, studio.typing ? white_dim : 0);
    lights.button(Button::Copy, 0);
    if (studio.typing) { // the keyboard
        for (unsigned at = 0; at < pad_count; ++at) {
            char c = key_for_pad(at);
            lights.pad(at, c == '\b' ? colour::red : c == ' ' ? colour::azure : std::isalpha(static_cast<unsigned char>(c)) ? colour::light_grey : colour::dark_grey);
        }
        for (unsigned step = 0; step < step_count; ++step) lights.step(step, step < 10 ? colour::light_grey : colour::dark_grey);
        lights.button(Button::Back, white_dim);
        return;
    }
    if (auto* top = studio.top()) {
        top->lights(lights);
        if (!holding() && top->screen_kind() == bindings::Screen::Play) studio.sequencer().lights(lights, now); // the steps
        studio.sequencer().transport_lights(lights, now); // Rec, Capture
    }
    settings_lights(lights, now); // Setup's pad lights, brightness and the light show
    if (holding()) {
        if (auto* sixteen = tracks()) sixteen->lights(lights); // the steps as the tracks, over the screen's
    } else if (studio.shift) { // the steps with a Shift job that works (docs/MOVE_CONTROLS.md); the metronome bright when on
        auto at = where();
        for (unsigned step = 0; step < step_count; ++step) {
            bindings::Use use;
            use.control = bindings::Control::Step;
            use.index = int(step);
            use.held = bindings::held::shift;
            const auto* row = bindings::find(at, use);
            if (!row) { use.gesture = bindings::Gesture::Tap; row = bindings::find(at, use); } // a tap's job (Shift + Step 16)
            bool works = row && row->status == bindings::Status::Done;
            lights.step(step, !works ? colour::off : row->job == bindings::Job::Metronome && studio.click() ? colour::white : colour::light_grey);
        }
    }
}

// Setup's lights (MIDI thread, under `lock`): the brightness on every light;
// on a Play screen, the pads white or dark (Pad lights), or the light show
// in time with the tempo (Melodee's: Pulse, Sweep, Chase, Ripple, Levels).
void MelodeeApp::settings_lights(move::Lights& lights, Clock::time_point now) {
    using namespace move;
    auto& studio = *own;
    const auto& settings = studio.settings;
    lights.set_brightness(settings.brightness);
    Screen* top = studio.top();
    if (!top || top->screen_kind() != bindings::Screen::Play || holding() || studio.shift) return;
    if (!settings.light_show) {
        if (settings.pad_lights == 0) return;
        for (unsigned at = 0; at < pad_count; ++at) {
            uint8_t was = lights.pad_wanted(at);
            lights.pad(at, settings.pad_lights == 2 || !was ? colour::off : was == colour::white ? colour::white : colour::light_grey);
        }
        return;
    }
    // Where the show is: the tempo's beats, playing or not (stopped: half as bright).
    double beats;
    if (studio.playing()) beats = double(studio.host().ticks()) / melodee::ticks_per_quarter;
    else beats = std::chrono::duration<double>(now.time_since_epoch()).count() * studio.tempo() / 600.0;
    unsigned beat = unsigned(beats);
    double phase = beats - std::floor(beats);
    unsigned bar = beat / 4;
    bool playing = studio.playing();
    auto colour = ui::track_colour(bar / 4); // a new colour every four bars
    uint8_t lit = playing ? colour.bright : colour.dim, soft = playing ? colour.dim : colour::off;
    uint32_t held = session().held_pads.load(std::memory_order_relaxed);
    for (unsigned at = 0; at < pad_count; ++at) {
        unsigned column = move::pad_column(at), row = move::pad_row(at);
        uint8_t shown = colour::off;
        switch (settings.show_style % 5) {
        case 0: // Pulse: the whole panel on the beat, harder on beat 1
            if (phase < 0.3) shown = beat % 4 == 0 ? (playing ? colour::white : lit) : lit;
            break;
        case 1: { // Sweep: a bar of light across, left to right and back, a beat each way
            double x = beat % 2 ? 7.0 - phase * 7.0 : phase * 7.0;
            double off = std::abs(double(column) - x);
            shown = off < 0.6 ? lit : off < 1.6 ? soft : colour::off;
            break;
        }
        case 2: { // Chase: a pad a sixteenth, pad 1 to 32 in the pad map's order, in two bars
            unsigned sixteenth = unsigned(beats * 4) % pad_count;
            unsigned number = logical_pad(uint8_t(at), pad_layout());
            shown = number == sixteenth ? lit : (number + 1) % pad_count == sixteenth ? soft : colour::off;
            break;
        }
        case 3: { // Ripple: a ring from the middle on beat 1
            double radius = (beat % 4 == 0 ? phase : 1.0 + phase) * 3.0;
            double dx = double(column) - 3.5, dy = double(row) - 1.5, d = std::sqrt(dx * dx + dy * dy);
            shown = std::abs(d - radius) < 0.7 ? lit : colour::off;
            break;
        }
        default: { // Levels: the mix as a meter, a column each side twice
            float level = std::max(studio.host().out_level(column % 2), 0.0f);
            double db = level > 0 ? 20.0 * std::log10(level) : -100.0;
            unsigned height = db > -6 ? 4 : db > -12 ? 3 : db > -24 ? 2 : db > -48 ? 1 : 0;
            shown = row < height ? (row == 3 ? colour::red : lit) : colour::off;
            break;
        }
        }
        if (held & (1u << at)) shown = colour::white; // a pad pressed flashes white
        lights.pad(at, shown);
    }
    unsigned step_at = unsigned(beats * 4) % step_count;
    for (unsigned step = 0; step < step_count; ++step) lights.step(step, step == step_at ? colour::white : colour::off);
}

// ---------------------------------------------------------------- screen thread

void MelodeeApp::draw(juce::Graphics& g) {
    std::lock_guard<std::mutex> guard(own->lock);
    auto& studio = *own;
    auto now = Clock::now();
    Screen* top = studio.top();
    if (studio.opening) { // a project opening: its name, how far
        ui::system::loading(g, studio.opening_name, studio.opening_samples_done, studio.opening_samples, studio.opening_synths_done,
                            studio.opening_synths, studio.opening_file);
        return;
    }
    if (studio.typing) draw_typing(g);
    else if (holding() && now < volume_until) draw_volume(g);
    else if (now < meter_until) draw_meter(g);
    else if (holding() && now - held_at >= hold_shown && tracks()) tracks()->draw(g);
    else if (top && top->screen_kind() == bindings::Screen::Play && studio.sequencer().draw(g)) {} // a step held: its card, TRIG, a lock
    else if (top && touched >= 0 && now < touched_until && !top->knob(unsigned(touched)).name.empty()) draw_touched(g);
    else if (top) top->draw(g);
    if (studio.dirty && !studio.typing) ui::system::unsaved_mark(g); // Melodee's UNSAVED: changes not saved yet
    if (now < studio.message_until) {
        ui::sheet(g, {ui::content_x, ui::detail_y - 2, ui::content_width, 10});
        move::draw_text_centred(g, ui::content_x, ui::detail_y, ui::content_width, studio.message);
    }
}

void MelodeeApp::draw_touched(juce::Graphics& g) {
    auto* top = own->top();
    auto shown = top->knob(unsigned(touched));
    ui::rail(g, "KNOB", std::to_string(touched + 1), top->knob_owner());
    ui::big_parameter(g, ui::content(4, 44), shown.name, shown.value, shown.fraction);
    if (auto detail = top->knob_detail(unsigned(touched)); !detail.empty()) ui::detail(g, detail.substr(0, 16));
}

void MelodeeApp::draw_volume(juce::Graphics& g) {
    auto& studio = *own;
    if (volume_loop >= 0) {
        int level = studio.loops().mix(unsigned(volume_loop)).level;
        std::string tag = "L" + std::to_string(volume_loop + 1);
        ui::rail(g, "VOL", tag, "LOOP");
        ui::big_parameter(g, ui::content(4, 44), tag + " VOLUME", melodee::decibels(level) + " DB", level / 127.0f);
        return;
    }
    int level = studio.project().tracks[volume_track].mix.level;
    ui::rail(g, "VOL", std::to_string(volume_track + 1), "TRK");
    ui::big_parameter(g, ui::content(4, 44), "TRACK " + std::to_string(volume_track + 1) + " VOLUME", melodee::decibels(level) + " DB", level / 127.0f);
}

// The main meter: what goes out, left and right, in dB, as the Move's VU
// meter shows it, with marks at -24, -12, -6, -3 and 0.
void MelodeeApp::draw_meter(juce::Graphics& g) {
    auto& studio = *own;
    float volume = studio.volume.load();
    float sides[2] {studio.host().out_level(0) * volume, studio.host().out_level(1) * volume};
    float loudest = std::max(sides[0], sides[1]);
    auto fraction = [](float peak) { return peak <= 0 ? 0.0f : std::clamp((20.0f * std::log10(peak) + 30.0f) / 30.0f, 0.0f, 1.0f); };
    char text[16];
    if (loudest <= 0.00001f) std::snprintf(text, sizeof text, "-INF");
    else std::snprintf(text, sizeof text, "%.1fDB", double(20.0f * std::log10(loudest)));
    ui::rail(g, "OUT", "", "");
    move::draw_text_centred(g, ui::content_x, 2, ui::content_width, text, 2);
    int w = ui::content_width - 4;
    for (int side = 0; side < 2; ++side) {
        int y = 22 + side * 9;
        g.drawRect(ui::content_x + 2, y, w, 7);
        g.fillRect(ui::content_x + 3, y + 1, int(std::lround(fraction(sides[side]) * float(w - 2))), 5);
    }
    for (int db : {-24, -12, -6, -3, 0}) g.fillRect(ui::content_x + 2 + (w - 1) * (db + 30) / 30, 40, 1, 2);
    move::draw_text(g, ui::content_x + 1, 44, "-24  -12 -6 -3 0");
    ui::detail(g, "VOLUME " + std::to_string(int(std::lround(volume * 100.0f))));
}

void MelodeeApp::draw_typing(juce::Graphics& g) {
    auto& studio = *own;
    // The name sheet: the strip around the key in the box, seven at a time.
    const auto& keys = strip();
    int picked = std::clamp(studio.typing_strip, 0, int(keys.size()) - 1);
    int first = std::clamp(picked - 3, 0, int(keys.size()) - 7);
    std::vector<std::string> shown;
    for (int n = first; n < first + 7; ++n) shown.push_back(keys[size_t(n)].size() == 1 && studio.shift ? upper(keys[size_t(n)]) : keys[size_t(n)]);
    ui::system::name_sheet(g, upper(studio.typing_title.empty() ? "TEXT" : studio.typing_title), studio.typed, studio.typing_room, shown,
                           size_t(picked - first), studio.shift);
}

// ---------------------------------------------------------------- service thread, and the rest

void MelodeeApp::stopping() { own->stopping(); }

void MelodeeApp::service() {
    own->service();
    // The usage log: a file each start, written whole every ten seconds while there is more.
    std::string text;
    {
        std::lock_guard<std::mutex> guard(own->lock);
        auto now = Clock::now();
        if (usage.size() == usage_written || now - usage_at < std::chrono::seconds(10)) return;
        usage_at = now;
        usage_written = usage.size();
        text = usage;
    }
    std::error_code error;
    auto folder = std::filesystem::path(own->home_folder()) / "Usage";
    std::filesystem::create_directories(folder, error);
    auto file = (folder / ("usage-" + std::to_string(started_unix) + ".txt")).string();
    write_whole(file, text.data(), text.size());
    give_to_owner_of(file, own->home_folder());
}

std::string control_path(const std::string& home) {
    std::error_code error;
    std::string folder = !home.empty() ? home : std::filesystem::is_directory("/data/UserData", error) ? "/data/UserData/Melodee" : "Melodee";
    std::filesystem::create_directories(folder, error);
    return (std::filesystem::path(folder) / "melodee-move.sock").string();
}

bool MelodeeApp::finished() const {
    std::lock_guard<std::mutex> guard(own->lock);
    return own->quit;
}

std::string MelodeeApp::command(const std::string& line) {
    // melodee-looper ARGS: the Looper's remote (docs/LOOPER.md).
    if (line.compare(0, 14, "melodee-looper") == 0) {
        std::lock_guard<std::mutex> guard(own->lock);
        return own->loops().remote(line.substr(14));
    }
    // melodee-render [serial|parallel]: the render workers left asleep (the
    // audio thread renders alone) or used, from the next block (MELODEE.md › Remote).
    if (line.compare(0, 14, "melodee-render") == 0) {
        auto& host = own->host();
        if (line.find("serial") != std::string::npos) host.set_render_parallel(false);
        else if (line.find("parallel") != std::string::npos) host.set_render_parallel(true);
        return std::string("render=") + (host.render_parallel() ? "parallel" : "serial") + " workers=" + std::to_string(host.render_threads());
    }
    // melodee-midi HEX... | panic: a message into MIDI in as if it came on the port, or MIDI Panic (src/app/midi.h).
    if (line.compare(0, 12, "melodee-midi") == 0) {
        std::lock_guard<std::mutex> guard(own->lock);
        return own->midi.command(*own, line.substr(std::min<size_t>(line.size(), 13)));
    }
    return {};
}

std::string MelodeeApp::status() const {
    std::lock_guard<std::mutex> guard(own->lock);
    return own->status() + " job=" + bindings::job_name(last_job);
}

unsigned MelodeeApp::slice_count() const {
    auto line = status();
    auto at = line.find(" slices=");
    return at == std::string::npos ? 0 : unsigned(std::strtoul(line.c_str() + at + 8, nullptr, 10));
}

std::string MelodeeApp::synth_value(const std::string& key) const {
    std::lock_guard<std::mutex> guard(own->lock);
    auto* track = own->module_track();
    const ModuleParam* param = track && track->module ? track->module->param(key) : nullptr;
    return param ? param->text() : std::string();
}

std::string MelodeeApp::ask_module(const std::string& key) {
    std::lock_guard<std::mutex> guard(own->lock);
    auto* track = own->module_track();
    return track ? own->module_get(*track, key) : std::string();
}

bool MelodeeApp::canvas_pixel(int x, int y) const {
    std::lock_guard<std::mutex> guard(own->lock);
    auto* track = own->module_track();
    return track && !track->canvas_pages.empty() && track->canvas_pages.front() && track->canvas_pages.front()->pixel(x, y);
}

} // namespace app
