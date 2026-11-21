// SPDX-License-Identifier: AGPL-3.0-only
// A Schwung module's pages, entered (docs/UI.md, "A module's pages"; Kerem,
// 2026-11-21): Schwung's own page controller decides them (schwung_pages.h)
// and this screen draws them in Melodee's look — the rail, a strip with the
// page's name (a held knob's name and value), Melodee's knob cells, lists and
// preset view, and Schwung's footer verbs on the detail line.
//
// The controls are Schwung's (its param pages, shadow_ui_param_pages.mjs):
// the wheel pages (inside an entered door: its cursor), Shift + the wheel
// jumps a section, the push is Schwung's click (a held knob: that parameter;
// none: the Sections picker; Shift: the Sections picker from anywhere), the
// knobs and their touches set the module, and Back goes up one layer at a
// time, out to the Device View at the top. The pads still play.
#include "app/screens/note_pages.h"

namespace app::screens {
namespace {

class ModulePages final : public Screen {
public:
    using Screen::Screen;

    // Entered from the Device View (sheets over the pages are pushed, so a
    // return from one is no entry): Schwung's controller starts over.
    void enter() override {
        held.fill(false);
        shifted = false;
        if (SchwungPages* pages = studio.pages_of(studio.pages_target)) pages->enter();
    }

    bindings::Screen screen_kind() const override { return bindings::Screen::Canvas; }

    void input(const move::Input& input) override {
        using Kind = move::Input::Kind;
        if (input.kind == Kind::Pad && input.index < pad_count) held[input.index] = input.pressed;
        SchwungPages* pages = studio.pages_of(studio.pages_target);
        if (!pages || studio.top() != this) return;
        if (input.kind == Kind::Touch && input.index < 8) pages->midi(input.pressed ? 0x90 : 0x80, input.index, input.pressed ? 127 : 0);
        if (input.kind == Kind::Button && input.index == uint8_t(move::Button::Shift)) shift_to(*pages, input.pressed);
        if (input.kind == Kind::Pad && input.pressed && input.value > 0 && pages->view().live_press) // a finger on a pad, for a module that asks
            pages->midi(0x90, move::pad_note(input.index), input.value);
    }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        SchwungPages* pages = studio.pages_of(studio.pages_target);
        if (!pages) {
            if (job == Job::CanvasBack || job == Job::Back) { studio.close(); return true; }
            return job == Job::CanvasTurn || job == Job::CanvasPush;
        }
        auto cc = [&](uint8_t number, uint8_t value) { pages->midi(0xb0, number, value); };
        // Shift as the bindings see it (held, or locked by a double press):
        // what Schwung reads for its section jump, Shift + click, fine knobs.
        if (job == Job::TurnKnob || job == Job::TurnKnobFine || job == Job::CanvasTurn || job == Job::CanvasPush)
            shift_to(*pages, (use.held & bindings::held::shift) != 0);
        auto detents = [&](uint8_t number, int delta) { // one message a detent, as the Move sends them
            for (int n = 0; n < std::min(std::abs(delta), 16); ++n) cc(number, delta > 0 ? 1 : 127);
        };
        switch (job) {
        case Job::TurnKnob: case Job::TurnKnobFine:
            if (use.index >= 0 && use.index < 8 && use.delta) {
                detents(uint8_t(move::knob_first_cc + use.index), use.delta);
                studio.changed(); // the values are saved with the project
            }
            return true;
        case Job::CanvasTurn: detents(move::main_knob_cc, use.delta); return true;
        case Job::CanvasPush:
            cc(move::button_cc(move::Button::MainPush), 127);
            cc(move::button_cc(move::Button::MainPush), 0);
            leave_if_asked();
            return true;
        case Job::CanvasBack:
            cc(move::button_cc(move::Button::Back), 127);
            cc(move::button_cc(move::Button::Back), 0);
            if (pages->left()) studio.close(); // its top: out to the Device View
            return true;
        default: return false; // the pads, the octave: the track's Play screen under this one
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        key_pads(studio, lights, held);
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim); // always a way up
    }

    Knob knob(unsigned) const override { return {}; } // a touched knob shows in the strip, as Schwung's header shows it

    std::string status() const override {
        SchwungPages* pages = const_cast<Studio&>(studio).pages_of(studio.pages_target);
        if (!pages) return " pages=-";
        const auto& v = pages->view();
        std::string what = v.enum_picker.shown ? "enum" : v.picker.shown ? "sections" : v.peek.shown ? "peek" : v.kind;
        return " pages=" + v.page + "," + std::to_string(v.index + 1) + "/" + std::to_string(v.count) + "," + what +
               (v.wheel ? ",wheel" : "") + (v.canvas.shown ? ",canvas" : "");
    }

    void draw(juce::Graphics& g) override {
        leave_if_asked();
        SchwungPages* pages = studio.pages_of(studio.pages_target);
        std::string abbrev = "SYN";
        if (const LoadedModule* module = target_module()) abbrev = upper(module->info().abbrev).substr(0, 4);
        if (!pages) {
            ui::rail(g, abbrev, track_label(), "");
            move::draw_text_centred(g, ui::content_x, 24, ui::content_width, "NO MODULE");
            ui::detail(g, "BACK: OUT");
            return;
        }
        const auto band = juce::Rectangle<int>(ui::content_x, ui::header_height + 1, ui::content_width, ui::detail_y - ui::header_height - 2);
        pages->frame(g, band); // Schwung's ticks; a drawn page's pixels in the band, the rest black
        const auto& v = pages->view();
        ui::rail(g, abbrev, track_label(), v.count ? std::to_string(v.index + 1) + "/" + std::to_string(v.count) : "");
        if (v.unsupported) { message(g, "UPDATE SCHWUNG", "ITS PAGES NEED 1.9.4"); return; }
        if (!v.ready) { message(g, "LOADING", ""); return; }

        // The strip: what is open, or the page; a held knob, its name and value.
        std::string left = v.page, right;
        if (v.enum_picker.shown) left = v.enum_picker.title;
        else if (v.picker.shown) left = "SECTIONS";
        else if (v.peek.shown) left = v.peek.title;
        else if (v.header.inverted) { left = v.header.left; right = v.header.right; }
        strip(g, upper(left), upper(right));

        const auto body = juce::Rectangle<int>(ui::content_x, ui::header_height + 2, ui::content_width, ui::subject_height - ui::header_height - 2);
        if (v.enum_picker.shown || v.picker.shown || v.peek.shown || !v.hint.empty()) { // over the page: on black, never over its drawing
            g.setColour(juce::Colours::black);
            g.fillRect(ui::content_x, ui::header_height, ui::content_width, ui::detail_y - ui::header_height - 1);
            g.setColour(juce::Colours::white);
        }
        if (v.enum_picker.shown) list(g, body, v.enum_picker, v.enum_picker.cursor);
        else if (v.picker.shown) list(g, body, v.picker, v.picker.cursor);
        else if (v.peek.shown) list(g, body, v.peek, v.peek.cursor);
        else if (v.canvas.shown) {} // the module drew it
        else if (v.kind == "preset") preset(g, body, v.preset);
        else if (v.kind == "menu") list(g, body, v.menu, v.menu.entered ? v.menu.cursor : -1);
        else if (v.kind == "items") items(g, body, v.items);
        else if (v.kind == "knobs") cells(g, body, v.cells);
        if (!v.hint.empty()) hint(g, body, v);
        if (v.notice.empty()) ui::hints(g, v.footer);
        else ui::detail(g, upper(v.notice).substr(0, 17));
    }

private:
    std::array<bool, pad_count> held {};
    bool shifted = false; // what Schwung's pages last heard of Shift

    void shift_to(SchwungPages& pages, bool down) {
        if (down == shifted) return;
        shifted = down;
        pages.midi(0xb0, move::button_cc(move::Button::Shift), down ? 127 : 0);
    }

    const LoadedModule* target_module() const {
        const auto& target = studio.pages_target;
        if (target.fx_entry != melodee::no_plugin) {
            FxModule* fx = const_cast<Studio&>(studio).fx_module(target.fx_entry);
            return fx ? fx->module.get() : nullptr;
        }
        ModuleTrack* track = const_cast<Studio&>(studio).module_track(target.track);
        return track ? track->module.get() : nullptr;
    }
    std::string track_label() const { return std::to_string(studio.pages_target.track + 1); }

    // Remove Module, asked from the pages: out first, then gone.
    void leave_if_asked() {
        if (!studio.remove_from_pages || !(*studio.remove_from_pages == studio.pages_target)) return;
        auto target = *studio.remove_from_pages;
        studio.remove_from_pages.reset();
        studio.close();
        studio.remove_module(target);
    }

    void message(juce::Graphics& g, std::string_view big, std::string_view small) {
        move::draw_text_centred(g, ui::content_x, 22, ui::content_width, big);
        ui::detail(g, small.empty() ? "BACK: OUT" : small);
    }

    static void strip(juce::Graphics& g, const std::string& left, const std::string& right) {
        g.setColour(juce::Colours::white);
        g.fillRect(ui::content_x, 0, ui::content_width, ui::header_height);
        g.setColour(juce::Colours::black);
        size_t room = size_t(ui::content_width / move::glyph_advance);
        std::string r = right.substr(0, room > 6 ? room - 6 : 0);
        move::draw_text(g, ui::content_x + 2, 1, left.substr(0, room - (r.empty() ? 0 : r.size() + 1)));
        if (!r.empty()) move::draw_text(g, ui::width - 1 - move::text_width(r), 1, r);
        g.setColour(juce::Colours::white);
    }

    static std::string fit(std::string label, std::string value, size_t width) {
        value = value.substr(0, width > 2 ? width - 2 : 0);
        label = label.substr(0, width - value.size() - (value.empty() ? 0 : 1));
        return label + std::string(width - label.size() - value.size(), ' ') + value;
    }

    static void list(juce::Graphics& g, juce::Rectangle<int> area, const ModuleView::List& from, int selected) {
        const size_t width = size_t((area.getWidth() - 6) / move::glyph_advance);
        std::vector<std::string> rows;
        for (const auto& row : from.rows) rows.push_back(fit(upper(row.label), upper(row.value), width));
        if (rows.empty()) rows.push_back("(NONE)");
        ui::list(g, area, rows, selected, 1);
    }

    static void items(juce::Graphics& g, juce::Rectangle<int> area, const ModuleView::List& from) {
        ModuleView::List marked = from;
        for (size_t n = 0; n < marked.rows.size(); ++n) marked.rows[n].label = (int(n) == from.current ? "* " : "  ") + marked.rows[n].label;
        list(g, area, marked, from.entered ? from.cursor : -1);
    }

    static void preset(juce::Graphics& g, juce::Rectangle<int> area, const ModuleView::Preset& p) {
        char number[16];
        std::snprintf(number, sizeof number, "%03d", std::clamp(p.index + 1, 0, 999));
        move::draw_text(g, area.getX() + 2, area.getY() + 2, number, 2);
        std::string name = upper(p.name.empty() ? "PRESET" : p.name);
        move::draw_text(g, area.getX() + 2, area.getY() + 20, name.substr(0, 16), name.size() <= 8 ? 2 : 1);
        if (p.count > 0) {
            std::string of = "OF " + std::to_string(p.count);
            move::draw_text(g, area.getRight() - move::text_width(of), area.getY() + 2, of);
        }
    }

    // Melodee's knob cells: the name, and the value as a bar where it has a
    // place (a number, an enum) or as text where it has none; a hole empty;
    // a held knob's cell inverted; a cell that opens something marked in its
    // corner (Schwung's door brackets).
    static void cells(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<ModuleView::Cell>& list) {
        const auto on = juce::Colours::white, off = juce::Colours::black;
        int cell_width = area.getWidth() / 4, cell_height = area.getHeight() / 2;
        size_t fits = size_t(std::max(1, (cell_width - 2) / move::glyph_advance));
        for (size_t knob = 0; knob < list.size() && knob < 8; ++knob) {
            const auto& c = list[knob];
            if (c.empty) continue;
            int x = area.getX() + int(knob % 4) * cell_width, y = area.getY() + int(knob / 4) * cell_height;
            g.setColour(on);
            if (c.touched) g.fillRect(x, y, cell_width - 1, cell_height - 1);
            g.setColour(c.touched ? off : on);
            move::draw_text(g, x + 1, y + 1, upper(c.short_name.empty() ? c.name : c.short_name).substr(0, fits));
            if (c.divable) { g.fillRect(x + cell_width - 4, y, 3, 1); g.fillRect(x + cell_width - 2, y, 1, 3); }
            double fraction = c.normalized;
            if (fraction < 0 && c.enum_index >= 0 && c.options > 1) fraction = double(c.enum_index) / double(c.options - 1);
            if (fraction >= 0 && c.turnable) {
                int bar_y = y + cell_height - 5, bar_width = cell_width - 4;
                if (c.read_only) for (int px = x + 1; px < x + 1 + bar_width; px += 2) g.fillRect(px, bar_y + 3, 1, 1);
                else g.fillRect(x + 1, bar_y + 3, bar_width, 1);
                g.fillRect(x + 1, bar_y, std::max(1, int(std::lround(std::clamp(fraction, 0.0, 1.0) * bar_width))), 3);
            } else {
                move::draw_text(g, x + 1, y + cell_height - move::glyph_height - 2, upper(c.value).substr(0, fits));
            }
        }
        g.setColour(on);
    }

    static void hint(juce::Graphics& g, juce::Rectangle<int> area, const ModuleView& v) {
        ui::sheet(g, area);
        int y = area.getY() + 2;
        if (!v.hint_title.empty()) { move::draw_text(g, area.getX() + 3, y, upper(v.hint_title).substr(0, 15)); y += move::glyph_height + 3; }
        for (const auto& line : v.hint) {
            if (y + move::glyph_height > area.getBottom()) break;
            move::draw_text(g, area.getX() + 3, y, upper(line).substr(0, 15));
            y += move::glyph_height + 2;
        }
    }
};

RegisterScreen registered("module-pages", make<ModulePages>);

} // namespace
} // namespace app::screens
