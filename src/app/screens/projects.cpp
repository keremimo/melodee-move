// SPDX-License-Identifier: AGPL-3.0-only
// The projects, as the Move's Set Overview (Shift + Step 1; docs/UI.md
// "Projects and system", docs/ui/projects.png; Kerem's decisions in
// docs/MOVE_CONTROLS.md): a folder's projects on the 32 pads, each keeping
// its pad, folders grey, the one picked pulsing. A pad picks; Back or a track
// button opens it (an empty pad: a new project there), as the Move enters
// Note Mode with the Set picked; Play opens it and plays it (the Move's
// preview). The wheel's push or Shift + a pad opens the picked one's options
// (Melodee's project sheet), a folder's push goes in and Back comes out.
// Copy + a pad, then a pad copies a project (asking before it replaces one);
// Delete + a pad asks, the pad again deletes; Delete alone asks for the one
// picked. Plus and Minus page through the folder, 32 pads a page.
//
// The files are the service thread's (Studio::ask_projects()); the folder as
// it read it last is Studio::listing.
#include "app/screens/common.h"
#include "ui/system_draw.h"
#include <filesystem>

namespace app::screens {
namespace {

using projects::Entry;
using Job = Studio::ProjectJob;
namespace sys = ui::system;

// The Set Overview's pads: the top row first, left to right, as the Move
// lays its 32 Sets.
unsigned pad_for(unsigned slot) { return (3 - slot / 8) * 8 + slot % 8; }
unsigned slot_for(unsigned pad) { return (3 - pad / 8) * 8 + pad % 8; }

// The sixteen colours a project or a track can take: the tracks' own.
constexpr const char* colour_names[16] {"AZURE", "ORANGE", "VIOLET", "GREEN", "YELLOW", "MAGENTA", "CYAN", "RED",
                                        "ROYAL", "PINK", "MINT", "PURPLE", "LIME", "ROSE", "SKY", "TAN"};
std::string colour_name(uint8_t palette) {
    for (unsigned n = 0; n < 16; ++n)
        if (ui::track_colours[n].bright == palette) return colour_names[n];
    return "NONE";
}

class Overview final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        // The open project's folder, its pad picked once the folder is read.
        auto root = studio.projects_root();
        auto folder = std::filesystem::path(studio.project_path()).parent_path().string();
        studio.listing_folder = folder.size() > root.size() && folder.compare(0, root.size() + 1, root + "/") == 0 ? folder.substr(root.size() + 1) : "";
        read();
        picked = -1;
        copy_from.clear();
    }
    bindings::Screen screen_kind() const override { return bindings::Screen::Overview; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        settle();
        unsigned pad_slot = page * projects::pads_a_page + slot_for(unsigned(std::max(0, use.index)));
        switch (job) {
        case Job::SelectProject:
            if (!copy_from.empty()) { paste(pad_slot); return true; }
            picked = int(pad_slot);
            return true;
        case Job::ProjectOptions:
            if (use.control == bindings::Control::Pad) picked = int(pad_slot);
            if (const Entry* item = entry(); item && item->kind == Entry::Kind::Folder && use.control == bindings::Control::WheelPush) {
                go_in(item->name);
                return true;
            }
            if (picked >= 0) studio.push("project-options");
            return true;
        case Job::PickProject:
            picked = std::clamp((picked < 0 ? int(page * projects::pads_a_page) : picked) + use.delta, 0, int(pages() * projects::pads_a_page) - 1);
            page = unsigned(picked) / projects::pads_a_page;
            return true;
        case Job::ProjectPage:
            page = unsigned(std::clamp(int(page) + (use.control == bindings::Control::Plus ? 1 : -1), 0, int(pages()) - 1));
            return true;
        case Job::CopyProject: {
            const Entry* item = studio.listing.at(pad_slot);
            if (!item || item->kind == Entry::Kind::Folder) return true;
            copy_from = item->path;
            picked = int(pad_slot);
            studio.say("COPY: A PAD");
            return true;
        }
        case Job::DeleteProject:
            picked = int(pad_slot);
            ask_delete(int(slot_for(unsigned(use.index))) >= 0 ? use.index : -1);
            return true;
        case Job::DeleteSelection: ask_delete(-1); return true;
        case Job::PreviewProject: {
            const Entry* item = entry();
            if (!item || item->kind == Entry::Kind::Folder || item->path == studio.project_path()) {
                if (studio.playing()) studio.stop(); else studio.play();
                return true;
            }
            Studio::ProjectJob open;
            open.kind = Studio::ProjectJob::Kind::Open;
            open.path = item->path;
            open.play = true;
            studio.ask_projects(open);
            return true;
        }
        case Job::ProjectVolume: { // the open project's tracks, all at once
            const Entry* item = entry();
            if (!item || item->path != studio.project_path()) { studio.say("OPEN IT FIRST"); return true; }
            for (unsigned t = 0; t < melodee::track_count; ++t)
                if (studio.project().tracks[t].used()) studio.set_level(t, int(studio.project().tracks[t].mix.level) + use.delta);
            return true;
        }
        case Job::SelectTrack: leave(use.index); return true; // a track button: into it, that track
        case Job::Back:
            if (!copy_from.empty()) { copy_from.clear(); return true; }
            if (!studio.listing.relative.empty() && picked < 0) { go_up(); return true; }
            leave(-1);
            return true;
        case Job::BackToPlay: studio.home(); return true;
        case Job::PlayPad: case Job::PlayNote: return true; // the pads are the projects here
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        settle();
        bool blink = std::chrono::duration_cast<std::chrono::milliseconds>(Studio::Clock::now().time_since_epoch()).count() / 300 % 2;
        for (unsigned slot = 0; slot < projects::pads_a_page; ++slot) {
            unsigned at = page * projects::pads_a_page + slot;
            const Entry* item = studio.listing.at(at);
            uint8_t shown = !item ? colour::off : item->kind == Entry::Kind::Folder ? colour::light_grey : colour_of(*item, at);
            if (int(at) == picked) shown = !item ? (blink ? colour::white : colour::dark_grey) : blink ? shown : colour::white;
            if (!copy_from.empty() && item && item->path == copy_from) shown = colour::white;
            lights.pad(pad_for(slot), shown);
        }
        for (unsigned step = 0; step < step_count; ++step) lights.step(step, colour::off);
        lights.button(Button::Back, white_dim);
        lights.button(Button::Copy, copy_from.empty() ? white_dim : white_bright);
        lights.button(Button::Delete, white_dim);
        lights.button(Button::Up, pages() > 1 ? white_dim : 0);
        lights.button(Button::Down, pages() > 1 ? white_dim : 0);
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu}) lights.button(button, 0);
    }

    void draw(juce::Graphics& g) override {
        settle();
        std::string bottom = std::to_string(page + 1) + "/" + std::to_string(pages());
        std::string where = upper(studio.listing.relative);
        if (studio.listings == 0) {
            sys::ProjectFacts empty;
            empty.kind = sys::ProjectFacts::Kind::Empty;
            sys::overview(g, 0, bottom, where, empty, "READING");
            return;
        }
        unsigned shown_pad = picked >= 0 ? unsigned(picked) % projects::pads_a_page : 0;
        const Entry* item = entry();
        sys::ProjectFacts facts;
        std::string left, right;
        if (!copy_from.empty()) {
            facts.kind = sys::ProjectFacts::Kind::Project;
            facts.name = upper(projects::name_of(copy_from));
            sys::overview(g, shown_pad, bottom, where, facts, "COPY TO: A PAD");
            return;
        }
        if (picked < 0 || !item) {
            facts.kind = sys::ProjectFacts::Kind::Empty;
            left = picked < 0 ? "A PAD PICKS" : !studio.settings.autosave && studio.dirty ? "BACK: DISCARDS" : "BACK: NEW";
        } else if (item->kind == Entry::Kind::Folder) {
            facts.kind = sys::ProjectFacts::Kind::Folder;
            facts.name = upper(item->name);
            facts.tracks = item->tracks;
            left = "PUSH: IN";
        } else {
            facts.kind = sys::ProjectFacts::Kind::Project;
            facts.name = upper(item->name);
            facts.tracks = item->tracks;
            facts.tempo = item->tempo;
            facts.mpc = item->kind == Entry::Kind::Mpc;
            facts.open = item->path == studio.project_path();
            facts.unsaved = facts.open && studio.dirty;
            facts.start = studio.settings.start == item->path;
            if (facts.open) {
                facts.tracks = studio.project().used_tracks();
                facts.tempo = studio.tempo();
                left = "PUSH: OPTIONS";
            } else {
                // AUTOSAVE off: the open project's changes are left behind.
                left = !studio.settings.autosave && studio.dirty ? "BACK: DISCARDS" : "BACK: OPEN";
                right = "PLAY";
            }
        }
        sys::overview(g, shown_pad, bottom, where, facts, left, right);
    }

    std::string status() const override {
        if (studio.top() != this) return {};
        settle();
        const Entry* item = entry();
        std::string name = item ? item->name : "-";
        std::replace(name.begin(), name.end(), ' ', '_');
        return " overview_page=" + std::to_string(page) + " overview_picked=" + std::to_string(picked) + " overview_item=" + name +
               " overview_items=" + std::to_string(studio.listing.items.size());
    }

    const Entry* entry() const { return picked >= 0 ? studio.listing.at(unsigned(picked)) : nullptr; }
    int picked_pad() const { return picked; }
    void read() { studio.ask_projects({}); seen = studio.listings; }
    void go_in(const std::string& name) {
        studio.listing_folder = studio.listing.relative.empty() ? name : studio.listing.relative + "/" + name;
        picked = -1;
        page = 0;
        read();
    }
    void go_up() {
        auto at = studio.listing.relative.rfind('/');
        studio.listing_folder = at == std::string::npos ? "" : studio.listing.relative.substr(0, at);
        picked = -1;
        page = 0;
        read();
    }

private:
    mutable unsigned page = 0;
    mutable int picked = -1;   // page * 32 + pad of the one picked; -1 none yet
    mutable unsigned seen = 0; // the listing last looked at
    std::string copy_from;     // Copy + a pad: the project to copy, until a pad takes it

    unsigned pages() const { return std::max(1u, studio.listing.pages); }
    // A new listing: the open project picked, if it is here and nothing else is.
    void settle() const {
        if (studio.listings == seen) return;
        seen = studio.listings;
        if (picked >= 0) return;
        for (const auto& item : studio.listing.items)
            if (item.path == studio.project_path()) { picked = item.pad; page = unsigned(item.pad) / projects::pads_a_page; }
    }
    uint8_t colour_of(const Entry& item, unsigned at) const {
        if (item.path == studio.project_path() && studio.extras().colour) return studio.extras().colour;
        if (item.colour) return item.colour;
        return ui::track_colour(at).bright; // none picked: the pad's place's
    }
    void ask_delete(int pad) {
        const Entry* item = entry();
        if (!item) return;
        if (item->path == studio.project_path()) { studio.say("IT IS OPEN"); return; }
        Studio::Question q;
        q.rail_top = "DEL";
        q.rail_big = std::to_string(picked % int(projects::pads_a_page) + 1);
        q.rail_bottom = item->kind == Entry::Kind::Folder ? "FLDR" : "PRJ";
        q.asked = item->kind == Entry::Kind::Folder ? "DELETE FOLDER?" : "DELETE PROJECT " + std::to_string(picked % int(projects::pads_a_page) + 1) + "?";
        q.line1 = upper(item->name);
        q.line2 = item->kind == Entry::Kind::Folder ? "ONLY WHEN EMPTY" : "ITS SAMPLES STAY";
        q.undoable = false;
        q.pad = pad;
        q.yes_key = pad >= 0 ? "PAD: YES" : "PUSH YES";
        std::string path = item->path;
        Studio* where = &studio;
        q.yes = [where, path] {
            Studio::ProjectJob remove;
            remove.kind = Studio::ProjectJob::Kind::Delete;
            remove.path = path;
            where->ask_projects(remove);
        };
        studio.ask(q);
    }
    void paste(unsigned slot) {
        const Entry* there = studio.listing.at(slot);
        std::string from = copy_from;
        copy_from.clear();
        Studio::ProjectJob copy;
        copy.kind = Studio::ProjectJob::Kind::Copy;
        copy.path = from;
        copy.pad = int(slot);
        picked = int(slot);
        if (!there) { studio.ask_projects(copy); return; }
        if (there->kind == Entry::Kind::Folder || there->path == studio.project_path() || there->path == from) { studio.say("NOT THERE"); return; }
        copy.name = there->path; // replaced: asked first, the pad again says yes (the Move's)
        Studio::Question q;
        q.rail_top = "COPY";
        q.rail_big = std::to_string(slot % projects::pads_a_page + 1);
        q.rail_bottom = "PRJ";
        q.asked = "REPLACE PROJECT " + std::to_string(slot % projects::pads_a_page + 1) + "?";
        q.line1 = upper(there->name) + ", WITH";
        q.line2 = upper(projects::name_of(from));
        q.pad = int(pad_for(slot % projects::pads_a_page));
        q.yes_key = "PAD: YES";
        Studio* where = &studio;
        q.yes = [where, copy] { where->ask_projects(copy); };
        studio.ask(q);
    }
    // Out to Play: the project picked opened (or a new one made on an empty
    // pad), then `track` of the bank (a track button), as the Move does.
    void leave(int track) {
        const Entry* item = entry();
        Studio::ProjectJob open;
        open.track = track;
        if (picked >= 0 && !item) {
            open.kind = Studio::ProjectJob::Kind::New;
            open.pad = picked;
            studio.ask_projects(open);
        } else if (item && item->kind != Entry::Kind::Folder && item->path != studio.project_path()) {
            open.kind = Studio::ProjectJob::Kind::Open;
            open.path = item->path;
            studio.ask_projects(open);
        } else if (track >= 0) {
            studio.select(unsigned(track) + ui::bank_of(studio.track()) * ui::tracks_a_bank);
        }
        studio.home();
    }
};

Overview& overview_of(Studio& studio) { return static_cast<Overview&>(*studio.screen("projects")); }

// The picked project's options (Melodee's project sheet; the Move's Set
// options): a list, the wheel walking it, a push doing the row; Back out.
class ProjectOptions final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        rows.clear();
        cursor = 0;
        editing = false;
        folders = false;
        armed = false;
        const Entry* item = overview_of(studio).entry();
        if (!item) {
            target = {};
            rows = {"NEW PROJECT", "NEW FOLDER"};
            return;
        }
        target = *item;
        bool open = item->path == studio.project_path();
        colour = open ? studio.extras().colour : item->colour;
        switch (item->kind) {
        case Entry::Kind::Folder: rows = {"OPEN", "RENAME", "DELETE"}; break;
        case Entry::Kind::Mpc: rows = {"OPEN AS A COPY", "LOAD KIT"}; break;
        case Entry::Kind::Project:
            if (open) rows = {"SAVE", "SAVE AS NEW", "RENAME", "COLOUR", "DUPLICATE", "MOVE TO FOLDER", "NEW FOLDER", "OPEN AT START"};
            // REVERT while it has changes since it was saved (melodee-sample's issue 18): its file opened again.
            if (open && studio.dirty) rows.insert(rows.begin() + 1, "REVERT");
            else rows = {"OPEN", "LOAD KIT", "RENAME", "COLOUR", "DUPLICATE", "MOVE TO FOLDER", "OPEN AT START", "DELETE"};
            break;
        }
    }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (editing) colour_index = std::clamp(colour_index + use.delta, 0, 16);
            else { cursor = std::clamp(cursor + use.delta, 0, int(shown().size()) - 1); armed = false; }
            return true;
        case Job::ListPush: push(); return true;
        case Job::Back:
            if (editing) { editing = false; return true; }
            if (folders) { folders = false; cursor = 0; return true; }
            studio.close();
            return true;
        case Job::PlayPad: case Job::PlayNote: return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        if (auto* under = studio.screen("projects")) under->lights(lights);
        lights.button(move::Button::Back, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        std::vector<sys::SetupRow> list;
        for (const auto& row : shown()) {
            sys::SetupRow drawn;
            drawn.name = row;
            if (row == "RENAME" || row == "SAVE AS NEW" || row == "MOVE TO FOLDER") drawn.door = true;
            if (row == "COLOUR") { drawn.value = editing ? colour_label(colour_index) : colour_name(colour); drawn.editing = editing; }
            if (row == "OPEN AT START") drawn.value = studio.settings.start == target.path ? "ON" : "OFF";
            if (row == "REVERT") drawn.value = armed && row == rows[size_t(cursor)] ? "PUSH AGAIN" : "TO SAVED";
            if (row == "OPEN" && discards()) drawn.value = armed && row == rows[size_t(cursor)] ? "PUSH AGAIN" : "CHANGES LOST";
            list.push_back(drawn);
        }
        sys::setup_list(g, folders ? "MOVE" : "OPT", target.kind == Entry::Kind::Folder ? "FLDR" : "PRJ", list, cursor);
    }

private:
    std::vector<std::string> rows, folder_rows;
    int cursor = 0;
    bool editing = false, folders = false;
    bool armed = false; // REVERT, or OPEN that leaves changes behind: pushed once, the next push does it
    int colour_index = 0;
    uint8_t colour = 0;
    Entry target;

    const std::vector<std::string>& shown() const { return folders ? folder_rows : rows; }
    // AUTOSAVE off with changes: opening another project leaves them behind.
    bool discards() const { return !studio.settings.autosave && studio.dirty && target.kind == Entry::Kind::Project; }
    static std::string colour_label(int index) { return index == 0 ? "NONE" : colour_names[index - 1]; }

    void ask(Studio::ProjectJob::Kind kind, std::string name = {}) {
        Studio::ProjectJob job;
        job.kind = kind;
        job.path = target.path;
        job.name = std::move(name);
        studio.ask_projects(job);
    }
    void push() {
        using Kind = Studio::ProjectJob::Kind;
        if (folders) {
            const auto& row = folder_rows[size_t(cursor)];
            auto root = studio.projects_root(), here = studio.listing.relative;
            std::string to;
            if (row == "UP") { auto at = here.rfind('/'); to = at == std::string::npos ? root : root + "/" + here.substr(0, at); }
            else to = root + "/" + (here.empty() ? "" : here + "/") + row.substr(0, row.size() - 2);
            ask(Kind::MoveTo, to);
            studio.close();
            return;
        }
        const auto& row = rows[size_t(cursor)];
        if (row == "COLOUR") {
            if (!editing) {
                editing = true;
                colour_index = 0;
                for (unsigned n = 0; n < 16; ++n)
                    if (ui::track_colours[n].bright == colour) colour_index = int(n) + 1;
                return;
            }
            Studio::ProjectJob job;
            job.kind = Kind::Colour;
            job.path = target.path;
            job.colour = colour_index ? ui::track_colours[size_t(colour_index - 1)].bright : 0;
            studio.ask_projects(job);
            colour = job.colour;
            editing = false;
            return;
        }
        if (row == "MOVE TO FOLDER") {
            folder_rows.clear();
            if (!studio.listing.relative.empty()) folder_rows.push_back("UP");
            for (const auto& item : studio.listing.items)
                if (item.kind == Entry::Kind::Folder) folder_rows.push_back(upper(item.name) + " >");
            if (folder_rows.empty()) { studio.say("NO FOLDER: MAKE ONE"); return; }
            folders = true;
            cursor = 0;
            return;
        }
        if (row == "OPEN AT START") {
            studio.settings.start = studio.settings.start == target.path ? std::string() : target.path;
            studio.settings_changed();
            return;
        }
        if ((row == "REVERT" || (row == "OPEN" && discards())) && !armed) { armed = true; return; } // asks once more
        armed = false;
        auto& overview = overview_of(studio);
        studio.close();
        if (row == "SAVE") ask(Kind::Save);
        else if (row == "REVERT") ask(Kind::Revert);
        else if (row == "OPEN" && target.kind == Entry::Kind::Folder) overview.go_in(target.name);
        else if (row == "OPEN" || row == "OPEN AS A COPY") ask(Kind::Open);
        else if (row == "LOAD KIT") { ask(Kind::LoadKit); studio.home(); }
        else if (row == "DUPLICATE") ask(Kind::Duplicate);
        else if (row == "NEW FOLDER") { Studio::ProjectJob job; job.kind = Kind::NewFolder; job.pad = overview.picked_pad(); studio.ask_projects(job); }
        else if (row == "NEW PROJECT") { Studio::ProjectJob job; job.kind = Kind::New; job.pad = overview.picked_pad(); studio.ask_projects(job); studio.home(); }
        else if (row == "DELETE") overview.job(bindings::Job::DeleteSelection, {});
        else if (row == "RENAME" || row == "SAVE AS NEW") {
            bool rename = row == "RENAME";
            Studio* where = &studio;
            std::string path = target.path;
            studio.ask_name(rename ? (target.kind == Entry::Kind::Folder ? "RENAME FOLDER" : "RENAME PROJECT") : "SAVE AS NEW",
                            rename ? target.name : target.name + " 2", 31, [where, path, rename](const std::string& name) {
                                Studio::ProjectJob job;
                                job.kind = rename ? Studio::ProjectJob::Kind::Rename : Studio::ProjectJob::Kind::SaveAs;
                                job.path = path;
                                job.name = name;
                                where->ask_projects(job);
                            });
        }
    }
};

RegisterScreen registered_overview("projects", make<Overview>, {{bindings::Job::Projects}});
RegisterScreen registered_options("project-options", make<ProjectOptions>);

} // namespace
} // namespace app::screens
