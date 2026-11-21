// SPDX-License-Identifier: AGPL-3.0-only
// The mix and the effects (docs/UI.md, "Mixer and effects"; mockups in
// docs/ui/mixer.png; Kerem's decisions in docs/MOVE_CONTROLS.md):
//
//   fx-browser   the wheel's push on an effect or `+`: Melodee's families,
//                each effect loading as the wheel turns (the Move's
//                Autoload), Schwung's audio effects loaded on the push, NONE
//   fx-options   Shift + push on an effect: bypass, move, copy, paste, remove
//   mixer        Shift + Step 4: eight tracks' levels, pans or sends on the
//                knobs (the wheel the page, Left / Right 1-8 or 9-16), MAIN
//                the returns and the compressor; the steps the sixteen
//                tracks; the push the page's chain
//   chain        a bus's chain (its return first) or the master's (its
//                compressor last), as a Device View
//   knob-fx, pad-fx, flex   Shift + Step 4 again, and again, and again
#include "app/pad_layout.h"
#include "app/screens/common.h"
#include "app/screens/device_fx.h"
#include "melodee/engine/pad_fx_info.h"
#include <cmath>

namespace app::screens {
namespace {

using bindings::Job;
using melodee::EffectKind;
using melodee::Family;

// A chain's name on the rail: the track's number, a bus's letter, MAIN, a pad.
std::string chain_tag(const Studio& studio, unsigned chain) {
    if (mixing::is_pad_chain(chain)) return pad_label((chain - melodee::pad_chain_base) % melodee::sampler_pads);
    if (chain < melodee::track_count) return std::to_string(chain + 1);
    if (chain == mixing::master) return "MAIN";
    (void)studio;
    return std::string(1, char('A' + int(chain - melodee::track_count)));
}

// What the master compressor takes off now: "-4.5DB", "0.0DB".
std::string reduction_text(const Studio& studio) {
    float taken = studio.host().compression();
    char text[16];
    std::snprintf(text, sizeof text, "%.1fDB", taken < 0.05f ? 0.0 : -double(taken));
    return text;
}

// ---------------------------------------------------------------- the effect browser

class FxBrowser final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::Browser; }

    void enter() override {
        level = Families;
        loaded = false;
        rows_now();
        cursor = 0;
        const auto& slot = mixing::slot(studio, studio.desk.chain, studio.desk.slot);
        if (studio.desk.slot < mixing::used(studio, studio.desk.chain) && slot.kind != uint8_t(EffectKind::Plugin))
            cursor = int(melodee::effect_info(EffectKind(slot.kind)).family); // the family of the one there
    }

    bool job(Job job, const bindings::Use& use) override {
        switch (job) {
        case Job::BrowseTurn: {
            int count = int(rows.size());
            if (!count) return true;
            cursor = std::clamp(cursor + use.delta, 0, count - 1);
            if (level == Effects) load(kinds[size_t(cursor)]); // Autoload
            return true;
        }
        case Job::BrowsePush: push(); return true;
        case Job::Back:
            if (level != Families) { cursor = level == Schwung ? schwung_row : family; level = Families; rows_now(); return true; }
            studio.close();
            return true;
        case Job::PlayPad: return false; // the pad plays and is the last pad (the Play screen's)
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        if (auto* play = studio.screen(home_screen(studio.kind()))) play->lights(lights);
        lights.button(move::Button::Back, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "LOAD", chain_tag(studio, studio.desk.chain), "FX" + std::to_string(studio.desk.slot + 1));
        bool foot = level != Families;
        ui::list(g, ui::content(0, foot ? 50 : ui::height), rows, cursor, 1);
        if (level == Effects) ui::detail(g, upper(melodee::family_name(Family(family))).substr(0, 8), "PUSH: OK");
        if (level == Schwung) ui::detail(g, rows.empty() ? "NONE INSTALLED" : "", rows.empty() ? "" : "PUSH: LOAD");
    }

    std::string status() const override { return " fx_level=" + std::to_string(int(level)) + " fx_row=" + std::to_string(cursor); }

private:
    enum Level { Families, Effects, Schwung } level = Families;
    std::vector<std::string> rows;
    std::vector<EffectKind> kinds;
    std::vector<std::string> ids;
    int cursor = 0, family = 0, schwung_row = 0, none_row = 0;
    bool loaded = false; // something was put in the slot here: the push leaves

    bool pad_chain() const { return mixing::is_pad_chain(studio.desk.chain); }

    void rows_now() {
        rows.clear();
        kinds.clear();
        ids.clear();
        switch (level) {
        case Families:
            for (unsigned f = 0; f < unsigned(Family::Plugins); ++f) rows.push_back(upper(melodee::family_name(Family(f))) + " >");
            schwung_row = int(rows.size());
            if (!pad_chain()) rows.push_back("SCHWUNG >"); // a pad's chain takes no plug-in, as in Melodee
            none_row = int(rows.size());
            rows.push_back("NONE");
            break;
        case Effects: {
            melodee::FamilyList list {};
            unsigned count = melodee::family_effects(Family(family), list);
            for (unsigned n = 0; n < count; ++n) {
                if (!melodee::effect_info(list[n]).built) continue;
                kinds.push_back(list[n]);
                rows.push_back(mixing::long_name(list[n]));
            }
            break;
        }
        case Schwung:
            studio.rescan_fx();
            for (const auto& info : studio.fx_catalog) {
                ids.push_back(info.id);
                rows.push_back(upper(info.name).substr(0, 16));
            }
            break;
        }
    }

    void load(EffectKind kind) {
        mixing::set_effect(studio, studio.desk.chain, studio.desk.slot, kind);
        studio.desk.slot = std::min(studio.desk.slot, mixing::used(studio, studio.desk.chain) - 1); // where it went
        loaded = true;
    }

    void push() {
        switch (level) {
        case Families:
            if (cursor == none_row) {
                if (studio.desk.slot < mixing::used(studio, studio.desk.chain)) mixing::set_effect(studio, studio.desk.chain, studio.desk.slot, EffectKind::None);
                studio.close();
                return;
            }
            if (!pad_chain() && cursor == schwung_row) { level = Schwung; rows_now(); cursor = 0; return; }
            family = cursor;
            level = Effects;
            rows_now();
            cursor = 0;
            {   // On the effect already there, when it is of this family.
                const auto& slot = mixing::slot(studio, studio.desk.chain, studio.desk.slot);
                for (size_t n = 0; n < kinds.size(); ++n) if (uint8_t(kinds[n]) == slot.kind) cursor = int(n);
            }
            return;
        case Effects:
            if (!kinds.empty() && (!loaded || mixing::slot(studio, studio.desk.chain, studio.desk.slot).kind != uint8_t(kinds[size_t(cursor)])))
                load(kinds[size_t(cursor)]);
            studio.close();
            return;
        case Schwung:
            if (ids.empty()) return;
            if (!studio.load_fx(studio.desk.chain, studio.desk.slot, ids[size_t(cursor)])) studio.say("NO ROOM");
            studio.close();
            return;
        }
    }
};

// ---------------------------------------------------------------- an effect's options

class FxOptions final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override { cursor = 0; }

    bool job(Job job, const bindings::Use& use) override {
        auto& desk = studio.desk;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: cursor = std::clamp(cursor + use.delta, 0, int(rows().size()) - 1); return true;
        case Job::ListPush:
            switch (cursor) {
            case 0: mixing::toggle_bypass(studio, desk.chain, desk.slot); return true; // it stays open, to hear the difference
            case 1: case 2: {
                int way = cursor == 1 ? -1 : 1;
                int to = int(desk.slot) + way;
                if (to < 0 || unsigned(to) >= mixing::used(studio, desk.chain)) { studio.say("NO FURTHER"); return true; }
                mixing::move_effect(studio, desk.chain, desk.slot, way);
                desk.slot = unsigned(to);
                studio.say(way < 0 ? "MOVED LEFT" : "MOVED RIGHT");
                break;
            }
            case 3: mixing::copy_effect(studio, desk.chain, desk.slot); studio.say("COPIED"); break;
            case 4: studio.say(mixing::paste_effect(studio, desk.chain, desk.slot) ? "PASTED" : "NOTHING TO PASTE"); break;
            case 5: mixing::set_effect(studio, desk.chain, desk.slot, EffectKind::None); studio.say("REMOVED"); break;
            }
            studio.close();
            return true;
        case Job::Back: studio.close(); return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        if (auto* play = studio.screen(home_screen(studio.kind()))) play->lights(lights);
        lights.button(move::Button::Back, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        const auto& slot = mixing::slot(studio, studio.desk.chain, studio.desk.slot);
        std::string name = mixing::short_name(EffectKind(slot.kind));
        if (slot.kind == uint8_t(EffectKind::Plugin))
            if (auto* fx = studio.fx_module(uint8_t(slot.params[0])); fx && fx->module) name = upper(fx->module->info().abbrev).substr(0, 4);
        ui::rail(g, "OPT", chain_tag(studio, studio.desk.chain), name);
        ui::list(g, ui::content(0, ui::height), rows(), cursor, 1);
    }

private:
    int cursor = 0;
    std::vector<std::string> rows() const {
        bool off = mixing::slot(studio, studio.desk.chain, studio.desk.slot).bypass;
        return {std::string("BYPASS       ") + (off ? " ON" : "OFF"), "MOVE LEFT", "MOVE RIGHT", "COPY", "PASTE", "REMOVE"};
    }
};

// ---------------------------------------------------------------- the Mixer

// The sixteen tracks on the steps: each its colour, the selected one white,
// a muted one grey, a soloed one blue, an empty one dark.
void track_steps(const Studio& studio, move::Lights& lights) {
    for (unsigned t = 0; t < move::step_count; ++t) {
        const auto& mix = studio.project().tracks[t].mix;
        lights.step(t, studio.kind(t) == TrackKind::Empty ? move::colour::off : t == studio.track() ? move::colour::white
                     : mix.solo ? move::colour::blue : mix.mute ? move::colour::dark_grey : ui::track_colour(t).bright);
    }
}

// The pads still play the track: the Play screen's lights under ours.
void play_lights(Studio& studio, move::Lights& lights) {
    if (auto* play = studio.screen(home_screen(studio.kind()))) play->lights(lights);
}

class Mixer final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::Mixer; }
    void enter() override { half = studio.track() / 8; }

    bool job(Job job, const bindings::Use& use) override {
        switch (job) {
        case Job::MixerPage: page = std::clamp(page + use.delta, 0, pages - 1); return true;
        case Job::MixerHalf: half = use.control == bindings::Control::Left ? 0 : 1; return true;
        case Job::TurnKnob: case Job::TurnKnobFine: turn(unsigned(use.index), use.delta); return true;
        case Job::PickTrack:
            studio.select(unsigned(use.index));
            studio.open(own_page()); // select() goes home: back here
            return true;
        case Job::OpenChain:
            if (page < 2) { studio.desk.show_effects = true; studio.home(); return true; } // the track's Device View, on its effects
            studio.desk.chain = page == 6 ? mixing::master : mixing::bus(unsigned(page - 2));
            studio.push("chain");
            return true;
        case Job::Mixer: studio.open("knob-fx"); return true; // Shift + Step 4 again
        case Job::Back: studio.close(); return true;
        default:
            return (use.held & bindings::held::step) || job == Job::ToggleStep; // the steps are the tracks here
        }
    }

    void lights(move::Lights& lights) override {
        play_lights(studio, lights);
        track_steps(studio, lights);
        lights.button(move::Button::Left, half ? move::white_dim : move::white_bright);
        lights.button(move::Button::Right, half ? move::white_bright : move::white_dim);
        lights.button(move::Button::Back, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        unsigned first = unsigned(half) * 8;
        ui::rail(g, "MIX", page == 6 ? "" : track_number(studio), page == 6 ? "MAIN" : half ? "9-16" : "1-8");
        std::vector<ui::mix::Item> hub {{"VOL"}, {"PAN"}, {"SNDA"}, {"SNDB"}, {"SNDC"}, {"SNDD"}, {"MAIN"}};
        ui::mix::item_row(g, hub, page);
        if (page == 6) { draw_main(g); return; }
        std::vector<ui::mix::Strip> list;
        for (unsigned t = first; t < first + 8; ++t) list.push_back(strip(t));
        int selected = int(studio.track()) - int(first);
        if (page >= 2) { // a send page: the bus's return last
            unsigned b = unsigned(page - 2);
            ui::mix::Strip back;
            back.label = std::string(1, char('A' + int(b)));
            back.value = studio.project().buses[b].level / 100.0f;
            list.push_back(back);
        }
        ui::mix::strips(g, ui::content(10, 44), list, selected >= 0 && selected < 8 ? selected : -1);
        const auto& track = studio.current();
        if (page >= 2) {
            unsigned b = unsigned(page - 2);
            ui::detail(g, "RTN " + std::to_string(studio.project().buses[b].level), "PUSH: FX");
        } else {
            ui::detail(g, upper(track.name).substr(0, 9), page == 0 ? mixing::level_text(track.mix.level) : mixing::pan_text(track.mix.pan));
        }
    }

    Knob knob(unsigned which) const override {
        if (page == 6) {
            if (which < 4) return {std::string("RETURN ") + char('A' + int(which)), std::to_string(studio.project().buses[which].level),
                                   studio.project().buses[which].level / 100.0f};
            if (which == 7) return {"COMP AMOUNT", std::to_string(studio.project().compressor.amount) + "%", studio.project().compressor.amount / 100.0f};
            return {};
        }
        unsigned t = unsigned(half) * 8 + which;
        if (studio.kind(t) == TrackKind::Empty) return {};
        const auto& track = studio.project().tracks[t];
        std::string who = std::to_string(t + 1) + " " + upper(track.name).substr(0, 6) + " ";
        if (page == 0) return {who + "LEVEL", mixing::level_text(track.mix.level), track.mix.level / 127.0f};
        if (page == 1) return {who + "PAN", mixing::pan_text(track.mix.pan), (track.mix.pan + 50) / 100.0f};
        unsigned b = unsigned(page - 2);
        return {who + ">" + char('A' + int(b)), std::to_string(track.mix.sends[b]), track.mix.sends[b] / 100.0f};
    }
    std::string knob_owner() const override {
        static const char* const names[] {"VOL", "PAN", "SNDA", "SNDB", "SNDC", "SNDD", "MAIN"};
        return names[page];
    }
    std::string status() const override { return " mixer_page=" + std::to_string(page) + " mixer_half=" + std::to_string(half); }

private:
    static constexpr int pages = 7; // VOL, PAN, SNDA-SNDD, MAIN
    int page = 0, half = 0;

    std::string own_page() const { return "mixer"; }

    ui::mix::Strip strip(unsigned t) const {
        const auto& mix = studio.project().tracks[t].mix;
        ui::mix::Strip s;
        s.label = std::to_string(t + 1);
        s.used = studio.kind(t) != TrackKind::Empty;
        s.muted = mix.mute;
        s.soloed = mix.solo;
        s.meter = meter_fraction(studio.host().meter(t));
        if (page == 1) { s.bipolar = true; s.value = mix.pan / 50.0f; }
        else s.value = page == 0 ? mix.level / 127.0f : mix.sends[unsigned(page - 2)] / 100.0f;
        return s;
    }

    void turn(unsigned which, int delta) {
        if (page == 6) {
            if (which < 4) mixing::set_return(studio, which, int(studio.project().buses[which].level) + delta);
            else if (which == 7) mixing::set_compressor(studio, mixing::CompAmount, int(studio.project().compressor.amount) + delta);
            return;
        }
        unsigned t = unsigned(half) * 8 + which;
        if (studio.kind(t) == TrackKind::Empty) return;
        const auto& mix = studio.project().tracks[t].mix;
        if (page == 0) studio.set_level(t, int(mix.level) + delta);
        else if (page == 1) mixing::set_pan(studio, t, mix.pan + delta);
        else mixing::set_send(studio, t, unsigned(page - 2), int(mix.sends[unsigned(page - 2)]) + delta);
    }

    void draw_main(juce::Graphics& g) {
        std::vector<ui::mix::Strip> list;
        for (unsigned b = 0; b < melodee::bus_count; ++b) {
            ui::mix::Strip s;
            s.label = std::string(1, char('A' + int(b)));
            s.value = studio.project().buses[b].level / 100.0f;
            list.push_back(s);
        }
        ui::mix::strips(g, {ui::content_x, 10, 60, 44}, list, -1);
        // What the compressor takes off, from the top; the main out, left and right.
        float reduction = std::clamp(studio.host().compression() / 12.0f, 0.0f, 1.0f);
        move::draw_text(g, 88, 47, "GR");
        g.drawRect(89, 18, 7, 27);
        g.fillRect(89, 18, 7, int(std::lround(reduction * 27.0f)));
        float volume = studio.volume.load();
        for (unsigned side = 0; side < 2; ++side) {
            int h = int(std::lround(meter_fraction(studio.host().out_level(side) * volume) * 24.0f));
            g.fillRect(105 + int(side) * 7, 45 - h, 5, h);
        }
        move::draw_text(g, 105, 47, "L R");
        ui::detail(g, studio.project().compressor.on ? reduction_text(studio) : "OFF", "PUSH: FX");
    }
};

// ---------------------------------------------------------------- a bus's or the master's chain

class Chain final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::Play; } // a Device View
    void enter() override { focus = 0; history.assign(size_t(ui::content_width), 0.0f); }
    void input(const move::Input& input) override { fx.input(input); }

    bool job(Job job, const bindings::Use& use) override {
        auto all = items();
        focus = std::min(focus, int(all.size()) - 1);
        const Entry& at = all[size_t(focus)];
        if (at.kind == Entry::Effect && fx.job(at.item, job, use)) return true;
        switch (job) {
        case Job::Focus: focus = std::clamp(focus + (use.delta > 0 ? 1 : -1), 0, int(all.size()) - 1); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: turn(at, unsigned(use.index), use.delta); return true;
        case Job::OpenBrowser: case Job::SampleOptions: case Job::ModuleMenu: return true;
        case Job::Mixer: studio.open("knob-fx"); return true;
        case Job::Back: studio.close(); return true;
        case Job::ShowFocus: studio.say(name_of(at)); return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        play_lights(studio, lights);
        lights.button(move::Button::Back, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        auto all = items();
        focus = std::min(focus, int(all.size()) - 1);
        const Entry& at = all[size_t(focus)];
        unsigned chain = studio.desk.chain;
        bool main = chain == mixing::master;
        std::string bottom = at.kind == Entry::Head ? "RTN" : at.kind == Entry::Comp ? "COMP" : fx.rail_bottom(at.item);
        ui::rail(g, main ? "MAIN" : "BUS", main ? "" : chain_tag(studio, chain), bottom);
        std::vector<ui::mix::Item> row;
        for (const auto& entry : all) row.push_back(entry.kind == Entry::Effect ? fx.row_item(entry.item) : ui::mix::Item {name_of(entry)});
        ui::mix::item_row(g, row, focus);
        if (at.kind == Entry::Effect) { fx.draw(g, at.item); return; }
        if (at.kind == Entry::Head) {
            unsigned b = chain - melodee::track_count;
            int level = studio.project().buses[b].level;
            ui::big_parameter(g, ui::content(12, 34), "RETURN", std::to_string(level), level / 100.0f);
            std::string from = "FROM";
            for (unsigned t = 0; t < melodee::track_count; ++t)
                if (studio.project().tracks[t].mix.sends[b] && studio.kind(t) != TrackKind::Empty && from.size() < 14) from += " " + std::to_string(t + 1);
            ui::detail(g, from.size() > 4 ? from : "NOTHING SENDS");
            return;
        }
        draw_comp(g);
    }

    Knob knob(unsigned which) const override {
        auto all = const_cast<Chain*>(this)->items();
        const Entry& at = all[size_t(std::min(focus, int(all.size()) - 1))];
        if (at.kind == Entry::Effect) return fx.knob(at.item, which);
        if (at.kind == Entry::Head) {
            unsigned b = studio.desk.chain - melodee::track_count;
            return which == 0 ? Knob {"RETURN", std::to_string(studio.project().buses[b].level), studio.project().buses[b].level / 100.0f} : Knob {};
        }
        const auto& c = studio.project().compressor;
        switch (which) {
        case 0: return {"ATTACK", std::to_string(c.attack), c.attack / 100.0f};
        case 1: return {"RELEASE", std::to_string(c.release), c.release / 100.0f};
        case 2: return {"AMOUNT", std::to_string(c.amount) + "%", c.amount / 100.0f};
        case 3: return {"IN BOOST", std::to_string(c.boost) + " DB", c.boost / 24.0f};
        case 4: return {"COLOR", c.color ? "ON" : "OFF", c.color ? 1.0f : 0.0f};
        case 5: return {"COMPRESSOR", c.on ? "ON" : "OFF", c.on ? 1.0f : 0.0f};
        case 6: return {"SIDECHAIN", c.sidechain ? "TRACK " + std::to_string(c.key + 1) : "OFF", c.sidechain ? (c.key + 1) / 16.0f : 0.0f};
        case 7: return {"KEY PAD", c.key_pad == melodee::MasterComp::whole_kit ? "WHOLE KIT" : pad_label(c.key_pad % 32u),
                        c.key_pad == melodee::MasterComp::whole_kit ? 0.0f : (c.key_pad + 1) / 32.0f};
        default: return {};
        }
    }
    std::string knob_owner() const override { return studio.desk.chain == mixing::master ? "MAIN" : "BUS"; }
    std::string status() const override { return " chain=" + chain_tag(studio, studio.desk.chain) + " chain_focus=" + std::to_string(focus); }

private:
    struct Entry { enum Kind { Head, Effect, Comp } kind = Effect; DeviceFx::Item item {}; };
    int focus = 0;
    DeviceFx fx {studio};
    std::vector<float> history;

    std::vector<Entry> items() {
        std::vector<Entry> out;
        unsigned chain = studio.desk.chain;
        if (chain != mixing::master) out.push_back({Entry::Head, {}});
        for (const auto& item : fx.chain_items(chain)) out.push_back({Entry::Effect, item});
        if (chain == mixing::master) out.push_back({Entry::Comp, {}});
        return out;
    }
    std::string name_of(const Entry& entry) const {
        if (entry.kind == Entry::Head) return chain_tag(studio, studio.desk.chain);
        if (entry.kind == Entry::Comp) return "COMP";
        return fx.row_item(entry.item).name;
    }

    void turn(const Entry& at, unsigned which, int delta) {
        if (at.kind == Entry::Head) {
            unsigned b = studio.desk.chain - melodee::track_count;
            if (which == 0) mixing::set_return(studio, b, int(studio.project().buses[b].level) + delta);
            return;
        }
        if (at.kind != Entry::Comp) return;
        using namespace mixing;
        const auto& c = studio.project().compressor;
        switch (which) {
        case 0: set_compressor(studio, CompAttack, c.attack + delta); break;
        case 1: set_compressor(studio, CompRelease, c.release + delta); break;
        case 2: set_compressor(studio, CompAmount, c.amount + delta); break;
        case 3: set_compressor(studio, CompBoost, c.boost + delta); break;
        case 4: set_compressor(studio, CompColor, delta > 0); break;
        case 5: set_compressor(studio, CompOn, delta > 0); break;
        case 6: { // OFF, then tracks 1-16
            int key = c.sidechain ? c.key + 1 : 0;
            key = std::clamp(key + (delta > 0 ? 1 : -1), 0, int(melodee::track_count));
            if (key == 0) set_compressor(studio, CompSidechain, 0);
            else { set_compressor(studio, CompKey, key - 1); set_compressor(studio, CompSidechain, 1); }
            break;
        }
        case 7: { // the whole kit, then its pads
            int pad = c.key_pad == melodee::MasterComp::whole_kit ? -1 : c.key_pad;
            set_compressor(studio, CompKeyPad, std::clamp(pad + (delta > 0 ? 1 : -1), -1, int(pad_count) - 1));
            break;
        }
        default: break;
        }
    }

    // The compressor's picture: what it took off over the bar, the beats
    // along the foot (the bar as it plays, else scrolling).
    void draw_comp(juce::Graphics& g) {
        float level_db = 0, reduce_db = 0;
        studio.host().compressor_activity(level_db, reduce_db);
        float taken = std::clamp(studio.host().compression() / 12.0f, 0.0f, 1.0f);
        int now = -1;
        if (studio.playing()) {
            unsigned bar = std::max(1u, studio.project().bar_ticks());
            now = int(studio.host().ticks() % bar * unsigned(history.size()) / bar);
            history[size_t(now)] = taken;
        } else {
            std::rotate(history.begin(), history.begin() + 1, history.end());
            history.back() = taken;
        }
        unsigned beats = std::max(1u, studio.project().bar_ticks() / std::max(1u, studio.project().beat_ticks()));
        ui::mix::reduction(g, ui::content(11, 40), history, int(beats), now);
        const auto& c = studio.project().compressor;
        ui::detail(g, !c.on ? "OFF" : c.sidechain ? "KEY " + std::to_string(c.key + 1) : "", reduction_text(studio));
    }
};

// ---------------------------------------------------------------- Knob FX

class KnobFx final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::KnobFx; }
    void enter() override {
        auto& k = studio.project().knob_fx;
        browse = k.choice;
        if (!k.on) mixing::knob_fx_on(studio, true); // the mode on, as the MPC's Knob FX button lit
    }

    bool job(Job job, const bindings::Use& use) override {
        switch (job) {
        case Job::KnobFxChoose: browse = (browse + int(melodee::knob_fx_count()) + (use.delta > 0 ? 1 : -1)) % int(melodee::knob_fx_count()); return true;
        case Job::KnobFxLoad: mixing::knob_fx_load(studio, unsigned(browse)); return true;
        case Job::KnobFxAll: studio.say(mixing::knob_fx_all(studio) ? "ALL TRACKS" : "NO TRACKS"); return true;
        case Job::BypassEffect: mixing::knob_fx_bypass(studio); return true;
        case Job::KnobFxTrack: if (studio.kind(unsigned(use.index)) != TrackKind::Empty) mixing::knob_fx_track(studio, unsigned(use.index)); return true;
        case Job::KnobFxPad: mixing::knob_fx_pad(studio, studio.track(), logical_pad(uint8_t(use.index), pad_layout())); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: {
            unsigned which = unsigned(use.index);
            if (which < 6) mixing::knob_fx_turn(studio, which, use.delta, job == Job::TurnKnobFine);
            else if (which == 6) mixing::knob_fx_edit(studio, use.delta > 0 ? 1 : 0);
            else mixing::knob_fx_turn(studio, melodee::knob_morph, use.delta, job == Job::TurnKnobFine);
            return true;
        }
        case Job::Mixer: studio.open("pad-fx"); return true;
        case Job::Back: studio.close(); return true;
        default: return (use.held & bindings::held::step) || job == Job::ToggleStep;
        }
    }

    void lights(move::Lights& lights) override {
        play_lights(studio, lights);
        const auto& k = studio.project().knob_fx;
        for (unsigned t = 0; t < move::step_count; ++t)
            lights.step(t, studio.kind(t) == TrackKind::Empty ? move::colour::off : k.track_on(t) ? ui::track_colour(t).bright : ui::track_colour(t).dim);
        if (studio.kind() == TrackKind::Kit) {
            const auto& track = studio.current();
            for (unsigned at = 0; at < pad_count; ++at)
                lights.pad(physical_pad(uint8_t(at), pad_layout()), k.pad_on(studio.track(), at) ? move::colour::white : track.pads[at].used() ? ui::track_colour(studio.track()).dim : move::colour::off);
        }
        lights.button(move::Button::Back, move::white_dim);
        lights.button(move::Button::Mute, k.bypass ? move::white_bright : move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        const auto& k = studio.project().knob_fx;
        ui::rail(g, "KFX", k.editing ? "B" : "A", !k.on ? "OFF" : k.bypass ? "BYP" : "ON");
        bool loaded = browse == k.choice && k.kind;
        std::string title = upper(melodee::knob_fx_entry(unsigned(browse)).name);
        move::draw_text(g, ui::content_x + 1, 1, (loaded ? "" : ">") + title.substr(0, 14));
        move::draw_text(g, ui::width - move::text_width(loaded ? "EDIT A" : "PUSH") - 1, 1, loaded ? (k.editing ? "EDIT B" : "EDIT A") : "PUSH");
        g.fillRect(ui::content_x, 9, ui::content_width, 1);
        std::array<std::string, 8> names;
        std::array<float, 8> values {};
        const auto& info = melodee::effect_info(EffectKind(k.kind));
        for (unsigned q = 0; q < melodee::effect_params && k.kind; ++q)
            if (info.params[q].label) {
                names[q] = info.params[q].label;
                values[q] = mixing::param_fraction(EffectKind(k.kind), q, q < 3 ? k.setting()[q] : k.params[q]);
            }
        names[6] = "A/B";
        values[6] = k.editing ? 1.0f : 0.0f;
        names[7] = "A>B";
        values[7] = k.morph / 127.0f;
        ui::knob_cells(g, ui::content(12, ui::height - 12), names, values);
    }

    Knob knob(unsigned which) const override {
        const auto& k = studio.project().knob_fx;
        if (which == 6) return {"EDITING", k.editing ? "B" : "A", k.editing ? 1.0f : 0.0f};
        if (which == 7) return {"A TO B", std::to_string(k.morph * 100 / 127) + "%", k.morph / 127.0f};
        const auto& info = melodee::effect_info(EffectKind(k.kind));
        if (!k.kind || which >= melodee::effect_params || !info.params[which].label) return {};
        int v = which < 3 ? k.setting()[which] : k.params[which];
        return {std::string(info.params[which].label) + (which < 3 ? (k.editing ? " B" : " A") : ""), mixing::param_text(EffectKind(k.kind), which, v),
                mixing::param_fraction(EffectKind(k.kind), which, v)};
    }
    std::string knob_owner() const override { return "KFX"; }
    std::string status() const override {
        const auto& k = studio.project().knob_fx;
        return " knob_fx=" + std::to_string(k.choice) + " knob_fx_on=" + std::to_string(int(k.active()));
    }

private:
    int browse = 0;
};

// ---------------------------------------------------------------- Pad FX

class PadFx final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::PadFx; }
    void enter() override { mixing::pads_are(studio, mixing::PadsAre::Punches); }
    void leave() override { mixing::pads_are(studio, mixing::PadsAre::Sound); spans.clear(); }

    void input(const move::Input& input) override {
        // The take, as Melodee keeps it (model/pattern.h PunchSpan): each punch
        // from its press to its release, in the sequencer's ticks.
        if (input.kind != move::Input::Kind::Pad || input.index >= 32 || studio.top() != this) return;
        int kind = mixing::punch_on(mixing::punch_bank(studio), logical_pad(input.index, pad_layout()));
        if (kind < 0 || !studio.playing()) return;
        uint64_t now = studio.host().ticks();
        if (input.pressed) {
            if (spans.size() >= 64) spans.erase(spans.begin());
            const auto& p = studio.project();
            melodee::PunchSpan span;
            span.from = now;
            span.kind = uint8_t(kind);
            span.target = uint8_t(p.punch_target ? studio.track() : 255);
            span.depth = p.punch_depth;
            span.snap = p.punch_snap;
            span.tail = p.punch_tail;
            spans.push_back(span);
            last_kind = kind;
        } else {
            for (auto& span : spans) if (span.kind == uint8_t(kind) && !span.until) span.until = std::max(now, span.from + 1);
        }
    }

    bool job(Job job, const bindings::Use& use) override {
        switch (job) {
        case Job::Punch: return true; // the audio thread punched it
        case Job::PunchBank: mixing::punch_bank(studio, use.delta ? use.delta : 1); return true;
        case Job::KeepPunches: keep(); return true;
        case Job::PunchOptions: studio.push("pad-fx-options"); return true;
        case Job::ClearKept: clear_kept(logical_pad(uint8_t(use.index), pad_layout())); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: turn(unsigned(use.index), use.delta); return true;
        case Job::Mixer: studio.open("flex"); return true;
        case Job::Back: studio.close(); return true;
        default: return (use.held & bindings::held::step) || job == Job::ToggleStep;
        }
    }

    void lights(move::Lights& lights) override {
        unsigned bank = mixing::punch_bank(studio);
        uint32_t sounding = studio.desk.perform.sounding.load(std::memory_order_relaxed);
        for (unsigned pad = 0; pad < 32; ++pad) {
            int kind = mixing::punch_on(bank, pad);
            lights.pad(physical_pad(uint8_t(pad), pad_layout()), kind < 0 ? move::colour::off : (sounding >> pad) & 1 ? move::colour::white
                           : melodee::punch_is_mpc(melodee::PunchKind(kind)) ? move::colour::orange : move::colour::cyan);
        }
        // Where the kept punches fall in the bar, the playhead green.
        int step = playing_step(studio);
        std::array<bool, 16> kept {};
        for (const auto& p : pattern().punches) {
            for (uint32_t t = p.tick; t < p.tick + p.length; t += melodee::sixteenth_ticks)
                kept[t / melodee::sixteenth_ticks % 16] = true;
        }
        for (unsigned s = 0; s < move::step_count; ++s)
            lights.step(s, int(s) == step ? move::colour::green : kept[s] ? move::colour::orange : move::colour::off);
        for (auto b : {move::Button::Up, move::Button::Down, move::Button::Back}) lights.button(b, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        const auto& p = studio.project();
        ui::rail(g, "PFX", std::to_string(mixing::punch_bank(studio) + 1), p.punch_target ? "TRK" : "MST");
        std::string held;
        uint32_t sounding = studio.desk.perform.sounding.load(std::memory_order_relaxed);
        unsigned bank = mixing::punch_bank(studio);
        for (unsigned pad = 0; pad < 32; ++pad)
            if ((sounding >> pad) & 1) {
                int kind = mixing::punch_on(bank, pad);
                if (kind >= 0) held += (held.empty() ? "" : " + ") + upper(melodee::punch_name(melodee::PunchKind(kind)));
            }
        move::draw_text(g, ui::content_x + 1, 1, (held.empty() ? std::string("HOLD A PAD") : held).substr(0, 16));
        std::vector<ui::mix::Lane> lanes;
        for (const auto& kept : pattern().punches) {
            int from = int(kept.tick / melodee::sixteenth_ticks % 16);
            lanes.push_back({from, std::min(16, from + std::max(1, int(kept.length / melodee::sixteenth_ticks))), true});
        }
        if (studio.playing()) { // this pass's take
            uint64_t now = studio.host().ticks();
            for (const auto& span : spans) {
                uint64_t until = span.until ? span.until : now;
                if (until + 16 * melodee::sixteenth_ticks < now) continue;
                int from = int(span.from / melodee::sixteenth_ticks % 16), to = int(until / melodee::sixteenth_ticks % 16) + 1;
                lanes.push_back({from, std::max(from + 1, to), false});
            }
        }
        if (lanes.size() > 6) lanes.erase(lanes.begin(), lanes.end() - 6);
        ui::mix::punch_lanes(g, ui::content(12, 40), lanes, playing_step(studio));
        static const char* const snaps[] {"Q1/4", "Q1/8", "Q1/16", "Q1/32"};
        ui::detail(g, p.punch_quantize ? snaps[p.punch_snap % 4] : "Q OFF", "PUSH: KEEP");
    }

    Knob knob(unsigned which) const override {
        const auto& p = studio.project();
        if (which < 3) {
            if (last_kind < 0 || !melodee::punch_is_mpc(melodee::PunchKind(last_kind))) return {};
            unsigned mpc = unsigned(last_kind) - melodee::punch_mpc_first;
            const auto& param = melodee::mpc_pad_fx(mpc).params[which];
            if (!param.label) return {};
            auto shown = melodee::show(param, p.pad_fx_params[mpc][which]);
            return {param.label, upper(shown.value + shown.unit), melodee::gauge_of(param, p.pad_fx_params[mpc][which])};
        }
        if (which == 3) return {"DEPTH", std::to_string(p.punch_depth) + "%", p.punch_depth / 100.0f};
        if (which == 4) return {"SNAP", melodee::punch_snaps[p.punch_snap % 4], p.punch_snap / 3.0f};
        if (which == 5) return {"TAIL", upper(melodee::punch_tails[p.punch_tail % 3]), p.punch_tail / 2.0f};
        return {};
    }
    std::string knob_owner() const override { return "PFX"; }
    std::string status() const override {
        return " punch_bank=" + std::to_string(mixing::punch_bank(studio)) + " kept=" + std::to_string(pattern().punches.size())
             + " sounding=" + std::to_string(studio.desk.perform.sounding.load());
    }

private:
    std::vector<melodee::PunchSpan> spans; // this take
    int last_kind = -1;

    const melodee::Pattern& pattern() const {
        unsigned t = studio.track();
        return const_cast<Studio&>(studio).patterns().at[t][studio.host().pattern_of(t) % melodee::pattern_count];
    }

    void send_pattern(unsigned t, unsigned p) {
        melodee::Command c {melodee::Command::Type::PatternData, uint8_t(t), uint8_t(p)};
        c.data = new melodee::Pattern(melodee::played_pattern(studio.patterns().at[t][p], studio.project().tracks[t].speed)); // freed when handed back
        if (!studio.send(c)) delete c.data;
        studio.changed();
    }

    // KEEP: the take laid into the pattern the track plays, where each punch fell.
    void keep() {
        if (!studio.playing() || spans.empty()) { studio.say(spans.empty() ? "PLAY, PUNCH, KEEP" : "PLAY FIRST"); return; }
        unsigned t = studio.track(), p = studio.host().pattern_of(t) % melodee::pattern_count;
        auto& pattern_now = studio.patterns().at[t][p];
        int position = studio.host().position_of(t);
        auto heard = melodee::heard_punches(spans.data(), unsigned(spans.size()), studio.host().ticks(), uint32_t(std::max(position, 0)), pattern_now.length);
        for (const auto& punch : heard)
            if (pattern_now.punches.size() < melodee::max_pattern_punches) pattern_now.punches.push_back(punch);
        spans.clear();
        send_pattern(t, p);
        studio.say("KEPT " + std::to_string(heard.size()));
    }

    void clear_kept(unsigned pad) {
        int kind = mixing::punch_on(mixing::punch_bank(studio), pad);
        if (kind < 0) return;
        unsigned t = studio.track(), p = studio.host().pattern_of(t) % melodee::pattern_count;
        auto& punches = studio.patterns().at[t][p].punches;
        auto before = punches.size();
        punches.erase(std::remove_if(punches.begin(), punches.end(), [kind](const melodee::PatternPunch& one) { return one.kind == uint8_t(kind); }),
                      punches.end());
        if (punches.size() == before) return;
        send_pattern(t, p);
        studio.say("TAKEN OUT");
    }

    void turn(unsigned which, int delta) {
        auto& p = studio.project();
        if (which < 3) {
            if (last_kind < 0 || !melodee::punch_is_mpc(melodee::PunchKind(last_kind))) return;
            unsigned mpc = unsigned(last_kind) - melodee::punch_mpc_first;
            const auto& param = melodee::mpc_pad_fx(mpc).params[which];
            if (!param.label) return;
            auto& value = p.pad_fx_params[mpc][which];
            value = int16_t(std::clamp<int>(value + delta, param.min, param.max));
            studio.send({melodee::Command::Type::PunchParam, 0, uint8_t(which), uint16_t(mpc), value});
            studio.changed();
        } else if (which == 3) mixing::set_punch_depth(studio, p.punch_depth + delta);
        else if (which == 4) mixing::set_punch_snap(studio, p.punch_snap + (delta > 0 ? 1 : -1));
        else if (which == 5) mixing::set_punch_tail(studio, p.punch_tail + (delta > 0 ? 1 : -1));
    }
};

class PadFxOptions final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override { cursor = 0; }
    bool job(Job job, const bindings::Use& use) override {
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: cursor = std::clamp(cursor + use.delta, 0, 3); return true;
        case Job::ListPush:
            switch (cursor) {
            case 0: mixing::toggle_punch_latch(studio); break;
            case 1: mixing::set_punch_target(studio, !studio.project().punch_target); break;
            case 2: mixing::toggle_punch_quantize(studio); break;
            case 3: mixing::set_punch_tail(studio, (studio.project().punch_tail + 1) % 3); break;
            }
            return true;
        case Job::Back: studio.close(); return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override { lights.button(move::Button::Back, move::white_dim); }
    void draw(juce::Graphics& g) override {
        const auto& p = studio.project();
        ui::rail(g, "OPT", "", "PFX");
        std::string tail = upper(melodee::punch_tails[p.punch_tail % 3]);
        ui::list(g, ui::content(0, ui::height),
                 {std::string("LATCH        ") + (studio.desk.perform.latch.load() ? " ON" : "OFF"),
                  std::string("ON      ") + (p.punch_target ? " TRACK" : "MASTER"), std::string("QUANTIZE     ") + (p.punch_quantize ? " ON" : "OFF"),
                  "TAIL" + std::string(size_t(std::max(1, 13 - int(tail.size()))), ' ') + tail},
                 cursor, 1);
    }

private:
    int cursor = 0;
};

// ---------------------------------------------------------------- Flex Beat

class Flex final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::Flex; }
    void enter() override { mixing::pads_are(studio, mixing::PadsAre::Flex); }
    void leave() override { mixing::pads_are(studio, mixing::PadsAre::Sound); }

    bool job(Job job, const bindings::Use& use) override {
        const auto& f = studio.project().flex;
        switch (job) {
        case Job::FlexPad: return true; // the audio thread started it
        case Job::TurnKnob: case Job::TurnKnobFine:
            if (use.index == 0) mixing::flex(studio, 1, use.delta > 0 ? 1 : 0);
            else if (use.index == 1) mixing::flex(studio, 2, f.mix + use.delta);
            else if (use.index == 2) mixing::flex(studio, 3, use.delta > 0 ? 1 : 0);
            return true;
        case Job::Mixer: studio.open("mixer"); return true; // round again
        case Job::Back: studio.close(); return true;
        default: return (use.held & bindings::held::step) || job == Job::ToggleStep;
        }
    }

    void lights(move::Lights& lights) override {
        unsigned playing = studio.host().flex_playing();
        for (unsigned pad = 0; pad < 32; ++pad)
            lights.pad(physical_pad(uint8_t(pad), pad_layout()), pad >= melodee::flex_pads ? move::colour::off : pad == playing && pad ? move::colour::white
                           : pad == 0 ? move::colour::dark_grey : move::colour::violet);
        steps_playhead(studio, lights);
        lights.button(move::Button::Back, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        const auto& f = studio.project().flex;
        unsigned playing = studio.host().flex_playing();
        ui::rail(g, "FLEX", playing ? std::to_string(playing + 1) : "", "");
        std::string name = upper(melodee::flex_beat(playing).name);
        move::draw_text_centred(g, ui::content_x, 15, ui::content_width, name.substr(0, 8), name.size() <= 8 ? 2 : 1);
        move::draw_text_centred(g, ui::content_x, 34, ui::content_width, std::string(f.mode ? "LOOP" : "ONE SHOT") + "  MIX " + std::to_string(f.mix));
        ui::detail(g, f.quantize ? "Q ON" : "Q OFF", "PADS 1-16");
    }

    Knob knob(unsigned which) const override {
        const auto& f = studio.project().flex;
        if (which == 0) return {"MODE", f.mode ? "LOOP" : "ONE SHOT", f.mode ? 1.0f : 0.0f};
        if (which == 1) return {"MIX", std::to_string(f.mix) + "%", f.mix / 100.0f};
        if (which == 2) return {"QUANTIZE", f.quantize ? "ON" : "OFF", f.quantize ? 1.0f : 0.0f};
        return {};
    }
    std::string knob_owner() const override { return "FLEX"; }
    std::string status() const override { return " flex=" + std::to_string(studio.host().flex_playing()); }
};

RegisterScreen fx_browser("fx-browser", make<FxBrowser>);
RegisterScreen fx_options("fx-options", make<FxOptions>);
RegisterScreen mixer("mixer", make<Mixer>, {{bindings::Job::Mixer}}); // Shift + Step 4
RegisterScreen chain("chain", make<Chain>);
RegisterScreen knob_fx("knob-fx", make<KnobFx>);
RegisterScreen pad_fx("pad-fx", make<PadFx>);
RegisterScreen pad_fx_options("pad-fx-options", make<PadFxOptions>);
RegisterScreen flex("flex", make<Flex>);

} // namespace
} // namespace app::screens
