// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/module_host.h"
#include "app/schwung_pages.h"
#include "app/project_store.h"
#include "melodee/model/limits.h"
#include "melodee/plugin/plugin_runner.h"
#include "move/queues.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Schwung's audio effects in Melodee's chains (docs/PARITY.md, "Schwung's
// audio FX modules in chains"): each one a module of the Move's Schwung
// install (/data/UserData/schwung/modules/audio_fx/<id>/<id>.so), loaded
// through Schwung's audio_fx v2 API as its Signal Chain loads one
// (module_host.*: LoadedModule::load_audio_fx), and played in a chain's slot
// as Melodee plays a plug-in effect: the slot is an EffectKind::Plugin whose
// first value is an entry of Project::plugins, and the engine host calls
// that entry's runner on the audio thread, in place, where the slot is.
//
// As with a synth (module_host.h): opened, created and set on the service
// thread, then handed to the audio thread, which alone calls it; handed
// back to be destroyed off it; its library never closed.
namespace app {

class FxRunner final : public melodee::PluginRunner {
public:
    // Before it is offered to the engine host (the service thread): the
    // module it plays.
    void attach(LoadedModule* module) { playing = module; }
    // Any thread but the audio thread, one at a time (the studio's lock): a
    // value, set before the next block it processes.
    bool set(const std::string& key, const std::string& value);
    // A call into the effect that held the audio thread 1 ms or more.
    uint32_t slow_calls() const { return slow.load(std::memory_order_relaxed); }
    uint32_t slowest_us() const { return worst.load(std::memory_order_relaxed); }
    uint32_t last_us() const { return last.load(std::memory_order_relaxed); }

    // Audio thread (melodee::PluginRunner): an effect takes no notes here.
    void midi(uint8_t, uint8_t, uint8_t) override {}
    void silence() override {}
    void render(float* left, float* right, unsigned frames) override;

private:
    static constexpr size_t key_size = 64, value_size = 192;
    struct Set { char key[key_size] {}; char value[value_size] {}; };
    LoadedModule* playing = nullptr;
    move::SpscRing<Set, 64> sets;
    std::array<int16_t, 2 * 256> block {};
    std::atomic<uint32_t> slow {0}, worst {0}, last {0};
};

// One Schwung effect of the project: the entry of Project::plugins its slot
// names, its module (once loaded) and what the project file keeps of it.
struct FxModule {
    uint8_t entry = melodee::no_plugin;
    FxRunner runner;
    std::unique_ptr<LoadedModule> module;
    SynthFile saved;        // as the file has it (a SynthFile: the module and its values); kept while it is missing
    std::string job;        // a module to load (the service thread's)
    bool offered = false;   // its runner is the engine host's
    bool missing = false;   // the module is not installed: the slot passes the sound through
    unsigned page = 0;      // the screen's: its page of knobs
    std::unique_ptr<SchwungPages> schwung_pages; // its pages by Schwung's own code (schwung_pages.h); null: Melodee's own
};

// Its state as the project file keeps it (the PLUG entry's): the module's
// id and each value as set_param() takes it.
SynthFile fx_state(const FxModule& fx);

} // namespace app
