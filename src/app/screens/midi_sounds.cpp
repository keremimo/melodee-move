// SPDX-License-Identifier: AGPL-3.0-only
// A MIDI track's Sounds (docs/UI.md, "MIDI and sync"): the wheel's push on
// the track's sound opens them, as the Move's browser: the MIDI engine's own
// ("MIDI OUT") and the ones saved, Melodee's `.mlds` files in
// Melodee/Sounds (the MPC's Sounds, kept the same way), the starred ones
// marked. Each loads as the wheel turns (the Move's Autoload); push or Back
// is done. Shift held: SAVE SOUND (the track's values under a new name),
// STAR or UNSTAR, SHOW all, the starred or the ones loaded lately.
#include "app/project_store.h"
#include "app/screens/common.h"
#include "melodee/engine/engine_info.h"
#include "melodee/model/sound_file.h"
#include <filesystem>
#include <fstream>
#include <set>

namespace app::screens {
namespace {

namespace fs = std::filesystem;
using melodee::Command;
using melodee::EngineKind;

enum Show { All, Starred, Recent, ShowCount };
const char* const show_names[] {"ALL", "STARRED", "RECENT"};

class MidiSounds final : public Screen {
public:
    using Screen::Screen;

    bindings::Screen screen_kind() const override { return options ? bindings::Screen::List : bindings::Screen::Browser; }

    void enter() override {
        options = false;
        option = 0;
        read_folder();
        list();
        // On the sound loaded last, when it is listed.
        cursor = 0;
        for (size_t n = 0; n < shown.size(); ++n)
            if (sounds[shown[n]].name == loaded_name()) cursor = int(n);
    }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        if (studio.current().engine != EngineKind::Midi) return false;
        switch (job) {
        case Job::BrowseTurn:
            if (shown.empty()) return true;
            cursor = std::clamp(cursor + use.delta, 0, int(shown.size()) - 1);
            load(shown[size_t(cursor)]); // as the Move autoloads
            return true;
        case Job::BrowsePush: studio.close(); return true;
        case Job::BrowserOptions: options = true; option = 0; return true;
        case Job::ListTurn: case Job::ListTurnFine:
            if (options) option = std::clamp(option + (use.delta > 0 ? 1 : -1), 0, 2);
            return true;
        case Job::ListPush:
            if (options) act();
            return true;
        case Job::Back:
            if (options) options = false;
            else studio.close();
            return true;
        case Job::PlayNote: return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        for (auto button : {move::Button::Left, move::Button::Right, move::Button::Up, move::Button::Down, move::Button::Loop, move::Button::Menu})
            lights.button(button, 0);
        lights.button(move::Button::Back, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        if (options) {
            ui::rail(g, "OPT", track_number(studio), "SND");
            bool starred = !shown.empty() && stars.count(sounds[shown[size_t(cursor)]].name);
            std::string show = std::string("SHOW") + std::string(12 - std::strlen(show_names[filter]), ' ') + show_names[filter];
            ui::list(g, ui::content(0, ui::height), {"SAVE SOUND", starred ? "UNSTAR" : "STAR", show}, option, 1);
            return;
        }
        ui::rail(g, "LOAD", track_number(studio), "SND");
        std::vector<std::string> rows;
        for (size_t index : shown) rows.push_back((stars.count(sounds[index].name) ? "* " : "") + upper(sounds[index].name).substr(0, 15));
        if (rows.empty()) rows.push_back(filter == Starred ? "NONE STARRED" : "NONE LOADED YET");
        ui::list(g, ui::content(0, 50), rows, shown.empty() ? -1 : cursor, 1);
        ui::detail(g, show_names[filter], "SHIFT: OPT");
    }

    std::string status() const override {
        if (studio.top() != this) return {};
        return " sounds=" + std::to_string(shown.size()) + " sound=" + (shown.empty() ? std::string("-") : sounds[shown[size_t(cursor)]].name) +
               " sounds_options=" + std::to_string(int(options));
    }

private:
    struct Sound { std::string name, file; unsigned factory = 0; bool own = false; };

    std::string folder() const { return (fs::path(studio.home_folder()) / "Sounds").string(); }
    std::string stars_file() const { return (fs::path(folder()) / "Starred.txt").string(); }
    std::string loaded_name() const { return studio.current().preset < sounds.size() ? sounds[studio.current().preset].name : std::string(); }

    // The engine's own sounds, then the saved ones for a MIDI track, by name.
    void read_folder() {
        sounds.clear();
        const auto& info = melodee::engine_info(EngineKind::Midi);
        for (unsigned n = 0; n < info.sound_count; ++n) sounds.push_back({info.sounds[n].name, {}, n, false});
        std::vector<Sound> own;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(folder(), error)) {
            if (entry.path().extension() != ".mlds" || own.size() >= 256) continue;
            std::ifstream in(entry.path(), std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            melodee::UserSound sound;
            if (!melodee::load_sound_file(bytes, sound) || sound.engine != EngineKind::Midi) continue;
            own.push_back({sound.name[0] ? std::string(sound.name) : entry.path().stem().string(), entry.path().string(), 0, true});
        }
        std::sort(own.begin(), own.end(), [](const Sound& a, const Sound& b) { return a.name < b.name; });
        sounds.insert(sounds.end(), own.begin(), own.end());
        stars.clear();
        std::ifstream in(stars_file());
        for (std::string line; std::getline(in, line);) if (!line.empty()) stars.insert(line);
    }
    void list() {
        shown.clear();
        for (size_t n = 0; n < sounds.size(); ++n) {
            bool recent = std::find(recents().begin(), recents().end(), sounds[n].name) != recents().end();
            if (filter == All || (filter == Starred && stars.count(sounds[n].name)) || (filter == Recent && recent)) shown.push_back(n);
        }
        cursor = std::clamp(cursor, 0, std::max(0, int(shown.size()) - 1));
    }
    static std::vector<std::string>& recents() { static std::vector<std::string> names; return names; } // this run's

    // Every value of the sound onto the track, and to the engine.
    void load(size_t index) {
        auto& track = studio.current();
        const auto& sound = sounds[index];
        if (sound.own) {
            std::ifstream in(sound.file, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            melodee::UserSound saved;
            if (!melodee::load_sound_file(bytes, saved) || saved.engine != EngineKind::Midi) { studio.say("SOUND UNREADABLE"); return; }
            track.params = saved.params;
            track.mods = saved.mods;
        } else {
            melodee::load_sound(track, EngineKind::Midi, sound.factory);
        }
        track.preset = uint16_t(index);
        const auto& info = melodee::engine_info(EngineKind::Midi);
        for (unsigned id = 0; id < info.param_count; ++id)
            studio.send({Command::Type::Param, uint8_t(studio.track()), 0, uint16_t(id), track.params[id]});
        auto& recent = recents();
        recent.erase(std::remove(recent.begin(), recent.end(), sound.name), recent.end());
        recent.insert(recent.begin(), sound.name);
        if (recent.size() > 16) recent.pop_back();
        studio.changed();
    }

    void act() {
        if (option == 0) save();
        else if (option == 1 && !shown.empty()) {
            const auto& name = sounds[shown[size_t(cursor)]].name;
            if (!stars.erase(name)) stars.insert(name);
            write_stars();
            studio.say(stars.count(name) ? "STARRED" : "UNSTARRED");
        } else if (option == 2) {
            filter = (filter + 1) % ShowCount;
            list();
            return; // the sheet stays: SHOW again
        }
        options = false;
    }

    // The track's values as a sound of its own, named after the track and
    // the first number free ("JUNO 2"); typing a name waits for the Name sheet.
    void save() {
        std::error_code error;
        fs::create_directories(folder(), error);
        std::string base = upper(studio.current().name);
        if (base.empty()) base = "MIDI";
        std::string name;
        for (unsigned n = 1; n < 1000; ++n) {
            name = base.substr(0, melodee::name_length - 5) + " " + std::to_string(n);
            if (!fs::exists(fs::path(folder()) / (name + ".mlds"), error)) break;
        }
        auto bytes = melodee::save_sound(melodee::sound_of(studio.current(), name.c_str()));
        auto failed = write_whole((fs::path(folder()) / (name + ".mlds")).string(), bytes.data(), bytes.size());
        studio.say(failed.empty() ? "SAVED " + name : "NOT SAVED");
        read_folder();
        list();
        for (size_t n = 0; n < shown.size(); ++n) if (sounds[shown[n]].name == name) cursor = int(n);
    }
    void write_stars() {
        std::error_code error;
        fs::create_directories(folder(), error);
        std::string text;
        for (const auto& name : stars) text += name + "\n";
        write_whole(stars_file(), text.data(), text.size());
    }

    std::vector<Sound> sounds;
    std::vector<size_t> shown;
    std::set<std::string> stars;
    int cursor = 0, option = 0;
    int filter = All;
    bool options = false;
};

RegisterScreen sounds("midi-sounds", make<MidiSounds>);

} // namespace
} // namespace app::screens
