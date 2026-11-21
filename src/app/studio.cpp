// SPDX-License-Identifier: AGPL-3.0-only
#include "app/studio.h"
#include "app/screen.h"
#include "melodee/engine/engine_info.h"
#include "melodee/shell/sample_decode.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

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
    path = (fs::path(home_path) / "Projects" / "Move.mldp").string();
    settings_path = (fs::path(home_path) / "Move.settings").string();
    modules_root = modules_folder.empty() ? move_modules : std::move(modules_folder);
    catalog = scan_modules(modules_root);
    pad_went.fill({});
    melodee::dsp::device_rate.store(44100.0f); // the Move's; audio_opened() says if it is not

    // The project, read before the audio thread starts.
    PluginStates states;
    auto failed = read_project_file(path, *song, *bank, states);
    bool fresh = failed == "missing", migrated = false;
    unsigned played = 0;
    if (!failed.empty() && !fresh) {
        // Unreadable: kept aside, never written over.
        std::rename(path.c_str(), (path + ".unreadable").c_str());
        std::fprintf(stderr, "melodee-move: %s unreadable (%s), kept as .unreadable\n", path.c_str(), failed.c_str());
        song = std::make_unique<melodee::Project>();
        bank = std::make_unique<melodee::PatternBank>();
        states = {};
        fresh = true;
        say("PROJECT UNREADABLE");
    }
    if (fresh) {
        // A new project, as the first scope had it: a kit on track 1, a
        // synth (no module yet) on track 2.
        copy_text(song->name, "Move");
        song->tracks[0] = fresh_track(TrackKind::Kit, 0);
        song->tracks[1] = fresh_track(TrackKind::Synth, 1);
    } else {
        migrated = migrate(path, *song, states, folder_of, this, played);
    }
    // The Move's own settings: the track played last, the metronome.
    if (std::ifstream in(settings_path); in) {
        std::string line;
        if (std::getline(in, line) && line == settings_header)
            while (std::getline(in, line)) {
                std::istringstream fields(line);
                std::string word;
                unsigned value = 0;
                if (!(fields >> word >> value)) continue;
                if (word == "track") played = value % melodee::track_count;
                else if (word == "click") click_on = value != 0;
                else if (word == "click_level") click_volume = std::min(value, 100u);
                else midi.read_setting(word, int(value)); // MIDI Config's keys
            }
    }
    selected = played;
    if (!fresh) midi.read_section_of(path); // each track's MIDI OUT
    static Studio* running = nullptr; // the one studio a process runs: its modules' MIDI out goes to its port
    running = this;
    set_external_midi([](const uint8_t* packets, int length) { return running ? running->midi.module_send(packets, length) : 0; });
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
    engine = std::make_unique<melodee::EngineHost>();
    engine->set_overload_guard(true); // as Melodee runs on the MPC
    engine->load(*song, *bank);
    for (unsigned t = 0; t < melodee::track_count; ++t)
        if (modules[t] && modules[t]->entry != melodee::no_plugin) engine->offer_plugin(modules[t]->entry, &modules[t]->runner);
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

    if (migrated) {
        // The first scope's side files folded in: saved whole, then kept aside.
        if (auto saving = write(project_bytes(), settings_text()); saving.empty()) retire_side_files(path);
        else std::fprintf(stderr, "melodee-move: migrating %s: %s\n", path.c_str(), saving.c_str());
    }
}

Studio::~Studio() {
    for (auto& module : modules) if (module) module->player.stop();
    for (auto& module : modules_leaving) if (module) module->player.stop();
    set_external_midi(nullptr); // the modules' MIDI out had this studio's port
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
}

void Studio::render(const move::AudioBlock& input, move::AudioBlock& output) {
    forward();
    module_input(input);
    engine->render(output.data(), move::audio_frames, volume.load(std::memory_order_relaxed), input.data());
    midi.after_render(*engine); // what is heard in this block goes out with it
}

void Studio::audio_opened(const move::AudioFormat& format) {
    engine->set_audio_format(double(format.rate), 0);
    Midi::audio_thread();
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
            engine->send({Type::NoteOn, uint8_t(track), 0, uint16_t(key), int16_t(std::max<int>(1, input.value))});
        } else if (went.track >= 0) {
            engine->send({Type::NoteOff, uint8_t(went.track), 0, uint16_t(went.key), 0});
            went = {};
        }
    } else if (input.kind == Kind::PadPressure && went.track >= 0 && went.key < 128) {
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
        module.saved = module_state(from);
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
    host.changed = [this] { changed(); }; // saved three seconds on, with the module (module_state())
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
    out << settings_header << '\n' << "track " << selected << '\n' << "click " << int(click_on) << '\n' << "click_level " << click_volume << '\n';
    midi.write_settings(out);
    return out.str();
}

SynthFile Studio::module_state(unsigned t) const {
    SynthFile file;
    const auto* track = modules[t % melodee::track_count].get();
    file.track = t;
    file.octave = octave(t);
    if (!track) return file;
    if (!track->module) { // a module that would not load this time stays named, for when it is back
        file.module = track->saved.module;
        file.preset = track->saved.preset;
        file.values = track->saved.values;
        file.pages = track->saved.pages;
        return file;
    }
    const auto& module = *track->module;
    auto is_page = [&](const std::string& key) {
        return std::any_of(module.canvases.begin(), module.canvases.end(), [&](const ModuleCanvas& canvas) { return canvas.key == key; });
    };
    file.module = module.info().id;
    file.preset = module.preset_param.empty() ? -1 : module.preset;
    for (const auto& param : module.params) // not a page's own key: it is a page, not a value
        if (param.key != module.preset_param && !param.trigger() && !is_page(param.key)) file.values.emplace_back(param.key, param.text());
    for (size_t n = 0; n < track->canvas_pages.size() && n < module.canvases.size(); ++n)
        if (track->canvas_pages[n])
            if (auto state = track->canvas_pages[n]->saved_state(); !state.empty()) file.pages.emplace_back(module.canvases[n].key, state);
    return file;
}

std::vector<uint8_t> Studio::project_bytes() const {
    // Each Schwung track's module, as its PLUG entry's state.
    PluginStates states;
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        const auto* module = modules[t].get();
        if (!module || module->entry == melodee::no_plugin) continue;
        states[module->entry] = synth_state(module_state(t));
    }
    auto bytes = project_file_bytes(*song, *bank, states);
    midi.append_section(bytes); // each track's MIDI OUT
    return bytes;
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

std::string Studio::restore() {
    std::vector<std::pair<unsigned, std::string>> wanted;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (restored) return {};
        restored = true;
        for (size_t slot = 0; slot < melodee::sample_slots; ++slot)
            if (song->samples[slot].path[0] && (slot >= samples.size() || !samples[slot])) wanted.emplace_back(unsigned(slot), song->samples[slot].path);
    }
    // Every sample read again, outside the lock; a file that has gone keeps
    // its place (and its pads), silent, so saving again keeps it too.
    unsigned missing = 0;
    for (const auto& [slot, file] : wanted) {
        std::string failed;
        auto sample = read_sample(file, failed);
        std::lock_guard<std::mutex> guard(lock);
        if (!sample) { ++missing; continue; }
        if (!engine->offer_sample(slot, sample->data.get())) { ++missing; continue; }
        song->samples.edit(slot).frames = sample->data->model_frames();
        if (slot >= samples.size()) samples.resize(slot + 1);
        samples[slot] = std::move(sample);
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
        auto failed = load_module_now(t, saved.module, &saved);
        std::lock_guard<std::mutex> guard(lock);
        if (!failed.empty()) say(upper(saved.module + " MISSING").substr(0, 16));
    }
    std::lock_guard<std::mutex> guard(lock);
    if (missing) say(std::to_string(missing) + " FILES MISSING");
    else if (!wanted.empty()) say("PROJECT LOADED");
    // A MIDI track's SysEx, sent as the project loads (its ON LOAD).
    for (unsigned t = 0; t < melodee::track_count; ++t)
        if (song->tracks[t].engine == EngineKind::Midi && song->tracks[t].params[melodee::midi_track::SysexOnLoad] && song->tracks[t].midi.sysex[0])
            midi.send_sysex(*this, t);
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
    while (engine->take_retired(sample)) {} // the samples are the studio's, kept for the run
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
        if (auto failed = install(std::move(sample), job.track, job.pad); !failed.empty()) say(failed);
    }
    midi.service(*this); // SysEx files read for the audio thread, the sent ones freed
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
    auto is_page = [&](const std::string& key) {
        return loaded && std::any_of(loaded->canvases.begin(), loaded->canvases.end(), [&](const ModuleCanvas& canvas) { return canvas.key == key; });
    };
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
        for (const auto& [key, value] : saved->values) // not a page's own key (an older save has `param browse 0`)
            if (ModuleParam* param = loaded->param(key); param && key != loaded->preset_param && !param->selector && !is_page(key)) {
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
            else if (saved && saved->module == id) // the page as it was left: its provider, where its menus were
                for (const auto& [key, state] : saved->pages)
                    if (key == canvas.key) started.back()->restore_state(state);
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
    }
    return {};
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
    for (const auto& screen : screens) line += screen->status();
    line += midi.status();
    return line;
}

} // namespace app
