// SPDX-License-Identifier: AGPL-3.0-only
// A synth track's Play screen: one of Schwung's sound generators (README,
// "How a Schwung module is hosted"). Pads are notes in rows of fourths, C2 at
// the bottom left; Up and Down move an octave; pressure is polyphonic
// aftertouch. Knobs 1-8 are the module's page (its ui_hierarchy, or its
// parameters eight at a time), Left / Right its pages, the jogwheel its
// presets. Push the jogwheel for the module's menu: turn to a row, push to
// edit a value or open a level or a list; Back goes up, then closes it. A
// page the module draws itself runs as Schwung runs it (canvas_page.h):
// push to enter it, then the jogwheel and its push are the page's, Back goes
// up and out.
#include "app/screens/common.h"
#include <juce_core/juce_core.h>
#include <limits>
#include <map>

namespace app::screens {
namespace {

constexpr int lowest_octave = -3, highest_octave = 4;

// What a cell has room for: a module's short name, or its name.
std::string cell_name(const ModuleParam& param) { return upper(param.short_name.empty() ? param.name : param.short_name); }

class SynthPlay final : public Screen {
public:
    using Screen::Screen;

    void enter() override { held.fill(false); refresh(); }

    bool surface(const move::Input& input) override {
        using Kind = move::Input::Kind;
        using move::Button;
        auto* track = studio.module_track();
        if (!track) return false;
        switch (input.kind) {
        case Kind::Pad:
            if (input.index < pad_count) held[input.index] = input.pressed;
            return true;
        case Kind::Knob: turn(*track, input.index, input.delta, studio.shift); return true;
        case Kind::MainKnob:
            if (shown_canvas(*track) >= 0 && track->canvas_entered) canvas_jog(*track, input.delta);
            else if (track->menu_open) menu_turn(*track, input.delta, studio.shift);
            else preset_by(*track, input.delta);
            return true;
        case Kind::Button: break;
        default: return false;
        }
        if (!input.pressed) return false;
        auto button = Button(input.index);
        if (int shown = shown_canvas(*track); shown >= 0 && (button == Button::MainPush || button == Button::Back)) {
            CanvasPage* canvas = track->canvas_pages[size_t(shown)].get();
            if (button == Button::MainPush) {
                if (!track->canvas_entered) track->canvas_entered = true; // the entering push is the host's
                else if (canvas) canvas->midi(0xb0, move::button_cc(Button::MainPush), 127);
            } else if (!(track->canvas_entered && canvas && canvas->back())) { // at its top: leave it
                track->canvas_entered = false;
                track->canvas_open = -1;
            }
            return true;
        }
        switch (button) {
        case Button::Back:
            if (!track->menu_open) return false;
            menu_back(*track);
            return true;
        case Button::MainPush:
            if (track->menu_open) menu_push(*track);
            else open_menu(*track);
            return true;
        case Button::Loop: studio.say("CHOP: KIT ONLY"); return true;
        case Button::Left:
        case Button::Right: page_by(*track, button == Button::Left ? -1 : 1); return true;
        case Button::Up:
        case Button::Down: {
            int octave = std::clamp(studio.octave(studio.track()) + (button == Button::Up ? 1 : -1), lowest_octave, highest_octave);
            studio.set_octave(studio.track(), octave);
            studio.say("OCTAVE " + note_name(studio.note_for(0, octave)));
            return true;
        }
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        auto* track = studio.module_track();
        note_pads(studio, lights, held, track && track->module);
        steps_playhead(studio, lights);
        bool paged = track && track->module && track->module->pages.size() > 1;
        int octave = studio.octave(studio.track());
        lights.button(Button::Left, paged && track->page > 0 ? white_dim : 0);
        lights.button(Button::Right, paged && track->page + 1 < track->module->pages.size() ? white_dim : 0);
        lights.button(Button::Up, octave < highest_octave ? white_dim : 0);
        lights.button(Button::Down, octave > lowest_octave ? white_dim : 0);
        lights.button(Button::Loop, 0);
        lights.button(Button::Menu, white_dim);
        lights.button(Button::Back, track && (track->menu_open || track->canvas_entered || track->canvas_open >= 0) ? white_dim : 0);
    }

    void draw(juce::Graphics& g) override {
        auto* track = studio.module_track();
        if (!track || !track->module) {
            ui::rail(g, "SYN", track_number(studio), note_name(studio.note_for(0, studio.octave(studio.track()))));
            move::draw_text_centred(g, ui::content_x, 18, ui::content_width, track && (track->loading || !track->job.empty()) ? "LOADING" : "NO SYNTH", 2);
            ui::detail(g, track && !track->saved.module.empty() ? upper(track->saved.module + " MISSING").substr(0, 16) : "MENU: MODULES");
            return;
        }
        if (int shown = shown_canvas(*track); shown >= 0) { draw_canvas(g, *track, shown); return; }
        if (track->menu_open) { draw_menu(g, *track); return; }
        auto& module = *track->module;
        // The rail as docs/UI.md has a module's page: module, track, page.
        const ModulePage* page = track->page < module.pages.size() ? &module.pages[track->page] : nullptr;
        ui::rail(g, upper(module.info().abbrev).substr(0, 4), track_number(studio), std::to_string(track->page + 1) + "/" + std::to_string(module.pages.size()));
        std::string title = upper(page ? page->name : module.info().name).substr(0, 12), lowest = note_name(studio.note_for(0, studio.octave(studio.track())));
        move::draw_text(g, ui::content_x + 1, 1, title);
        move::draw_text(g, ui::width - int(lowest.size()) * move::glyph_advance, 1, lowest); // the bottom-left pad's note
        std::array<std::string, 8> names;
        std::array<float, 8> values {};
        for (unsigned n = 0; page && n < page->knobs.size() && n < 8; ++n)
            if (const ModuleParam* param = module.param(page->knobs[n])) {
                names[n] = cell_name(*param);
                values[n] = float(param->fraction());
            }
        ui::knob_cells(g, ui::content(11, ui::subject_height - 11), names, values);
        auto where = bar_beat(studio);
        if (!module.preset_param.empty()) {
            char number[16];
            std::snprintf(number, sizeof number, "%03d", std::clamp(module.preset + 1, 0, 999));
            ui::detail(g, upper(module.preset_name.empty() ? module.info().name : module.preset_name).substr(0, 12), where.empty() ? number : where);
        } else ui::detail(g, upper(module.info().name).substr(0, 12), where);
    }

    Knob knob(unsigned which) const override {
        auto* track = const_cast<Studio&>(studio).module_track();
        if (!track || !track->module || track->page >= track->module->pages.size()) return {};
        const auto& keys = track->module->pages[track->page].knobs;
        if (which >= keys.size()) return {};
        const ModuleParam* param = track->module->param(keys[which]);
        if (!param) return {};
        return {upper(param->name).substr(0, 16), upper(param->shown()).substr(0, 12), float(param->fraction())};
    }
    std::string knob_owner() const override {
        auto* track = const_cast<Studio&>(studio).module_track();
        return track && track->module ? upper(track->module->info().abbrev).substr(0, 4) : "SYN";
    }

    std::string status() const override {
        auto* track = const_cast<Studio&>(studio).module_track();
        if (!track) return " menu=- canvas=- entered=0";
        int shown = shown_canvas(*track);
        return std::string(" menu=") + (track->menu_open && !track->menu_path.empty() ? track->menu_path.back() : "-") + " canvas="
             + (shown >= 0 ? track->module->canvases[size_t(shown)].key : "-") + " entered=" + std::to_string(int(track->canvas_entered));
    }

private:
    // The module's own page on screen, else -1.
    int shown_canvas(const ModuleTrack& track) const {
        if (!track.module || studio.top() != this) return -1;
        auto valid = [&](int index) { return index >= 0 && index < int(track.canvas_pages.size()) ? index : -1; };
        if (track.canvas_open >= 0) return valid(track.canvas_open);
        if (track.menu_open || track.page >= track.module->pages.size()) return -1;
        return valid(track.module->pages[track.page].canvas);
    }

    // Ask the module what its page's knobs are now.
    void refresh() {
        auto* track = studio.module_track();
        if (!track || !track->module || track->page >= track->module->pages.size()) return;
        for (const auto& key : track->module->pages[track->page].knobs) track->player.ask(key);
    }

    void turn(ModuleTrack& track, unsigned which, int delta, bool fine) {
        if (!track.module || track.page >= track.module->pages.size()) return;
        const auto& keys = track.module->pages[track.page].knobs;
        if (which >= keys.size()) return;
        if (keys[which] == track.module->preset_param) { preset_by(track, delta); return; }
        ModuleParam* param = track.module->param(keys[which]);
        if (!param || !delta) return;
        if (param->trigger()) { // an action: a turn up does it
            if (delta > 0) studio.synth_set(track, param->key, "trigger");
            return;
        }
        param->turn(delta, fine);
        studio.synth_set(track, param->key, param->text());
        studio.changed();
    }

    void page_by(ModuleTrack& track, int way) {
        if (!track.module || track.module->pages.empty()) return;
        track.page = unsigned(std::clamp(int(track.page) + way, 0, int(track.module->pages.size()) - 1));
        track.canvas_entered = false;
        refresh();
    }

    void preset_by(ModuleTrack& track, int way) {
        if (!track.module) return;
        auto& module = *track.module;
        if (module.preset_param.empty()) { page_by(track, way); return; }
        int last = module.preset_count > 0 ? module.preset_count - 1 : std::numeric_limits<int>::max();
        int to = std::clamp(module.preset + way, 0, last);
        if (to == module.preset) return;
        module.preset = to;
        if (auto* param = module.param(module.preset_param)) param->value = to;
        studio.synth_set(track, module.preset_param, std::to_string(to));
        // A preset sets every parameter: ask for each again, to show and save.
        for (const auto& param : module.params)
            if (param.key != module.preset_param) track.player.ask(param.key);
        if (!module.preset_name_param.empty()) track.player.ask(module.preset_name_param);
        studio.changed();
    }

    // ---- the module's menu
    struct MenuRow {
        enum class Kind : uint8_t { Preset, Param, Level, Item, Canvas } kind = Kind::Param;
        std::string key, label;
        int index = 0; // an Item's
    };

    std::vector<MenuRow> menu_rows(const ModuleTrack& track) const {
        std::vector<MenuRow> rows;
        if (!track.module || track.menu_path.empty()) return rows;
        const auto& module = *track.module;
        const ModuleLevel* at = module.level(track.menu_path.back());
        if (!at) return rows;
        using Kind = MenuRow::Kind;
        if (!at->list_param.empty() && at->list_param == module.preset_param) rows.push_back({Kind::Preset, at->list_param, "PRESET"});
        if (!at->items_param.empty())
            for (const auto& [index, label] : track.pick_items) rows.push_back({Kind::Item, at->select_param, label, index});
        for (const auto& item : at->items) {
            if (item.kind == MenuItem::Kind::Level) {
                const ModuleLevel* to = module.level(item.key);
                if (to) rows.push_back({Kind::Level, item.key, item.label.empty() ? to->name : item.label});
                continue;
            }
            const ModuleParam* param = const_cast<LoadedModule&>(module).param(item.key);
            if (param && !param->selector) rows.push_back({Kind::Param, item.key, item.label.empty() ? (param->label.empty() ? param->name : param->label) : item.label});
        }
        if (rows.empty()) // a level that lists nothing: its knobs
            for (const auto& page : module.pages)
                if (page.level == module.level_index(at->id))
                    for (const auto& key : page.knobs)
                        if (const ModuleParam* param = const_cast<LoadedModule&>(module).param(key)) rows.push_back({Kind::Param, key, param->label.empty() ? param->name : param->label});
        if (at->id == "root") // the module's own pages, last
            for (size_t n = 0; n < module.canvases.size(); ++n) rows.push_back({Kind::Canvas, module.canvases[n].key, module.canvases[n].name, int(n)});
        return rows;
    }

    std::string menu_text(const ModuleTrack& track, const MenuRow& row, bool editing) const {
        constexpr size_t width = 16; // characters in a list row
        auto& module = *track.module;
        auto fit = [&](std::string label, std::string value) {
            if (editing) value = "[" + value + "]";
            value = value.substr(0, width - 2);
            label = label.substr(0, width - value.size() - 1);
            return label + std::string(width - label.size() - value.size(), ' ') + value;
        };
        using Kind = MenuRow::Kind;
        switch (row.kind) {
        case Kind::Preset: {
            char number[16];
            std::snprintf(number, sizeof number, "%03d", std::clamp(module.preset + 1, 0, 999));
            return fit(std::string(number) + " " + upper(module.preset_name), editing ? "<>" : "");
        }
        case Kind::Param: {
            const ModuleParam* param = module.param(row.key);
            return fit(upper(row.label), param ? upper(param->shown()) : "?");
        }
        case Kind::Level:
        case Kind::Canvas: return upper(row.label).substr(0, width - 2) + " >";
        case Kind::Item: {
            const ModuleParam* chosen = module.param(row.key);
            bool current = chosen && std::lround(chosen->value) == row.index;
            return (current ? "* " : "  ") + upper(row.label).substr(0, width - 2);
        }
        }
        return {};
    }

    void open_menu(ModuleTrack& track) {
        if (!track.module) return;
        track.menu_open = true;
        track.menu_path.clear();
        enter_level(track, "root");
        // Where the knobs are: that level's menu.
        const auto& module = *track.module;
        if (track.page < module.pages.size() && module.pages[track.page].level != 0) enter_level(track, module.levels[module.pages[track.page].level].id);
    }

    void enter_level(ModuleTrack& track, const std::string& id) {
        const ModuleLevel* to = track.module ? track.module->level(id) : nullptr;
        if (!to) return;
        track.menu_path.push_back(id);
        track.menu_cursor = 0;
        track.menu_editing = false;
        track.pick_items.clear();
        track.pick_key = to->items_param;
        if (!track.pick_key.empty() && !track.pick_reading) track.pick_reading = track.player.ask_long(track.pick_key);
        if (!to->select_param.empty()) track.player.ask(to->select_param);
        // The knobs follow to the level's own page, where it has one.
        const auto& module = *track.module;
        size_t index = module.level_index(id);
        bool here = track.page < module.pages.size() && module.pages[track.page].level == index;
        for (size_t n = 0; n < module.pages.size() && !here; ++n)
            if (module.pages[n].level == index) { track.page = unsigned(n); here = true; }
        unsigned asked = 0;
        for (const auto& row : menu_rows(track))
            if (row.kind == MenuRow::Kind::Param && asked++ < 48) track.player.ask(row.key);
    }

    void menu_turn(ModuleTrack& track, int delta, bool fine) {
        auto rows = menu_rows(track);
        if (rows.empty() || !delta) return;
        track.menu_cursor = std::clamp(track.menu_cursor, 0, int(rows.size()) - 1);
        if (!track.menu_editing) {
            track.menu_cursor = std::clamp(track.menu_cursor + delta, 0, int(rows.size()) - 1);
            return;
        }
        const auto& row = rows[size_t(track.menu_cursor)];
        if (row.kind == MenuRow::Kind::Preset) { preset_by(track, delta); return; }
        ModuleParam* param = track.module->param(row.key);
        if (row.kind != MenuRow::Kind::Param || !param) return;
        param->turn(delta, fine);
        studio.synth_set(track, param->key, param->text());
        studio.changed();
    }

    void menu_push(ModuleTrack& track) {
        auto rows = menu_rows(track);
        if (rows.empty()) return;
        track.menu_cursor = std::clamp(track.menu_cursor, 0, int(rows.size()) - 1);
        const auto row = rows[size_t(track.menu_cursor)];
        auto& module = *track.module;
        switch (row.kind) {
        case MenuRow::Kind::Preset: track.menu_editing = !track.menu_editing; break;
        case MenuRow::Kind::Param:
            if (const ModuleParam* param = module.param(row.key); param && param->trigger()) studio.synth_set(track, row.key, "trigger"); // an action
            else track.menu_editing = !track.menu_editing;
            break;
        case MenuRow::Kind::Canvas: {
            if (module.canvases[size_t(row.index)].as_page) { // to its page, entered
                for (size_t n = 0; n < module.pages.size(); ++n)
                    if (module.pages[n].canvas == row.index) track.page = unsigned(n);
                track.menu_open = false;
            } else track.canvas_open = row.index; // over the menu, until it is left
            track.canvas_entered = true;
            break;
        }
        case MenuRow::Kind::Level: enter_level(track, row.key); break;
        case MenuRow::Kind::Item: {
            if (ModuleParam* chosen = module.param(row.key)) chosen->value = row.index;
            studio.synth_set(track, row.key, std::to_string(row.index));
            // A bank changes the presets: ask for them again.
            for (const auto* key : {&module.preset_param, &module.preset_count_param, &module.preset_name_param})
                if (!key->empty()) track.player.ask(*key);
            studio.changed();
            const ModuleLevel* at = module.level(track.menu_path.back());
            std::string to = at ? at->navigate_to : std::string();
            if (!to.empty() && module.level(to)) {
                track.menu_path.clear();
                if (to != "root") enter_level(track, "root");
                enter_level(track, to);
            } else menu_back(track);
            break;
        }
        }
    }

    void menu_back(ModuleTrack& track) {
        if (track.menu_editing) { track.menu_editing = false; return; }
        if (track.menu_path.size() <= 1) { track.menu_open = false; return; }
        std::string left = track.menu_path.back();
        track.menu_path.pop_back();
        track.pick_items.clear();
        track.pick_key.clear();
        auto rows = menu_rows(track);
        track.menu_cursor = 0;
        for (size_t n = 0; n < rows.size(); ++n)
            if (rows[n].kind == MenuRow::Kind::Level && rows[n].key == left) track.menu_cursor = int(n);
    }

    void draw_menu(juce::Graphics& g, ModuleTrack& track) {
        const auto& module = *track.module;
        ui::rail(g, upper(module.info().abbrev).substr(0, 4), track_number(studio), "MENU");
        const ModuleLevel* at = track.menu_path.empty() ? nullptr : module.level(track.menu_path.back());
        move::draw_text(g, ui::content_x + 1, 1, upper(at ? at->name : module.info().name).substr(0, 16));
        auto rows = menu_rows(track);
        if (rows.empty()) {
            move::draw_text_centred(g, ui::content_x, 26, ui::content_width, track.pick_reading || !track.pick_key.empty() ? "READING" : "NOTHING HERE");
            return;
        }
        std::vector<std::string> texts;
        int cursor = std::clamp(track.menu_cursor, 0, int(rows.size()) - 1);
        for (size_t n = 0; n < rows.size(); ++n) texts.push_back(menu_text(track, rows[n], track.menu_editing && int(n) == cursor));
        ui::list(g, ui::content(10, ui::height - 10), texts, cursor, 1);
    }

    // ---- the module's own pages
    void canvas_jog(ModuleTrack& track, int delta) {
        int shown = shown_canvas(track);
        if (shown < 0 || !track.canvas_pages[size_t(shown)] || !delta) return;
        uint8_t value = uint8_t(delta > 0 ? std::min(delta, 63) : 128 + std::max(delta, -64)); // the jogwheel's own relative CC
        track.canvas_pages[size_t(shown)]->midi(0xb0, move::main_knob_cc, value);
    }

    void draw_canvas(juce::Graphics& g, ModuleTrack& track, int index) {
        const auto& module = *track.module;
        const auto& about = module.canvases[size_t(index)];
        CanvasPage* canvas = track.canvas_pages[size_t(index)].get();
        if (!canvas) {
            ui::rail(g, upper(module.info().abbrev).substr(0, 4), track_number(studio), "PAGE");
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "PAGE FAILED");
            ui::detail(g, "SEE THE LOG");
            return;
        }
        std::map<std::string, std::string> values;
        for (const auto& key : about.extra_keys) values[key] = studio.module_get(track, key);
        canvas->draw(g, values);
        // Below the page: whose it is, and what the jogwheel's push does.
        g.setColour(juce::Colours::black);
        g.fillRect(0, 47, ui::width, ui::height - 47);
        g.setColour(juce::Colours::white);
        move::draw_text(g, 1, 56, upper(module.info().abbrev + " " + about.name).substr(0, 15));
        std::string hint = track.canvas_entered ? "BACK: OUT" : "PUSH: IN";
        move::draw_text(g, ui::width - move::text_width(hint), 56, hint);
    }

    std::array<bool, pad_count> held {}; // pads down, for their lights
};

RegisterScreen registered("synth-play", make<SynthPlay>, {}, kind_bit(TrackKind::Synth));

} // namespace
} // namespace app::screens
