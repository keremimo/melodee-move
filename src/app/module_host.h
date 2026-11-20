// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/schwung_api.h"
#include "move/audio.h"
#include "move/queues.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Schwung's sound generators, hosted in Melodee: the synths the Move's
// Schwung install already has (/data/UserData/schwung/modules/
// sound_generators), each loaded from its own dsp.so through Schwung's v2
// API (schwung_api.h), as Schwung's Signal Chain loads a synth.
//
// A module is opened, created and asked for its parameters on the service
// thread before it plays (as Schwung's loader does), then handed to the
// audio thread, which alone calls it from then on: notes, parameter sets
// and reads, render. A module let go is handed back to be destroyed off the
// audio thread; its library is never closed (a module may own threads).
namespace app {

struct ModuleInfo {
    std::string id, name, abbrev, dir, dsp; // dsp: the library's path
    int api = 0;                            // only 2 is loaded
    std::string manifest;                   // module.json, as text
};
// The sound generators under `modules_root` (its sound_generators folder),
// by name.
std::vector<ModuleInfo> scan_modules(const std::string& modules_root);

struct ModuleParam {
    std::string key, name, short_name;
    enum class Kind : uint8_t { Float, Int, Enum } kind = Kind::Float;
    double min = 0, max = 1, step = 0, value = 0;
    std::vector<std::string> options; // Enum
    bool options_as_string = false;   // the module wants an enum's name, not its index
    std::string unit;
    std::string text() const;         // the value as set_param() takes it
    std::string shown() const;        // the value for the screen
    double fraction() const;
    void turn(int detents, bool fine);
    void take(const std::string& answer); // a get_param() answer
};
struct ModulePage { std::string name; std::vector<std::string> knobs; };

class LoadedModule {
public:
    // Opens, creates and reads the module: the service thread, never the
    // audio thread. Null with `error` set when it cannot.
    static std::unique_ptr<LoadedModule> load(const ModuleInfo& info, std::string& error);
    ~LoadedModule(); // destroys the instance: never on the audio thread

    // Before the module is live only (the service thread that loaded it).
    void set_now(const std::string& key, const std::string& value);
    std::string get_now(const std::string& key);

    ModuleParam* param(const std::string& key);
    const ModuleInfo& info() const { return about; }

    std::vector<ModuleParam> params;
    std::vector<ModulePage> pages;
    std::string preset_param, preset_count_param, preset_name_param; // a preset list, where it has one
    int preset = 0, preset_count = 0;
    std::string preset_name;

    // The audio thread's.
    plugin_api_v2_t* api = nullptr;
    void* instance = nullptr;

private:
    LoadedModule() = default;
    void read_parameters();
    ModuleInfo about;
};

// The audio side of one synth track.
class SynthPlayer {
public:
    static constexpr size_t key_size = 64, value_size = 192;
    struct Answer { char key[key_size] {}; char value[value_size] {}; };

    // Any thread but the audio thread, one at a time (the app's lock).
    bool set_module(LoadedModule* module); // null: none
    bool set(const std::string& key, const std::string& value);
    bool ask(const std::string& key); // answered on a later block
    bool answer(Answer& out);
    LoadedModule* retired();          // a module the audio thread has let go: destroy it

    // Audio thread.
    void note_on(int note, int velocity);
    void note_off(int note);
    void pressure(int note, int value);
    void render(const move::AudioBlock& input, move::AudioBlock& output, float gain); // adds
    bool live() const { return current != nullptr; }

private:
    struct Command {
        enum class Kind : uint8_t { Module, Set, Ask } kind = Kind::Set;
        LoadedModule* module = nullptr;
        char key[key_size] {};
        char value[value_size] {};
    };
    void apply();
    void midi(uint8_t status, uint8_t data1, uint8_t data2);

    move::SpscRing<Command, 256> commands;
    move::SpscRing<Answer, 64> answers;
    move::SpscRing<LoadedModule*, 8> retiring;
    LoadedModule* current = nullptr;
    std::array<int16_t, move::audio_frames * move::audio_channels> block {};
};

// What a module reads of Melodee's clock (get_bpm(), schwung_move_info()).
void set_module_tempo(float bpm);

} // namespace app
