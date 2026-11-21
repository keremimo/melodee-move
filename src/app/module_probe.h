// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>

// Loads each of Schwung's sound generators under `root` as Melodee's synth
// track would, prints what it found (parameters, pages, presets), plays a
// chord through it and times its renders. Offline: the Move's device is
// not touched, so it runs beside Ableton's app. 1 when one would not load
// or stayed silent.
namespace app {

// With `pictures`, each module's own pages (canvas scripts) are started too
// and drawn there as PNGs, four times size: first, two seconds later (a page
// that fetches in the background), and entered with a turn of the jogwheel.
int probe_modules(const std::string& root, const std::string& pictures = {});

// Module `id` under `root` drawn by Schwung's own knob-page code
// (schwung_pages.h), driven with the Move's controls as raw MIDI: each
// step's screen a PNG in `pictures`, and how long a screen took.
int probe_schwung_pages(const std::string& root, const std::string& id, const std::string& pictures);

} // namespace app
