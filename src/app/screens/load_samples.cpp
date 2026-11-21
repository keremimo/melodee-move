// SPDX-License-Identifier: AGPL-3.0-only
// Load (Menu on a kit track): sound files onto the last pad, read by
// Melodee's own read_audio (WAV, AIFF, FLAC, Ogg, MP3). It opens on a list
// of places: the Move's own sample folders, Ableton's Core Library, all of
// /data/UserData (or only --samples DIR). The jogwheel picks, its push opens
// a place or folder or loads, Back goes up, then back to the places.
#include "app/screens/common.h"
#include "app/song_desk.h"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace app::screens {
namespace {

namespace fs = std::filesystem;

struct MovePlace { const char* name; const char* path; const char* tag; };
constexpr MovePlace move_places[] {
    {"MY SAMPLES", "/data/UserData/UserLibrary/Samples", "SMP"},
    {"RECORDINGS", "/data/UserData/UserLibrary/Recordings", "REC"},
    {"CORE LIBRARY", "/data/CoreLibrary/Samples", "CORE"},
    {"ALL USER DATA", "/data/UserData", "ALL"},
};

// The kinds Melodee's read_audio() reads (shell/sample_decode.h).
bool is_sound_file(const fs::path& path) {
    auto extension = upper(path.extension().string());
    return extension == ".WAV" || extension == ".AIF" || extension == ".AIFF" || extension == ".FLAC" || extension == ".OGG" || extension == ".MP3";
}

// A Standard MIDI File: its notes load onto the track's pattern playing (song_desk.h, as Melodee's browser loads one).
bool is_midi_file(const fs::path& path) {
    auto extension = upper(path.extension().string());
    return extension == ".MID" || extension == ".MIDI" || extension == ".SMF";
}

class LoadSamples final : public Screen {
public:
    explicit LoadSamples(Studio& studio) : Screen(studio) {
        std::error_code error;
        if (!studio.library.empty()) places.push_back({"SAMPLES", studio.library, "SMP"});
        else
            for (const auto& candidate : move_places)
                if (fs::is_directory(candidate.path, error)) places.push_back({candidate.name, candidate.path, candidate.tag});
        if (places.empty()) places.push_back({"HERE", fs::current_path(error).string(), "HERE"});
        if (places.size() == 1) place = 0; // one place: straight into it
        root = folder = places[size_t(std::max(place, 0))].path;
    }

    void enter() override { list_folder(); }

    bindings::Screen screen_kind() const override { return bindings::Screen::Browser; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::BrowseTurn: cursor = std::clamp(cursor + use.delta, 0, std::max(0, int(entries.size()) - 1)); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: return true;
        case Job::Back:
            if (place >= 0 && fs::path(folder) != fs::path(root)) { folder = fs::path(folder).parent_path().string(); cursor = 0; list_folder(); }
            else if (place >= 0 && places.size() > 1) { place = -1; cursor = 0; list_folder(); } // back to the places
            else studio.close();
            return true;
        case Job::BrowsePush:
            if (place < 0) { open_place(cursor); return true; }
            if (cursor < int(entries.size())) {
                const auto& entry = entries[size_t(cursor)];
                auto path = (fs::path(folder) / entry.name).string();
                if (entry.folder) { folder = path; cursor = 0; list_folder(); }
                else if (is_midi_file(path)) { // its notes onto the pattern playing
                    std::ifstream in(path, std::ios::binary);
                    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    auto failed = studio.desk().load_midi(bytes, studio.track());
                    studio.say(failed.empty() ? "NOTES ONTO " + pattern_name(studio.current().pattern) : failed.substr(0, 16));
                }
                else { studio.jobs.push_back({path, studio.track(), studio.pad()}); studio.say("LOADING"); }
            }
            return true;
        default: return false; // a pad: the kit's Play screen picks it, the one a sample loads onto
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, at == studio.pad() ? colour::white : studio.current().pads[at].used() ? ui::track_colour(studio.track()).dim : colour::off);
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop}) lights.button(button, 0);
        lights.button(Button::Menu, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "LOAD", pad_label(studio.pad()), !studio.jobs.empty() ? "..." : place < 0 ? "FROM" : places[size_t(place)].tag);
        if (entries.empty()) {
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "NO SOUND FILES");
            ui::detail(g, upper(fs::path(folder).filename().string()).substr(0, 16));
            return;
        }
        std::vector<std::string> rows;
        for (const auto& entry : entries) rows.push_back(entry.folder ? entry.name + " >" : entry.name);
        bool midi = cursor < int(entries.size()) && is_midi_file(entries[size_t(cursor)].name);
        ui::list(g, ui::content(0, midi ? 52 : ui::height), rows, cursor, 1);
        if (midi) ui::detail(g, "PUSH: ONTO " + pattern_name(studio.current().pattern));
    }

private:
    struct Place { std::string name, path, tag; };
    struct Entry { std::string name; bool folder = false; };

    void open_place(int index) {
        if (index < 0 || index >= int(places.size())) return;
        place = index;
        root = folder = places[size_t(index)].path;
        cursor = 0;
        list_folder();
    }

    void list_folder() {
        entries.clear();
        if (place < 0) { // the places themselves
            for (const auto& where : places) entries.push_back({where.name, true});
            cursor = std::clamp(cursor, 0, std::max(0, int(entries.size()) - 1));
            return;
        }
        std::error_code error;
        for (const auto& item : fs::directory_iterator(folder, fs::directory_options::skip_permission_denied, error)) {
            auto name = item.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            if (item.is_directory(error)) entries.push_back({name, true});
            else if (is_sound_file(item.path()) || is_midi_file(item.path())) entries.push_back({name, false});
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.folder != b.folder) return a.folder;
            return upper(a.name) < upper(b.name);
        });
        cursor = std::clamp(cursor, 0, std::max(0, int(entries.size()) - 1));
    }

    std::vector<Place> places;
    int place = -1; // -1: the list of places
    std::string root, folder;
    std::vector<Entry> entries;
    int cursor = 0;
};

RegisterScreen registered("load-samples", make<LoadSamples>); // the wheel's push on a kit's pad (the Device View)

} // namespace
} // namespace app::screens
