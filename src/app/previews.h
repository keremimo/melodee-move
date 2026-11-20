// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>

// Melodee's real screens as PNGs (four times size), offline: a made-up drum
// loop loaded onto A01, then Play, a touched knob, Load, Chop and To Pads,
// each reached as the controls would reach it. With `modules` (a Schwung
// modules folder), the synth track too, with the first module there.
namespace app {

int write_previews(const std::string& directory, const std::string& modules = {});

} // namespace app
