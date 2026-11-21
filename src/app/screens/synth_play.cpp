// SPDX-License-Identifier: AGPL-3.0-only
// A synth track's Play screen: one of Schwung's sound generators (README,
// "How a Schwung module is hosted"), as the Move's Device View
// (docs/MOVE_CONTROLS.md). Along the top its items, the module, its preset,
// FX1, FX2: the wheel walks them, its push opens the one it is on (the
// module: Load modules; the preset: its presets, loaded as the wheel turns,
// push or Back done). Pads are notes in rows of fourths, C2 at the bottom
// left; Plus and Minus move an octave; pressure is polyphonic aftertouch.
// Knobs 1-8 are the module's page (its ui_hierarchy, or its parameters
// eight at a time), Shift + the wheel its pages. Shift + push opens the
// module's menu: turn to a row, push to edit a value or open a level or a
// list; Back goes up, then closes it. A page the module draws itself runs as
// Schwung runs it (canvas_page.h), but is a door until pushed (Kerem,
// 2026-11-20, docs/ui/synth.png): outside it the Device View stays, its
// knobs work and the wheel walks the items (the mix, the effects, the plus);
// push goes in, then the wheel and its push are the page's, Back goes up and
// out.
#include "app/screens/note_pages.h"
#include "app/screens/device_fx.h"
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

    void enter() override { held.fill(false); browsing = false; refresh(); }

    bindings::Screen screen_kind() const override {
        auto* track = const_cast<Studio&>(studio).module_track();
        if (!track || !track->module) return bindings::Screen::Play;
        if (browsing) return bindings::Screen::Browser;
        if (const SchwungPages* pages = schwung(*track)) // its menu, a list, an entered page: the wheel and Back are Schwung's
            return pages->wants_wheel() ? bindings::Screen::Canvas : bindings::Screen::Play;
        if (shown_canvas(*track) >= 0 && track->canvas_entered) return bindings::Screen::Canvas; // a door until pushed
        if (track->menu_open) return bindings::Screen::List;
        return bindings::Screen::Play;
    }

    void input(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Pad && input.index < pad_count) {
            held[input.index] = input.pressed;
            if (input.pressed) last_pad = int(input.index);
        }
        // Schwung's pages read the knobs' touches (a cell held, its value
        // shown large) and Shift (fine turns) as its shadow UI does.
        if (auto* track = studio.module_track(); track)
            if (SchwungPages* pages = schwung(*track)) {
                if (input.kind == move::Input::Kind::Touch && input.index < 8)
                    pages->midi(input.pressed ? 0x90 : 0x80, input.index, input.pressed ? 127 : 0);
                if (input.kind == move::Input::Kind::Button && input.index == uint8_t(move::Button::Shift))
                    pages->midi(0xb0, move::button_cc(move::Button::Shift), input.pressed ? 127 : 0);
            }
        fx.input(input);
    }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        auto* track = studio.module_track();
        if (!track) return false;
        // The track item's mix and the effects (device_fx.h), but never under
        // the module's menu, its presets or its own page.
        auto effects = fx.track_items();
        focus = std::min(focus, 1 + int(effects.size()));
        bool device = !browsing && !track->menu_open && shown_canvas(*track) < 0;
        if (device && focus >= 2 && fx.job(effects[size_t(focus - 2)], job, use)) return true;
        if (device && focus == 0 && fx.mix_job(studio.track(), job, use)) return true;
        if (SchwungPages* pages = schwung(*track); pages && schwung_job(*track, *pages, job, use)) return true;
        switch (job) {
        case Job::PlayNote: return true; // its sound went from the audio thread
        case Job::TurnKnob: case Job::TurnKnobFine:
            if (NotePage* page = melodee_page()) { note_page_turn(studio, studio.track(), *page, unsigned(use.index), use.delta); return true; }
            turn(*track, unsigned(use.index), use.delta, job == Job::TurnKnobFine);
            return true;
        case Job::Octave: studio.say(octave_or_bank(studio, use.control == bindings::Control::Plus ? 1 : -1, lowest_octave, highest_octave)); return true;
        // The Device View.
        case Job::Focus: focus = std::clamp(focus + (use.delta > 0 ? 1 : -1), 0, 1 + int(effects.size())); return true;
        case Job::ShowFocus: studio.say(item(*track, focus)); return true;
        case Job::OpenBrowser:
            if (focus == 1 && shown_canvas(*track) >= 0) track->canvas_entered = true; // in through the door: the page's own push from here
            else if (focus == 1 && track->module && !track->module->preset_param.empty()) browsing = true;
            else studio.open("load-modules");
            return true;
        case Job::ModuleMenu:
            if (!track->module) studio.open("load-modules");
            else if (!track->menu_open) open_menu(*track);
            return true;
        case Job::Page: page_by(*track, use.delta > 0 ? 1 : -1); return true;
        // Its presets, loaded as the wheel turns.
        case Job::BrowseTurn: preset_by(*track, use.delta); return true;
        case Job::BrowsePush: browsing = false; return true;
        // Its menu.
        case Job::ListTurn: menu_turn(*track, use.delta, false); return true;
        case Job::ListTurnFine: menu_turn(*track, use.delta, true); return true;
        case Job::ListPush: menu_push(*track); return true;
        case Job::Back:
            if (browsing) { browsing = false; return true; }
            if (track->menu_open) { menu_back(*track); return true; }
            return false;
        // Its own page.
        case Job::CanvasTurn:
            if (track->canvas_entered) canvas_jog(*track, use.delta);
            return true;
        case Job::CanvasPush: {
            int shown = shown_canvas(*track);
            if (shown < 0) return false;
            CanvasPage* canvas = track->canvas_pages[size_t(shown)].get();
            if (!track->canvas_entered) track->canvas_entered = true; // the entering push is the host's
            else if (canvas) canvas->midi(0xb0, move::button_cc(move::Button::MainPush), 127);
            return true;
        }
        case Job::CanvasBack: {
            int shown = shown_canvas(*track);
            if (shown < 0) return false;
            CanvasPage* canvas = track->canvas_pages[size_t(shown)].get();
            if (!(track->canvas_entered && canvas && canvas->back())) { // at its top: leave it
                track->canvas_entered = false;
                track->canvas_open = -1;
            }
            return true;
        }
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        auto* track = studio.module_track();
        if (track && track->module) key_pads(studio, lights, held); // as Keys & Scale lays them out (app/keys.h)
        else for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, move::colour::off);
        steps_playhead(studio, lights);
        int octave = studio.octave(studio.track());
        // Left / Right (bars), Loop (Loop Mode) and Note/Session (Patterns) wait for wave 2.
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Up, octave < highest_octave ? white_dim : 0);
        lights.button(Button::Down, octave > lowest_octave ? white_dim : 0);
        const SchwungPages* pages = track ? schwung(*track) : nullptr;
        lights.button(Button::Back, track && (browsing || track->menu_open || track->canvas_entered || track->canvas_open >= 0 || (pages && pages->wants_wheel())) ? white_dim : 0);
    }

    void draw(juce::Graphics& g) override {
        auto* track = studio.module_track();
        if (!track || !track->module) {
            ui::rail(g, "SYN", track_number(studio), note_name(studio.note_for(0, studio.octave(studio.track()))));
            move::draw_text_centred(g, ui::content_x, 18, ui::content_width, track && (track->loading || !track->job.empty()) ? "LOADING" : "NO SYNTH", 2);
            ui::detail(g, track && !track->saved.module.empty() ? upper(track->saved.module + " MISSING").substr(0, 16) : "PUSH: MODULES");
            return;
        }
        if (studio.desk.show_effects) { // from the Mixer's push: the effects, out of any page or menu
            focus = 2;
            studio.desk.show_effects = false;
            browsing = track->menu_open = track->canvas_entered = false;
            track->canvas_open = -1;
        }
        if (SchwungPages* pages = schwung(*track)) { pages->draw(g); return; } // the whole screen, as Schwung draws it
        if (int shown = shown_canvas(*track); shown >= 0 && track->canvas_entered) { draw_canvas(g, *track, shown); return; }
        if (track->menu_open) { draw_menu(g, *track); return; }
        if (browsing) { draw_presets(g, *track); return; }
        auto& module = *track->module;
        settle(*track); // a page its visible_if hides now (a mode turned): the nearest shown
        // The rail as docs/UI.md has a module's page: module, track, page.
        const ModulePage* page = track->page < module.pages.size() ? &module.pages[track->page] : nullptr;
        auto knobs = module.page_knobs(track->page);
        auto effects = fx.track_items();
        focus = std::min(focus, 1 + int(effects.size()));
        ui::rail(g, upper(module.info().abbrev).substr(0, 4), track_number(studio),
                 focus == 0 ? "MIX" : focus >= 2 ? fx.rail_bottom(effects[size_t(focus - 2)]) : std::to_string(position(*track) + 1) + "/" + std::to_string((track->schwung_pages ? size_t(track->schwung_pages->page_count()) : shown_pages(*track).size()) + 2));
        fx.draw_row(g, item(*track, 0), item(*track, 1), effects, focus);
        if (focus == 0) { fx.draw_mix(g, studio.track()); return; }
        if (focus >= 2) { fx.draw(g, effects[size_t(focus - 2)]); return; }
        if (NotePage* own = melodee_page()) { // ARP, CHORD (screens/note_pages.h)
            draw_note_page(g, studio, studio.track(), *own, note_page_right(studio, studio.track(), *own));
            auto [left, more] = note_page_detail(studio, studio.track(), *own);
            ui::detail(g, left, more);
            return;
        }
        std::string title = upper(page ? page->name : module.info().name).substr(0, 12), lowest = note_name(studio.note_for(0, studio.octave(studio.track())));
        move::draw_text(g, ui::content_x + 1, 11, title);
        move::draw_text(g, ui::width - int(lowest.size()) * move::glyph_advance, 11, lowest); // the bottom-left pad's note
        std::array<std::string, 8> names;
        std::array<float, 8> values {};
        for (unsigned n = 0; page && n < knobs.size() && n < 8; ++n)
            if (const ModuleParam* param = module.param(knobs[n])) {
                names[n] = cell_name(*param);
                values[n] = float(param->fraction());
            }
        ui::knob_cells(g, ui::content(21, ui::subject_height - 21), names, values);
        auto where = bar_beat(studio);
        if (keys_mode(studio.current()) == KeysMode::Drums && last_pad >= 0) { // the drum hit last, named
            auto drum = pad_notes(studio.project(), studio.current(), unsigned(last_pad));
            if (drum.count) { ui::detail(g, drum_name(drum.keys[0]).substr(0, 16)); return; }
        }
        if (shown_canvas(*track) >= 0) { ui::detail(g, upper(module.info().name).substr(0, 10), "PUSH: IN"); return; } // its own page's door
        if (!module.preset_param.empty()) {
            char number[16];
            std::snprintf(number, sizeof number, "%03d", std::clamp(module.preset + 1, 0, 999));
            ui::detail(g, upper(module.preset_name.empty() ? module.info().name : module.preset_name).substr(0, 12), where.empty() ? number : where);
        } else ui::detail(g, upper(module.info().name).substr(0, 12), where);
    }

    bool knob_lane(unsigned which, unsigned& param, unsigned& pad) const override {
        if (const_cast<SynthPlay*>(this)->melodee_page()) return false;
        auto* track = const_cast<Studio&>(studio).module_track();
        if (!track || !track->module || browsing || track->menu_open || shown_canvas(*track) >= 0) return false;
        if (focus == 0) return fx.mix_lane(studio.track(), which, param, pad);
        auto effects = fx.track_items();
        pad = 0;
        return focus >= 2 && size_t(focus - 2) < effects.size() && fx.lane(effects[size_t(focus - 2)], which, param);
    }
    Knob knob(unsigned which) const override {
        if (const NotePage* own = const_cast<SynthPlay*>(this)->melodee_page()) return note_page_knob(studio, studio.track(), *own, which);
        auto* track = const_cast<Studio&>(studio).module_track();
        if (track && schwung(*track)) return {}; // Schwung's pages show a touched knob themselves
        if (track && track->module && !browsing && !track->menu_open && shown_canvas(*track) < 0) {
            if (focus == 0) return fx.mix_knob(studio.track(), which);
            auto effects = fx.track_items();
            if (focus >= 2) return size_t(focus - 2) < effects.size() ? fx.knob(effects[size_t(focus - 2)], which) : Knob {};
        }
        if (!track || !track->module || track->page >= track->module->pages.size()) return {};
        auto keys = track->module->page_knobs(track->page);
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
        const SchwungPages* pages = schwung(*track);
        return std::string(" schwung_page=") + (pages ? pages->page_name() + "," + std::to_string(pages->page_index() + 1) + "/" + std::to_string(pages->page_count()) + (pages->wants_wheel() ? ",wheel" : "") : "-")
             + " menu=" + (track->menu_open && !track->menu_path.empty() ? track->menu_path.back() : "-") + " canvas="
             + (shown >= 0 ? track->module->canvases[size_t(shown)].key : "-") + " entered=" + std::to_string(int(track->canvas_entered));
    }

private:
    int focus = 1;          // the Device View's item the wheel is on: 0 the module (its mix), 1 its preset, 2 on its effects
    DeviceFx fx {studio};
    bool browsing = false;  // its presets, the wheel walking them
    // Melodee's own pages after the module's (Kerem, 2026-11-20): ARP and CHORD last; a track opens on the
    // module's, its knobs Schwung's controls. Each track's: 0 the module's page (ModuleTrack::page), 1 ARP, 2 CHORD.
    std::array<uint8_t, melodee::track_count> own_page {};
    int last_pad = -1; // Drums: the drum hit last, named
    NotePage* melodee_page() {
        static NotePage pages[] {NotePage::Arp, NotePage::Arp, NotePage::Chord};
        auto* track = studio.module_track();
        if (!track || !track->module || track->menu_open || browsing || track->canvas_open >= 0) return nullptr;
        if (focus != 1) return nullptr; // the Device View on the module (0: the track's mix, 2 on: its effects, device_fx.h)
        uint8_t at = own_page[studio.track()];
        return at ? &pages[at] : nullptr;
    }
    // Schwung's own code drawing the module's pages (schwung_pages.h): on
    // the module's item, off ARP and CHORD, no sheet of Melodee's over it.
    SchwungPages* schwung(const ModuleTrack& track) const {
        if (!track.module || !track.schwung_pages || studio.top() != this) return nullptr;
        if (focus != 1 || browsing || own_page[studio.track()]) return nullptr;
        return track.schwung_pages.get();
    }

    // The Move's controls to Schwung's pages, as its shadow UI takes them:
    // knobs turn, the wheel's push is its click (with Shift held, its own
    // Shift + click), Shift + the wheel its pages (on to ARP and CHORD after
    // its last), and while it has the wheel (its menu, a list, an entered
    // page) the wheel and Back. The wheel alone still walks the Device View.
    bool schwung_job(ModuleTrack& track, SchwungPages& pages, bindings::Job job, const bindings::Use& use) {
        using bindings::Job;
        auto cc = [&](uint8_t number, uint8_t value) { pages.midi(0xb0, number, value); };
        auto press = [&](move::Button button) { cc(move::button_cc(button), 127); cc(move::button_cc(button), 0); };
        auto wheel = [&](int delta) { // one message a detent, as the Move sends them
            for (int n = 0; n < std::min(std::abs(delta), 16); ++n) cc(move::main_knob_cc, delta > 0 ? 1 : 127);
        };
        switch (job) {
        case Job::TurnKnob: case Job::TurnKnobFine:
            if (use.index < 0 || use.index >= 8 || !use.delta) return true;
            for (int n = 0; n < std::min(std::abs(use.delta), 16); ++n) cc(uint8_t(71 + use.index), use.delta > 0 ? 1 : 127);
            studio.changed();
            return true;
        case Job::Page: {
            if (pages.wants_wheel()) { wheel(use.delta); return true; }
            if (use.delta > 0 && pages.page_index() >= pages.page_count() - 1) { page_by(track, 1); return true; } // on to ARP
            cc(move::button_cc(move::Button::Shift), 0); // Shift is the binding's here: a page, not Schwung's section jump
            wheel(use.delta > 0 ? 1 : -1);
            cc(move::button_cc(move::Button::Shift), 127);
            return true;
        }
        case Job::OpenBrowser: case Job::ModuleMenu: case Job::CanvasPush: case Job::ListPush:
            press(move::Button::MainPush);
            return true;
        case Job::CanvasTurn: case Job::ListTurn: case Job::ListTurnFine: wheel(use.delta); return true;
        case Job::Back: case Job::CanvasBack:
            press(move::Button::Back);
            return !pages.left(); // at its top: Melodee's Back
        default: return false;
        }
    }

    // The page as Shift + the wheel counts them: the module's, ARP, CHORD.
    unsigned position(const ModuleTrack& track) const {
        auto shown = shown_pages(track);
        uint8_t own = own_page[studio.track()];
        if (own && track.schwung_pages) return unsigned(track.schwung_pages->page_count()) + own - 1;
        if (own) return unsigned(shown.size()) + own - 1;
        return unsigned(std::count_if(shown.begin(), shown.end(), [&](size_t n) { return n < track.page; }));
    }
    // The module's pages its visible_if shows now, in order.
    std::vector<size_t> shown_pages(const ModuleTrack& track) const {
        std::vector<size_t> shown;
        if (track.module)
            for (size_t n = 0; n < track.module->pages.size(); ++n)
                if (track.module->page_shown(n)) shown.push_back(n);
        return shown;
    }
    // Off a page hidden now: the next shown, else the one before, else stay.
    void settle(ModuleTrack& track) {
        if (!track.module || track.module->page_shown(track.page)) return;
        auto shown = shown_pages(track);
        if (shown.empty()) return;
        auto next = std::find_if(shown.begin(), shown.end(), [&](size_t n) { return n > track.page; });
        track.page = unsigned(next != shown.end() ? *next : shown.back());
        track.canvas_entered = false;
        refresh();
    }

    std::string item(ModuleTrack& track, int which) const {
        switch (which) {
        case 0: return track.module ? upper(track.module->info().abbrev).substr(0, 4) : "SYN";
        case 1: {
            if (!track.module || track.module->preset_param.empty()) return "----";
            char number[8];
            std::snprintf(number, sizeof number, "%03d", std::clamp(track.module->preset + 1, 0, 999));
            return number;
        }
        default: return "FX" + std::to_string(which - 1);
        }
    }

    void draw_presets(juce::Graphics& g, ModuleTrack& track) {
        auto& module = *track.module;
        ui::rail(g, "LOAD", track_number(studio), "PRST");
        char number[8];
        std::snprintf(number, sizeof number, "%03d", std::clamp(module.preset + 1, 0, 999));
        move::draw_text(g, ui::content_x + 2, 4, number, 3);
        std::string name = upper(module.preset_name.empty() ? "PRESET" : module.preset_name);
        move::draw_text(g, ui::content_x + 2, 30, name.substr(0, 16), name.size() <= 8 ? 2 : 1);
        ui::detail(g, upper(module.info().abbrev).substr(0, 6), "PUSH: OK");
    }

    // The module's own page on screen (entered, or its door), else -1: never
    // while the wheel is on the track's mix or its effects.
    int shown_canvas(const ModuleTrack& track) const {
        if (!track.module || studio.top() != this) return -1;
        if (focus != 1 && !track.canvas_entered && track.canvas_open < 0) return -1;
        if (own_page[studio.track()] && track.canvas_open < 0 && !track.menu_open) return -1; // ARP, CHORD
        auto valid = [&](int index) { return index >= 0 && index < int(track.canvas_pages.size()) ? index : -1; };
        if (track.canvas_open >= 0) return valid(track.canvas_open);
        if (track.menu_open || track.page >= track.module->pages.size()) return -1;
        return valid(track.module->pages[track.page].canvas);
    }

    // Ask the module what its page's knobs are now.
    void refresh() {
        auto* track = studio.module_track();
        if (!track || !track->module || track->page >= track->module->pages.size()) return;
        for (const auto& key : track->module->page_knobs(track->page))
            if (!key.empty()) track->player.ask(key);
    }

    void turn(ModuleTrack& track, unsigned which, int delta, bool fine) {
        if (!track.module || track.page >= track.module->pages.size()) return;
        auto keys = track.module->page_knobs(track.page);
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
        if (!track.module) return;
        auto shown = shown_pages(track); // the pages visible_if hides are passed over
        const int pages = int(shown.size());
        int at = std::clamp(int(position(track)) + way, 0, pages + 1);
        auto& own = own_page[studio.track()];
        if (at < pages) { own = 0; track.page = unsigned(shown[size_t(at)]); }
        else own = uint8_t(at - pages + 1);
        track.canvas_entered = false;
        if (!own) refresh(); // the module's knobs asked for: Melodee's own pages ask it nothing
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
        // Its name first, for the screen; then, as a preset sets every
        // parameter, each again, to show and save (Surge's 300 wait their turn).
        if (!module.preset_name_param.empty()) track.player.ask(module.preset_name_param, true);
        for (const auto& param : module.params)
            if (param.key != module.preset_param) track.player.ask(param.key);
        studio.changed();
    }

    // ---- the module's menu
    struct MenuRow {
        enum class Kind : uint8_t { Preset, Param, Level, Item, Canvas, AutoSample } kind = Kind::Param;
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
        auto canvas_of = [&](const std::string& key) {
            for (size_t n = 0; n < module.canvases.size(); ++n)
                if (module.canvases[n].key == key) return int(n);
            return -1;
        };
        for (const auto& item : at->items) {
            if (!module.shown(item.shown_if)) continue; // its visible_if fails now
            if (item.kind == MenuItem::Kind::Level) {
                const ModuleLevel* to = module.level(item.key);
                if (to && module.level_shown(module.level_index(item.key))) rows.push_back({Kind::Level, item.key, item.label.empty() ? to->name : item.label});
                continue;
            }
            if (int n = canvas_of(item.key); n >= 0) { // its own page, where the level lists it (Webstream's Browse)
                rows.push_back({Kind::Canvas, item.key, item.label.empty() ? module.canvases[size_t(n)].name : item.label, n});
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
        if (at->id == "root") { // the module's own pages no level lists, then Melodee's auto sampler (Kerem, 2026-11-20), last
            auto listed = [&](const std::string& key) {
                for (const auto& level : module.levels)
                    for (const auto& item : level.items)
                        if (item.kind == MenuItem::Kind::Param && item.key == key) return true;
                return false;
            };
            for (size_t n = 0; n < module.canvases.size(); ++n)
                if (!listed(module.canvases[n].key)) rows.push_back({Kind::Canvas, module.canvases[n].key, module.canvases[n].name, int(n)});
            if (!module.source) rows.push_back({Kind::AutoSample, "", "AUTO SAMPLE"});
        }
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
        case Kind::Canvas:
        case Kind::AutoSample: return upper(row.label).substr(0, width - 2) + " >";
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
            if (module.canvases[size_t(row.index)].as_page) { // to its page, entered (off ARP or CHORD)
                for (size_t n = 0; n < module.pages.size(); ++n)
                    if (module.pages[n].canvas == row.index) track.page = unsigned(n);
                own_page[studio.track()] = 0;
                focus = 1; // the module's item: Back comes out to its door
                track.menu_open = false;
            } else track.canvas_open = row.index; // over the menu, until it is left
            track.canvas_entered = true;
            break;
        }
        case MenuRow::Kind::Level: enter_level(track, row.key); break;
        case MenuRow::Kind::AutoSample: // the AUTO SAMPLE sheet, over Play
            track.menu_open = false;
            track.menu_editing = false;
            studio.open("auto-sample");
            break;
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
        // As Schwung hands it: which page, its knobs and their values with
        // the extra keys', and the band above Melodee's own rows.
        CanvasPage::Frame frame;
        frame.key = about.key;
        frame.height = 47;
        for (size_t n = 0; n < module.pages.size(); ++n)
            if (module.pages[n].canvas == index) { frame.keys = module.page_knobs(n); break; }
        // A knob's value is the one Melodee keeps; only one the module moves
        // itself, and the extra keys, cost a read (one audio block each).
        std::map<std::string, std::string> values;
        for (const auto& key : frame.keys)
            if (const ModuleParam* param = const_cast<LoadedModule&>(module).param(key)) values[key] = param->live ? studio.module_get(track, key) : param->text();
        for (const auto& key : about.extra_keys) values[key] = studio.module_get(track, key);
        canvas->draw(g, values, frame);
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
