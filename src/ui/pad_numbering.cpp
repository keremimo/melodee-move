// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/pad_numbering.h"
#include "move/controls.h"
#include "move/font.h"
#include "move/lights.h"
#include "ui/widgets.h"
#include <string>

namespace ui {

void pad_numbering(juce::Graphics& g, app::PadLayout layout) {
    using app::PadLayout;
    const juce::Colour on = juce::Colours::white, off = juce::Colours::black;
    const int cell = 12, gap = layout == PadLayout::Banks ? 6 : 0;
    const int left = content_x + (content_width - 8 * cell - gap) / 2;
    for (unsigned pad = 0; pad < move::pad_count; ++pad) {
        int column = int(move::pad_column(pad)), row = int(move::pad_row(pad));
        int x = left + column * cell + (column >= 4 ? gap : 0), y = 2 + (3 - row) * cell;
        bool starts = layout == PadLayout::Banks ? column % 4 == 0 : column == 0;
        g.setColour(on);
        if (!starts) { g.drawRect(x + 3, y + 3, cell - 6, cell - 6); continue; }
        g.fillRect(x - 1, y, cell + 1, cell - 1);
        g.setColour(off);
        auto text = std::to_string(app::logical_pad(uint8_t(pad), layout) + 1);
        move::draw_text(g, x + (cell - move::text_width(text)) / 2, y + 2, text);
    }
    g.setColour(on);
}

uint8_t numbering_colour(unsigned number, app::PadLayout layout) {
    using app::PadLayout;
    if (number == 0 || (layout == PadLayout::Banks && number == 16)) return move::colour::white;
    if (layout == PadLayout::Banks) return number < 16 ? move::colour::azure : move::colour::violet;
    return (number / 8) % 2 ? move::colour::violet : move::colour::azure;
}

} // namespace ui
