// SPDX-License-Identifier: AGPL-3.0-only
// Load (the wheel's push on a kit's pad, the Device View): sound files onto
// the last pad, read by Melodee's own read_audio (WAV, AIFF, FLAC, Ogg,
// MP3). It opens on a list of places: STARRED and RECENT first (Melodee's,
// when they hold files), the Move's own sample folders, Melodee's, Ableton's
// Core Library, all of /data/UserData (or only --samples DIR). The jogwheel
// picks, and each file plays as it lands (PREVIEW, on Sampling's settings
// page); its push opens a place or folder, loads a file (into RECENT), or
// installs an MPC expansion; Shift + push stars a file; Delete asks, then
// deletes (docs/MOVE_CONTROLS.md, Decisions); a pad picks the pad a file
// loads onto. Back goes up, then back to the places. Kit depth: a bank file
// (.mldb) loads into the bank of the pad in view, an MPC drum program (.xpm,
// .xty) opens the LOAD KIT sheet (load_kit.cpp), Shift held the browser's
// options (Save Sample, Save Bank). A Standard MIDI File's notes load onto
// the track's pattern playing (song_desk.h, as Melodee's browser loads one).
#include "app/instruments.h"
#include "app/sampling.h"
#include "app/pad_layout.h"
#include "app/screens/kit_common.h"
#include "app/song_desk.h"
#include "melodee/shell/mpc_program.h"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace app::screens {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct MovePlace { const char* name; const char* path; const char* tag; };
constexpr MovePlace move_places[] {
    {"MY SAMPLES", "/data/UserData/UserLibrary/Samples", "SMP"},
    {"RECORDINGS", "/data/UserData/UserLibrary/Recordings", "REC"},
    {"CORE LIBRARY", "/data/CoreLibrary/Samples", "CORE"},
    {"ALL USER DATA", "/data/UserData", "ALL"},
};

std::string extension_of(const fs::path& path) { return upper(path.extension().string()); }
// The kinds Melodee's read_audio() reads (shell/sample_decode.h).
bool is_sound_file(const fs::path& path) {
    auto extension = extension_of(path);
    return extension == ".WAV" || extension == ".AIF" || extension == ".AIFF" || extension == ".FLAC" || extension == ".OGG" || extension == ".MP3";
}
bool is_expansion(const fs::path& path) { return extension_of(path) == ".XPN"; }
bool is_program(const fs::path& path) { auto e = extension_of(path); return e == ".XPM" || e == ".XTY"; }
bool is_bank_file(const fs::path& path) { return extension_of(path) == ".MLDB"; }
bool is_midi_file(const fs::path& path) { auto e = extension_of(path); return e == ".MID" || e == ".MIDI" || e == ".SMF"; }

class LoadSamples final : public Screen {
public:
    explicit LoadSamples(Studio& studio) : Screen(studio) {
        std::error_code error;
        if (!studio.library.empty()) places.push_back({"SAMPLES", studio.library, "SMP"});
        else {
            for (const auto& candidate : move_places)
                if (fs::is_directory(candidate.path, error)) places.push_back({candidate.name, candidate.path, candidate.tag});
            // Melodee's own folder (before the Core Library): takes, edits and expansions in its
            // Samples, and what the web page uploads.
            places.insert(places.begin() + std::min<size_t>(places.size(), 2), {"MELODEE", studio.home_folder(), "MLD"});
        }
        if (places.empty()) places.push_back({"HERE", fs::current_path(error).string(), "HERE"});
        if (places.size() == 1) place = 0; // one place: straight into it
        root = folder = places[size_t(std::max(place, 0))].path;
    }

    void enter() override {
        seen_installs = sampling().installs;
        seen_deletes = sampling().deletes;
        seen_changes = studio.file_changes;
        list_folder();
        landed();
    }
    void leave() override { sampling().preview_file("", studio.track()); }

    bindings::Screen screen_kind() const override { return bindings::Screen::Browser; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::BrowseTurn:
            cursor = std::clamp(cursor + use.delta, 0, std::max(0, int(entries.size()) - 1));
            landed();
            return true;
        case Job::TurnKnob: case Job::TurnKnobFine: return true;
        case Job::Back:
            if (list != List::None) { list = List::None; cursor = 0; list_folder(); } // a list opened from the top: back there
            else if (place >= 0 && fs::path(folder) != fs::path(root)) { auto was = fs::path(folder).filename().string(); folder = fs::path(folder).parent_path().string(); list_folder(); land_on(was); }
            else if (place >= 0 && places.size() > 1) { place = -1; cursor = 0; list_folder(); } // back to the places
            else { studio.close(); return true; }
            landed();
            return true;
        case Job::BrowsePush: push(); return true;
        case Job::StarFile: {
            const Entry* entry = at_cursor();
            if (!entry || entry->kind != Kind::File) { studio.say("STAR: A FILE"); return true; }
            bool now = sampling().star(entry->path);
            studio.say(now ? "STARRED" : "UNSTARRED");
            if (list == List::Starred) list_folder();
            return true;
        }
        case Job::DeleteFile: {
            const Entry* entry = at_cursor();
            if (!entry || (entry->kind != Kind::File && entry->kind != Kind::Folder)) return true;
            pending = entry->path;
            refused = sampling().may_delete(pending);
            studio.push("delete-file");
            return true;
        }
        case Job::BrowserOptions: studio.push("browser-options"); return true; // Shift held: Save Sample, Save Bank
        default: return false; // a pad: the kit's Play screen picks it, the one a sample loads onto
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        auto& s = sampling();
        if (s.installs != seen_installs) { // an expansion installed: its folder opened
            seen_installs = s.installs;
            if (!s.installed.empty()) open_folder(s.installed);
        }
        if (s.deletes != seen_deletes) { seen_deletes = s.deletes; list_folder(); landed(); }
        if (studio.file_changes != seen_changes) { // the web page put a file here, or took one away
            seen_changes = studio.file_changes;
            const auto* was = cursor < int(entries.size()) ? &entries[size_t(cursor)] : nullptr;
            std::string name = was ? was->name : std::string();
            list_folder();
            if (!name.empty()) land_on(name);
        }
        // The pad a file loads onto blinks (the Move's browser), the kit's in their colour, where the pad layout puts them.
        kit_pads(studio, lights, int(studio.pad()));
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop}) lights.button(button, 0);
        lights.button(Button::Menu, 0);
        lights.button(Button::Back, white_dim);
        const Entry* entry = at_cursor();
        lights.button(Button::Delete, entry && (entry->kind == Kind::File || entry->kind == Kind::Folder) ? white_dim : 0);
    }

    void draw(juce::Graphics& g) override {
        auto& s = sampling();
        if (!s.installing.empty()) { // an expansion going in
            unsigned percent = s.install_total ? s.install_done * 100 / s.install_total : 0;
            ui::rail(g, "XPN", std::to_string(std::min(percent, 99u)), "%");
            move::draw_text(g, ui::content_x + 2, 2, "INSTALLING");
            move::draw_text(g, ui::content_x + 2, 14, s.installing.substr(0, 16));
            g.drawRect(ui::content_x + 2, 30, ui::content_width - 6, 6);
            g.fillRect(ui::content_x + 3, 31, (ui::content_width - 8) * int(percent) / 100, 4);
            ui::detail(g, "FILE " + std::to_string(s.install_done) + "/" + std::to_string(s.install_total));
            return;
        }
        std::string tag = list == List::Starred ? "STAR" : list == List::Recent ? "RCNT" : place < 0 ? "FROM" : places[size_t(place)].tag;
        ui::rail(g, "LOAD", pad_label(studio.pad()), !studio.jobs.empty() ? "..." : tag);
        if (entries.empty()) {
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, list == List::None ? "NO SOUND FILES" : "NONE YET");
            ui::detail(g, upper(fs::path(folder).filename().string()).substr(0, 16));
            return;
        }
        std::vector<std::string> rows;
        for (const auto& entry : entries) rows.push_back(row_of(entry));
        const Entry* entry = at_cursor();
        if (entry && entry->kind == Kind::Midi) { // its notes go onto the pattern playing
            ui::list(g, ui::content(0, 52), rows, cursor, 1);
            ui::detail(g, "PUSH: ONTO " + pattern_name(studio.current().pattern));
            return;
        }
        bool playing = entry && entry->kind == Kind::File && s.previewing == entry->path;
        if (!playing) { ui::list(g, ui::content(0, ui::height), rows, cursor, 1); return; }
        // The file playing: a head through it, the seconds heard of its length.
        ui::list(g, ui::content(0, 50), rows, cursor, 1);
        double heard = std::chrono::duration<double>(Clock::now() - s.preview_at).count(), length = s.preview_frames / 44100.0;
        heard = std::min(heard, length);
        g.fillRect(ui::content_x + 1, 51, int((ui::content_width - 4) * (length > 0 ? heard / length : 0)), 2);
        char text[24];
        std::snprintf(text, sizeof text, "%.1f/%.1fS", heard, length);
        ui::detail(g, "PREVIEW", text);
    }

    std::string status() const override {
        return " browse=" + std::string(list == List::Starred ? "starred" : list == List::Recent ? "recent" : place < 0 ? "places" : "folder") +
               " starred=" + std::to_string(sampling().starred.size()) + " recent=" + std::to_string(sampling().recent.size()) +
               " preview=" + (sampling().previewing.empty() ? std::string("-") : fs::path(sampling().previewing).filename().string());
    }

    // The delete sheet's.
    const std::string& delete_target() const { return pending; }
    const std::string& delete_refused() const { return refused; }
    void deleted() { pending.clear(); }

private:
    enum class Kind : uint8_t { Place, List, Folder, File, Expansion, Program, Bank, Midi };
    enum class List : uint8_t { None, Starred, Recent };
    struct Place { std::string name, path, tag; };
    struct Entry { std::string name, path; Kind kind = Kind::File; List list = List::None; int place = -1; std::string tag {}; };

    Sampling& sampling() const { return studio.sampling(); }
    const Entry* at_cursor() const { return cursor >= 0 && cursor < int(entries.size()) ? &entries[size_t(cursor)] : nullptr; }

    std::string row_of(const Entry& entry) const {
        switch (entry.kind) {
        case Kind::Place: case Kind::Folder: return entry.name + " >";
        case Kind::List: {
            auto count = std::to_string(entry.list == List::Starred ? sampling().starred.size() : sampling().recent.size()) + " >";
            std::string name = entry.name;
            while (name.size() + count.size() < 16) name += ' ';
            return name + count;
        }
        case Kind::Expansion: return entry.name + " XPN";
        case Kind::Program: case Kind::Bank: { // the name, then KIT (a drum program), PROG or BANK
            auto name = fs::path(entry.name).stem().string().substr(0, 11);
            return name + std::string(12 - name.size(), ' ') + entry.tag;
        }
        default: return (sampling().is_starred(entry.path) ? "*" : "") + entry.name;
        }
    }

    // The cursor landed: a file plays (PREVIEW), anything else stops one.
    void landed() {
        const Entry* entry = at_cursor();
        bool plays = entry && ((entry->kind == Kind::File && entry->tag.empty()) || entry->kind == Kind::Program); // an instrument: no preview
        sampling().preview_file(plays ? entry->path : std::string(), studio.track());
    }

    void push() {
        const Entry* entry = at_cursor();
        if (!entry) return;
        switch (entry->kind) {
        case Kind::Place: open_place(entry->place); break;
        case Kind::List: list = entry->list; cursor = 0; list_folder(); break;
        case Kind::Folder: folder = entry->path; cursor = 0; list_folder(); break;
        case Kind::Expansion: sampling().install(entry->path); return;
        case Kind::Program: // a drum program: the LOAD KIT sheet (load_kit.cpp); keygroups load with the instruments
            if (entry->tag != "KIT") { studio.say("PROGRAMS: NOT YET"); return; }
            studio.program_path = entry->path;
            studio.push("load-kit");
            return;
        case Kind::Bank:
            if (auto failed = studio.load_bank_file(entry->path, studio.pad() / 16); !failed.empty()) studio.say(failed);
            return;
        case Kind::Midi: { // its notes onto the pattern playing
            std::ifstream in(entry->path, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto failed = studio.song_desk().load_midi(bytes, studio.track());
            studio.say(failed.empty() ? "NOTES ONTO " + pattern_name(studio.current().pattern) : failed.substr(0, 16));
            return;
        }
        case Kind::File:
            studio.jobs.push_back({entry->path, studio.track(), studio.pad()});
            sampling().loaded(entry->path);
            studio.say("LOADING");
            return;
        }
        landed();
    }

    void open_place(int index) {
        if (index < 0 || index >= int(places.size())) return;
        place = index;
        list = List::None;
        root = folder = places[size_t(index)].path;
        cursor = 0;
        list_folder();
    }
    // A folder anywhere (an expansion just installed): the place it is in, then the folder.
    void open_folder(const std::string& path) {
        int found = -1;
        for (size_t n = 0; n < places.size(); ++n)
            if (path.rfind(places[n].path, 0) == 0) { found = int(n); break; }
        if (found >= 0) { place = found; root = places[size_t(found)].path; }
        else { place = std::max(place, 0); root = path; } // in no place (--samples elsewhere): Back leaves it
        list = List::None;
        folder = path;
        cursor = 0;
        list_folder();
        landed();
    }
    void land_on(const std::string& name) {
        for (size_t n = 0; n < entries.size(); ++n)
            if (entries[n].name == name) { cursor = int(n); return; }
    }

    void list_folder() {
        entries.clear();
        auto& s = sampling();
        auto lists_first = [&] { // STARRED and RECENT lead, when they hold files
            if (!s.starred.empty()) entries.push_back({"STARRED", {}, Kind::List, List::Starred});
            if (!s.recent.empty()) entries.push_back({"RECENT", {}, Kind::List, List::Recent});
        };
        if (list != List::None) { // a list: its files, each named by the folder it is in on the detail
            for (const auto& path : list == List::Starred ? s.starred : s.recent)
                entries.push_back({fs::path(path).filename().string(), path, Kind::File});
        } else if (place < 0) { // the places themselves
            lists_first();
            for (size_t n = 0; n < places.size(); ++n) entries.push_back({places[n].name, places[n].path, Kind::Place, List::None, int(n)});
        } else {
            std::error_code error;
            std::vector<Entry> found;
            for (const auto& item : fs::directory_iterator(folder, fs::directory_options::skip_permission_denied, error)) {
                auto name = item.path().filename().string();
                if (name.empty() || name[0] == '.' || name == "[Previews]") continue; // an expansion's previews play from its programs
                if (name.size() > 12 && name.compare(name.size() - 12, 12, "_[TrackData]") == 0) continue;
                if (item.is_directory(error)) found.push_back({name, item.path().string(), Kind::Folder});
                else if (is_sound_file(item.path())) found.push_back({name, item.path().string(), Kind::File});
                else if (is_expansion(item.path())) found.push_back({name, item.path().string(), Kind::Expansion});
                else if (is_program(item.path()))
                    found.push_back({name, item.path().string(), Kind::Program, List::None, -1, melodee::is_drum_program(item.path().string()) ? "KIT" : "PROG"});
                else if (is_bank_file(item.path())) found.push_back({name, item.path().string(), Kind::Bank, List::None, -1, "BANK"});
                else if (is_midi_file(item.path())) found.push_back({name, item.path().string(), Kind::Midi});
                else if (instrument_file(item.path().string()) && !soundfont_file(item.path().string())) // an SFZ, EXS, DecentSampler
                    found.push_back({name, item.path().string(), Kind::File, List::None, -1, instrument_format(item.path().string())});
            }
            std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) {
                auto rank = [](Kind k) { return k == Kind::Program ? 0 : k == Kind::Folder ? 1 : 2; }; // programs, folders, files
                if (rank(a.kind) != rank(b.kind)) return rank(a.kind) < rank(b.kind);
                return upper(a.name) < upper(b.name);
            });
            if (places.size() == 1 && fs::path(folder) == fs::path(places.front().path)) lists_first(); // one place: they lead its top
            entries.insert(entries.end(), found.begin(), found.end());
        }
        cursor = std::clamp(cursor, 0, std::max(0, int(entries.size()) - 1));
    }

    std::vector<Place> places;
    int place = -1; // -1: the list of places
    List list = List::None;
    std::string root, folder;
    std::vector<Entry> entries;
    int cursor = 0;
    unsigned seen_installs = 0, seen_deletes = 0, seen_changes = 0;
    std::string pending, refused;
};

// Delete in the browser: asks (push yes, Back no); what may not go says why.
class DeleteFile final : public Screen {
public:
    using Screen::Screen;
    LoadSamples& browser() const { return static_cast<LoadSamples&>(*studio.screen("load-samples")); }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(bindings::Job job, const bindings::Use&) override {
        using bindings::Job;
        switch (job) {
        case Job::ListPush:
            if (browser().delete_refused().empty()) { studio.sampling().delete_path(browser().delete_target()); studio.say("DELETING"); }
            browser().deleted();
            studio.close();
            return true;
        case Job::Back: browser().deleted(); studio.close(); return true;
        case Job::ListTurn: case Job::ListTurnFine: case Job::TurnKnob: case Job::TurnKnobFine: case Job::PlayPad: return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        browser().lights(lights);
        lights.button(move::Button::Back, move::white_dim);
    }
    void draw(juce::Graphics& g) override {
        const auto& target = browser().delete_target();
        const auto& refused = browser().delete_refused();
        ui::rail(g, "DEL", "", std::filesystem::is_directory(target) ? "DIR" : "FILE");
        auto name = upper(fs::path(target).filename().string()).substr(0, 16);
        if (!refused.empty()) {
            move::draw_text_centred(g, ui::content_x, 4, ui::content_width, refused == "CORE LIBRARY" ? "READ ONLY" : "KEPT", 2);
            move::draw_text_centred(g, ui::content_x, 26, ui::content_width, name);
            move::draw_text_centred(g, ui::content_x, 36, ui::content_width, refused.substr(0, 16));
            ui::detail(g, "NOT DELETED");
            return;
        }
        move::draw_text_centred(g, ui::content_x, 4, ui::content_width, "DELETE", 2);
        move::draw_text_centred(g, ui::content_x, 26, ui::content_width, name);
        move::draw_text_centred(g, ui::content_x, 36, ui::content_width, ("FROM " + upper(fs::path(target).parent_path().filename().string())).substr(0, 16));
        ui::detail(g, "PUSH YES", "BACK NO");
    }
};

RegisterScreen registered("load-samples", make<LoadSamples>); // the wheel's push on a kit's pad (the Device View)
RegisterScreen deleting("delete-file", make<DeleteFile>);

} // namespace
} // namespace app::screens
