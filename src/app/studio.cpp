// SPDX-License-Identifier: AGPL-3.0-only
#include "app/studio.h"
#include "app/screen.h"
#include "melodee/engine/engine_info.h"
#include "melodee/shell/mpc_project.h"
#include "melodee/shell/sample_decode.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace app {
namespace {

using melodee::Command;
using melodee::EngineKind;
using Type = melodee::Command::Type;
namespace fs = std::filesystem;

constexpr const char* move_user_data = "/data/UserData";
constexpr const char* move_home = "/data/UserData/Melodee";
constexpr const char* move_modules = "/data/UserData/schwung/modules"; // Schwung's own install
constexpr int synth_base_note = 48;                                  // the bottom-left pad at octave 0: C2
constexpr auto autosave_idle = std::chrono::milliseconds(3000); // as Melodee's autosave on the MPC (shell/autosave.h)
constexpr auto autosave_retry = std::chrono::seconds(10);
constexpr auto message_time = std::chrono::milliseconds(1500);
constexpr const char* settings_header = "melodee-move settings 1";

std::string upper(std::string text) {
    for (auto& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}
template <size_t N> void copy_text(char (&to)[N], const std::string& from) { std::snprintf(to, N, "%s", from.c_str()); }

// A track as a new one of its kind starts: Melodee's own first sound for
// the engine (engine_info.h), and the Move's name and octave.
melodee::Track fresh_track(TrackKind kind, unsigned number) {
    melodee::Track track;
    auto engine = kind == TrackKind::Kit ? EngineKind::Sampler : kind == TrackKind::Synth ? EngineKind::Plugin
                : kind == TrackKind::Keys ? EngineKind::Keygroup : kind == TrackKind::Midi ? EngineKind::Midi
                : kind == TrackKind::Audio ? EngineKind::Audio : EngineKind::None;
    if (engine == EngineKind::None) return track;
    melodee::load_sound(track, engine, 0);
    const char* names[] {"", "KIT", "SYNTH", "KEYS", "MIDI", "AUDIO", "PLUG-IN"};
    copy_text(track.name, std::string(names[unsigned(kind)]) + " " + std::to_string(number + 1));
    track.octave = 3; // the bottom-left pad C2 (48), as the first scope's synth track
    if (kind == TrackKind::Midi) {
        track.params[melodee::midi_track::Channel] = int16_t(std::clamp<unsigned>(number + 1, 1, 16));
        track.midi_channel = 0;
    }
    return track;
}

// A Schwung module's folder, by its id: the catalog's, or where Schwung
// keeps a synth.
std::string folder_of(const std::string& id, const void* context) {
    const auto& studio = *static_cast<const Studio*>(context);
    for (const auto& info : studio.catalog)
        if (info.id == id) return info.dir;
    return (fs::path(studio.modules_root) / "sound_generators" / id).string();
}

// Setup's pad response: a hit's velocity, soft (more of the range from a
// light touch), as it is, hard, or always full. A table read on the audio
// thread: made once, before it starts.
struct Curves {
    std::array<std::array<uint8_t, 128>, 4> by {};
    Curves() {
        for (int v = 0; v < 128; ++v) {
            double x = v / 127.0;
            by[0][size_t(v)] = uint8_t(std::clamp(int(std::lround(127.0 * std::pow(x, 0.6))), 1, 127));
            by[1][size_t(v)] = uint8_t(std::clamp(v, 1, 127));
            by[2][size_t(v)] = uint8_t(std::clamp(int(std::lround(127.0 * std::pow(x, 1.6))), 1, 127));
            by[3][size_t(v)] = 127;
        }
    }
};
const Curves curves;
int velocity_of(uint8_t response, int velocity) { return curves.by[response & 3][size_t(std::clamp(velocity, 0, 127))]; }

} // namespace

const char* kind_name(TrackKind kind) {
    static const char* names[] {"empty", "kit", "synth", "keys", "midi", "audio", "plugin"};
    return names[unsigned(kind) % unsigned(TrackKind::Count)];
}
const char* kind_tag(TrackKind kind) {
    static const char* tags[] {"----", "KIT", "SYN", "KEYS", "MIDI", "AUD", "PLUG"};
    return tags[unsigned(kind) % unsigned(TrackKind::Count)];
}

std::unique_ptr<Sample> read_sample(const std::string& path, std::string& error) {
    auto sample = std::make_unique<Sample>();
    sample->name = fs::path(path).filename().string();
    sample->path = path;
    sample->data = std::make_unique<melodee::SampleData>();
    error = melodee::read_audio(path, *sample->data, 60, nullptr); // WAV, AIFF, FLAC, Ogg, MP3, as Melodee reads them
    if (!error.empty()) return nullptr;
    melodee::make_overview(*sample->data, sample->overview);
    auto mono = melodee::model_mono(*sample->data);
    sample->hit_count = melodee::find_hits_in(mono, sample->hits, sample->beats, &sample->autos);
    return sample;
}

// ---------------------------------------------------------------- start and end

Studio::Studio(std::string library_folder, std::string home_folder, std::string modules_folder)
    : library(std::move(library_folder)), song(std::make_unique<melodee::Project>()), bank(std::make_unique<melodee::PatternBank>()) {
    std::error_code error;
    home_path = std::move(home_folder);
    if (home_path.empty()) home_path = fs::is_directory(move_user_data, error) ? move_home : (fs::current_path(error) / "Melodee").string();
    path = (fs::path(home_path) / "Projects" / "Move.mldp").string(); // the first scope's one project, the default
    settings_path = (fs::path(home_path) / "Move.settings").string();
    modules_root = modules_folder.empty() ? move_modules : std::move(modules_folder);
    catalog = scan_modules(modules_root);
    pad_went.fill({});
    melodee::dsp::device_rate.store(44100.0f); // the Move's; audio_opened() says if it is not
    unsigned played = 0;
    read_settings(); // the Move's own: the project to open, the track played last, the metronome, Setup
    {
        std::error_code missing;
        if (!settings.start.empty() && fs::exists(settings.start, missing)) path = settings.start;
        else if (!last_path.empty() && fs::exists(last_path, missing)) path = last_path;
        played = last_track;
    }

    // The project, read before the audio thread starts.
    PluginStates states;
    auto failed = read_project_file(path, *song, *bank, states, &move_extras);
    bool fresh = failed == "missing", migrated = false;
    if (!failed.empty() && !fresh) {
        // Unreadable: kept aside, never written over.
        std::rename(path.c_str(), (path + ".unreadable").c_str());
        std::fprintf(stderr, "melodee-move: %s unreadable (%s), kept as .unreadable\n", path.c_str(), failed.c_str());
        song = std::make_unique<melodee::Project>();
        bank = std::make_unique<melodee::PatternBank>();
        states = {};
        move_extras = {};
        fresh = true;
        say("PROJECT UNREADABLE");
    }
    if (fresh) {
        // A new project, as the first scope had it: a kit on track 1, a
        // synth (no module yet) on track 2.
        copy_text(song->name, projects::name_of(path));
        song->tracks[0] = fresh_track(TrackKind::Kit, 0);
        song->tracks[1] = fresh_track(TrackKind::Synth, 1);
    } else {
        unsigned first_scope = played;
        migrated = migrate(path, *song, states, folder_of, this, first_scope);
        if (migrated) played = first_scope;
    }
    selected = played % melodee::track_count;
    setup_modules_from(states);
    engine = std::make_unique<melodee::EngineHost>();
    engine->set_overload_guard(true); // as Melodee runs on the MPC
    engine->set_parking(true);        // a project rebuilt while the audio thread waits, silent (rebuild())
    engine->load(*song, *bank);
    for (unsigned t = 0; t < melodee::track_count; ++t)
        if (modules[t] && modules[t]->entry != melodee::no_plugin) engine->offer_plugin(modules[t]->entry, &modules[t]->runner);
    pad_response.store(settings.response);
    pad_pressure.store(settings.pressure);
    send({Type::Click, 0, 0, uint16_t(click_volume), int16_t(click_on)});
    set_module_tempo(song->tempo / 10.0f);
    audio_track.store(int(selected));
    audio_kind.store(int(kind(selected)));
    audio_octave.store(octave(selected));

    // The screens, one of each, and the track's Play screen shown.
    for (const auto& entry : registered_screens()) {
        auto made = entry.make(*this);
        made->own_id = entry.id;
        screens.push_back(std::move(made));
    }
    home();
    history.reset(live());

    if (migrated) {
        // The first scope's side files folded in: saved whole, then kept aside.
        if (auto saving = write(project_bytes(), settings_text()); saving.empty()) retire_side_files(path);
        else std::fprintf(stderr, "melodee-move: migrating %s: %s\n", path.c_str(), saving.c_str());
    }
}

Studio::~Studio() {
    for (auto& module : modules) if (module) module->player.stop();
    for (auto& module : modules_leaving) if (module) module->player.stop();
    std::unique_lock<std::mutex> guard(lock);
    if (!dirty) return;
    auto bytes = project_bytes();
    auto settings = settings_text();
    dirty = false;
    guard.unlock();
    write(std::move(bytes), std::move(settings));
}

// ---------------------------------------------------------------- audio thread

void Studio::forward() {
    melodee::Command command;
    while (commands.pop(command)) engine->send(command); // a full host queue counts it lost (status: dropped)
    // The service thread wants the engine parked (rebuild()): the Park goes
    // behind every command sent before it, from the queue's one producer.
    if (park_asked.load(std::memory_order_acquire) && !park_serial.load(std::memory_order_relaxed)) {
        park_asked.store(false, std::memory_order_relaxed);
        park_serial.store(engine->park(), std::memory_order_release);
    }
}

void Studio::render(const move::AudioBlock& input, move::AudioBlock& output) {
    forward();
    module_input(input);
    engine->render(output.data(), move::audio_frames, volume.load(std::memory_order_relaxed), input.data());
}

void Studio::audio_opened(const move::AudioFormat& format) {
    engine->set_audio_format(double(format.rate), 0);
}

void Studio::audio_event(const move::SurfaceEvent& event) {
    using Kind = move::Input::Kind;
    forward(); // whatever the screens sent first (a pad's sound set this cycle plays at once)
    const auto& input = event.input;
    if (input.index >= pad_count) return;
    Went& went = pad_went[input.index];
    if (input.kind == Kind::Pad) {
        if (input.pressed) {
            went = {};
            if (pads_typing.load(std::memory_order_relaxed)) return; // a letter, not a note
            if (pads_quiet.load(std::memory_order_relaxed)) return;  // another job (docs/MOVE_CONTROLS.md)
            int track = audio_track.load(std::memory_order_relaxed);
            auto kind = TrackKind(audio_kind.load(std::memory_order_relaxed));
            int key = kind == TrackKind::Kit ? int(melodee::sampler_first_note + input.index)
                    : kind == TrackKind::Synth || kind == TrackKind::Keys || kind == TrackKind::Midi || kind == TrackKind::Plugin
                        ? note_for(input.index, audio_octave.load(std::memory_order_relaxed)) : -1;
            if (key < 0) return;
            went = {track, key};
            int velocity = velocity_of(pad_response.load(std::memory_order_relaxed), input.value); // Setup's pad response
            engine->send({Type::NoteOn, uint8_t(track), 0, uint16_t(key), int16_t(velocity)});
        } else if (went.track >= 0) {
            engine->send({Type::NoteOff, uint8_t(went.track), 0, uint16_t(went.key), 0});
            went = {};
        }
    } else if (input.kind == Kind::PadPressure && went.track >= 0 && went.key < 128 && pad_pressure.load(std::memory_order_relaxed)) {
        // Melodee's sampler takes no pressure; a note track's engine does.
        if (TrackKind(audio_kind.load(std::memory_order_relaxed)) != TrackKind::Kit)
            engine->send({Type::Pressure, uint8_t(went.track), 0, uint16_t(went.key), int16_t(input.value)});
    }
}

// ---------------------------------------------------------------- the model, under `lock`

bool Studio::send(const melodee::Command& command) {
    if (commands.push(command)) return true;
    say("BUSY, TRY AGAIN");
    return false;
}

void Studio::say(std::string text) {
    message = std::move(text);
    message_until = Clock::now() + message_time;
}

void Studio::changed() {
    dirty = true;
    changed_at = Clock::now();
}

TrackKind Studio::kind(unsigned t) const {
    const auto& track = song->tracks[t % melodee::track_count];
    switch (track.engine) {
    case EngineKind::Sampler: return TrackKind::Kit;
    case EngineKind::Keygroup: return TrackKind::Keys;
    case EngineKind::Midi: return TrackKind::Midi;
    case EngineKind::Audio: return TrackKind::Audio;
    case EngineKind::Plugin: return modules[t % melodee::track_count] ? TrackKind::Synth : TrackKind::Plugin;
    default: return TrackKind::Empty;
    }
}

int Studio::octave(unsigned t) const { return int(song->tracks[t % melodee::track_count].octave) - 3; }

void Studio::set_octave(unsigned t, int value) {
    song->tracks[t % melodee::track_count].octave = int8_t(3 + value);
    if (t == selected) audio_octave.store(value, std::memory_order_relaxed);
    changed();
}

int Studio::note_for(unsigned at, int octave_now) const {
    return std::clamp(synth_base_note + 12 * octave_now + int(move::pad_column(at)) + 5 * int(move::pad_row(at)), 0, 127);
}

void Studio::select(unsigned t) {
    t %= melodee::track_count;
    if (typing) finish_typing(true);
    if (t != selected) {
        selected = t;
        message_until = {}; // the other track's news
        changed();
    }
    audio_track.store(int(t), std::memory_order_relaxed);
    audio_kind.store(int(kind(t)), std::memory_order_relaxed);
    audio_octave.store(octave(t), std::memory_order_relaxed);
    home();
}

void Studio::apply_track(unsigned t) {
    auto* pack = new melodee::TrackPack(t, song->tracks[t], bank->at[t]); // freed once the audio thread hands it back
    Command command {Type::TrackLoad, uint8_t(t)};
    command.track_pack = pack;
    if (!send(command)) delete pack;
}

ModuleTrack& Studio::make_module_track(unsigned t) {
    modules[t] = std::make_unique<ModuleTrack>();
    modules[t]->page = 0;
    return *modules[t];
}

void Studio::drop_module_track(unsigned t) {
    auto& module = modules[t];
    if (!module) return;
    if (module->entry != melodee::no_plugin && engine) {
        // Its runner is the audio thread's until it is handed back.
        song->plugins[module->entry] = melodee::PluginRef {};
        engine->offer_plugin(module->entry, nullptr);
        modules_leaving.push_back(std::move(module));
    } else {
        module.reset();
    }
}

bool Studio::new_track(unsigned t, TrackKind made) {
    t %= melodee::track_count;
    if (song->tracks[t].used() || made == TrackKind::Empty || made == TrackKind::Plugin) return false;
    song->tracks[t] = fresh_track(made, t);
    if (made == TrackKind::Synth) make_module_track(t);
    apply_track(t);
    changed();
    return true;
}

void Studio::delete_track(unsigned t) {
    t %= melodee::track_count;
    drop_module_track(t);
    auto mix = song->tracks[t].mix; // its mix strip stays, as Melodee's Delete leaves it
    song->tracks[t] = melodee::Track {};
    song->tracks[t].mix = mix;
    for (auto& pattern : bank->at[t]) pattern = melodee::Pattern {};
    apply_track(t);
    if (t == selected) select(t);
    changed();
}

bool Studio::copy_track(unsigned from, unsigned to) {
    from %= melodee::track_count;
    to %= melodee::track_count;
    if (from == to || !song->tracks[from].used()) return false;
    drop_module_track(to);
    auto copy = song->tracks[from];
    copy_text(copy.name, std::string(song->tracks[from].name).substr(0, 20) + "+");
    if (copy.engine == EngineKind::Plugin && modules[from]) {
        // Its own instance of the same module, set as the one copied.
        auto& module = make_module_track(to);
        copy.plugin = melodee::no_plugin;
        if (auto* source = modules[from].get(); source->module) {
            module.saved.module = source->module->info().id;
            module.saved.preset = source->module->preset_param.empty() ? -1 : source->module->preset;
            for (const auto& param : source->module->params)
                if (param.key != source->module->preset_param && !param.trigger()) module.saved.values.emplace_back(param.key, param.text());
        } else {
            module.saved = source->saved;
        }
        module.job = module.saved.module;
        module.job_restores = true;
    } else if (copy.engine == EngineKind::Plugin) {
        return false; // another host's plug-in: its own copy is its host's to make
    }
    copy.mix = song->tracks[to].mix; // the target keeps its strip, as Melodee's Copy leaves it
    song->tracks[to] = copy;
    bank->at[to] = bank->at[from];
    apply_track(to);
    if (to == selected) select(to);
    changed();
    return true;
}

ModuleTrack* Studio::module_track(unsigned t) { return modules[t % melodee::track_count].get(); }

void Studio::select_pad(unsigned at) {
    auto& track = current();
    if (at < pad_count && at != track.last_pad) { track.last_pad = uint8_t(at); changed(); }
}

const Sample* Studio::sample_of(unsigned t, unsigned at) const {
    const auto& track = song->tracks[t % melodee::track_count];
    if (at >= pad_count || !track.pads[at].used() || track.pads[at].sample >= samples.size()) return nullptr;
    const auto& sample = samples[track.pads[at].sample];
    return sample && sample->data ? sample.get() : nullptr; // a file that was missing at load plays nothing
}

void Studio::set_pad(unsigned t, unsigned at, const melodee::PadSound& sound) {
    song->tracks[t].pads[at] = sound;
    sound_pad(t, at, sound);
    changed();
}

void Studio::sound_pad(unsigned t, unsigned at, const melodee::PadSound& sound) {
    Command command {Type::Pad, uint8_t(t), 0, uint16_t(at)};
    command.pad = sound;
    send(command);
}

void Studio::sound_pads(unsigned t) {
    for (unsigned at = 0; at < pad_count; ++at) sound_pad(t, at, song->tracks[t].pads[at]);
}

melodee::SampleChop* Studio::chop_of(unsigned slot) {
    return slot < melodee::sample_slots ? &song->chops.edit(slot) : nullptr;
}

std::string Studio::install(std::unique_ptr<Sample> sample, unsigned t, unsigned at) {
    if (song->tracks[t].engine != EngineKind::Sampler) return "NOT A KIT";
    // The first slot no file of the project names.
    unsigned slot = 0;
    while (slot < melodee::sample_slots && (song->samples[slot].path[0] || (slot < samples.size() && samples[slot]))) ++slot;
    if (slot >= melodee::sample_slots) return "MEMORY FULL";
    if (!engine->offer_sample(slot, sample->data.get())) return "BUSY, TRY AGAIN";
    auto& ref = song->samples.edit(slot);
    copy_text(ref.path, sample->path);
    copy_text(ref.name, sample->name);
    ref.frames = sample->data->model_frames();
    if (slot >= samples.size()) samples.resize(slot + 1);
    samples[slot] = std::move(sample);
    melodee::PadSound sound;
    sound.sample = uint16_t(slot);
    set_pad(t, at, sound);
    char text[24];
    std::snprintf(text, sizeof text, "ON %c%02u", 'A' + int(at / 16), at % 16 + 1);
    say(text);
    return {};
}

// ---------------------------------------------------------------- transport and tempo

void Studio::play() { send({Type::Play}); }
void Studio::stop() { send({Type::Stop}); }
void Studio::restart() {
    if (!engine->playing()) return;
    Command command {Type::Locate};
    command.position = 0;
    send(command);
}

void Studio::set_tempo(unsigned tenths) {
    song->tempo = uint16_t(std::clamp(tenths, 300u, 3000u));
    send({Type::Tempo, 0, 0, 0, int16_t(song->tempo)});
    set_module_tempo(song->tempo / 10.0f);
    changed();
}

void Studio::set_swing(unsigned percent) {
    song->swing = uint8_t(std::clamp(percent, 50u, 75u));
    send({Type::Swing, 0, 0, 0, int16_t(song->swing)});
    changed();
}

void Studio::set_level(unsigned t, int level) {
    auto& mix = song->tracks[t % melodee::track_count].mix;
    mix.level = uint8_t(std::clamp(level, 0, 127));
    send({Type::Level, uint8_t(t), 0, 0, int16_t(mix.level)});
    changed();
}

void Studio::set_mute(unsigned t, bool on) {
    song->tracks[t % melodee::track_count].mix.mute = on;
    send({Type::Mute, uint8_t(t), 0, 0, int16_t(on)});
    changed();
}

void Studio::set_solo(unsigned t, bool on) {
    song->tracks[t % melodee::track_count].mix.solo = on;
    send({Type::Solo, uint8_t(t), 0, 0, int16_t(on)});
    changed();
}

void Studio::set_click(bool on, unsigned level) {
    click_on = on;
    click_volume = std::min(level, 100u);
    send({Type::Click, 0, 0, uint16_t(click_volume), int16_t(click_on)});
    changed();
}

// ---------------------------------------------------------------- Schwung's modules, under `lock`

void Studio::synth_set(ModuleTrack& track, const std::string& key, const std::string& value) {
    track.player.set(key, value);
    track.set_at[key] = track.player.last_serial();
    if (std::find(track.edited.begin(), track.edited.end(), key) == track.edited.end()) track.edited.push_back(key); // undo compares it
}

std::string Studio::module_get(ModuleTrack& track, const std::string& key) {
    if (!track.module) return {};
    if (!track.player.rendering()) return track.module->get_now(key); // no audio thread: the module is this thread's to ask
    std::string value;
    track.player.get(key, value, std::chrono::milliseconds(20));
    return value;
}

void Studio::rescan_modules() { catalog = scan_modules(modules_root); }

CanvasPage::Host Studio::canvas_host(unsigned t) {
    CanvasPage::Host host;
    host.get = [this, t](const std::string& key) { auto* module = module_track(t); return module ? module_get(*module, key) : std::string(); };
    host.set = [this, t](const std::string& key, const std::string& value) {
        auto* module = module_track(t);
        if (!module) return;
        synth_set(*module, key, value);
        if (ModuleParam* param = module->module ? module->module->param(key) : nullptr) param->take(value); // the knobs and the save see it too
    };
    host.text_entry = [this, t](const std::string& title, const std::string& initial) {
        auto* module = module_track(t);
        typing = true;
        typed = initial;
        typing_title = title;
        typing_track = int(t);
        typing_canvas = -1;
        if (module && module->module) {
            if (module->canvas_open >= 0) typing_canvas = module->canvas_open;
            else if (module->page < module->module->pages.size()) typing_canvas = module->module->pages[module->page].canvas;
        }
        pads_typing.store(true);
    };
    host.open_file = [this](const std::string& file) { // a download: onto the first empty pad of a kit, to play and chop
        for (unsigned kit = 0; kit < melodee::track_count; ++kit) {
            if (song->tracks[kit].engine != EngineKind::Sampler) continue;
            unsigned free_pad = 0;
            while (free_pad < pad_count && song->tracks[kit].pads[free_pad].used()) ++free_pad;
            if (free_pad == pad_count) continue;
            jobs.push_back({file, kit, free_pad});
            char text[24];
            std::snprintf(text, sizeof text, "TO KIT %u %c%02u", kit + 1, 'A' + int(free_pad / 16), free_pad % 16 + 1);
            say(text);
            return;
        }
        say("NO EMPTY KIT PAD");
    };
    return host;
}

void Studio::finish_typing(bool cancelled) {
    typing = false;
    pads_typing.store(false);
    auto* module = typing_track >= 0 ? module_track(unsigned(typing_track)) : nullptr;
    int index = typing_canvas;
    typing_canvas = typing_track = -1;
    if (module && index >= 0 && index < int(module->canvas_pages.size()) && module->canvas_pages[size_t(index)])
        module->canvas_pages[size_t(index)]->text_entered(typed, cancelled);
}

// ---------------------------------------------------------------- undo (history.h)

Live Studio::live() {
    Live now {*song, *bank, move_extras};
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        const auto* module = modules[t].get();
        if (!module) continue;
        auto& state = now.modules[t];
        state.present = true;
        bool pending = module->loading || !module->job.empty();
        if (pending && module->job_restores) { // loading as it was saved, copied or undone: that is its state
            state.file = module->saved;
        } else if (pending) {
            state.settled = false; // a module picked: its own step closes once it is in (load_module_now())
        } else if (module->module) {
            state.file.module = module->module->info().id;
            state.file.preset = module->module->preset_param.empty() ? -1 : module->module->preset;
            for (const auto& param : module->module->params)
                if (param.key != module->module->preset_param && !param.trigger()) state.file.values.emplace_back(param.key, param.text());
        } else {
            state.file = module->saved; // missing: named as saved
        }
        state.file.track = t;
        state.file.octave = 0;
        now.edited[t] = module->edited;
    }
    return now;
}

void Studio::name_step(std::string what, int track) {
    step_name = std::move(what);
    step_track = track;
}

void Studio::not_a_step() { step_kept = false; }

void Studio::hold_step(std::string what, int track) {
    close_step(); // what came before is a step of its own
    holding_step = true;
    name_step(std::move(what), track);
}

void Studio::release_step() {
    holding_step = false;
    close_step();
}

bool Studio::close_step(const std::string& fallback, bool keep) {
    if (holding_step) return false;
    std::string what = !step_name.empty() ? step_name : !fallback.empty() ? fallback : "EDIT";
    bool kept = history.close(live(), what, step_track, Clock::now(), keep && step_kept);
    for (auto& module : modules)
        if (module) module->edited.clear();
    step_name.clear();
    step_track = -1;
    step_kept = true;
    return kept;
}

bool Studio::undo() {
    close_step();
    auto now = live();
    const Step* step = history.undo(now);
    if (!step) return false;
    last_step = step->what;
    apply_step(*step, false);
    return true;
}

bool Studio::redo() {
    close_step();
    auto now = live();
    const Step* step = history.redo(now);
    if (!step) return false;
    last_step = step->what;
    apply_step(*step, true);
    return true;
}

void Studio::apply_step(const Step& step, bool forward) {
    auto module_of = [&](unsigned t) -> const Step::Module* {
        for (const auto& change : step.modules)
            if (change.track == t) return &change;
        return nullptr;
    };
    // Each track as it was: its Schwung module track made or let go as its
    // engine now says, then the engine's track made whole again.
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        if (!step.tracks.test(t)) continue;
        const auto& track = song->tracks[t];
        const auto* change = module_of(t);
        const ModuleState* state = change && change->whole ? (forward ? &change->after : &change->before) : nullptr;
        bool synth = track.engine == EngineKind::Plugin && (track.plugin == melodee::no_plugin || !song->plugins[track.plugin].used() || modules[t] || (state && state->present));
        if (!synth && modules[t]) drop_module_track(t);
        if (synth && !modules[t]) make_module_track(t);
        if (auto* module = modules[t].get(); module && module->entry == melodee::no_plugin && track.plugin != melodee::no_plugin) {
            module->entry = track.plugin; // its entry as the project names it again
            engine->offer_plugin(module->entry, &module->runner);
        }
        if (auto* module = modules[t].get(); module && state && state->present) {
            const auto& file = state->file;
            if (file.module.empty()) {
                if (module->module) { drop_module_track(t); make_module_track(t); } // back to waiting for one
            } else if (!module->module || module->module->info().id != file.module) {
                module->saved = file; // that module again, as it was (the service thread loads it)
                module->job = file.module;
                module->job_restores = true;
            } else {
                auto& loaded = *module->module;
                if (file.preset >= 0 && !loaded.preset_param.empty() && file.preset != loaded.preset) {
                    loaded.preset = file.preset;
                    if (auto* param = loaded.param(loaded.preset_param)) param->value = file.preset;
                    synth_set(*module, loaded.preset_param, std::to_string(file.preset));
                }
                for (const auto& [key, value] : file.values)
                    if (ModuleParam* param = loaded.param(key); param && param->text() != value) {
                        synth_set(*module, key, value);
                        param->take(value);
                    }
            }
        } else if (auto* module = modules[t].get(); module && change && !change->whole && module->module) {
            for (const auto& key : change->keys)
                if (ModuleParam* param = module->module->param(key.key)) {
                    const auto& value = forward ? key.after : key.before;
                    synth_set(*module, key.key, value);
                    param->take(value);
                }
        }
        apply_track(t);
    }
    // The rest of the project: the tempo and swing by their commands; anything
    // else (a bus, the master, the song) by rebuilding the engine, parked.
    if (step.globals) {
        send({Type::Tempo, 0, 0, 0, int16_t(song->tempo)});
        send({Type::Swing, 0, 0, 0, int16_t(song->swing)});
        set_module_tempo(song->tempo / 10.0f);
        if (!step.tempo_only) rebuild_wanted = true;
    }
    // The selected track as the pads play it, and its Play screen if its kind changed.
    auto was = TrackKind(audio_kind.load(std::memory_order_relaxed));
    audio_kind.store(int(kind(selected)), std::memory_order_relaxed);
    audio_octave.store(octave(selected), std::memory_order_relaxed);
    if (kind(selected) != was) home();
    // What the modules' remaking changed (an entry let go) is no step.
    history.close(live(), {}, -1, Clock::now(), false);
    for (auto& module : modules)
        if (module) module->edited.clear();
    changed();
}

// ---------------------------------------------------------------- the screens

Screen* Studio::top() const { return stack.empty() ? nullptr : stack.back(); }

Screen* Studio::screen(const std::string& id) const {
    for (const auto& made : screens)
        if (made->id() == id) return made.get();
    return nullptr;
}

void Studio::home() {
    while (!stack.empty()) { auto* left = stack.back(); stack.pop_back(); left->leave(); }
    if (auto* play = screen(home_screen(kind()))) { stack.push_back(play); play->enter(); }
}

void Studio::open(const std::string& id) {
    auto* to = screen(id);
    if (!to) return;
    while (stack.size() > 1) { auto* left = stack.back(); stack.pop_back(); left->leave(); }
    stack.push_back(to);
    to->enter();
}

void Studio::push(const std::string& id) {
    auto* to = screen(id);
    if (!to) return;
    stack.push_back(to);
    to->enter();
}

void Studio::close() {
    if (stack.size() <= 1) return;
    auto* left = stack.back();
    stack.pop_back();
    left->leave();
}

// ---------------------------------------------------------------- service thread

std::string Studio::settings_text() const {
    std::ostringstream out;
    out << settings_header << '\n' << "track " << selected << '\n' << "click " << int(click_on) << '\n' << "click_level " << click_volume << '\n'
        << "response " << unsigned(settings.response) << '\n' << "pressure " << int(settings.pressure) << '\n'
        << "pad_lights " << unsigned(settings.pad_lights) << '\n' << "pad_fade " << unsigned(settings.pad_fade) << '\n'
        << "brightness " << unsigned(settings.brightness) << '\n' << "light_show " << int(settings.light_show) << '\n'
        << "show_style " << unsigned(settings.show_style) << '\n' << "full " << int(settings.full) << '\n'
        << "usage_log " << int(settings.usage_log) << '\n' << "last " << path << '\n';
    if (!settings.start.empty()) out << "start " << settings.start << '\n';
    return out.str();
}

void Studio::read_settings() {
    std::ifstream in(settings_path);
    std::string line;
    if (!std::getline(in, line) || line != settings_header) return;
    while (std::getline(in, line)) {
        auto space = line.find(' ');
        if (space == std::string::npos) continue;
        std::string word = line.substr(0, space), rest = line.substr(space + 1);
        unsigned value = unsigned(std::strtoul(rest.c_str(), nullptr, 10));
        if (word == "track") last_track = value % melodee::track_count;
        else if (word == "click") click_on = value != 0;
        else if (word == "click_level") click_volume = std::min(value, 100u);
        else if (word == "response") settings.response = uint8_t(std::min(value, 3u));
        else if (word == "pressure") settings.pressure = value != 0;
        else if (word == "pad_lights") settings.pad_lights = uint8_t(std::min(value, 2u));
        else if (word == "pad_fade") settings.pad_fade = uint8_t(std::min(value, 3u));
        else if (word == "brightness") settings.brightness = uint8_t(std::min(value, 3u));
        else if (word == "light_show") settings.light_show = value != 0;
        else if (word == "show_style") settings.show_style = uint8_t(std::min(value, 4u));
        else if (word == "full") settings.full = value != 0;
        else if (word == "usage_log") settings.usage_log = value != 0;
        else if (word == "last") last_path = rest;
        else if (word == "start") settings.start = rest;
    }
}

void Studio::settings_changed() {
    pad_response.store(settings.response, std::memory_order_relaxed);
    pad_pressure.store(settings.pressure, std::memory_order_relaxed);
    dirty = true; // written with the project at the next save (Move.settings beside it)
    changed_at = Clock::now();
}

std::vector<uint8_t> Studio::project_bytes() const {
    // Each Schwung track's module, as its PLUG entry's state.
    PluginStates states;
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        const auto* module = modules[t].get();
        if (!module || module->entry == melodee::no_plugin) continue;
        SynthFile file;
        file.track = t;
        file.octave = octave(t);
        if (module->module) {
            file.module = module->module->info().id;
            file.preset = module->module->preset_param.empty() ? -1 : module->module->preset;
            for (const auto& param : module->module->params)
                if (param.key != module->module->preset_param && !param.trigger()) file.values.emplace_back(param.key, param.text());
        } else { // a module that would not load this time stays named, for when it is back
            file.module = module->saved.module;
            file.preset = module->saved.preset;
            file.values = module->saved.values;
        }
        states[module->entry] = synth_state(file);
    }
    return project_file_bytes(*song, *bank, states, &move_extras);
}

std::string Studio::write(std::vector<uint8_t> bytes, std::string settings) {
    std::error_code error;
    auto projects = fs::path(path).parent_path();
    fs::create_directories(projects, error);
    give_to_owner_of(home_path, move_user_data);
    give_to_owner_of(projects.string(), move_user_data);
    auto failed = write_whole(path, bytes.data(), bytes.size());
    if (failed.empty()) failed = write_whole(settings_path, settings.data(), settings.size());
    if (failed.empty())
        for (const auto& file : {path, settings_path}) give_to_owner_of(file, move_user_data);
    return failed;
}

std::string Studio::save_now() {
    std::unique_lock<std::mutex> guard(lock);
    auto bytes = project_bytes();
    auto settings = settings_text();
    dirty = false;
    guard.unlock();
    auto error = write(std::move(bytes), std::move(settings));
    guard.lock();
    if (error.empty()) ++saves;
    else dirty = true;
    return error;
}

unsigned Studio::read_missing_samples() {
    std::vector<std::pair<unsigned, std::string>> wanted;
    {
        std::lock_guard<std::mutex> guard(lock);
        for (size_t slot = 0; slot < melodee::sample_slots; ++slot)
            if (song->samples[slot].path[0] && (slot >= samples.size() || !samples[slot])) wanted.emplace_back(unsigned(slot), song->samples[slot].path);
        opening_samples = unsigned(wanted.size());
        opening_samples_done = 0;
    }
    // Every sample read again, outside the lock; a file that has gone keeps
    // its place (and its pads), silent, so saving again keeps it too.
    unsigned missing = 0;
    for (const auto& [slot, file] : wanted) {
        {
            std::lock_guard<std::mutex> guard(lock);
            opening_file = fs::path(file).filename().string();
        }
        std::string failed;
        auto sample = read_sample(file, failed);
        std::lock_guard<std::mutex> guard(lock);
        ++opening_samples_done;
        if (!sample) { ++missing; continue; }
        if (!engine->offer_sample(slot, sample->data.get())) { ++missing; continue; }
        if (song->samples[slot].frames != sample->data->model_frames()) song->samples.edit(slot).frames = sample->data->model_frames();
        if (slot >= samples.size()) samples.resize(slot + 1);
        samples[slot] = std::move(sample);
    }
    return missing;
}

std::string Studio::restore() {
    {
        std::lock_guard<std::mutex> guard(lock);
        if (restored) return {};
        restored = true;
        opening_synths = opening_synths_done = 0;
        for (const auto& module : modules) opening_synths += module && !module->job.empty() && module->job_restores;
    }
    unsigned missing = read_missing_samples();
    bool any = false;
    {
        std::lock_guard<std::mutex> guard(lock);
        any = opening_samples > 0;
    }
    // The modules, each as it was saved.
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        SynthFile saved;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!modules[t] || modules[t]->job.empty() || !modules[t]->job_restores) continue;
            saved = modules[t]->saved;
            modules[t]->job.clear();
            modules[t]->job_restores = false;
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            opening_file = upper(saved.module);
        }
        auto failed = load_module_now(t, saved.module, &saved);
        std::lock_guard<std::mutex> guard(lock);
        ++opening_synths_done;
        if (!failed.empty()) say(upper(saved.module + " MISSING").substr(0, 16));
    }
    std::lock_guard<std::mutex> guard(lock);
    opening = false; // the Loading sheet down
    if (missing) say(std::to_string(missing) + " FILES MISSING");
    else if (any) say("PROJECT LOADED");
    history.reset(live()); // the project as opened: no step undoes the opening
    return {};
}

// ---------------------------------------------------------------- the engine rebuilt, parked

Studio::Parked Studio::park_engine() {
    // The audio thread parks the engine at its next block, behind every
    // command sent before (forward()); with no audio thread (a test, a tool)
    // there is nothing to wait for.
    uint64_t blocks = engine->blocks_rendered();
    park_serial.store(0, std::memory_order_relaxed);
    park_asked.store(true, std::memory_order_release);
    auto began = Clock::now();
    while (Clock::now() - began < std::chrono::seconds(2)) {
        uint32_t serial = park_serial.load(std::memory_order_acquire);
        if (serial && engine->parked(serial)) return {Parked::Yes, serial};
        if (!serial && Clock::now() - began > std::chrono::milliseconds(60) && engine->blocks_rendered() == blocks) {
            park_asked.store(false, std::memory_order_relaxed);
            if (!park_serial.load(std::memory_order_acquire)) return {Parked::NoAudio, 0};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    park_asked.store(false, std::memory_order_relaxed);
    if (uint32_t serial = park_serial.load(std::memory_order_acquire)) engine->resume(serial); // called off
    return {Parked::Failed, 0};
}

std::string Studio::rebuild() {
    std::unique_ptr<melodee::ProjectPack> pack;
    bool was_playing = false;
    {
        std::lock_guard<std::mutex> guard(lock);
        rebuild_wanted = false;
        pack = std::make_unique<melodee::ProjectPack>(*song, *bank); // allocates: here, never on the audio thread
        was_playing = engine->playing();
    }
    auto parked = park_engine();
    if (parked.result == Parked::Failed) {
        std::lock_guard<std::mutex> guard(lock);
        rebuild_wanted = true; // tried again on the next pass
        return "busy";
    }
    engine->reload(*pack, false); // the audio thread waits, silent; Melodee's way of loading without a late block
    if (parked.result == Parked::Yes) engine->resume(parked.serial);
    ++rebuilds;
    pack.reset(); // the patterns played before, freed here
    std::lock_guard<std::mutex> guard(lock);
    send({Type::Click, 0, 0, uint16_t(click_volume), int16_t(click_on)});
    if (was_playing) play(); // reload stops the transport: it goes on from the top
    return {};
}

void Studio::take_back() {
    const melodee::TrackPack* track_pack = nullptr;
    while (engine->take_done_track(track_pack)) delete track_pack;
    const melodee::ProjectPack* project_pack = nullptr;
    while (engine->take_done_pack(project_pack)) delete project_pack;
    melodee::Pattern* pattern = nullptr;
    while (engine->take_retired_pattern(pattern)) delete pattern;
    const melodee::SampleData* sample = nullptr;
    std::vector<std::unique_ptr<Sample>> let_go;
    while (engine->take_retired(sample)) { // the samples are the studio's, kept for the run; another project's go now
        std::lock_guard<std::mutex> guard(lock);
        auto at = std::find_if(samples_leaving.begin(), samples_leaving.end(), [sample](const auto& s) { return s && s->data.get() == sample; });
        if (at == samples_leaving.end()) continue;
        let_go.push_back(std::move(*at));
        samples_leaving.erase(at);
    }
    let_go.clear(); // freed outside the lock
    melodee::MidiOutEvent out;
    while (engine->take_midi(out)) {} // MIDI out: not yet on the Move
    // A Schwung track's runner, handed back: its track can go now.
    std::vector<std::unique_ptr<ModuleTrack>> gone;
    {
        std::lock_guard<std::mutex> guard(lock);
        melodee::PluginRunner* runner = nullptr;
        while (engine->take_retired_plugin(runner)) {
            auto at = std::find_if(modules_leaving.begin(), modules_leaving.end(), [runner](const auto& m) { return &m->runner == runner; });
            if (at == modules_leaving.end()) continue;
            gone.push_back(std::move(*at));
            modules_leaving.erase(at);
        }
    }
    gone.clear(); // its thread stopped and its modules destroyed, outside the lock
}

void Studio::take_answers() {
    std::lock_guard<std::mutex> guard(lock);
    for (auto& track : modules) {
        if (!track) continue;
        // A call that held the audio thread up: logged, with what it was.
        SynthPlayer::Slow slow;
        while (track->player.slow(slow)) {
            ++slow_calls;
            slowest_us = std::max(slowest_us, slow.us);
            std::fprintf(stderr, "melodee-move: slow synth call: %s %s took %.1f ms\n", track->module ? track->module->info().id.c_str() : "-",
                         slow.what, slow.us / 1000.0);
        }
        SynthPlayer::Answer answer;
        while (track->player.answer(answer)) {
            if (!track->module) continue;
            std::string key = answer.key, value = answer.value;
            // Asked before a later set of the same key: the module has the newer value.
            if (auto set = track->set_at.find(key); set != track->set_at.end() && answer.serial < set->second) continue;
            auto& module = *track->module;
            if (key == module.preset_name_param) module.preset_name = value;
            else if (key == module.preset_count_param) module.preset_count = std::atoi(value.c_str());
            else if (auto* param = module.param(key)) {
                param->take(value);
                if (key == module.preset_param) module.preset = int(param->value);
            }
        }
        // An items list, read whole: [{"index": 0, "label": "GM"}, ...] or names.
        std::string text;
        if (track->pick_reading && track->player.long_answer(text)) {
            track->pick_reading = false;
            track->pick_items.clear();
            juce::var list;
            if (juce::JSON::parse(juce::String::fromUTF8(text.c_str()), list).wasOk())
                if (auto* entries = list.getArray())
                    for (int n = 0; n < entries->size(); ++n) {
                        const auto& entry = entries->getReference(n);
                        if (entry.isString()) { track->pick_items.emplace_back(n, entry.toString().toStdString()); continue; }
                        auto* object = entry.getDynamicObject();
                        if (!object) continue;
                        int index = object->hasProperty("index") ? int(object->getProperty("index")) : n;
                        auto label = object->hasProperty("label") ? object->getProperty("label") : object->getProperty("name");
                        track->pick_items.emplace_back(index, label.toString().toStdString());
                    }
            track->pick_key.clear();
        }
        if (!track->pick_key.empty() && !track->pick_reading) track->pick_reading = track->player.ask_long(track->pick_key);
    }
}

void Studio::destroy_retired() {
    std::vector<std::unique_ptr<LoadedModule>> gone;
    {
        std::lock_guard<std::mutex> guard(lock);
        for (auto& track : modules) {
            if (!track) continue;
            while (LoadedModule* old = track->player.retired()) {
                auto at = std::find_if(track->leaving.begin(), track->leaving.end(), [old](const auto& held) { return held.get() == old; });
                if (at == track->leaving.end()) continue;
                gone.push_back(std::move(*at));
                track->leaving.erase(at);
            }
        }
    }
    gone.clear(); // destroy_instance(), outside the lock
}

void Studio::service() {
    if (!restored) restore();
    take_back();
    take_answers();
    destroy_retired();
    // A module asked for (a track's job).
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        std::string wanted;
        SynthFile saved;
        bool with_saved = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!modules[t] || modules[t]->job.empty()) continue;
            wanted.swap(modules[t]->job);
            with_saved = modules[t]->job_restores; // a copied track: set as the one copied
            modules[t]->job_restores = false;
            saved = modules[t]->saved;
        }
        auto failed = load_module_now(t, wanted, with_saved ? &saved : nullptr);
        std::lock_guard<std::mutex> guard(lock);
        if (!failed.empty()) say(upper(failed).substr(0, 16));
    }
    std::vector<Job> todo;
    {
        std::lock_guard<std::mutex> guard(lock);
        todo.swap(jobs);
        loading = !todo.empty();
    }
    for (const auto& job : todo) {
        std::string error;
        auto sample = read_sample(job.path, error);
        std::lock_guard<std::mutex> guard(lock);
        if (!sample) { say(upper(error).substr(0, 16)); continue; }
        if (auto failed = install(std::move(sample), job.track, job.pad); !failed.empty()) { say(failed); continue; }
        char label[8];
        std::snprintf(label, sizeof label, "%c%02u", 'A' + int(job.pad / 16), job.pad % 16 + 1);
        name_step(std::string("LOAD ") + label, int(job.track)); // a sample loaded is a step of its own
        close_step();
    }
    bool rebuild_now = false;
    std::vector<ProjectJob> asked;
    {
        std::lock_guard<std::mutex> guard(lock);
        rebuild_now = rebuild_wanted;
        asked.swap(project_jobs);
    }
    if (rebuild_now) rebuild();
    for (const auto& job : asked) run_project_job(job);
    std::unique_lock<std::mutex> guard(lock);
    loading = false;
    // Autosave: three seconds after the last change, as on the MPC.
    auto now = Clock::now();
    if (dirty && now - changed_at >= autosave_idle && now >= retry_at) {
        auto bytes = project_bytes();
        auto settings = settings_text();
        dirty = false;
        guard.unlock();
        auto error = write(std::move(bytes), std::move(settings));
        guard.lock();
        if (error.empty()) ++saves;
        else { dirty = true; retry_at = now + autosave_retry; say("NOT SAVED"); }
    }
}

std::string Studio::load_now(const std::string& file, unsigned at) {
    std::string error;
    auto sample = read_sample(file, error);
    std::lock_guard<std::mutex> guard(lock);
    if (!sample) return error.empty() ? "unreadable" : error;
    return install(std::move(sample), selected, at);
}

uint8_t Studio::free_plugin_entry() const {
    for (unsigned entry = 0; entry < melodee::max_plugins; ++entry) {
        if (song->plugins[entry].used()) continue;
        bool taken = false;
        for (const auto& module : modules) taken |= module && module->entry == entry;
        for (const auto& module : modules_leaving) taken |= module && module->entry == entry;
        if (!taken) return uint8_t(entry);
    }
    return melodee::no_plugin;
}

std::string Studio::load_module_now(unsigned t, const std::string& id, const SynthFile* saved) {
    ModuleInfo info;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!modules[t]) return "not a synth track";
        auto found = std::find_if(catalog.begin(), catalog.end(), [&](const ModuleInfo& choice) { return choice.id == id; });
        if (found == catalog.end()) return "no module " + id;
        info = *found;
        modules[t]->loading = true;
    }
    // Opened, created and set before it plays, outside the lock: a module
    // may take its time (Surge reads its patches).
    std::string error;
    auto loaded = LoadedModule::load(info, error);
    if (loaded && saved && saved->module == id) {
        // A bank (an items list's choice) first, then the preset in it, then
        // each parameter.
        for (const auto& [key, value] : saved->values)
            if (ModuleParam* param = loaded->param(key); param && param->selector) {
                loaded->set_now(key, value);
                param->take(value);
            }
        if (saved->preset >= 0 && !loaded->preset_param.empty()) {
            loaded->set_now(loaded->preset_param, std::to_string(saved->preset));
            loaded->preset = saved->preset;
            for (auto& param : loaded->params) param.take(loaded->get_now(param.key));
        }
        for (const auto& [key, value] : saved->values)
            if (ModuleParam* param = loaded->param(key); param && key != loaded->preset_param && !param->selector) {
                loaded->set_now(key, value);
                param->take(value);
            }
        if (!loaded->preset_name_param.empty()) loaded->preset_name = loaded->get_now(loaded->preset_name_param);
    }
    // Its own pages' scripts, started before it plays (never under the lock:
    // a script's first run may take its time).
    std::vector<std::unique_ptr<CanvasPage>> started;
    if (loaded)
        for (const auto& canvas : loaded->canvases) {
            std::string failed;
            started.push_back(CanvasPage::load(canvas.script, canvas_host(t), failed));
            if (!started.back()) std::fprintf(stderr, "melodee-move: %s's page %s would not start: %s\n", id.c_str(), canvas.key.c_str(), failed.c_str());
        }
    std::unique_lock<std::mutex> guard(lock);
    auto* track = modules[t].get();
    if (!track) return "track gone"; // deleted meanwhile: `loaded` destroyed here, never played
    track->loading = false;
    if (!loaded) return error.empty() ? "would not load" : error;
    // Its first module: an entry of the project's plug-ins, the runner offered.
    if (track->entry == melodee::no_plugin) {
        uint8_t entry = free_plugin_entry();
        if (entry == melodee::no_plugin) return "too many plug-ins";
        track->entry = entry;
        song->tracks[t].plugin = entry;
        engine->offer_plugin(entry, &track->runner);
        send({Type::TrackPlugin, uint8_t(t), 0, 0, int16_t(entry)});
    }
    if (!track->player.set_module(loaded.get())) {
        guard.unlock();
        return "busy, try again";
    }
    auto& ref = song->plugins[track->entry];
    copy_text(ref.path, info.dir);
    copy_text(ref.name, info.name);
    if (track->module) track->leaving.push_back(std::move(track->module));
    track->module = std::move(loaded);
    track->canvas_pages.swap(started); // the module before's pages end with this function, outside the lock
    track->canvas_open = -1;
    track->canvas_entered = false;
    if (typing && typing_track == int(t)) { typing = false; pads_typing.store(false); typing_canvas = typing_track = -1; }
    track->set_at.clear();
    track->page = 0;
    track->menu_open = false;
    track->pick_items.clear();
    track->pick_key.clear();
    track->saved = {};
    if (!saved) {
        if (t == selected && stack.size() > 1) home(); // to play it
        say(upper(info.name).substr(0, 16));
        changed();
        name_step("SYNTH " + upper(info.name), int(t)); // a module picked is a step of its own (one undo brings the old one back)
        close_step();
    } else {
        history.settle(t, live().modules[t]); // back as a step left it: no step of its own
    }
    return {};
}

// ---------------------------------------------------------------- projects

std::string Studio::projects_root() const { return (fs::path(home_path) / "Projects").string(); }

std::string Studio::project_name() const { return projects::name_of(path); }

void Studio::ask_projects(ProjectJob job) { project_jobs.push_back(std::move(job)); }

void Studio::setup_modules_from(const PluginStates& states) {
    // Schwung's modules: each PLUGIN track whose entry holds a module's
    // state, and each PLUGIN track with none yet (a synth track waiting for
    // one). Any other plug-in is another host's to play.
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        const auto& track = song->tracks[t];
        if (track.engine != EngineKind::Plugin) continue;
        SynthFile saved;
        bool ours = track.plugin < melodee::max_plugins && read_synth_state(states[track.plugin], saved);
        if (!ours && track.plugin < melodee::max_plugins && song->plugins[track.plugin].used()) continue;
        auto& module = make_module_track(t);
        if (ours) {
            module.entry = track.plugin;
            module.saved = saved;
            module.job = saved.module;
            module.job_restores = true;
        }
    }
}

void Studio::free_unused_slots() {
    // Melodee's save: a slot no pad plays and no undo or redo could bring
    // back is emptied, its chop with it; nothing is deleted from storage.
    std::vector<bool> used(melodee::sample_slots);
    for (const auto& track : song->tracks)
        for (const auto& pad : track.pads) {
            if (pad.used() && pad.sample < used.size()) used[pad.sample] = true;
            for (const auto& layer : pad.layers)
                if (layer.used() && layer.sample < used.size()) used[layer.sample] = true;
        }
    history.slots_in_use(used);
    unsigned freed = 0;
    for (size_t slot = 0; slot < song->samples.span(); ++slot) {
        if (used[slot] || !song->samples[slot].path[0]) continue;
        if (slot < samples.size() && samples[slot]) {
            if (!engine->offer_sample(unsigned(slot), nullptr)) continue; // the engine's still: next save
            samples_leaving.push_back(std::move(samples[slot]));
        }
        song->samples.edit(slot) = {};
        if (song->chops[slot].used()) song->chops.edit(slot) = {};
        ++freed;
    }
    if (freed) history.close(live(), {}, -1, Clock::now(), false); // no step: no pad plays them
}

std::string Studio::save_project(const std::string& as) {
    std::unique_lock<std::mutex> guard(lock);
    if (!as.empty()) path = as;
    free_unused_slots();
    auto bytes = project_bytes();
    auto settings = settings_text();
    dirty = false;
    std::string file = path;
    guard.unlock();
    auto error = write(std::move(bytes), std::move(settings));
    guard.lock();
    if (error.empty()) ++saves;
    else dirty = true;
    return error;
}

std::string Studio::open_project(const std::string& file, bool fresh) {
    // What is open now, saved first.
    {
        std::unique_lock<std::mutex> guard(lock);
        if (dirty) {
            auto bytes = project_bytes();
            auto settings_now = settings_text();
            dirty = false;
            guard.unlock();
            if (auto failed = write(std::move(bytes), std::move(settings_now)); !failed.empty()) {
                guard.lock();
                dirty = true;
                say("NOT SAVED");
                return failed;
            }
        }
    }
    // The next one read, outside the lock.
    auto next = std::make_unique<melodee::Project>();
    auto next_bank = std::make_unique<melodee::PatternBank>();
    PluginStates states;
    MoveExtras next_extras;
    std::string next_path = file;
    bool copy = false;
    if (fresh) {
        copy_text(next->name, projects::name_of(file));
        next->tracks[0] = fresh_track(TrackKind::Kit, 0);
        next->tracks[1] = fresh_track(TrackKind::Synth, 1);
    } else if (melodee::is_mpc_project(file)) {
        // An MPC project opens as a copy: a .mldp beside it, not saved yet.
        std::ifstream in(file, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        melodee::MpcImport report;
        auto failed = melodee::import_mpc_project(bytes, file, *next, *next_bank, report);
        if (!failed.empty()) return failed;
        auto folder = fs::path(file).parent_path().string();
        next_path = projects::path_for(folder, projects::free_name(folder, projects::name_of(file)));
        copy_text(next->name, projects::name_of(next_path));
        copy = true;
    } else if (auto failed = read_project_file(file, *next, *next_bank, states, &next_extras); !failed.empty()) {
        return failed;
    }
    auto pack = std::make_unique<melodee::ProjectPack>(*next, *next_bank); // allocates: here, never on the audio thread
    {
        std::lock_guard<std::mutex> guard(lock);
        opening = true;
        opening_name = projects::name_of(next_path);
        opening_file.clear();
        opening_samples = opening_samples_done = opening_synths = opening_synths_done = 0;
    }
    // The sound fades out and the audio thread waits while the project is built.
    auto parked = park_engine();
    if (parked.result == Parked::Failed) {
        std::lock_guard<std::mutex> guard(lock);
        opening = false;
        say("BUSY, TRY AGAIN");
        return "busy";
    }
    {
        std::lock_guard<std::mutex> guard(lock);
        if (typing) finish_typing(true);
        for (unsigned t = 0; t < melodee::track_count; ++t) drop_module_track(t);
        // The last project's samples, let go once the audio thread hands them back.
        for (size_t slot = 0; slot < samples.size(); ++slot) {
            if (!samples[slot]) continue;
            engine->offer_sample(unsigned(slot), nullptr);
            samples_leaving.push_back(std::move(samples[slot]));
        }
        samples.clear();
        song.swap(next);
        bank.swap(next_bank);
        move_extras = next_extras;
        engine->reload(*pack, false); // parked: this thread's to do
        setup_modules_from(states);
        for (unsigned t = 0; t < melodee::track_count; ++t)
            if (modules[t] && modules[t]->entry != melodee::no_plugin) engine->offer_plugin(modules[t]->entry, &modules[t]->runner);
        path = next_path;
        selected = 0;
        stack.clear();
        select(0);
        send({Type::Click, 0, 0, uint16_t(click_volume), int16_t(click_on)});
        set_module_tempo(song->tempo / 10.0f);
        restored = false; // the service thread reads its samples and modules next, the Loading sheet up
        history.reset(live());
        dirty = copy || fresh; // an import or a new one saved soon; a project opened is as its file
        changed_at = Clock::now();
        if (parked.result == Parked::Yes) engine->resume(parked.serial); // the sound fades back in
        ++rebuilds;
    }
    pack.reset(); // the last project's patterns, freed here
    next.reset();
    next_bank.reset();
    return {};
}

std::string Studio::load_kit(const std::string& file) {
    auto other = std::make_unique<melodee::Project>();
    auto other_bank = std::make_unique<melodee::PatternBank>();
    PluginStates states;
    if (melodee::is_mpc_project(file)) {
        std::ifstream in(file, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        melodee::MpcImport report;
        if (auto failed = melodee::import_mpc_project(bytes, file, *other, *other_bank, report); !failed.empty()) return failed;
    } else if (auto failed = read_project_file(file, *other, *other_bank, states); !failed.empty()) {
        return failed;
    }
    const melodee::Track* kit = nullptr;
    for (const auto& track : other->tracks)
        if (track.engine == EngineKind::Sampler) { kit = &track; break; }
    if (!kit) return "NO KIT THERE";
    {
        std::lock_guard<std::mutex> guard(lock);
        unsigned t = selected;
        if (song->tracks[t].used() && song->tracks[t].engine != EngineKind::Sampler) return "NOT A KIT TRACK";
        // Its samples into this project's free slots, each once.
        std::map<unsigned, unsigned> slot_of;
        unsigned next_free = 0;
        auto free_slot = [&]() -> int {
            while (next_free < melodee::sample_slots && (song->samples[next_free].path[0] || (next_free < samples.size() && samples[next_free]))) ++next_free;
            return next_free < melodee::sample_slots ? int(next_free++) : -1;
        };
        melodee::Track copy = *kit;
        for (auto& pad : copy.pads) {
            if (!pad.used()) continue;
            auto mapped = slot_of.find(pad.sample);
            if (mapped == slot_of.end()) {
                int slot = free_slot();
                if (slot < 0) return "MEMORY FULL";
                song->samples.edit(unsigned(slot)) = other->samples[pad.sample];
                if (other->chops[pad.sample].used()) song->chops.edit(unsigned(slot)) = other->chops[pad.sample];
                mapped = slot_of.emplace(pad.sample, unsigned(slot)).first;
            }
            pad.sample = uint16_t(mapped->second);
            for (auto& layer : pad.layers) // a pad's other layers: each its own slot too
                if (layer.used()) {
                    auto layered = slot_of.find(layer.sample);
                    if (layered == slot_of.end()) {
                        int slot = free_slot();
                        if (slot < 0) { layer = {}; continue; }
                        song->samples.edit(unsigned(slot)) = other->samples[layer.sample];
                        layered = slot_of.emplace(layer.sample, unsigned(slot)).first;
                    }
                    layer.sample = uint16_t(layered->second);
                }
        }
        copy.mix = song->tracks[t].mix;       // the track keeps its strip
        copy_text(copy.name, std::string(kit->name));
        song->tracks[t] = copy;
        apply_track(t);
        select(t);
        name_step("LOAD KIT", int(t)); // one undo brings the track back as it was
        close_step();
        changed();
    }
    unsigned missing = read_missing_samples();
    std::lock_guard<std::mutex> guard(lock);
    say(missing ? std::to_string(missing) + " FILES MISSING" : "KIT LOADED");
    return {};
}

void Studio::run_project_job(const ProjectJob& job) {
    using Kind = ProjectJob::Kind;
    std::string failed, made;
    auto folder_path = [&] {
        std::lock_guard<std::mutex> guard(lock);
        return listing_folder.empty() ? projects_root() : projects_root() + "/" + listing_folder;
    };
    switch (job.kind) {
    case Kind::List: break;
    case Kind::Open: failed = open_project(job.path); break;
    case Kind::New: {
        auto folder = folder_path();
        auto file = projects::path_for(folder, projects::free_name(folder, "Project"));
        failed = open_project(file, true);
        if (failed.empty()) {
            if (job.pad >= 0) projects::keep_pad(folder, fs::path(file).filename().string(), job.pad);
            failed = save_project();
        }
        break;
    }
    case Kind::Save: failed = save_project(); break;
    case Kind::SaveAs: {
        auto folder = folder_path();
        auto file = projects::path_for(folder, projects::free_name(folder, job.name.empty() ? project_name() : job.name));
        failed = save_project(file);
        if (failed.empty()) projects::keep_pad(folder, fs::path(file).filename().string(), projects::free_pad(folder));
        break;
    }
    case Kind::Rename: {
        bool open_one = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            open_one = job.path == path;
        }
        failed = projects::rename(job.path, job.name, made);
        if (failed.empty() && open_one) {
            std::lock_guard<std::mutex> guard(lock);
            path = made; // the open project follows its file
            copy_text(song->name, job.name);
            changed();
        }
        break;
    }
    case Kind::Duplicate: failed = projects::duplicate(job.path, made); break;
    case Kind::Delete: {
        bool open_one = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            open_one = job.path == path;
        }
        failed = open_one ? "IT IS OPEN" : projects::remove(job.path);
        break;
    }
    case Kind::MoveTo: {
        bool open_one = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            open_one = job.path == path;
        }
        failed = projects::move_to(job.path, job.name, made);
        if (failed.empty() && open_one) {
            std::lock_guard<std::mutex> guard(lock);
            path = made;
        }
        break;
    }
    case Kind::NewFolder: {
        failed = projects::new_folder(folder_path(), made);
        if (failed.empty() && job.pad >= 0) projects::keep_pad(folder_path(), fs::path(made).filename().string(), job.pad);
        break;
    }
    case Kind::LoadKit: failed = load_kit(job.path); break;
    case Kind::Copy: { // onto a pad: a copy of the project there, what was there gone (asked first)
        auto folder = folder_path();
        if (!job.name.empty()) failed = projects::remove(job.name);
        if (failed.empty()) failed = projects::duplicate(job.path, made);
        if (failed.empty() && job.pad >= 0) projects::keep_pad(folder, fs::path(made).filename().string(), job.pad);
        break;
    }
    case Kind::Colour: {
        bool open_one = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            open_one = job.path == path;
            if (open_one) { move_extras.colour = job.colour; name_step("COLOUR"); close_step(); changed(); }
        }
        if (!open_one) { // another project's file: read, coloured, written whole
            auto other = std::make_unique<melodee::Project>();
            auto other_bank = std::make_unique<melodee::PatternBank>();
            PluginStates states;
            MoveExtras extras_there;
            failed = read_project_file(job.path, *other, *other_bank, states, &extras_there);
            if (failed.empty()) {
                extras_there.colour = job.colour;
                auto bytes = project_file_bytes(*other, *other_bank, states, &extras_there);
                failed = write_whole(job.path, bytes.data(), bytes.size());
            }
        }
        break;
    }
    }
    // Saved before the folder is read, so the open one shows as it is.
    if (job.kind != Kind::List && job.kind != Kind::Open && job.kind != Kind::New && job.kind != Kind::Save) {
        bool unsaved = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            unsaved = dirty;
        }
        if (unsaved) save_now();
    }
    std::string relative;
    {
        std::lock_guard<std::mutex> guard(lock);
        relative = listing_folder;
    }
    auto read = projects::read_folder(projects_root(), relative);
    std::lock_guard<std::mutex> guard(lock);
    listing = std::move(read);
    ++listings;
    project_message = failed.empty() ? std::string() : upper(failed).substr(0, 16);
    if (!failed.empty()) say(project_message);
}

std::string Studio::status() const {
    unsigned used = 0, loaded = 0, tracks = 0;
    const auto& track = current();
    for (unsigned at = 0; at < pad_count; ++at) used += track.pads[at].used();
    for (const auto& sample : samples) loaded += sample && sample->data;
    for (const auto& each : song->tracks) tracks += each.used();
    const auto* module = modules[selected].get();
    char text[512];
    std::snprintf(text, sizeof text,
                  "page=%s track=%u kind=%s tracks=%u pad=%c%02u samples=%u pads=%u loading=%d saves=%u unsaved=%d synth=%s octave=%d "
                  "typing=%d playing=%d tick=%u tempo=%.1f click=%d swing=%u mute=%d solo=%d slow_calls=%u slowest_ms=%.1f stream_late=%u dropped=%u",
                  top() ? top()->id().c_str() : "-", selected + 1, kind_name(kind()), tracks, 'A' + int(pad() / 16), pad() % 16 + 1, loaded, used,
                  int(loading), saves, int(dirty), module && module->module ? module->module->info().id.c_str() : "-", octave(selected), int(typing),
                  int(engine->playing()), engine->ticks(), song->tempo / 10.0, int(click_on), unsigned(song->swing), int(track.mix.mute),
                  int(track.mix.solo), slow_calls, slowest_us / 1000.0,
                  module ? module->player.stream_late() : 0u, engine->dropped());
    std::string line = text;
    line += " undo=" + std::to_string(history.size()) + " redo=" + std::to_string(history.redos()) + " rebuilds=" + std::to_string(rebuilds);
    std::string name = project_name();
    std::replace(name.begin(), name.end(), ' ', '_');
    line += " project=" + name + " opening=" + std::to_string(int(opening)) + " listings=" + std::to_string(listings);
    if (!last_step.empty()) {
        std::string step = last_step;
        std::replace(step.begin(), step.end(), ' ', '_');
        line += " undo_last=" + step;
    }
    for (const auto& screen : screens) line += screen->status();
    return line;
}

} // namespace app
