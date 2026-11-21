// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Melodee's projects as the Move's Set Overview shows Sets (Shift + Step 1):
// the files of a folder under Projects/, each on a pad of its own, 32 a
// page. A project keeps its pad, as a Set keeps its slot: the pads are kept
// in the folder's `.move-pads` (a line a pad: "pad<TAB>file"); a project
// put there from elsewhere (the web page, the MPC) takes the first free pad.
// Folders are on pads too. MPC projects (`.xpj`) are listed beside Melodee's
// and open as a copy.
//
// Files and folders, on the service thread: never the MIDI or audio thread.
// Each write whole or not at all (write_whole()).
namespace app::projects {

constexpr unsigned pads_a_page = 32;
constexpr const char* extension = ".mldp";

struct Entry {
    enum class Kind : uint8_t { Folder, Project, Mpc } kind = Kind::Project;
    std::string name;      // the file's name without its extension, or the folder's
    std::string file;      // the file or folder's name in its folder
    std::string path;      // the whole path
    unsigned tracks = 0;   // a project's tracks in use; a folder's projects
    unsigned tempo = 0;    // tenths of a BPM
    uint8_t colour = 0;    // the project's own (MoveExtras::colour), 0 none
    int pad = -1;          // where it lies: page * 32 + pad
};

struct Folder {
    std::string root, relative; // Projects/, and the folder under it ("" the top)
    std::vector<Entry> items;   // by pad
    unsigned pages = 1;
    const Entry* at(unsigned pad) const {
        for (const auto& item : items)
            if (item.pad == int(pad)) return &item;
        return nullptr;
    }
    std::string path() const { return relative.empty() ? root : root + "/" + relative; }
};

// A folder read: its projects' facts (each file read) and their pads, the
// pads file written again when a new one took a free pad.
Folder read_folder(const std::string& root, const std::string& relative);
// A project's facts from its file: false when it is not one.
bool read_facts(const std::string& path, Entry& entry);
// A file's pad, kept: `pad` -1 lets it go (deleted, moved away).
void keep_pad(const std::string& folder, const std::string& file, int pad);

// A name free in `folder` from `base`: "Project 1", "Night Bus 2".
std::string free_name(const std::string& folder, const std::string& base);
// The first free pad of `folder`, from `from`.
int free_pad(const std::string& folder, int from = 0);
// File jobs; each says what went wrong, empty on success. `made` is the new path.
std::string duplicate(const std::string& path, std::string& made);
std::string rename(const std::string& path, const std::string& name, std::string& made);
std::string remove(const std::string& path);
std::string move_to(const std::string& path, const std::string& folder, std::string& made);
std::string new_folder(const std::string& parent, std::string& made);
// A project path in `folder` for `name` (its extension added).
std::string path_for(const std::string& folder, const std::string& name);
std::string name_of(const std::string& path); // its file name without the extension

} // namespace app::projects
