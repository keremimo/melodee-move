// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/schwung_api.h"
#include "move/audio.h"
#include "move/queues.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
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
// by name; or, with `folder` "audio_fx", its audio effects, each library
// where Schwung's Signal Chain looks for it: <id>/<id>.so.
std::vector<ModuleInfo> scan_modules(const std::string& modules_root, const std::string& folder = "sound_generators");

struct ModuleParam {
    std::string key, name, short_name;
    std::string label;                // what the module's menu calls it, where it names it
    enum class Kind : uint8_t { Float, Int, Enum } kind = Kind::Float;
    double min = 0, max = 1, step = 0, value = 0;
    std::vector<std::string> options; // Enum
    bool options_as_string = false;   // the module wants an enum's name, not its index
    std::string unit;
    bool selector = false;            // an items list's choice (a bank, a soundfont): no knob, set first
    // An action, not a value (webstream's play/pause, stop): an enum of
    // "idle" and "trigger", sent "trigger" when pushed or turned.
    bool trigger() const;
    std::string text() const;         // the value as set_param() takes it
    std::string shown() const;        // the value for the screen
    double fraction() const;
    void turn(int detents, bool fine);
    void take(const std::string& answer); // a get_param() answer
};
// A page of knobs: up to eight keys, "" an empty knob; `level` the menu
// level it belongs to; `canvas` a page the module draws itself (an index
// into LoadedModule::canvases), with the root level's knobs.
struct ModulePage { std::string name; std::vector<std::string> knobs; size_t level = 0; int canvas = -1; };

// A page the module draws itself: a `type: "canvas"` parameter, its script
// run by CanvasPage. `as_page`: one of the module's pages; otherwise opened
// from its menu. `extra_keys`: values the page is given each time it draws.
struct ModuleCanvas {
    std::string key, name, script;
    bool as_page = false, enterable = false, page_first = false;
    std::vector<std::string> extra_keys;
};

// The module's menu, as its ui_hierarchy lays it out (Schwung's
// docs/MODULES.md, "ui_hierarchy"): levels of parameters and links to other
// levels; a level may be a preset list (list_param) or a list of items to
// choose from (items_param / select_param, e.g. a bank or a soundfont).
struct MenuItem {
    enum class Kind : uint8_t { Param, Level } kind = Kind::Param;
    std::string key;   // a parameter's key, or a level's id
    std::string label; // empty: the parameter's own name
};
struct ModuleLevel {
    std::string id, name;
    std::vector<MenuItem> items;
    std::string list_param, count_param, name_param;    // a preset list
    std::string items_param, select_param, navigate_to; // a list to choose from
};

class LoadedModule {
public:
    // Opens, creates and reads the module: the service thread, never the
    // audio thread. Null with `error` set when it cannot.
    static std::unique_ptr<LoadedModule> load(const ModuleInfo& info, std::string& error);
    // An audio effect (Schwung's audio_fx v2 API), opened as Signal Chain
    // opens one: the service thread too.
    static std::unique_ptr<LoadedModule> load_audio_fx(const ModuleInfo& info, std::string& error);
    ~LoadedModule(); // destroys the instance: never on the audio thread

    // Before the module is live only (the service thread that loaded it).
    void set_now(const std::string& key, const std::string& value);
    std::string get_now(const std::string& key);

    ModuleParam* param(const std::string& key);
    const ModuleInfo& info() const { return about; }

    std::vector<ModuleParam> params;
    std::vector<ModulePage> pages;
    std::vector<ModuleLevel> levels; // root first; never empty once loaded
    // A source, not an instrument: it takes no MIDI ("midi_in": false, as
    // Webstream and Radio Garden declare). SynthPlayer renders it ahead, on
    // a thread of its own, so a stall of its (a stream starting) is never
    // the audio thread's.
    bool source = false;
    std::vector<ModuleCanvas> canvases;
    const ModuleLevel* level(const std::string& id) const;
    size_t level_index(const std::string& id) const; // 0 (root) when unknown
    std::string preset_param, preset_count_param, preset_name_param; // a preset list, where it has one
    int preset = 0, preset_count = 0;
    std::string preset_name;

    // The audio thread's. An audio effect has `fx_api` instead of `api`.
    plugin_api_v2_t* api = nullptr;
    audio_fx_api_v2_t* fx_api = nullptr;
    void* instance = nullptr;

private:
    LoadedModule() = default;
    void read_parameters();
    ModuleInfo about;
};

// The audio side of one synth track.
//
// An instrument is called on the audio thread, its notes in the cycle the
// pad was read. A source (LoadedModule::source) is called on the player's
// own thread instead, every call, as Schwung's contract wants one thread
// for a module: it renders about twelve blocks (35 ms) ahead, and the audio
// thread only takes the next finished block. Whichever thread plays the
// current module takes the commands; the hand-over between them happens in
// the queue, at the module command.
class SynthPlayer {
public:
    SynthPlayer();
    ~SynthPlayer();
    void stop(); // ends the source thread: before the modules it may play are destroyed
    unsigned stream_late() const { return late.load(std::memory_order_relaxed); } // blocks a source had not made in time
    static constexpr size_t key_size = 64, value_size = 192;
    // `serial`: the ask's, to tell an answer from before a later set.
    struct Answer { char key[key_size] {}; char value[value_size] {}; uint64_t serial = 0; };

    // Any thread but the audio thread, one at a time (the app's lock).
    bool set_module(LoadedModule* module); // null: none
    bool set(const std::string& key, const std::string& value);
    bool ask(const std::string& key); // answered on a later block
    uint64_t last_serial() const { return serial; } // the last set's or ask's
    // A read too long for an Answer (an items list): one at a time, answered
    // on a later block. False while one is pending.
    bool ask_long(const std::string& key);
    bool long_answer(std::string& out);
    // A read answered now, for a page script's getParam: waits for the
    // audio thread to make it, `wait` at most. Any thread but the audio
    // thread. False when it was not answered in time.
    bool get(const std::string& key, std::string& out, std::chrono::milliseconds wait);
    bool rendering() const; // the audio thread has rendered in the last 50 ms
    bool answer(Answer& out);
    LoadedModule* retired();          // a module the audio thread has let go: destroy it
    // A call into the module that took the audio thread `slow_us` or more:
    // what it was and how long, for the log.
    static constexpr uint32_t slow_us = 1000;
    // On a source's own thread, rendering ahead, only a stall that eats into
    // its 35 ms lead is worth telling.
    static constexpr uint32_t source_slow_us = 10000;
    struct Slow { char what[96] {}; uint32_t us = 0; };
    bool slow(Slow& out);

    // Audio thread.
    void note_on(int note, int velocity);
    void note_off(int note);
    void pressure(int note, int value);
    void midi(uint8_t status, uint8_t data1, uint8_t data2); // any message, as a pad's note goes
    void render(const move::AudioBlock& input, move::AudioBlock& output, float gain); // adds
    // Adds `frames` of the module's sound, as floats, for Melodee's engine
    // host (SchwungRunner): the module renders whole blocks of 128 frames,
    // as Schwung calls it, and a call for fewer takes them from the block
    // made last. The input goes to the device page apart (module_input()).
    void render_add(float* left, float* right, unsigned frames);
    bool live() const { return current != nullptr; }

private:
    struct Command {
        enum class Kind : uint8_t { Module, Set, Ask } kind = Kind::Set;
        LoadedModule* module = nullptr;
        char key[key_size] {};
        char value[value_size] {};
        uint64_t serial = 0;
    };
    using Clock = std::chrono::steady_clock;
    bool apply(); // false: the module is now the other thread's to play
    void work();  // the source thread
    bool next_block(); // `block` filled with the module's next block (false: nothing, silence)
    void timed(Clock::time_point begun, const char* kind, const char* key, const char* value);

    move::SpscRing<Command, 256> commands;
    move::SpscRing<Answer, 64> answers;
    move::SpscRing<LoadedModule*, 8> retiring;
    move::SpscRing<Slow, 16> slows;
    // The long read: 0 none, 1 asked, 2 answered.
    std::atomic<int> long_state {0};
    char long_key[key_size] {};
    int long_length = 0;
    std::vector<char> long_buffer = std::vector<char>(65536);
    // The read answered now: as the long read, one caller at a time.
    std::mutex sync_lock;
    std::atomic<int> sync_state {0};
    char sync_key[key_size] {};
    int sync_length = 0;
    std::vector<char> sync_buffer = std::vector<char>(65536);
    std::atomic<int64_t> rendered_at {0}; // steady_clock, nanoseconds
    LoadedModule* current = nullptr; // the playing thread's
    uint64_t serial = 0; // the producer's
    // A source's thread, and what passes between it and the audio thread.
    enum : int { audio_thread_plays = 0, source_thread_plays = 1 };
    std::atomic<int> player {audio_thread_plays};
    struct Midi { uint8_t bytes[3] {}; };
    move::SpscRing<Midi, 64> source_midi; // the audio thread's pads, to a source
    using Block = std::array<int16_t, move::audio_frames * move::audio_channels>;
    static constexpr uint64_t ahead = 12; // 35 ms: webstream was seen to stall 22 ms
    move::SpscRing<Block, 16> source_blocks;
    std::atomic<uint64_t> made {0}, taken {0};
    std::atomic<bool> primed {false}, stopping {false};
    std::atomic<unsigned> late {0};
    std::thread source_thread;
    std::array<int16_t, move::audio_frames * move::audio_channels> block {};
    unsigned block_used = move::audio_frames; // render_add(): frames of `block` already handed out
};

// Audio thread, once a block before any module renders: the input, where a
// module reading the device page looks for it.
void module_input(const move::AudioBlock& input);

// What a module reads of Melodee's clock (get_bpm(), schwung_move_info()).
void set_module_tempo(float bpm);

} // namespace app
