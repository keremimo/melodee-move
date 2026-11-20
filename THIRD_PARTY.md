# Third-party parts

| Part | Where | Licence |
| --- | --- | --- |
| [JUCE](https://github.com/juce-framework/JUCE) 9.0.5 (`juce_core`, `juce_events`, `juce_graphics`) | fetched to `build/deps/JUCE` by `tools/dependencies.py`, linked into the binary | AGPLv3 (JUCE's open-source licence) |
| GCC's libstdc++ and libgcc | linked into the binary statically | GPL-3.0 with the GCC Runtime Library Exception |
| FreeType, Fontconfig, libpng, Brotli, libuuid, Expat, zlib | linked dynamically, through JUCE; copied from Ubuntu 22.04 into `dist/*/lib` | FreeType Licence; MIT-style; libpng; MIT; BSD-3-Clause; MIT; zlib |
| JACK client library (`libjack`) | loaded at run time by the JACK host; on the Move, poundhard's own copy | LGPL-2.1-or-later |
| The 5 x 7 font table | `src/move/font.cpp`, taken from PoundHard's `armbian/launcher/movedisp.py` (the classic 5 x 7 glyph set) | MIT (PoundHard contributors) |
| The Move's MIDI numbers and LED palette | `src/move/controls.h`, `src/move/lights.cpp`, as listed in Schwung's `shared/constants.mjs` after Cycling '74's [rnbo.move.control](https://github.com/Cycling74/rnbo.move.control) | MIT; facts about the hardware |
| The Move's device protocol: the page layout, ioctl numbers, the screen's pull handshake, USB-MIDI cables | `src/move/spi_protocol.*`, written from [Schwung](https://github.com/charlesvestal/schwung)'s `docs/SPI_PROTOCOL.md` and [schwung-spi](https://github.com/charlesvestal/schwung-spi) (both MIT, Copyright (c) 2025-2026 Charles Vestal), which read it from Ableton's GPL-2.0 `ablspi` driver source | facts about the hardware; MIT references |
| How a standalone program owns the Move: the cycle's order, the MIDI reset that ends the power-on light show, fading in, realtime refused to `ableton` | `src/move/spi_host.cpp`, written after reading Schwung's `launch-standalone.sh` and `boot-select.c` (MIT) and [EvilGenius for Move](https://github.com/Quixotic7/evilgeniusmove)'s `src/plat_move.c` (MPL-2.0); no code copied | references only |

Not part of this repository and never shipped: `jack_move.so` (the Move's
JACK driver), Ableton's `/opt/move`, Schwung, and poundhard-armbian, which
each Move owner installs or builds.
