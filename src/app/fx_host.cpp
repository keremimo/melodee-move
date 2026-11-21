// SPDX-License-Identifier: AGPL-3.0-only
#include "app/fx_host.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace app {

bool FxRunner::set(const std::string& key, const std::string& value) {
    Set made;
    std::snprintf(made.key, sizeof made.key, "%s", key.c_str());
    std::snprintf(made.value, sizeof made.value, "%s", value.c_str());
    return sets.push(made);
}

void FxRunner::render(float* left, float* right, unsigned frames) {
    LoadedModule* module = playing;
    if (!module || !module->fx_api || !module->instance) return; // not loaded: the sound goes through
    auto* fx = module->fx_api;
    Set pending;
    while (sets.pop(pending))
        if (fx->set_param) fx->set_param(module->instance, pending.key, pending.value);
    frames = std::min<unsigned>(frames, unsigned(block.size() / 2));
    // Schwung's effects take the Move's own format: stereo, interleaved,
    // 16-bit, in place.
    for (unsigned f = 0; f < frames; ++f) {
        block[2 * f] = int16_t(std::lround(std::clamp(left[f], -1.0f, 1.0f) * 32767.0f));
        block[2 * f + 1] = int16_t(std::lround(std::clamp(right[f], -1.0f, 1.0f) * 32767.0f));
    }
    auto begun = std::chrono::steady_clock::now();
    fx->process_block(module->instance, block.data(), int(frames));
    auto us = uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begun).count());
    last.store(us, std::memory_order_relaxed);
    if (us >= 1000) {
        slow.fetch_add(1, std::memory_order_relaxed);
        if (us > worst.load(std::memory_order_relaxed)) worst.store(us, std::memory_order_relaxed);
    }
    for (unsigned f = 0; f < frames; ++f) {
        left[f] = block[2 * f] / 32768.0f;
        right[f] = block[2 * f + 1] / 32768.0f;
    }
}

SynthFile fx_state(const FxModule& fx) {
    if (!fx.module) return fx.saved;
    SynthFile state;
    state.module = fx.module->info().id;
    state.preset = fx.module->preset_param.empty() ? -1 : fx.module->preset;
    for (const auto& param : fx.module->params)
        if (param.key != fx.module->preset_param && !param.trigger()) state.values.emplace_back(param.key, param.text());
    if (fx.schwung_pages) { // its drawn pages' state, a page each
        auto states = juce::JSON::parse(juce::String(fx.schwung_pages->page_states()));
        if (auto* object = states.getDynamicObject())
            for (const auto& property : object->getProperties())
                if (auto text = property.value.toString().toStdString(); text != "{}" && !text.empty())
                    state.pages.emplace_back(property.name.toString().toStdString(), text);
    }
    return state;
}

} // namespace app
