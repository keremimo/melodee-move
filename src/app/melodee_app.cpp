// SPDX-License-Identifier: AGPL-3.0-only
#include "app/melodee_app.h"
#include "app/screen.h"
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
constexpr auto hold_shown = std::chrono::milliseconds(200); // a track button held this long shows the sixteen
constexpr auto volume_linger = std::chrono::milliseconds(1200);

// Shift + a step opens a setting, as on the Move (its manual, "Shift + Step"):
// the step (0 is Step 1) and the screen. Step 6 also turns the metronome on
// or off.
struct ShiftStep { unsigned step; const char* screen; };
constexpr ShiftStep shift_steps[] {{4, "tempo"}, {5, "metronome"}, {6, "groove"}};
constexpr auto turn_linger = std::chrono::milliseconds(1200);

// Typing, for a module's page: the pads a keyboard, top row first ('\b'
// backspace), the steps digits and marks.
constexpr const char* keyboard_rows[4] {"abcdefgh", "ijklmnop", "qrstuvwx", "yz -.'&\b"};
constexpr const char* step_keys = "1234567890()/:,!";
char key_for_pad(unsigned pad) { return keyboard_rows[3 - pad / 8][pad % 8]; }

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

bool MelodeeApp::trigger(move::Button button) {
    auto& studio = *own;
    for (const auto& entry : registered_screens())
        for (const auto& when : entry.triggers) {
            if (when.button != button || when.shift != studio.shift) continue;
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

void MelodeeApp::surface(const move::SurfaceEvent& event) {
    using Kind = move::Input::Kind;
    using move::Button;
    const auto& input = event.input;
    std::lock_guard<std::mutex> guard(own->lock);
    auto& studio = *own;
    studio.shift = session().shift.load();
    if (studio.typing) { // a module's page asked for text: the pads and steps are a keyboard
        if (input.kind == Kind::Pad && input.pressed && input.index < pad_count) type(key_for_pad(input.index));
        else if (input.kind == Kind::Step && input.pressed && input.index < 16) type(step_keys[input.index]);
        else if (input.kind == Kind::Button && input.pressed) {
            auto button = Button(input.index);
            if (button == Button::Delete) type('\b');
            else if (button == Button::MainPush) studio.finish_typing(false);
            else if (button == Button::Back || button == Button::Menu) studio.finish_typing(true);
        }
        return;
    }
    Screen* top = studio.top();
    switch (input.kind) {
    case Kind::Button: {
        auto button = Button(input.index);
        if (button == Button::Mute) { // with a track button: that track; alone: the selected one
            if (input.pressed) { mute_down = true; mute_used = false; }
            else {
                if (mute_down && !mute_used) studio.set_mute(studio.track(), !studio.current().mix.mute);
                mute_down = false;
            }
            return;
        }
        if (int row = track_button(button); row >= 0) {
            unsigned t = ui::bank_of(studio.track()) * ui::tracks_a_bank + unsigned(row);
            if (mute_down && input.pressed) { // Mute + track: mute it; Shift + Mute + track: solo it
                mute_used = true;
                const auto& mix = studio.project().tracks[t].mix;
                if (studio.shift) studio.set_solo(t, !mix.solo);
                else studio.set_mute(t, !mix.mute);
                return;
            }
            if (input.pressed) {
                if (!holding()) {
                    held_at = Clock::now();
                    if (auto* sixteen = tracks()) sixteen->enter();
                }
                rows_down.set(size_t(row));
                studio.select(t);
            } else rows_down.reset(size_t(row));
            return;
        }
        // A track button held: Copy and Delete are the sixteen tracks' (with a step).
        if (holding() && (button == Button::Copy || button == Button::Delete)) {
            if (auto* sixteen = tracks()) sixteen->surface(input);
            return;
        }
        if (button == Button::Play) {
            // Shift + Play is the Move's retrigger: left for the bindings table (docs/MOVE_CONTROLS.md).
            if (input.pressed && !studio.shift) {
                if (studio.playing()) studio.stop();
                else studio.play();
            }
            return;
        }
        if (top && top->surface(input)) return;
        if (input.pressed) trigger(button);
        return;
    }
    case Kind::Step:
        if (holding()) { // the steps are the sixteen tracks
            if (auto* sixteen = tracks()) sixteen->surface(input);
            return;
        }
        if (studio.shift) { // the Move's settings
            if (input.pressed)
                for (const auto& setting : shift_steps)
                    if (setting.step == input.index) {
                        if (setting.step == 5) studio.set_click(!studio.click(), studio.click_level());
                        studio.open(setting.screen);
                    }
            return;
        }
        break;
    case Kind::MasterKnob:
        if (holding()) { // a track button held: that track's volume, as on the Move
            studio.set_level(studio.track(), int(studio.current().mix.level) + input.delta);
            volume_until = Clock::now() + volume_linger;
            return;
        }
        studio.volume = std::clamp(studio.volume.load() + input.delta * 0.01f, 0.0f, 1.0f);
        return;
    case Kind::Touch:
        if (input.index >= move::knob_count) break;
        if (input.pressed) { touched = input.index; touched_until = Clock::time_point::max(); }
        else if (touched == int(input.index)) touched_until = Clock::now() + touch_linger;
        return;
    case Kind::Knob:
        if (top) top->surface(input);
        if (touched != int(input.index) || touched_until != Clock::time_point::max()) {
            touched = input.index;
            touched_until = Clock::now() + turn_linger;
        }
        return;
    default: break;
    }
    if (top) top->surface(input);
}

void MelodeeApp::lights(move::Lights& lights, Clock::time_point) {
    using namespace move;
    std::lock_guard<std::mutex> guard(own->lock);
    auto& studio = *own;
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
    lights.button(Button::Shift, session().shift ? white_bright : white_dim);
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
    if (auto* top = studio.top()) top->lights(lights);
    if (holding()) {
        if (auto* sixteen = tracks()) sixteen->lights(lights); // the steps as the tracks, over the screen's
    } else if (session().shift) { // the steps that open a setting
        for (unsigned step = 0; step < step_count; ++step) lights.step(step, colour::off);
        for (const auto& setting : shift_steps)
            lights.step(setting.step, setting.step == 5 && studio.click() ? colour::white : colour::light_grey);
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
    else if (holding() && now - held_at >= hold_shown && tracks()) tracks()->draw(g);
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
    int level = studio.current().mix.level;
    ui::rail(g, "VOL", std::to_string(studio.track() + 1), "TRK");
    ui::big_parameter(g, ui::content(4, 44), "TRACK " + std::to_string(studio.track() + 1) + " VOLUME", melodee::decibels(level) + " DB", level / 127.0f);
}

void MelodeeApp::draw_typing(juce::Graphics& g) {
    auto& studio = *own;
    move::draw_text(g, 1, 1, upper(studio.typing_title.empty() ? "TEXT" : studio.typing_title).substr(0, 21));
    g.fillRect(0, 10, ui::width, 1);
    std::string line = studio.typed + "_";
    if (line.size() > 21) line = line.substr(line.size() - 21);
    move::draw_text(g, 1, 24, line);
    move::draw_text(g, 1, 46, "PADS: LETTERS");
    move::draw_text(g, 1, 56, "PUSH: OK  BACK: CANCEL");
}

// ---------------------------------------------------------------- service thread, and the rest

void MelodeeApp::service() { own->service(); }

std::string MelodeeApp::status() const {
    std::lock_guard<std::mutex> guard(own->lock);
    return own->status();
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
