// SPDX-License-Identifier: AGPL-3.0-only
#include "app/screens/device_fx.h"
#include "melodee/model/limits.h"
#include "melodee/model/pad_params.h"
#include <cmath>

namespace app::screens {
namespace {

using melodee::EffectKind;
using bindings::Job;

// A Schwung effect's knob name, cut to its cell.
std::string cell_name(const ModuleParam& param) { return upper(param.short_name.empty() ? param.name : param.short_name); }

} // namespace

float meter_fraction(float peak) {
    if (peak <= 0.0f) return 0.0f;
    return std::clamp((20.0f * std::log10(peak) + 48.0f) / 48.0f, 0.0f, 1.0f);
}

std::vector<DeviceFx::Item> DeviceFx::track_items() const {
    std::vector<Item> out;
    unsigned t = studio.track();
    if (studio.kind() == TrackKind::Kit) {
        unsigned pad = melodee::pad_chain(t, studio.pad());
        unsigned used = mixing::used(studio, pad);
        for (unsigned k = 0; k < used; ++k) out.push_back({pad, k, false, true});
        if (used < mixing::slots(studio, pad)) out.push_back({pad, used, true, true});
    }
    for (const auto& item : chain_items(t)) out.push_back(item);
    return out;
}

std::vector<DeviceFx::Item> DeviceFx::chain_items(unsigned chain) const {
    std::vector<Item> out;
    unsigned used = mixing::used(studio, chain);
    for (unsigned k = 0; k < used; ++k) out.push_back({chain, k, false, false});
    if (used < mixing::slots(studio, chain)) out.push_back({chain, used, true, false});
    return out;
}

FxModule* DeviceFx::schwung(const Item& item) const {
    if (item.plus) return nullptr;
    const auto& slot = mixing::slot(studio, item.chain, item.slot);
    return slot.kind == uint8_t(EffectKind::Plugin) ? studio.fx_module(uint8_t(slot.params[0])) : nullptr;
}

ui::mix::Item DeviceFx::row_item(const Item& item) const {
    if (item.plus) return {"+", item.pad, false};
    const auto& slot = mixing::slot(studio, item.chain, item.slot);
    std::string name = mixing::short_name(EffectKind(slot.kind));
    if (FxModule* fx = schwung(item))
        name = upper(fx->module ? fx->module->info().abbrev : fx->saved.module).substr(0, 4);
    return {name, item.pad, slot.bypass};
}

void DeviceFx::draw_row(juce::Graphics& g, const std::string& track_item, const std::string& sound_item, const std::vector<Item>& items,
                        int focus) const {
    std::vector<ui::mix::Item> row {{track_item}, {sound_item}};
    for (const auto& item : items) row.push_back(row_item(item));
    ui::mix::item_row(g, row, focus);
}

std::string DeviceFx::rail_bottom(const Item& item) const {
    if (item.pad) return pad_label(studio.pad());
    return "FX" + std::to_string(item.slot + 1);
}

bool DeviceFx::job(const Item& item, Job job, const bindings::Use& use) {
    FxModule* fx = schwung(item);
    switch (job) {
    case Job::TurnKnob: case Job::TurnKnobFine: {
        if (item.plus) return true;
        bool fine = job == Job::TurnKnobFine;
        unsigned which = unsigned(use.index);
        if (fx) {
            if (!fx->module || fx->page >= fx->module->pages.size()) return true;
            const auto& keys = fx->module->pages[fx->page].knobs;
            ModuleParam* param = which < keys.size() ? fx->module->param(keys[which]) : nullptr;
            if (!param || param->trigger()) return true;
            param->turn(use.delta, fine);
            studio.fx_set(*fx, param->key, param->text());
            return true;
        }
        const auto& slot = mixing::slot(studio, item.chain, item.slot);
        if (which < melodee::effect_params) mixing::turn_param(studio, item.chain, item.slot, which, use.delta, fine);
        else if (which == 6 && melodee::fx_keyable(slot.kind)) mixing::turn_key(studio, item.chain, item.slot, use.delta);
        return true;
    }
    case Job::Page: // a Schwung effect's pages
        if (fx && fx->module && !fx->module->pages.empty())
            fx->page = unsigned(std::clamp(int(fx->page) + (use.delta > 0 ? 1 : -1), 0, int(fx->module->pages.size()) - 1));
        return true;
    case Job::OpenBrowser:
        studio.desk.chain = item.chain;
        studio.desk.slot = item.slot;
        studio.push("fx-browser");
        return true;
    case Job::SampleOptions: case Job::ModuleMenu: // Shift + push: its options
        if (item.plus) { studio.say("PUSH: BROWSE"); return true; }
        studio.desk.chain = item.chain;
        studio.desk.slot = item.slot;
        studio.push("fx-options");
        return true;
    case Job::BypassEffect:
        if (!item.plus) {
            mixing::toggle_bypass(studio, item.chain, item.slot);
            studio.say(mixing::slot(studio, item.chain, item.slot).bypass ? "BYPASSED" : "ON");
        }
        return true;
    case Job::RemoveEffect:
        if (!item.plus) { mixing::set_effect(studio, item.chain, item.slot, EffectKind::None); studio.say("REMOVED"); }
        return true;
    case Job::CopyEffect:
        if (copied && mixing::has_clipboard(studio)) { // Copy still held: pasted here
            studio.say(mixing::paste_effect(studio, item.chain, item.slot) ? "PASTED" : "NOT HERE");
        } else if (!item.plus) {
            mixing::copy_effect(studio, item.chain, item.slot);
            copied = true;
            studio.say("COPIED");
        }
        return true;
    default: return false;
    }
}

void DeviceFx::input(const move::Input& input) {
    if (input.kind == move::Input::Kind::Button && input.index == uint8_t(move::Button::Copy) && !input.pressed) copied = false;
}

void DeviceFx::draw(juce::Graphics& g, const Item& item) const {
    if (item.plus) {
        move::draw_text_centred(g, ui::content_x, 16, ui::content_width, "ADD AN", 2);
        move::draw_text_centred(g, ui::content_x, 32, ui::content_width, "EFFECT", 2);
        unsigned used = mixing::used(studio, item.chain);
        ui::detail(g, std::to_string(used) + "/" + std::to_string(mixing::slots(studio, item.chain)), "PUSH: BROWSE");
        return;
    }
    std::array<std::string, 8> names;
    std::array<float, 8> values {};
    std::string title, right;
    if (FxModule* fx = schwung(item)) {
        if (!fx->module) {
            move::draw_text(g, ui::content_x + 1, 11, upper(fx->saved.module).substr(0, 16));
            move::draw_text_centred(g, ui::content_x, 26, ui::content_width, fx->missing ? "MISSING" : "LOADING", 2);
            return;
        }
        auto& module = *fx->module;
        const ModulePage* page = fx->page < module.pages.size() ? &module.pages[fx->page] : nullptr;
        title = upper(module.info().name);
        right = std::to_string(fx->page + 1) + "/" + std::to_string(std::max<size_t>(1, module.pages.size()));
        for (unsigned n = 0; page && n < page->knobs.size() && n < 8; ++n)
            if (const ModuleParam* param = module.param(page->knobs[n])) {
                names[n] = cell_name(*param);
                values[n] = float(param->fraction());
            }
    } else {
        const auto& slot = mixing::slot(studio, item.chain, item.slot);
        auto kind = EffectKind(slot.kind);
        const auto& info = melodee::effect_info(kind);
        title = mixing::long_name(kind);
        right = item.pad ? std::to_string(item.slot + 1) + "/" + std::to_string(mixing::slots(studio, item.chain))
              : melodee::fx_keyable(slot.kind) && slot.key ? "KEY " + mixing::key_name(slot.key).substr(mixing::key_name(slot.key).rfind(' ') + 1)
              : upper(melodee::family_name(info.family)).substr(0, 5);
        for (unsigned q = 0; q < melodee::effect_params; ++q)
            if (info.params[q].label) {
                names[q] = info.params[q].label;
                values[q] = mixing::param_fraction(kind, q, slot.params[q]);
            }
        if (melodee::fx_keyable(slot.kind) && item.chain < melodee::chain_count) {
            names[6] = "KEY";
            values[6] = slot.key / float(melodee::fx_keys - 1);
        }
        if (slot.bypass) right = "BYPASS";
    }
    move::draw_text(g, ui::content_x + 1, 11, title.substr(0, size_t(std::max(1, int(16 - right.size() - 1)))));
    move::draw_text(g, ui::width - move::text_width(right) - 1, 11, right);
    ui::knob_cells(g, ui::content(20, ui::height - 20), names, values);
}

Knob DeviceFx::knob(const Item& item, unsigned which) const {
    if (item.plus) return {};
    if (FxModule* fx = schwung(item)) {
        if (!fx->module || fx->page >= fx->module->pages.size()) return {};
        const auto& keys = fx->module->pages[fx->page].knobs;
        const ModuleParam* param = which < keys.size() ? fx->module->param(keys[which]) : nullptr;
        if (!param) return {};
        return {upper(param->name).substr(0, 16), upper(param->shown()).substr(0, 12), float(param->fraction())};
    }
    const auto& slot = mixing::slot(studio, item.chain, item.slot);
    auto kind = EffectKind(slot.kind);
    const auto& info = melodee::effect_info(kind);
    if (which < melodee::effect_params && info.params[which].label)
        return {info.params[which].label, mixing::param_text(kind, which, slot.params[which]), mixing::param_fraction(kind, which, slot.params[which])};
    if (which == 6 && melodee::fx_keyable(slot.kind) && item.chain < melodee::chain_count)
        return {"KEY", mixing::key_name(slot.key), slot.key / float(melodee::fx_keys - 1)};
    return {};
}

bool DeviceFx::lane(const Item& item, unsigned which, unsigned& param) const {
    if (item.plus || item.pad || which >= melodee::effect_params || schwung(item)) return false;
    const auto& slot = mixing::slot(studio, item.chain, item.slot);
    if (!melodee::effect_info(EffectKind(slot.kind)).params[which].label) return false;
    if (item.chain == mixing::master) { param = melodee::motion_master_fx(item.slot, which); return true; }
    if (item.chain < melodee::track_count) { param = melodee::motion_fx_param(item.slot, which); return true; }
    return false; // a bus's chain: its return is the Mixer's
}

// ---------------------------------------------------------------- the track item: its mix

bool DeviceFx::mix_job(unsigned t, Job job, const bindings::Use& use) {
    bool kit = studio.kind(t) == TrackKind::Kit;
    if (!kit) pad_sends = false;
    if (job == Job::Page) { pad_sends = kit && use.delta > 0; return true; }
    if (job != Job::TurnKnob && job != Job::TurnKnobFine) return false;
    const auto& mix = studio.project().tracks[t % melodee::track_count].mix;
    int delta = use.delta;
    if (pad_sends) { // the last pad's own sends (Melodee's PSND)
        unsigned pad = studio.pad();
        if (use.index < 4)
            mixing::set_pad_send(studio, t, pad, unsigned(use.index), int(studio.project().tracks[t].pads[pad].sends[unsigned(use.index)]) + delta);
        return true;
    }
    switch (use.index) {
    case 0: studio.set_level(t, int(mix.level) + delta); return true;
    case 1: mixing::set_pan(studio, t, mix.pan + delta); return true;
    case 2: case 3: case 4: case 5: {
        unsigned bus = unsigned(use.index - 2);
        mixing::set_send(studio, t, bus, int(mix.sends[bus]) + delta);
        return true;
    }
    default: return true;
    }
}

void DeviceFx::draw_mix(juce::Graphics& g, unsigned t) const {
    const auto& track = studio.project().tracks[t % melodee::track_count];
    const auto& mix = track.mix;
    if (pad_sends && studio.kind(t) == TrackKind::Kit) {
        const auto& sends = track.pads[studio.pad()].sends;
        move::draw_text(g, ui::content_x + 1, 11, pad_label(studio.pad()) + " SENDS");
        move::draw_text(g, ui::width - move::text_width("2/2") - 1, 11, "2/2");
        ui::knob_cells(g, ui::content(20, 34), {">A", ">B", ">C", ">D", "", "", "", ""},
                       {sends[0] / 100.0f, sends[1] / 100.0f, sends[2] / 100.0f, sends[3] / 100.0f, 0, 0, 0, 0});
        ui::detail(g, "BEFORE THE CHAIN");
        return;
    }
    move::draw_text(g, ui::content_x + 1, 11, upper(track.name).substr(0, 15));
    std::array<std::string, 8> names {"VOL", "PAN", ">A", ">B", ">C", ">D", "", ""};
    std::array<float, 8> values {mix.level / 127.0f, (mix.pan + 50) / 100.0f, mix.sends[0] / 100.0f, mix.sends[1] / 100.0f,
                                 mix.sends[2] / 100.0f, mix.sends[3] / 100.0f, 0, 0};
    ui::knob_cells(g, {ui::content_x, 20, ui::content_width - 6, 34}, names, values);
    // Its meter, beside them.
    int h = int(std::lround(meter_fraction(studio.host().meter(t)) * 34.0f));
    g.fillRect(ui::width - 4, 54 - h, 3, h);
    g.fillRect(ui::width - 5, 20, 1, 34);
    char cpu[16];
    std::snprintf(cpu, sizeof cpu, "CPU %d%%", int(std::lround(studio.host().track_load(t) * 100.0f)));
    std::string where = bar_beat(studio);
    ui::detail(g, mix.mute ? "MUTED" : mixing::level_text(mix.level), where.empty() ? cpu : where);
}

bool DeviceFx::mix_lane(unsigned t, unsigned which, unsigned& param, unsigned& pad) const {
    if (pad_sends && studio.kind(t) == TrackKind::Kit) {
        if (which >= 4) return false;
        param = melodee::motion_pad_param(melodee::PadParam(unsigned(melodee::PadParam::SendA) + which));
        pad = studio.pad();
        return true;
    }
    pad = 0;
    if (which == 0) param = melodee::motion_level;
    else if (which == 1) param = melodee::motion_pan;
    else if (which < 6) param = melodee::motion_send(which - 2);
    else return false;
    return true;
}

Knob DeviceFx::mix_knob(unsigned t, unsigned which) const {
    const auto& mix = studio.project().tracks[t % melodee::track_count].mix;
    if (pad_sends && studio.kind(t) == TrackKind::Kit) {
        if (which >= 4) return {};
        unsigned send = studio.project().tracks[t].pads[studio.pad()].sends[which];
        return {pad_label(studio.pad()) + " SEND " + char('A' + int(which)), std::to_string(send), send / 100.0f};
    }
    switch (which) {
    case 0: return {"LEVEL", mixing::level_text(mix.level), mix.level / 127.0f};
    case 1: return {"PAN", mixing::pan_text(mix.pan), (mix.pan + 50) / 100.0f};
    case 2: case 3: case 4: case 5: {
        unsigned bus = which - 2;
        return {std::string("SEND ") + char('A' + int(bus)), std::to_string(mix.sends[bus]), mix.sends[bus] / 100.0f};
    }
    default: return {};
    }
}

} // namespace app::screens
