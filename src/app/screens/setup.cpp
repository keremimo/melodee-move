// SPDX-License-Identifier: AGPL-3.0-only
// Setup (Shift + Step 2, the Move's own; docs/UI.md "Projects and system",
// docs/ui/projects.png) as Melodee's Settings: a list the wheel walks, a push
// opening a door or editing a value (the wheel turns it, a push keeps it,
// Back leaves it as it was; a value of two flips at once). PADS: the pads'
// response and pressure. LIGHTS: pad lights, pad fade, brightness, the light
// show and its style; LAYOUT (move-pad-layout's). MIDI and MIDI SYNC
// (move-midi's MIDI Config) and WEB PAGE (move-web's) where those screens
// are here. FEATURES Full or Basic, the usage log,
// ABOUT, and BACK TO MOVE (Ableton's app again, as Shift + Back). Every
// setting is the same for every project, kept in Move.settings.
#include "app/screens/common.h"
#include "ui/system_draw.h"
#include <sys/statvfs.h>

namespace app::screens {
namespace {

namespace sys = ui::system;

struct Row {
    const char* name;
    std::vector<const char*> values; // none: a door or an action
    const char* door = "";           // a screen id, or "" for an action / a level of this list
};

class Setup final : public Screen {
public:
    using Screen::Screen;

    void enter() override { level = 0; cursor = 0; editing = false; }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        auto shown = rows();
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (editing) {
                const auto& row = shown[size_t(cursor)];
                set(row.name, std::clamp(value(row.name) + use.delta, 0, int(row.values.size()) - 1));
            } else {
                cursor = std::clamp(cursor + use.delta, 0, int(shown.size()) - 1);
            }
            return true;
        case Job::ListPush: push(shown); return true;
        case Job::Back:
            if (editing) { set(shown[size_t(cursor)].name, was); editing = false; return true; }
            if (level) { cursor = level == 1 ? 0 : 1; level = 0; return true; }
            studio.close();
            return true;
        case Job::PlayPad: case Job::PlayNote: return false; // the pads play on
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        if (auto* play = studio.screen(home_screen(studio.kind())); play && play != this) play->lights(lights);
        lights.button(move::Button::Back, move::white_dim);
        // Setup's icon under Step 2 flashes while it is open, as on the Move, and its step with it.
        bool on = std::chrono::duration_cast<std::chrono::milliseconds>(Studio::Clock::now().time_since_epoch()).count() / 400 % 2;
        lights.step(1, on ? move::colour::white : move::colour::dark_grey);
        lights.step_icon(1, on ? move::white_bright : 0);
    }

    void draw(juce::Graphics& g) override {
        std::vector<sys::SetupRow> list;
        for (const auto& row : rows()) {
            sys::SetupRow drawn;
            drawn.name = row.name;
            drawn.door = row.values.empty() && (row.door[0] || std::string(row.name) == "PADS" || std::string(row.name) == "LIGHTS");
            if (!row.values.empty()) drawn.value = row.values[size_t(std::clamp(value(row.name), 0, int(row.values.size()) - 1))];
            drawn.editing = editing && std::string(row.name) == rows()[size_t(cursor)].name;
            list.push_back(drawn);
        }
        sys::setup_list(g, level == 1 ? "PADS" : level == 2 ? "LGHT" : "SETP", level ? "SETP" : "", list, cursor);
    }

    std::string status() const override {
        if (studio.top() != this) return {};
        return " setup_level=" + std::to_string(level) + " setup_row=" + std::to_string(cursor) + " setup_editing=" + std::to_string(int(editing));
    }

private:
    int level = 0, cursor = 0, was = 0; // 0 Setup, 1 PADS, 2 LIGHTS
    bool editing = false;

    std::vector<Row> rows() const {
        if (level == 1) {
            std::vector<Row> pads {{"RESPONSE", {"SOFT", "MED", "HARD", "FULL"}}, {"PRESSURE", {"OFF", "ON"}}};
            if (studio.screen("setup-pads")) pads.push_back({"LAYOUT", {}, "setup-pads"}); // move-pad-layout's: Move rows or two MPC banks
            return pads;
        }
        if (level == 2)
            return {{"PAD LIGHTS", {"COLOUR", "WHITE", "OFF"}}, {"PAD FADE", {"60 MS", "100 MS", "150 MS", "250 MS"}},
                    {"BRIGHTNESS", {"LOW", "MID", "HIGH", "MAX"}}, {"LIGHT SHOW", {"OFF", "ON"}},
                    {"SHOW STYLE", {"PULSE", "SWEEP", "CHASE", "RIPPLE", "LEVELS"}}};
        std::vector<Row> top {{"PADS", {}}, {"LIGHTS", {}}};
        // The other areas' doors, where their screens are here: move-midi's
        // MIDI Config (MIDI, and MIDI SYNC as Ableton's Setup has both),
        // move-web's page.
        if (studio.screen("midi-config")) {
            top.push_back({"MIDI", {}, "midi-config"});
            top.push_back({"MIDI SYNC", {}, studio.screen("midi-sync") ? "midi-sync" : "midi-config"});
        }
        if (studio.screen("web-page")) top.push_back({"WEB PAGE", {}, "web-page"});
        top.push_back({"FEATURES", {"BASIC", "FULL"}});
        top.push_back({"USAGE LOG", {"OFF", "ON"}});
        top.push_back({"ABOUT", {}, "about"});
        top.push_back({"BACK TO MOVE", {}});
        return top;
    }
    int value(const std::string& name) const {
        const auto& s = studio.settings;
        if (name == "RESPONSE") return s.response;
        if (name == "PRESSURE") return s.pressure;
        if (name == "PAD LIGHTS") return s.pad_lights;
        if (name == "PAD FADE") return s.pad_fade;
        if (name == "BRIGHTNESS") return s.brightness;
        if (name == "LIGHT SHOW") return s.light_show;
        if (name == "SHOW STYLE") return s.show_style;
        if (name == "FEATURES") return s.full;
        if (name == "USAGE LOG") return s.usage_log;
        return 0;
    }
    void set(const std::string& name, int v) {
        auto& s = studio.settings;
        if (name == "RESPONSE") s.response = uint8_t(v);
        else if (name == "PRESSURE") s.pressure = v != 0;
        else if (name == "PAD LIGHTS") s.pad_lights = uint8_t(v);
        else if (name == "PAD FADE") s.pad_fade = uint8_t(v);
        else if (name == "BRIGHTNESS") s.brightness = uint8_t(v);
        else if (name == "LIGHT SHOW") s.light_show = v != 0;
        else if (name == "SHOW STYLE") s.show_style = uint8_t(v);
        else if (name == "FEATURES") s.full = v != 0;
        else if (name == "USAGE LOG") s.usage_log = v != 0;
        studio.settings_changed();
    }
    void push(const std::vector<Row>& shown) {
        const auto& row = shown[size_t(cursor)];
        std::string name = row.name;
        if (editing) { editing = false; return; } // kept
        if (!row.values.empty()) {
            if (row.values.size() == 2) { set(name, 1 - value(name)); return; } // on or off: at once
            was = value(name);
            editing = true;
            return;
        }
        if (name == "PADS") { level = 1; cursor = 0; return; }
        if (name == "LIGHTS") { level = 2; cursor = 0; return; }
        if (name == "BACK TO MOVE") { studio.quit = true; return; } // saved on the way out, as Shift + Back
        if (row.door[0]) studio.push(row.door);
    }
};

// About: how the Move is doing (Melodee's ABOUT): the audio cycle's share and
// its longest, late exchanges, room left, the modules and their slow calls.
class About final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override { read_room(); }
    bool job(bindings::Job job, const bindings::Use&) override {
        if (job == bindings::Job::Back || job == bindings::Job::ListPush) { studio.close(); return true; }
        return job == bindings::Job::ListTurn || job == bindings::Job::ListTurnFine;
    }
    void lights(move::Lights& lights) override {
        if (auto* play = studio.screen(home_screen(studio.kind())); play && play != this) play->lights(lights);
        lights.button(move::Button::Back, move::white_dim);
    }
    void draw(juce::Graphics& g) override {
        sys::About about;
        about.cpu_percent = studio.machine.cpu_percent;
        about.peak_us = studio.machine.peak_us;
        about.late = studio.machine.late;
        about.free_gb = free_gb;
        about.synths = 0;
        for (unsigned t = 0; t < melodee::track_count; ++t)
            if (auto* module = studio.module_track(t); module && module->module) ++about.synths;
        about.slow_calls = studio.slow_calls;
        about.version = studio.machine.version;
        sys::about(g, about);
    }

private:
    double free_gb = 0;
    void read_room() { // one statvfs, as the screen opens
        struct statvfs room {};
        if (statvfs(studio.home_folder().c_str(), &room) == 0) free_gb = double(room.f_bavail) * double(room.f_frsize) / 1e9;
    }
};

RegisterScreen registered_setup("setup", make<Setup>, {{bindings::Job::Setup}});
RegisterScreen registered_about("about", make<About>);

} // namespace
} // namespace app::screens
