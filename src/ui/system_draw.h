// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <string>
#include <string_view>
#include <vector>

// The screens of projects, Setup, undo and naming (docs/UI.md, "Projects
// and system"), drawn from plain data: the mockups
// (mockups_projects.cpp) draw them with an example, the app's screens
// (src/app/screens/) with what is there, so a screen is its mockup.
namespace ui::system {

// "LEFT        RIGHT" in `chars` characters: the right flush right, the
// left cut to make room.
std::string two_ends(std::string_view left, std::string_view right, size_t chars = 16);

// Every other pixel of `area` black: what is dim on a one-bit screen (a step
// undone, a row that does nothing).
void dim(juce::Graphics& g, juce::Rectangle<int> area);
// A mark in the rail's top right corner: the project has changes not saved
// yet (Melodee's UNSAVED).
void unsaved_mark(juce::Graphics& g);

// ---------------------------------------------------------------- projects

// What the Set Overview shows of the pad in hand.
struct ProjectFacts {
    enum class Kind : uint8_t { Project, Folder, Empty, Up } kind = Kind::Project;
    std::string name;     // as the file is named, or the folder
    unsigned tracks = 0;  // a project's tracks in use; a folder's projects
    unsigned tempo = 0;   // tenths of a BPM
    bool open = false;    // the project playing now
    bool unsaved = false; // it has changes not saved yet (the open one)
    bool mpc = false;     // an MPC project, opened as a copy
    bool start = false;   // opened at start
};
// Rail: PROJ / the pad (1-32) / the page or the folder's depth.
// `where`: the folder, empty at the top. `detail`: what the wheel or Back does.
void overview(juce::Graphics& g, unsigned pad, std::string_view bottom, std::string_view where, const ProjectFacts& facts,
              std::string_view detail_left, std::string_view detail_right = {});

// The question before a loss (Melodee's one sheet): what is asked, one or two
// lines of what goes, whether undo brings it back, and the keys.
struct Question {
    std::string rail_top, rail_big, rail_bottom;
    std::string asked;                 // "DELETE NIGHT BUS?"
    std::array<std::string, 2> lines;  // what goes
    bool undoable = false;             // "UNDO BRINGS IT BACK", else "NO UNDO"
    std::string yes = "PUSH YES", no = "BACK NO";
};
void question(juce::Graphics& g, const Question& q);

// Loading a project: its name, how far, the file being read.
void loading(juce::Graphics& g, std::string_view name, unsigned samples_done, unsigned samples, unsigned synths_done,
             unsigned synths, std::string_view reading);

// ---------------------------------------------------------------- naming

// The name sheet, typed the Move's way: the wheel walks a strip of
// characters and its push types the one in the box; the pads type letters
// at once. `strip` is the characters around the one picked (`picked` its
// index there); a cell of more than one character is a key (SPC, DEL, OK).
void name_sheet(juce::Graphics& g, std::string_view title, std::string_view text, size_t room, const std::vector<std::string>& strip,
                size_t picked, bool upper);

// ---------------------------------------------------------------- undo

struct UndoRow {
    std::string what;   // "TUNE A05"
    int track = -1;     // 0-15, or -1 for the project's
    std::string age;    // "NOW", "2M", "1H"
    bool undone = false;
    bool start = false; // the state before every step
};
// The undo list: rows top down (the steps undone, then the steps done, then
// START), the cursor's row inverted, the state in hand marked, the rows
// above the cursor struck through (undone once the cursor's state is reached).
void undo_list(juce::Graphics& g, const std::vector<UndoRow>& rows, int cursor, int now);

// ---------------------------------------------------------------- Setup

struct SetupRow {
    std::string name, value; // value empty: a door (" >") or an action
    bool door = false;       // opens a list
    bool editing = false;    // the wheel turns the value: drawn [VALUE]
};
void setup_list(juce::Graphics& g, std::string_view rail_top, std::string_view rail_bottom, const std::vector<SetupRow>& rows, int selected);

struct About {
    float cpu_percent = 0;     // the audio cycle's mean share
    unsigned peak_us = 0;      // its longest
    unsigned late = 0;         // late exchanges since start
    double free_gb = 0;        // room under /data/UserData
    unsigned synths = 0;       // Schwung modules loaded
    unsigned slow_calls = 0;   // calls into them over 1 ms
    std::string version;
};
void about(juce::Graphics& g, const About& about);

} // namespace ui::system
