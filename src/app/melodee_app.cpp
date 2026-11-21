// SPDX-License-Identifier: AGPL-3.0-only
#include "app/melodee_app.h"
#include <cmath>
#include "app/screen.h"
#include "app/sequencer.h"
#include "move/display.h"
#include "move/font.h"
#include "ui/track_style.h"
#include "ui/widgets.h"
#include "melodee/engine/engine_info.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace app {
namespace {

constexpr auto touch_linger = std::chrono::milliseconds(600);
constexpr auto hold_shown = bindings::Gestures::hold_time; // a track button held this long shows the sixteen (the Move's hold)
constexpr auto volume_linger = std::chrono::milliseconds(1200);

constexpr auto turn_linger = std::chrono::milliseconds(1200);

// Typing, for a module's page: the pads a keyboard in the pad layout
// (pad_layout.h: reading order, Delete bottom right), the steps digits and
// marks.
constexpr const char* step_keys = "1234567890()/:,!";
char key_for_pad(unsigned pad) { return typing_key(uint8_t(pad), pad_layout()); }

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
    : own(std::make_unique<Studio>(std::move(library), std::move(home), std::move(modules))) {}

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
    if (studio.typed.size() >= 120) return;
    studio.typed += studio.shift ? char(std::toupper(static_cast<unsigned char>(c))) : c;
}

// The jobs that are the same on every screen.
bool MelodeeApp::global(bindings::Job job, const bindings::Use& use) {
    using bindings::Job;
    auto& studio = *own;
    unsigned bank = ui::bank_of(studio.track()) * ui::tracks_a_bank;
    switch (job) {
    // The track buttons: a tap selects, held the sixteen (drawn), a step picks.
    case Job::SelectTrack: studio.select(bank + unsigned(use.index)); return true;
    case Job::PeekTrack: return true;
    case Job::PickTrack: case Job::CopyTrack: case Job::ClearTrack:
        if (!holding()) return false; // the sixteen latched: the screen's
        if (auto* sixteen = tracks()) sixteen->job(job, use);
        return true;
    case Job::TrackLevel: {
        unsigned t = held_track();
        studio.set_level(t, int(studio.project().tracks[t].mix.level) + use.delta);
        volume_track = t;
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
    case Job::Quit: return true; // the runtime's (Shift + Back never arrives)
    case Job::ShiftSteps: return true; // lights() shows them
    case Job::ShiftLock: studio.say(gestures.shift_locked() ? "SHIFT LOCKED" : "SHIFT"); return true;
    // Typing, for a module's page that asked: the pads and steps a keyboard.
    case Job::TypeKey: type(key_for_pad(unsigned(use.index))); return true;
    case Job::TypeDigit: type(step_keys[use.index]); return true;
    case Job::TypeBackspace: type('\b'); return true;
    case Job::TypeDone: studio.finish_typing(false); return true;
    case Job::TypeCancel: studio.finish_typing(true); return true;
    case Job::Uppercase: return true; // type() reads Shift
    default: return false;
    }
}

void MelodeeApp::act(const bindings::Use& use) {
    auto& studio = *own;
    const bindings::Binding* row = bindings::find(where(), use);
    if (!row || row->job == bindings::Job::Nothing) return;
    last_job = row->job;
    bool done = global(row->job, use);
    Screen* top = studio.top();
    if (!done) done = top && top->job(row->job, use);
    // The track's Play screen under the one on top keeps what that one leaves.
    if (auto* play = studio.screen(home_screen(studio.kind())); !done && play && play != top) done = play->job(row->job, use);
    // Then the sequencer (sequencer.h): the steps, bars and patterns, Rec, Capture.
    if (!done) done = studio.sequencer().job(row->job, use);
    if (!done) trigger(row->job);
    studio.sequencer().after(row->job, use); // a knob moved while recording, written
}

void MelodeeApp::settle() {
    auto& studio = *own;
    studio.shift = (gestures.held() & bindings::held::shift) != 0;
    studio.quiet_pads(!bindings::pad_sounds(where(), gestures.held(bindings::Control::Pad), 0));
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

void MelodeeApp::lights(move::Lights& lights, Clock::time_point now) {
    using namespace move;
    std::lock_guard<std::mutex> guard(own->lock);
    auto& studio = *own;
    // Holds that came due (a track button, Undo, Back held).
    for (const auto& use : gestures.tick(now)) act(use);
    settle();
    studio.sequencer().tick(now);
    // The track buttons: the selected track's bank, each its colour, the selected one bright.
    // Soloed blue, as on the Move; muted grey.
    unsigned first = ui::bank_of(studio.track()) * ui::tracks_a_bank;
    for (unsigned n = 0; n < 4; ++n) {
        unsigned t = first + n;
        auto colour = ui::track_colour(t);
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

// ---------------------------------------------------------------- screen thread

void MelodeeApp::draw(juce::Graphics& g) {
    std::lock_guard<std::mutex> guard(own->lock);
    auto& studio = *own;
    auto now = Clock::now();
    Screen* top = studio.top();
    if (studio.typing) draw_typing(g);
    else if (holding() && now < volume_until) draw_volume(g);
    else if (now < meter_until) draw_meter(g);
    else if (holding() && now - held_at >= hold_shown && tracks()) tracks()->draw(g);
    else if (top && top->screen_kind() == bindings::Screen::Play && studio.sequencer().draw(g)) {} // a step held: its card, TRIG, a lock
    else if (top && touched >= 0 && now < touched_until && !top->knob(unsigned(touched)).name.empty()) draw_touched(g);
    else if (top) top->draw(g);
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
}

void MelodeeApp::draw_volume(juce::Graphics& g) {
    auto& studio = *own;
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
    move::draw_text(g, 1, 1, upper(studio.typing_title.empty() ? "TEXT" : studio.typing_title).substr(0, 21));
    g.fillRect(0, 10, ui::width, 1);
    std::string line = studio.typed + "_";
    if (line.size() > 21) line = line.substr(line.size() - 21);
    move::draw_text(g, 1, 24, line);
    move::draw_text(g, 1, 46, "PADS: LETTERS");
    move::draw_text(g, 1, 56, "PUSH: OK  BACK: DEL");
}

// ---------------------------------------------------------------- service thread, and the rest

void MelodeeApp::service() { own->service(); }

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
