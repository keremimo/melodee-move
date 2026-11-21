// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/system_draw.h"
#include "move/font.h"
#include "ui/widgets.h"
#include <algorithm>
#include <cstdio>

namespace ui::system {
namespace {

const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

std::string tempo_text(unsigned tenths) {
    char text[16];
    if (tenths % 10) std::snprintf(text, sizeof text, "%u.%u BPM", tenths / 10, tenths % 10);
    else std::snprintf(text, sizeof text, "%u BPM", tenths / 10);
    return text;
}

// A name as large as it fits: two lines of scale 1 at most.
void name_large(juce::Graphics& g, int y, std::string_view name) {
    if (move::text_width(name, 2) <= content_width - 2) { move::draw_text(g, content_x + 1, y, name, 2); return; }
    size_t fits = size_t(content_width - 2) / size_t(move::glyph_advance);
    move::draw_text(g, content_x + 1, y, name.substr(0, fits));
    if (name.size() > fits) move::draw_text(g, content_x + 1, y + 9, name.substr(fits, fits));
}

} // namespace

std::string two_ends(std::string_view left, std::string_view right, size_t chars) {
    size_t room = right.empty() ? chars : chars > right.size() + 1 ? chars - right.size() - 1 : 0;
    std::string text(left.substr(0, room));
    if (right.empty()) return text;
    text.resize(chars - std::min(chars, right.size()), ' ');
    return text + std::string(right.substr(0, chars));
}

void dim(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(off);
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX() + ((y ^ area.getX()) & 1); x < area.getRight(); x += 2) g.fillRect(x, y, 1, 1);
    g.setColour(on);
}

void unsaved_mark(juce::Graphics& g) {
    g.setColour(off);
    g.fillRect(rail_width - 4, 1, 3, 3);
    g.setColour(on);
}

// ---------------------------------------------------------------- projects

void overview(juce::Graphics& g, unsigned pad, std::string_view bottom, std::string_view where, const ProjectFacts& facts,
              std::string_view detail_left, std::string_view detail_right) {
    rail(g, "PROJ", std::to_string(pad + 1), bottom);
    if (facts.open && facts.unsaved) unsaved_mark(g);
    g.setColour(on);
    if (!where.empty()) move::draw_text(g, content_x + 1, 0, std::string(where.substr(0, 16)));
    g.fillRect(content_x, 8, content_width, 1);
    using Kind = ProjectFacts::Kind;
    switch (facts.kind) {
    case Kind::Empty:
        move::draw_text(g, content_x + 1, 14, "EMPTY", 2);
        move::draw_text(g, content_x + 1, 36, "A NEW PROJECT");
        break;
    case Kind::Up:
        move::draw_text(g, content_x + 1, 14, "UP", 2);
        move::draw_text(g, content_x + 1, 36, "THE FOLDER ABOVE");
        break;
    case Kind::Folder:
        name_large(g, 14, facts.name + " >");
        move::draw_text(g, content_x + 1, 36, std::to_string(facts.tracks) + (facts.tracks == 1 ? " PROJECT" : " PROJECTS"));
        break;
    case Kind::Project: {
        name_large(g, 14, facts.name);
        std::string line = facts.mpc ? "MPC  " : std::to_string(facts.tracks) + " TRK  ";
        move::draw_text(g, content_x + 1, 36, line + tempo_text(facts.tempo));
        std::string chips = facts.open ? (facts.unsaved ? "OPEN  UNSAVED" : "OPEN") : facts.mpc ? "OPENS AS A COPY" : "";
        if (facts.start) chips += chips.empty() ? "AT START" : "  START";
        if (!chips.empty()) move::draw_text(g, content_x + 1, 45, chips.substr(0, 17));
        break;
    }
    }
    detail(g, detail_left, detail_right);
}

void question(juce::Graphics& g, const Question& q) {
    rail(g, q.rail_top, q.rail_big, q.rail_bottom);
    g.setColour(on);
    move::draw_text(g, content_x + 1, 1, q.asked.substr(0, 17));
    g.fillRect(content_x, 10, content_width, 1);
    move::draw_text(g, content_x + 1, 14, q.lines[0].substr(0, 17));
    move::draw_text(g, content_x + 1, 23, q.lines[1].substr(0, 17));
    // Whether undo brings it back: a chip.
    std::string chip = q.undoable ? "UNDO BRINGS BACK" : "NO UNDO";
    int w = move::text_width(chip) + 4;
    if (q.undoable) g.drawRect(content_x + 1, 35, w, 11);
    else g.fillRect(content_x + 1, 35, w, 11);
    g.setColour(q.undoable ? on : off);
    move::draw_text(g, content_x + 3, 37, chip);
    g.setColour(on);
    detail(g, q.yes, q.no);
}

void loading(juce::Graphics& g, std::string_view name, unsigned samples_done, unsigned samples, unsigned synths_done, unsigned synths,
             std::string_view reading) {
    rail(g, "LOAD", "", "PRJ");
    g.setColour(on);
    name_large(g, 1, name);
    unsigned total = samples + synths, done = samples_done + synths_done;
    g.drawRect(content_x + 1, 21, content_width - 3, 5);
    g.fillRect(content_x + 2, 22, total ? int(float(content_width - 5) * float(done) / float(total)) : content_width - 5, 3);
    move::draw_text(g, content_x + 1, 30, two_ends("SAMPLES", std::to_string(samples_done) + "/" + std::to_string(samples)));
    move::draw_text(g, content_x + 1, 40, two_ends("SYNTHS", std::to_string(synths_done) + "/" + std::to_string(synths)));
    detail(g, std::string(reading.substr(0, 16)));
}

// ---------------------------------------------------------------- naming

void name_sheet(juce::Graphics& g, std::string_view title, std::string_view text, size_t room, const std::vector<std::string>& strip,
                size_t picked, bool upper) {
    rail(g, "NAME", upper ? "AB" : "ab", std::to_string(text.size()) + "/" + std::to_string(room));
    g.setColour(on);
    move::draw_text(g, content_x + 1, 0, std::string(title.substr(0, 17)));
    // The name with its cursor, the end of it in view.
    std::string line = std::string(text) + "_";
    if (line.size() > 17) line = line.substr(line.size() - 17);
    move::draw_text(g, content_x + 1, 11, line);
    // How much of its room it takes.
    g.fillRect(content_x + 1, 20, std::max(1, int(float(content_width - 3) * float(text.size()) / float(std::max<size_t>(room, 1)))), 1);
    // The strip: the picked cell in a box, inverted.
    constexpr int cell = 14, top = 26, tall = 20;
    int count = int(strip.size()), left = content_x + (content_width - count * cell) / 2;
    for (int n = 0; n < count; ++n) {
        int x = left + n * cell;
        bool chosen = size_t(n) == picked;
        g.setColour(on);
        if (chosen) g.fillRect(x, top, cell - 1, tall);
        g.setColour(chosen ? off : on);
        const auto& key = strip[size_t(n)];
        if (key.size() == 1) move::draw_text_centred(g, x, top + 3, cell - 1, key, 2);
        else move::draw_text_centred(g, x, top + 7, cell - 1, key.substr(0, 2));
    }
    g.setColour(on);
    detail(g, "PUSH TYPE", "BACK <");
}

// ---------------------------------------------------------------- undo

void undo_list(juce::Graphics& g, const std::vector<UndoRow>& rows, int cursor, int now) {
    int steps = cursor - now; // > 0: undo that many; < 0: redo
    rail(g, "UNDO", steps ? std::to_string(std::abs(steps)) : "", std::to_string(std::max<int>(0, int(rows.size()) - 1)) + " ST");
    constexpr int row_height = 9, visible = 5;
    int first = std::clamp(cursor - visible / 2, 0, std::max(0, int(rows.size()) - visible));
    for (int n = 0; n < visible && first + n < int(rows.size()); ++n) {
        const auto& row = rows[size_t(first + n)];
        int at = first + n, y = n * row_height;
        std::string left = row.start ? "START" : (row.track >= 0 ? std::to_string(row.track + 1) + " " : "  ") + row.what;
        std::string text = two_ends(left, row.start ? "" : row.age, 15);
        bool chosen = at == cursor;
        g.setColour(on);
        if (chosen) g.fillRect(content_x, y, content_width - 3, row_height);
        g.setColour(chosen ? off : on);
        move::draw_text(g, content_x + 2, y + 1, text);
        if (at == now) { // the state in hand: a notch at the right
            g.fillRect(content_x + content_width - 6, y + 2, 2, 5);
            g.fillRect(content_x + content_width - 7, y + 3, 1, 3);
        }
        g.setColour(on);
        if (at < cursor) g.fillRect(content_x + 2, y + 4, move::text_width(text), 1); // struck: undone when the cursor's state is reached
    }
    if (int(rows.size()) > visible) {
        int track = visible * row_height, thumb = std::max(3, track * visible / int(rows.size()));
        g.fillRect(width - 1, (track - thumb) * first / std::max(1, int(rows.size()) - visible), 1, thumb);
    }
    if (steps > 0) detail(g, "PUSH: UNDO " + std::to_string(steps));
    else if (steps < 0) detail(g, "PUSH: REDO " + std::to_string(-steps));
    else detail(g, "NOW", "BACK");
}

// ---------------------------------------------------------------- Setup

void setup_list(juce::Graphics& g, std::string_view rail_top, std::string_view rail_bottom, const std::vector<SetupRow>& rows, int selected) {
    rail(g, rail_top, "", rail_bottom);
    std::vector<std::string> texts;
    for (const auto& row : rows) {
        if (row.door) texts.push_back(row.name + " >");
        else if (row.editing) texts.push_back(two_ends(row.name, "[" + row.value + "]", 16));
        else texts.push_back(two_ends(row.name, row.value, 16));
    }
    list(g, content(0, height), texts, selected, 1);
}

void about(juce::Graphics& g, const About& a) {
    rail(g, "ABT", "", "SETP");
    g.setColour(on);
    char text[32];
    std::snprintf(text, sizeof text, "%.0f%% %uUS", double(a.cpu_percent), a.peak_us);
    move::draw_text(g, content_x + 1, 1, two_ends("CPU", text));
    move::draw_text(g, content_x + 1, 11, two_ends("LATE BLOCKS", std::to_string(a.late)));
    std::snprintf(text, sizeof text, "%.1f GB", a.free_gb);
    move::draw_text(g, content_x + 1, 21, two_ends("FREE", text));
    move::draw_text(g, content_x + 1, 31, two_ends("SYNTHS " + std::to_string(a.synths), std::to_string(a.slow_calls) + " SLOW"));
    move::draw_text(g, content_x + 1, 41, two_ends("VERSION", a.version));
    detail(g, "BACK");
}

} // namespace ui::system
