# Third-party parts

| Part | Where | Licence |
| --- | --- | --- |
| [JUCE](https://github.com/juce-framework/JUCE) 9.0.5 (`juce_core`, `juce_events`, `juce_graphics`) | fetched to `build/deps/JUCE` by `tools/dependencies.py`, linked into the binary | AGPLv3 (JUCE's open-source licence) |
| JACK client library (`libjack`) | linked dynamically; on the Move, poundhard's own copy | LGPL-2.1-or-later |
| FreeType, Fontconfig | linked dynamically, through JUCE | FreeType Licence; MIT-style |
| The 5 x 7 font table | `src/move/font.cpp`, taken from PoundHard's `armbian/launcher/movedisp.py` (the classic 5 x 7 glyph set) | MIT (PoundHard contributors) |
| The Move's MIDI numbers and LED palette | `src/move/controls.h`, `src/move/lights.cpp`, as listed in Schwung's `shared/constants.mjs` after Cycling '74's [rnbo.move.control](https://github.com/Cycling74/rnbo.move.control) | MIT; facts about the hardware |

Not part of this repository and never shipped: `jack_move.so` (the Move's
JACK driver, from the owner's own RNBO install), Ableton's `/opt/move`, and
poundhard-armbian itself, which each Move owner builds from
[nzimas/poundhard-armbian](https://github.com/nzimas/poundhard-armbian).
