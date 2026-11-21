// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <string>

// Melodee's real screens as PNGs (four times size), offline: a made-up drum
// loop loaded onto A01, then Play, a touched knob, Load, Chop and To Pads,
// each reached as the controls would reach it. With `modules` (a Schwung
// modules folder), the synth track too, with the first module there.
namespace app {

int write_previews(const std::string& directory, const std::string& modules = {});
// The sequencer's screens (seq_previews.cpp), into `folder`.
bool write_sequencer_previews(const juce::File& folder);

} // namespace app
