// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/lights.h"
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

// The Rail design's screens (docs/UI.md): drawn with the UI kit at the
// screen's real 128 x 64 one bit, as PNGs four times size, and a sheet of
// each set, the design's reference: rail.png (the first scope), tracks.png
// (tracks, transport and tempo). A set's screens are in a file of its own
// (mockups.cpp, mockups_tracks.cpp), so screens for new areas go in new
// files without touching the others.
namespace ui {

int write_mockups(const std::string& directory);
// The screens and lights docs/MOVE_CONTROLS.md moves or adds: controls.png
// and controls/ (mockups_controls.cpp).
bool write_controls_mockups(const std::string& directory);
// The pad layouts, Move rows and two MPC banks: pads.png and pads/
// (mockups_pads.cpp).
bool write_pads_mockups(const std::string& directory);
// Kit depth: 128 pads, the pad pages, 16 Pitches, Sample Options: kit.png
// and kit/ (mockups_kit.cpp).
bool write_kit_mockups(const std::string& directory);
// The mixer and effects: mixer.png and mixer/ (mockups_mixer.cpp).
bool write_mixer_mockups(const std::string& directory);
// MIDI and sync (docs/UI.md, "MIDI and sync"): midi.png and midi/
// (mockups_midi.cpp).
bool write_midi_mockups(const std::string& directory);

namespace mockup {

using Draw = std::function<void(juce::Graphics&)>;
struct Screen { const char* name; const char* caption; Draw draw; };

// The Move's lights, in colour on the sheet: the four track buttons, the 32
// pads, the 16 steps and Play, each a palette index (move/lights.h; 0 off).
struct Surface {
    std::array<uint8_t, 4> tracks {};
    std::array<uint8_t, move::pad_count> pads {}; // bottom left first, as move::pad_row()
    std::array<uint8_t, move::step_count> steps {};
    uint8_t play = 0;
    const char* note = ""; // what a hand is doing, under the drawing
};
struct Panel { const char* name; const char* caption; Surface surface; };

// A sheet: a title, a line under it, the screens two to a row, then the
// light panels two to a row.
juce::Image sheet(const char* title, const char* subtitle, const std::vector<Screen>& screens, const std::vector<Panel>& panels = {});
juce::Image screen_image(const Draw& draw, int scale);
juce::Image surface_image(const Surface& surface, int width, int height);
bool write_png(const juce::Image& image, const juce::File& file);

std::vector<Screen> rail_screens();
std::vector<Screen> tracks_screens();
std::vector<Panel> tracks_panels();
// The sequencer's screens and lights (mockups_sequencer.cpp): sequencer.png
// and sequencer/.
std::vector<Screen> sequencer_screens();
bool write_sequencer_mockups(const std::string& directory);

} // namespace mockup
} // namespace ui
