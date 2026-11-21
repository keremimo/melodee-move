// SPDX-License-Identifier: AGPL-3.0-only
// A synth track's Play screen: one of Schwung's sound generators (README,
// "How a Schwung module is hosted"), as the Move's Device View in the shape
// of Schwung's chain editor (Kerem, 2026-11-21; docs/UI.md, "A synth
// track"). Along the top its items: the track's mix, ARP and CHORD (Melodee's
// MIDI effects, before the sound as Schwung's MIDI FX boxes are), the module,
// its effects and the plus. The wheel walks them; the knobs are the item's
// (on the module, its box's knobs as in Schwung's chain editor). On the
// module the push enters its pages (screens/module_pages.cpp: Schwung's own
// page code, Melodee's look), Shift + push swaps the module, Delete + push
// removes it, Mute + push bypasses it. On ARP or CHORD the push shows the
// page whole, Back comes out; Mute + push turns it off and on. The mix and
// the effects are device_fx.h's. Pads are notes in rows of fourths, C2 at the
// bottom left; Plus and Minus move an octave; pressure is polyphonic
// aftertouch.
#include "app/screens/note_pages.h"
#include "app/screens/device_fx.h"
#include <juce_core/juce_core.h>
#include <limits>

namespace app::screens {
namespace {

constexpr int lowest_octave = -3, highest_octave = 4;
// The items before the effects: the track's mix, ARP, CHORD, the module.
constexpr int item_mix = 0, item_arp = 1, item_chord = 2, item_sound = 3, item_effects = 4;

// What a cell has room for: a module's short name, or its name.
std::string cell_name(const ModuleParam& param) { return upper(param.short_name.empty() ? param.name : param.short_name); }

class SynthPlay final : public Screen {
public:
    using Screen::Screen;

    void enter() override { held.fill(false); note_shown = -1; refresh(); }

    void input(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Pad && input.index < pad_count) {
            held[input.index] = input.pressed;
            if (input.pressed) last_pad = int(input.index);
        }
        fx.input(input);
    }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        auto* track = studio.module_track();
        if (!track) return false;
        auto effects = fx.track_items();
        focus = std::clamp(focus, 0, item_effects + int(effects.size()) - 1);
        if (note_shown < 0) {
            if (focus >= item_effects && fx.job(effects[size_t(focus - item_effects)], job, use)) { focus += fx.take_moved(); return true; }
            if (focus == item_mix && fx.mix_job(studio.track(), job, use)) return true;
        }
        switch (job) {
        case Job::PlayNote: return true; // its sound went from the audio thread
        case Job::TurnKnob: case Job::TurnKnobFine:
            if (const NotePage* page = note_page()) { note_page_turn(studio, studio.track(), *page, unsigned(use.index), use.delta); return true; }
            if (focus == item_sound) turn(*track, unsigned(use.index), use.delta, job == Job::TurnKnobFine);
            return true;
        case Job::Octave: studio.say(octave_or_bank(studio, use.control == bindings::Control::Plus ? 1 : -1, lowest_octave, highest_octave)); return true;
        // The Device View.
        case Job::Focus:
            if (note_shown >= 0) return true;
            focus = std::clamp(focus + (use.delta > 0 ? 1 : -1), 0, item_effects + int(effects.size()) - 1);
            if (focus == item_sound) refresh();
            return true;
        case Job::OpenBrowser: // push: into the item
            if (focus == item_arp || focus == item_chord) { note_shown = focus; return true; }
            if (focus == item_sound && track->module) {
                studio.pages_target = {studio.track(), melodee::no_plugin};
                studio.open("module-pages");
                return true;
            }
            studio.open("load-modules");
            return true;
        case Job::ModuleMenu: // Shift + push: the module swapped (Schwung's Shift + click), the loader's REPLACE asks first
            if (focus == item_arp || focus == item_chord) return true;
            studio.open("load-modules");
            return true;
        case Job::BypassEffect: // Mute + push
            if (focus == item_arp || focus == item_chord) { toggle_note_effect(focus); return true; }
            if (focus == item_sound && track->module) {
                bool off = !track->player.bypassed.load();
                track->player.bypassed.store(off);
                studio.say(off ? "BYPASSED" : "ON");
            }
            return true;
        case Job::RemoveEffect: // Delete + push
            if (focus == item_arp || focus == item_chord) { if (note_on(focus)) toggle_note_effect(focus); return true; }
            if (focus == item_sound && track->module) studio.remove_module({studio.track(), melodee::no_plugin});
            return true;
        case Job::Page: return true; // Shift + the wheel: the module's pages are entered with the push
        case Job::Back:
            if (note_shown >= 0) { note_shown = -1; return true; }
            return false;
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
        lights.button(Button::Back, note_shown >= 0 ? white_dim : 0);
    }

    void draw(juce::Graphics& g) override {
        auto* track = studio.module_track();
        auto effects = fx.track_items();
        if (studio.desk.show_effects) { // from the Mixer's push: the effects
            focus = item_effects;
            note_shown = -1;
            studio.desk.show_effects = false;
        }
        focus = std::clamp(focus, 0, item_effects + int(effects.size()) - 1);
        std::string abbrev = track && track->module ? upper(track->module->info().abbrev).substr(0, 4) : "SYN";
        if (const NotePage* page = note_page(); page && note_shown >= 0) { // ARP or CHORD, whole
            ui::rail(g, abbrev, track_number(studio), note_page_title(*page));
            draw_note_page(g, studio, studio.track(), *page, note_page_right(studio, studio.track(), *page));
            auto [left, more] = note_page_detail(studio, studio.track(), *page);
            ui::detail(g, left, more);
            return;
        }
        ui::rail(g, abbrev, track_number(studio), rail_bottom(effects));
        draw_row(g, track, effects);
        if (focus == item_mix) { fx.draw_mix(g, studio.track()); return; }
        if (focus >= item_effects) { fx.draw(g, effects[size_t(focus - item_effects)]); return; }
        if (const NotePage* page = note_page()) {
            draw_note_page(g, studio, studio.track(), *page, note_page_right(studio, studio.track(), *page));
            auto [left, more] = note_page_detail(studio, studio.track(), *page);
            ui::detail(g, left, more);
            return;
        }
        if (!track || !track->module) {
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, track && (track->loading || !track->job.empty()) ? "LOADING" : "NO SYNTH", 2);
            ui::detail(g, track && !track->saved.module.empty() ? upper(track->saved.module + " MISSING").substr(0, 16) : "PUSH: MODULES");
            return;
        }
        // The module's box: its name, the knobs Schwung's chain editor gives it.
        auto& module = *track->module;
        std::string title = upper(module.info().name).substr(0, 12), lowest = note_name(studio.note_for(0, studio.octave(studio.track())));
        move::draw_text(g, ui::content_x + 1, 11, title);
        move::draw_text(g, ui::width - int(lowest.size()) * move::glyph_advance, 11, lowest); // the bottom-left pad's note
        auto keys = module.chain_knobs();
        std::array<std::string, 8> names;
        std::array<float, 8> values {};
        for (unsigned n = 0; n < keys.size() && n < 8; ++n)
            if (const ModuleParam* param = module.param(keys[n])) {
                names[n] = cell_name(*param);
                values[n] = float(param->fraction());
            }
        ui::knob_cells(g, ui::content(21, ui::subject_height - 21), names, values);
        auto where = bar_beat(studio);
        if (keys_mode(studio.current()) == KeysMode::Drums && last_pad >= 0) { // the drum hit last, named
            auto drum = pad_notes(studio.project(), studio.current(), unsigned(last_pad));
            if (drum.count) { ui::detail(g, drum_name(drum.keys[0]).substr(0, 16)); return; }
        }
        if (track->player.bypassed.load()) { ui::detail(g, "BYPASSED", "PUSH: PAGES"); return; }
        if (!module.preset_param.empty()) {
            char number[16];
            std::snprintf(number, sizeof number, "%03d", std::clamp(module.preset + 1, 0, 999));
            ui::detail(g, upper(module.preset_name.empty() ? module.info().name : module.preset_name).substr(0, 12), where.empty() ? number : where);
        } else ui::detail(g, upper(module.info().name).substr(0, 12), where);
    }

    bool knob_lane(unsigned which, unsigned& param, unsigned& pad) const override {
        if (note_shown >= 0) return false;
        auto effects = fx.track_items();
        if (focus == item_mix) return fx.mix_lane(studio.track(), which, param, pad);
        pad = 0;
        return focus >= item_effects && size_t(focus - item_effects) < effects.size() && fx.lane(effects[size_t(focus - item_effects)], which, param);
    }
    Knob knob(unsigned which) const override {
        if (const NotePage* page = note_page()) return note_page_knob(studio, studio.track(), *page, which);
        auto effects = fx.track_items();
        if (focus == item_mix) return fx.mix_knob(studio.track(), which);
        if (focus >= item_effects) return size_t(focus - item_effects) < effects.size() ? fx.knob(effects[size_t(focus - item_effects)], which) : Knob {};
        auto* track = const_cast<Studio&>(studio).module_track();
        if (!track || !track->module) return {};
        auto keys = track->module->chain_knobs();
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
        static const char* names[] {"mix", "arp", "chord", "sound"};
        std::string item = focus < item_effects ? names[focus] : "fx" + std::to_string(focus - item_effects + 1);
        auto* track = const_cast<Studio&>(studio).module_track();
        return " item=" + item + " item_shown=" + std::to_string(int(note_shown >= 0)) +
               " bypassed=" + std::to_string(int(track && track->player.bypassed.load()));
    }

private:
    int focus = item_sound; // the Device View's item the wheel is on (item_mix ... the effects, the plus)
    int note_shown = -1;    // ARP or CHORD shown whole (the push), until Back
    DeviceFx fx {studio};
    int last_pad = -1; // Drums: the drum hit last, named

    const NotePage* note_page() const {
        static const NotePage pages[] {NotePage::Arp, NotePage::Chord};
        int at = note_shown >= 0 ? note_shown : focus;
        return at == item_arp || at == item_chord ? &pages[at - item_arp] : nullptr;
    }
    bool note_on(int item) const {
        const auto& t = studio.project().tracks[studio.track()];
        return item == item_arp ? t.arp : t.chord;
    }
    void toggle_note_effect(int item) { // its ON knob, turned
        NotePage page = item == item_arp ? NotePage::Arp : NotePage::Chord;
        note_page_turn(studio, studio.track(), page, 0, note_on(item) ? -1 : 1);
        studio.say(std::string(note_page_title(page)) + (note_on(item) ? " ON" : " OFF"));
    }

    std::string rail_bottom(const std::vector<DeviceFx::Item>& effects) const {
        switch (focus) {
        case item_mix: return "MIX";
        case item_arp: return "ARP";
        case item_chord: return "CHRD";
        case item_sound: return "SND";
        default: return fx.rail_bottom(effects[size_t(focus - item_effects)]);
        }
    }

    void draw_row(juce::Graphics& g, ModuleTrack* track, const std::vector<DeviceFx::Item>& effects) const {
        const auto& t = studio.project().tracks[studio.track()];
        std::vector<ui::mix::Item> row;
        row.push_back({track && track->module ? upper(track->module->info().abbrev).substr(0, 4) : "SYN"});
        row.push_back({"ARP", false, !t.arp});
        row.push_back({"CHRD", false, !t.chord});
        std::string sound = "----";
        if (track && track->module && !track->module->preset_param.empty()) {
            char number[8];
            std::snprintf(number, sizeof number, "%03d", std::clamp(track->module->preset + 1, 0, 999));
            sound = number;
        } else if (track && track->module) sound = upper(track->module->info().abbrev).substr(0, 4);
        row.push_back({sound, false, track && track->player.bypassed.load()});
        for (const auto& item : effects) row.push_back(fx.row_item(item));
        ui::mix::item_row(g, row, focus);
    }

    // Ask the module what its box's knobs are now.
    void refresh() {
        auto* track = studio.module_track();
        if (!track || !track->module) return;
        for (const auto& key : track->module->chain_knobs())
            if (!key.empty()) track->player.ask(key);
    }

    void turn(ModuleTrack& track, unsigned which, int delta, bool fine) {
        if (!track.module) return;
        auto keys = track.module->chain_knobs();
        if (which >= keys.size() || keys[which].empty()) return;
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

    std::array<bool, pad_count> held {}; // pads down, for their lights
};

RegisterScreen registered("synth-play", make<SynthPlay>, {}, kind_bit(TrackKind::Synth));

} // namespace
} // namespace app::screens
