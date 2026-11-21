// SPDX-License-Identifier: AGPL-3.0-only
// Schwung's audio effects in Melodee's chains (fx_host.h): the studio's
// side. Each is an entry of the project's plug-ins, named by an
// EffectKind::Plugin slot; asked for under the lock, loaded and offered to
// the engine host on the service thread, handed back by it when its slot
// lets it go, destroyed off the audio thread.
#include "app/studio.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace app {
namespace {

using melodee::Command;
using melodee::EffectKind;
using Type = melodee::Command::Type;

std::string upper(std::string text) {
    for (auto& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}
template <size_t N> void copy_text(char (&to)[N], const std::string& from) { std::snprintf(to, N, "%s", from.c_str()); }

} // namespace

void Studio::rescan_fx() { fx_catalog = scan_modules(modules_root, "audio_fx"); }

FxModule* Studio::fx_module(uint8_t entry) { return entry < melodee::max_plugins ? fx[entry].get() : nullptr; }

void Studio::fx_set(FxModule& effect, const std::string& key, const std::string& value) {
    if (!effect.module) return;
    if (ModuleParam* param = effect.module->param(key)) param->take(value);
    if (effect.offered) effect.runner.set(key, value);
    else effect.module->set_now(key, value); // not the audio thread's yet: this thread's to call
    changed();
}

uint8_t Studio::reserve_fx_entry(const ModuleInfo& info) {
    uint8_t entry = free_plugin_entry();
    if (entry == melodee::no_plugin) return entry;
    auto& ref = song->plugins[entry];
    ref = melodee::PluginRef {};
    copy_text(ref.path, info.dir.empty() ? std::string("schwung") : info.dir); // used from now: no one else takes it
    copy_text(ref.name, info.name.empty() ? info.id : info.name);
    fx[entry] = std::make_unique<FxModule>();
    fx[entry]->entry = entry;
    return entry;
}

namespace {
// Slot `at` of `chain` as a Schwung effect on `entry`, to the engine.
void place(Studio& studio, unsigned chain, unsigned at, uint8_t entry) {
    auto& c = studio.project().chain(chain);
    auto& s = c.slots[at];
    s = melodee::EffectSlot {};
    s.kind = uint8_t(EffectKind::Plugin);
    s.params[0] = entry;
    studio.send({Type::FxKind, uint8_t(chain), 0, uint16_t(at), int16_t(EffectKind::Plugin)});
    studio.send({Type::FxParam, uint8_t(chain), 0, uint16_t(at), int16_t(entry)});
    studio.send({Type::FxBypass, uint8_t(chain), 0, uint16_t(at), 0});
    studio.send({Type::FxKey, uint8_t(chain), 0, uint16_t(at), 0});
}
} // namespace

bool Studio::load_fx(unsigned chain, unsigned at, const std::string& id) {
    if (chain >= melodee::chain_count) return false; // a kit pad's own chain takes no plug-in, as in Melodee
    auto found = std::find_if(fx_catalog.begin(), fx_catalog.end(), [&](const ModuleInfo& info) { return info.id == id; });
    if (found == fx_catalog.end()) return false;
    auto& c = song->chain(chain);
    at = std::min(at, c.count());
    if (at >= c.size) return false;
    uint8_t entry = reserve_fx_entry(*found);
    if (entry == melodee::no_plugin) { say("TOO MANY PLUG-INS"); return false; }
    if (c.slots[at].kind == uint8_t(EffectKind::Plugin)) fx_released(uint8_t(c.slots[at].params[0]));
    fx[entry]->job = id;
    place(*this, chain, at, entry);
    say(upper(found->name).substr(0, 16));
    changed();
    return true;
}

bool Studio::copy_fx(uint8_t from, unsigned chain, unsigned at) {
    if (chain >= melodee::chain_count) return false;
    const FxModule* source = fx_module(from);
    if (!source) return false;
    SynthFile state = fx_state(*source);
    auto found = std::find_if(fx_catalog.begin(), fx_catalog.end(), [&](const ModuleInfo& info) { return info.id == state.module; });
    ModuleInfo info = found != fx_catalog.end() ? *found : ModuleInfo {};
    if (info.id.empty()) info.id = info.name = state.module;
    auto& c = song->chain(chain);
    at = std::min(at, c.count());
    if (at >= c.size) return false;
    uint8_t entry = reserve_fx_entry(info);
    if (entry == melodee::no_plugin) { say("TOO MANY PLUG-INS"); return false; }
    if (c.slots[at].kind == uint8_t(EffectKind::Plugin)) fx_released(uint8_t(c.slots[at].params[0]));
    fx[entry]->saved = state;
    fx[entry]->job = state.module;
    place(*this, chain, at, entry);
    changed();
    return true;
}

void Studio::fx_released(uint8_t entry) {
    if (entry >= melodee::max_plugins || !fx[entry]) return;
    song->plugins[entry] = melodee::PluginRef {};
    if (fx[entry]->offered) {
        engine->offer_plugin(entry, nullptr); // handed back (take_back), then destroyed
        fx_leaving.push_back(std::move(fx[entry]));
    } else {
        fx[entry].reset(); // never the audio thread's
    }
    fx[entry].reset();
}

void Studio::release_chain_fx(const melodee::Chain& chain) {
    for (const auto& s : chain.slots)
        if (s.kind == uint8_t(EffectKind::Plugin)) fx_released(uint8_t(s.params[0]));
}

void Studio::copy_chain_fx(melodee::Chain& chain) {
    for (auto& s : chain.slots) {
        if (s.kind != uint8_t(EffectKind::Plugin)) continue;
        const FxModule* source = fx_module(uint8_t(s.params[0]));
        if (!source) { s = melodee::EffectSlot {}; continue; } // another host's plug-in: its own to copy
        SynthFile state = fx_state(*source);
        auto found = std::find_if(fx_catalog.begin(), fx_catalog.end(), [&](const ModuleInfo& info) { return info.id == state.module; });
        ModuleInfo info = found != fx_catalog.end() ? *found : ModuleInfo {};
        if (info.id.empty()) info.id = info.name = state.module;
        uint8_t entry = reserve_fx_entry(info);
        if (entry == melodee::no_plugin) { s = melodee::EffectSlot {}; continue; }
        fx[entry]->saved = state;
        fx[entry]->job = state.module;
        s.params[0] = entry;
    }
    // Packed from the front again, where a slot was emptied.
    std::stable_partition(chain.slots.begin(), chain.slots.end(), [](const melodee::EffectSlot& s) { return s.used(); });
}

void Studio::setup_fx(const PluginStates& states) {
    for (unsigned n = 0; n < melodee::chain_count; ++n)
        for (const auto& s : song->chain(n).slots) {
            if (s.kind != uint8_t(EffectKind::Plugin)) continue;
            auto entry = uint8_t(s.params[0]);
            if (entry >= melodee::max_plugins || fx[entry]) continue;
            SynthFile saved;
            if (!read_synth_state(states[entry], saved)) continue; // another host's plug-in: left as it is
            fx[entry] = std::make_unique<FxModule>();
            fx[entry]->entry = entry;
            fx[entry]->saved = saved;
            fx[entry]->job = saved.module;
        }
}

void Studio::service_fx() {
    for (unsigned entry = 0; entry < melodee::max_plugins; ++entry) {
        ModuleInfo info;
        SynthFile saved;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!fx[entry] || fx[entry]->job.empty()) continue;
            auto id = std::move(fx[entry]->job);
            fx[entry]->job.clear();
            saved = fx[entry]->saved;
            auto found = std::find_if(fx_catalog.begin(), fx_catalog.end(), [&](const ModuleInfo& one) { return one.id == id; });
            if (found == fx_catalog.end()) {
                fx[entry]->missing = true;
                fx[entry]->saved.module = id;
                say(upper(id + " MISSING").substr(0, 16));
                continue;
            }
            info = *found;
        }
        // Opened, created and set outside the lock, as a synth is.
        std::string error;
        auto loaded = LoadedModule::load_audio_fx(info, error);
        if (loaded && saved.module == info.id) {
            for (const auto& [key, value] : saved.values)
                if (ModuleParam* param = loaded->param(key)) {
                    loaded->set_now(key, value);
                    param->take(value);
                }
        }
        std::lock_guard<std::mutex> guard(lock);
        FxModule* effect = fx[entry].get();
        if (!effect) continue; // let go meanwhile: `loaded` destroyed here, never played
        if (!loaded) {
            effect->missing = true;
            say(upper(error.empty() ? "FX WOULD NOT LOAD" : error).substr(0, 16));
            continue;
        }
        effect->missing = false;
        effect->module = std::move(loaded);
        effect->runner.attach(effect->module.get());
        effect->saved = {};
        effect->offered = true;
        engine->offer_plugin(entry, &effect->runner);
    }
}

std::unique_ptr<FxModule> Studio::take_back_fx(melodee::PluginRunner* runner) {
    auto at = std::find_if(fx_leaving.begin(), fx_leaving.end(), [runner](const auto& effect) { return &effect->runner == runner; });
    if (at == fx_leaving.end()) return nullptr;
    auto effect = std::move(*at);
    fx_leaving.erase(at);
    return effect;
}

void Studio::fx_states(PluginStates& states) const {
    for (unsigned entry = 0; entry < melodee::max_plugins; ++entry)
        if (fx[entry]) states[entry] = synth_state(fx_state(*fx[entry]));
}

} // namespace app
