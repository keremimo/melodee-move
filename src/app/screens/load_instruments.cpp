// SPDX-License-Identifier: AGPL-3.0-only
// The instrument browser (docs/UI.md, "Instruments"): the wheel's push on a
// keygroup track's instrument. Instruments Melodee reads (an SFZ, an MPC
// keygroup program .xpm or MPC 3's .xty, an EXS24 instrument, a
// DecentSampler preset) and sound files, which a keygroup plays from the
// note found in them; a SoundFont opens as a folder of its presets. It opens
// on a list of places, as Load does: the Move's sample folders, Ableton's
// Core Library, Melodee's own Samples (the auto sampler's Keygroups among
// them), all of /data/UserData (or only --samples DIR). The jogwheel picks,
// its push opens a place, a folder or a SoundFont, or loads onto the track;
// Back goes up, then back to the places, then out.
#include "app/instruments.h"
#include "app/screens/note_pages.h"
#include <filesystem>

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

bool sound_file(const fs::path& path) {
    auto extension = upper(path.extension().string());
    return extension == ".WAV" || extension == ".AIF" || extension == ".AIFF" || extension == ".FLAC" || extension == ".OGG" || extension == ".MP3";
}

class LoadInstruments final : public Screen {
public:
    explicit LoadInstruments(Studio& studio) : Screen(studio) {}

    void enter() override {
        places.clear();
        std::error_code error;
        if (!studio.library.empty()) places.push_back({"SAMPLES", studio.library, "INS"});
        else {
            for (const auto& candidate : move_places)
                if (fs::is_directory(candidate.path, error)) places.push_back({candidate.name, candidate.path, "INS"});
        }
        auto own = (fs::path(studio.home_folder()) / "Samples").string();
        if (fs::is_directory(own, error)) places.push_back({"MELODEE", own, "INS"});
        if (places.empty()) places.push_back({"HERE", fs::current_path(error).string(), "INS"});
        if (place < 0 && places.size() == 1) open_place(0);
        if (place >= int(places.size())) { place = -1; cursor = 0; }
        list_folder();
    }

    bindings::Screen screen_kind() const override { return bindings::Screen::Browser; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::BrowseTurn: cursor = std::clamp(cursor + use.delta, 0, std::max(0, int(entries.size()) - 1)); return true;
        case Job::PlayNote: case Job::TurnKnob: case Job::TurnKnobFine: return true;
        case Job::Back:
            if (!soundfont.empty()) { soundfont.clear(); cursor = 0; list_folder(); }
            else if (place >= 0 && fs::path(folder) != fs::path(root)) { folder = fs::path(folder).parent_path().string(); cursor = 0; list_folder(); }
            else if (place >= 0 && places.size() > 1) { place = -1; cursor = 0; list_folder(); }
            else studio.close();
            return true;
        case Job::BrowsePush: {
            if (place < 0) { open_place(cursor); return true; }
            if (cursor >= int(entries.size())) return true;
            const auto& entry = entries[size_t(cursor)];
            if (entry.kind == Entry::Kind::Folder) { folder = entry.path; cursor = 0; list_folder(); return true; }
            if (entry.kind == Entry::Kind::Soundfont) { soundfont = entry.path; cursor = 0; list_folder(); return true; }
            studio.jobs.push_back({entry.path, studio.track(), 0});
            studio.say("LOADING");
            return true;
        }
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        std::array<bool, pad_count> none {};
        key_pads(studio, lights, none);
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "LOAD", track_number(studio), !studio.jobs.empty() ? "..." : place < 0 ? "FROM" : !soundfont.empty() ? "SF2" : "INS");
        if (entries.empty()) {
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, error.empty() ? "NOTHING TO LOAD" : upper(error).substr(0, 16));
            ui::detail(g, upper(fs::path(soundfont.empty() ? folder : soundfont).filename().string()).substr(0, 16));
            return;
        }
        std::vector<std::string> rows;
        for (const auto& entry : entries) rows.push_back(entry.row);
        ui::list(g, ui::content(0, soundfont.empty() ? ui::height : 50), rows, cursor, 1);
        if (!soundfont.empty())
            ui::detail(g, upper(fs::path(soundfont).filename().string()).substr(0, 9), std::to_string(cursor + 1) + "/" + std::to_string(entries.size()));
    }

    std::string status() const override { return " browse=" + std::to_string(entries.size()) + (soundfont.empty() ? "" : " sf2=1"); }

private:
    struct Place { std::string name, path, tag; };
    struct Entry {
        enum class Kind : uint8_t { Folder, Soundfont, Instrument, Sample } kind = Kind::Folder;
        std::string name, path, row;
    };

    void open_place(int index) {
        if (index < 0 || index >= int(places.size())) return;
        place = index;
        root = folder = places[size_t(index)].path;
        soundfont.clear();
        cursor = 0;
        list_folder();
    }

    // A row: the name, the kind on the right (16 characters in all).
    static std::string row_of(std::string name, const std::string& tag) {
        name = upper(name);
        size_t room = 16 - tag.size() - 1;
        if (name.size() > room) name = name.substr(0, room);
        return name + std::string(16 - name.size() - tag.size(), ' ') + tag;
    }

    void list_folder() {
        entries.clear();
        error.clear();
        if (place < 0) {
            for (const auto& where : places) entries.push_back({Entry::Kind::Folder, where.name, where.path, where.name + " >"});
            cursor = std::clamp(cursor, 0, std::max(0, int(entries.size()) - 1));
            return;
        }
        if (!soundfont.empty()) { // a SoundFont's presets, as a folder
            for (const auto& preset : soundfont_presets(soundfont, error)) {
                char number[8];
                std::snprintf(number, sizeof number, "%03d ", preset.program);
                entries.push_back({Entry::Kind::Instrument, preset.name, preset_path(soundfont, preset.index), upper(std::string(number) + preset.name).substr(0, 16)});
            }
            cursor = std::clamp(cursor, 0, std::max(0, int(entries.size()) - 1));
            return;
        }
        std::error_code failed;
        for (const auto& item : fs::directory_iterator(folder, fs::directory_options::skip_permission_denied, failed)) {
            auto name = item.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            auto path = item.path().string();
            if (item.is_directory(failed)) entries.push_back({Entry::Kind::Folder, name, path, upper(name).substr(0, 14) + " >"});
            else if (soundfont_file(path)) entries.push_back({Entry::Kind::Soundfont, name, path, row_of(item.path().stem().string(), ">")});
            else if (instrument_file(path)) entries.push_back({Entry::Kind::Instrument, name, path, row_of(item.path().stem().string(), instrument_format(path))});
            else if (sound_file(item.path())) entries.push_back({Entry::Kind::Sample, name, path, row_of(item.path().stem().string(), upper(item.path().extension().string().substr(1)))});
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if ((a.kind == Entry::Kind::Folder) != (b.kind == Entry::Kind::Folder)) return a.kind == Entry::Kind::Folder;
            return upper(a.name) < upper(b.name);
        });
        cursor = std::clamp(cursor, 0, std::max(0, int(entries.size()) - 1));
    }

    std::vector<Place> places;
    int place = -1; // -1: the list of places
    std::string root, folder, soundfont, error;
    std::vector<Entry> entries;
    int cursor = 0;
};

RegisterScreen registered("load-instruments", make<LoadInstruments>); // the wheel's push on a keygroup's instrument

} // namespace
} // namespace app::screens
