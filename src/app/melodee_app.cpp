// SPDX-License-Identifier: AGPL-3.0-only
#include "app/melodee_app.h"
#include <juce_core/juce_core.h>
#include "move/display.h"
#include "move/font.h"
#include "ui/widgets.h"
#include "melodee/shell/chop.h"
#include "melodee/shell/sample_decode.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>

namespace app {
namespace {

using melodee::ChopMode;
using melodee::PadSound;
namespace fs = std::filesystem;

constexpr const char* move_user_data = "/data/UserData";
// Where the Move keeps sound files, as Load lists them (the ones that exist).
struct MovePlace { const char* name; const char* path; const char* tag; };
constexpr MovePlace move_places[] {
    {"MY SAMPLES", "/data/UserData/UserLibrary/Samples", "SMP"},
    {"RECORDINGS", "/data/UserData/UserLibrary/Recordings", "REC"},
    {"CORE LIBRARY", "/data/CoreLibrary/Samples", "CORE"},
    {"ALL USER DATA", "/data/UserData", "ALL"},
};
constexpr const char* move_home = "/data/UserData/Melodee";
constexpr const char* move_modules = "/data/UserData/schwung/modules"; // Schwung's own install
constexpr int synth_base_note = 48;                                  // the bottom-left pad at octave 0: C2
constexpr int lowest_octave = -3, highest_octave = 4;
constexpr auto autosave_idle = std::chrono::milliseconds(3000); // as Melodee's autosave on the MPC (shell/autosave.h)
constexpr auto autosave_retry = std::chrono::seconds(10);
constexpr auto touch_linger = std::chrono::milliseconds(600);
constexpr auto turn_linger = std::chrono::milliseconds(1200);
constexpr auto message_time = std::chrono::milliseconds(1500);
constexpr unsigned default_tempo_tenths = 1200; // Beats' fallback where a sample has no steady beat
constexpr std::array<ChopMode, 4> chop_types {ChopMode::Auto, ChopMode::Beats, ChopMode::Regions, ChopMode::Threshold};
constexpr const char* to_pads_names[] {"POLY", "MONO", "THRU"};

std::string pad_label(unsigned pad) {
    char text[8];
    std::snprintf(text, sizeof text, "%c%02u", 'A' + int(pad / 16), pad % 16 + 1);
    return text;
}
std::string seconds(uint32_t frames) {
    char text[16];
    std::snprintf(text, sizeof text, "%.2fS", frames / 44100.0);
    return text;
}
std::string upper(std::string text) {
    for (auto& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}
std::string two_digits(unsigned value) {
    char text[8];
    std::snprintf(text, sizeof text, "%02u", value % 100);
    return text;
}
std::vector<float> peaks_of(const melodee::Overview& overview) {
    std::vector<float> peaks(overview.size());
    for (size_t n = 0; n < overview.size(); ++n) peaks[n] = overview[n] / 255.0f;
    return peaks;
}
// The synth track's pads: rows of fourths, as the Move lays out notes.
int note_for(unsigned pad, int octave) {
    return std::clamp(synth_base_note + 12 * octave + int(move::pad_column(pad)) + 5 * int(move::pad_row(pad)), 0, 127);
}
std::string note_name(int note) {
    static const char* names[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[note % 12] + std::to_string(note / 12 - 2); // Ableton's names: 60 is C3
}
bool natural(int note) {
    int in_octave = note % 12;
    return in_octave != 1 && in_octave != 3 && in_octave != 6 && in_octave != 8 && in_octave != 10;
}
// What a cell has room for: a module's short name, or its name.
std::string cell_name(const ModuleParam& param) { return upper(param.short_name.empty() ? param.name : param.short_name); }

// The kinds Melodee's read_audio() reads (shell/sample_decode.h).
bool is_sound_file(const fs::path& path) {
    auto extension = upper(path.extension().string());
    return extension == ".WAV" || extension == ".AIF" || extension == ".AIFF" || extension == ".FLAC" || extension == ".OGG"
        || extension == ".MP3";
}

} // namespace

MelodeeApp::MelodeeApp(std::string library_folder, std::string home_folder, std::string modules)
    : library(std::move(library_folder)), home(std::move(home_folder)), modules_root(std::move(modules)) {
    std::error_code error;
    if (!library.empty()) places.push_back({"SAMPLES", library, "SMP"});
    else
        for (const auto& candidate : move_places)
            if (fs::is_directory(candidate.path, error)) places.push_back({candidate.name, candidate.path, candidate.tag});
    if (places.empty()) places.push_back({"HERE", fs::current_path(error).string(), "HERE"});
    if (places.size() == 1) place = 0; // one place: straight into it
    library = places[size_t(std::max(place, 0))].path;
    if (home.empty()) home = fs::is_directory(move_user_data, error) ? move_home : (fs::current_path(error) / "Melodee").string();
    folder = library;
    project = (fs::path(home) / "Projects" / "Move.mldp").string();
    if (modules_root.empty()) modules_root = move_modules;
    catalog = scan_modules(modules_root);
    pad_went.fill(-1);
}

MelodeeApp::~MelodeeApp() {
    std::unique_lock<std::mutex> guard(lock);
    if (!dirty) return;
    auto kit = snapshot();
    auto synth_file = synth_snapshot();
    dirty = false;
    guard.unlock();
    write(kit, synth_file);
}

// ---------------------------------------------------------------- audio thread

void MelodeeApp::render(const move::AudioBlock& input, move::AudioBlock& output) {
    float gain = volume.load(std::memory_order_relaxed);
    player.render(output, gain);
    synth.render(input, output, gain);
}

void MelodeeApp::audio_opened(const move::AudioFormat& format) {
    player.opened(format.rate);
}

void MelodeeApp::audio_event(const move::SurfaceEvent& event) {
    using Kind = move::Input::Kind;
    const auto& input = event.input;
    if (input.index >= pad_count) return;
    int& went = pad_went[input.index];
    if (input.kind == Kind::Pad) {
        if (input.pressed) {
            if (audio_track.load(std::memory_order_relaxed) == int(Track::Synth)) {
                went = note_for(input.index, audio_octave.load(std::memory_order_relaxed));
                synth.note_on(went, input.value);
            } else {
                went = -1;
                player.hit(input.index, input.value);
            }
        } else {
            if (went >= 0) synth.note_off(went);
            else player.release(input.index);
            went = -1;
        }
    } else if (input.kind == Kind::PadPressure && went >= 0) {
        synth.pressure(went, input.value); // Melodee's sampler takes no pressure
    }
}

// ---------------------------------------------------------------- the model, under `lock`

const MelodeeApp::Sample* MelodeeApp::sample_of(unsigned at) const {
    if (at >= pad_count || !pads[at].used() || pads[at].sample >= samples.size()) return nullptr;
    const auto& sample = samples[pads[at].sample];
    return sample && sample->data ? sample.get() : nullptr; // a file that was missing at load plays nothing
}

void MelodeeApp::say(std::string text) {
    message = std::move(text);
    message_until = Clock::now() + message_time;
}

void MelodeeApp::changed() {
    dirty = true;
    changed_at = Clock::now();
}

void MelodeeApp::select_pad(unsigned at) {
    if (at < pad_count && at != pad) { pad = at; changed(); }
}

void MelodeeApp::set_pad(unsigned at, const PadSound& sound) {
    pads[at] = sound;
    if (page != Page::Chop && page != Page::ToPads) player.set_pad(at, sound);
    changed();
}

void MelodeeApp::remember_chop() {
    const Sample* sample = sample_of(pad);
    uint16_t slot = pads[pad].sample;
    if (!sample || slot >= chops.size()) return;
    auto& chop = chops[slot];
    chop.from = pads[pad].start;
    chop.to = pads[pad].end;
    chop.mode = mode;
    chop.amount = int16_t(amounts[unsigned(mode)]);
    chop.slices = slices;
    chop.hand = {};
    changed();
}

void MelodeeApp::open_place(int index) {
    if (index < 0 || index >= int(places.size())) return;
    place = index;
    library = folder = places[size_t(index)].path;
    cursor = 0;
    list_folder();
}

void MelodeeApp::list_folder() {
    entries.clear();
    if (place < 0) { // the places themselves
        for (const auto& where : places) entries.push_back({where.name, true});
        cursor = std::clamp(cursor, 0, std::max(0, int(entries.size()) - 1));
        return;
    }
    std::error_code error;
    for (const auto& item : fs::directory_iterator(folder, fs::directory_options::skip_permission_denied, error)) {
        auto name = item.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (item.is_directory(error)) entries.push_back({name, true});
        else if (is_sound_file(item.path())) entries.push_back({name, false});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.folder != b.folder) return a.folder;
        return upper(a.name) < upper(b.name);
    });
    cursor = std::clamp(cursor, 0, std::max(0, int(entries.size()) - 1));
}

void MelodeeApp::enter_chop() {
    const Sample* sample = sample_of(pad);
    if (!sample) { say("EMPTY PAD"); return; }
    page = Page::Chop;
    selected = 0;
    // The sample's chop as it was left, where it was of this same region.
    uint16_t slot = pads[pad].sample;
    uint32_t frames = sample->data->model_frames();
    if (slot < chops.size() && chops[slot].used() && chops[slot].from == pads[pad].start &&
        (chops[slot].to ? chops[slot].to : frames) == (pads[pad].end ? pads[pad].end : frames)) {
        const auto& kept = chops[slot];
        mode = kept.mode;
        amounts[unsigned(mode)] = kept.amount;
        slices = kept.slices;
        play_slices();
        return;
    }
    chop_again();
}

void MelodeeApp::leave_chop() {
    page = Page::Play;
    for (unsigned at = 0; at < pad_count; ++at) player.set_pad(at, pads[at]);
}

void MelodeeApp::chop_again() {
    const Sample* sample = sample_of(pad);
    if (!sample) return;
    const auto& data = *sample->data;
    uint32_t frames = data.model_frames();
    uint32_t from = pads[pad].start, to = pads[pad].end ? pads[pad].end : frames;
    if (mode == ChopMode::Auto)
        slices = melodee::auto_chop(sample->autos, melodee::auto_slice_count(amounts[unsigned(mode)]), from, to, frames,
                                    sample->hits, sample->hit_count, sample->beats);
    else {
        int amount = mode == ChopMode::Beats ? int(melodee::division_sixteenths(unsigned(amounts[unsigned(mode)]), 16))
                                             : amounts[unsigned(mode)];
        slices = melodee::chop(mode, amount, from, to, frames, sample->hits, sample->hit_count, default_tempo_tenths, &sample->beats);
    }
    // Every cut on a zero crossing, so no slice clicks.
    for (unsigned n = 1; n < slices.count; ++n) {
        uint32_t at = melodee::zero_crossing(data, slices.start[n]);
        if (slices.stop[n - 1] == slices.start[n]) slices.stop[n - 1] = at;
        slices.start[n] = at;
    }
    selected = std::min(selected, slices.count ? slices.count - 1 : 0);
    play_slices();
    remember_chop();
}

void MelodeeApp::play_slices() {
    const Sample* sample = sample_of(pad);
    if (!sample) return;
    uint32_t frames = sample->data->model_frames();
    for (unsigned at = 0; at < pad_count; ++at)
        player.set_pad(at, at < slices.count ? melodee::slice_pad(pads[pad], slices, at, frames, melodee::SlicePlay::Poly, 0) : PadSound {});
}

void MelodeeApp::slices_to_pads() {
    const Sample* sample = sample_of(pad);
    if (!sample || !slices.count) return;
    uint32_t frames = sample->data->model_frames();
    auto play = melodee::SlicePlay(std::clamp(to_pads, 0, 2));
    uint8_t group = melodee::slice_group(pads[pad], pads);
    PadSound parent = pads[pad];
    unsigned placed = 0, first = pad_count;
    for (unsigned at = 0; at < pad_count && placed < slices.count; ++at) {
        if (pads[at].used()) continue;
        if (first == pad_count) first = at;
        pads[at] = melodee::slice_pad(parent, slices, placed++, frames, play, group);
    }
    if (!placed) { say("NO EMPTY PADS"); return; }
    leave_chop();
    select_pad(first);
    changed();
    say(std::to_string(placed) + " SLICES FROM " + pad_label(first));
}

void MelodeeApp::turn_knob(unsigned which, int delta, bool fine) {
    const Sample* sample = sample_of(pad);
    if (!sample) return;
    uint32_t frames = sample->data->model_frames();
    uint32_t step = std::max<uint32_t>(1, frames / (fine ? 8192 : 256));
    auto move_by = [&](uint32_t value, uint32_t low, uint32_t high) {
        int64_t moved = int64_t(value) + int64_t(delta) * int64_t(step);
        return uint32_t(std::clamp<int64_t>(moved, low, std::max(low, high)));
    };
    if (page == Page::Chop) {
        if (!slices.count) return;
        unsigned n = selected;
        if (which == 0) { // the selected slice's start; the slice before follows where they touch
            uint32_t low = n ? slices.start[n - 1] + melodee::min_slice : pads[pad].start;
            bool touching = n && slices.touches(n - 1, frames);
            uint32_t start = move_by(slices.start[n], low, slices.end(n, frames) - melodee::min_slice);
            if (touching) slices.stop[n - 1] = start;
            slices.start[n] = start;
        } else if (which == 1) { // its end; the slice after follows where they touch
            bool touching = slices.touches(n, frames);
            uint32_t high = n + 1 < slices.count ? slices.end(n + 1, frames) - melodee::min_slice : frames;
            uint32_t end = move_by(slices.end(n, frames), slices.start[n] + melodee::min_slice, high);
            if (touching) slices.start[n + 1] = end;
            slices.stop[n] = end == frames ? 0 : end;
        } else if (which == 2 && delta) {
            auto at = std::find(chop_types.begin(), chop_types.end(), mode) - chop_types.begin();
            mode = chop_types[size_t((at + (delta > 0 ? 1 : 3)) % 4)];
            chop_again();
            return;
        } else if (which == 3) {
            int& amount = amounts[unsigned(mode)];
            switch (mode) {
            case ChopMode::Auto: amount = std::clamp(amount + delta, 0, int(melodee::auto_counts) - 1); break;
            case ChopMode::Beats: amount = std::clamp(amount + delta, 0, int(melodee::beat_divisions) - 1); break;
            case ChopMode::Regions: amount = std::clamp(amount + delta, 2, int(melodee::max_slices)); break;
            default: amount = std::clamp(amount + delta, 0, 100); break;
            }
            chop_again();
            return;
        } else return;
        play_slices();
        remember_chop();
        return;
    }
    PadSound sound = pads[pad];
    switch (which) {
    case 0:
        if (fine) sound.fine = int8_t(std::clamp(sound.fine + delta * 5, -50, 50));
        else sound.pitch = int8_t(std::clamp(sound.pitch + delta, -36, 36));
        break;
    case 1: sound.level = uint8_t(std::clamp(int(sound.level) + delta, 0, 127)); break;
    case 2: sound.pan = int8_t(std::clamp(sound.pan + delta, -50, 50)); break;
    case 3: sound.start = move_by(sound.start, 0, (sound.end ? sound.end : frames) - melodee::min_slice); break;
    case 4: {
        uint32_t end = move_by(sound.end ? sound.end : frames, sound.start + melodee::min_slice, frames);
        sound.end = end == frames ? 0 : end;
        break;
    }
    case 5: sound.attack = uint8_t(std::clamp(int(sound.attack) + delta, 0, 100)); break;
    case 6: sound.decay = uint8_t(std::clamp(int(sound.decay) + delta, 0, 100)); break;
    case 7:
        sound.cutoff = uint8_t(std::clamp(int(sound.cutoff) + delta, 0, 100));
        sound.filter = sound.cutoff < 100 ? melodee::PadFilter::Lpf4 : melodee::PadFilter::Off;
        break;
    default: return;
    }
    set_pad(pad, sound);
}

MelodeeApp::Knob MelodeeApp::knob(unsigned which) const {
    if (track == Track::Synth) return synth_knob(which);
    const Sample* sample = sample_of(pad);
    uint32_t frames = sample ? std::max<uint32_t>(1, sample->data->model_frames()) : 1;
    char text[24];
    if (page == Page::Chop || page == Page::ToPads) {
        unsigned n = std::min(selected, slices.count ? slices.count - 1 : 0);
        switch (which) {
        case 0: return {"START", seconds(slices.start[n]), float(slices.start[n]) / float(frames)};
        case 1: return {"END", seconds(slices.end(n, frames)), float(slices.end(n, frames)) / float(frames)};
        case 2: return {"TYPE", upper(melodee::chop_mode_name(mode)), 0};
        case 3: {
            int amount = amounts[unsigned(mode)];
            if (mode == ChopMode::Auto) std::snprintf(text, sizeof text, "%u", melodee::auto_slice_count(amount));
            else if (mode == ChopMode::Beats) std::snprintf(text, sizeof text, "%s", melodee::division_value(unsigned(amount)));
            else if (mode == ChopMode::Regions) std::snprintf(text, sizeof text, "%d", amount);
            else std::snprintf(text, sizeof text, "%d%%", amount);
            return {mode == ChopMode::Auto ? "SLICES" : mode == ChopMode::Beats ? "DIVISION" : mode == ChopMode::Regions ? "SLICES" : "THRESHOLD",
                    text, 0};
        }
        default: return {"", "", 0};
        }
    }
    const PadSound& sound = pads[pad];
    switch (which) {
    case 0: std::snprintf(text, sizeof text, "%+d ST", sound.pitch); return {"TUNE", text, (sound.pitch + 36) / 72.0f};
    case 1: std::snprintf(text, sizeof text, "%d", sound.level); return {"LEVEL", text, sound.level / 127.0f};
    case 2:
        if (sound.pan == 0) std::snprintf(text, sizeof text, "C");
        else std::snprintf(text, sizeof text, "%c%d", sound.pan < 0 ? 'L' : 'R', std::abs(int(sound.pan)));
        return {"PAN", text, (sound.pan + 50) / 100.0f};
    case 3: return {"START", seconds(sound.start), float(sound.start) / float(frames)};
    case 4: {
        uint32_t end = sound.end ? sound.end : frames;
        return {"END", seconds(end), float(end) / float(frames)};
    }
    case 5: std::snprintf(text, sizeof text, "%d", sound.attack); return {"ATTACK", text, sound.attack / 100.0f};
    case 6: std::snprintf(text, sizeof text, "%d", sound.decay); return {"DECAY", text, sound.decay / 100.0f};
    case 7:
        if (sound.filter == melodee::PadFilter::Off) std::snprintf(text, sizeof text, "OFF");
        else std::snprintf(text, sizeof text, "%d", sound.cutoff);
        return {"CUTOFF", text, sound.cutoff / 100.0f};
    default: return {"", "", 0};
    }
}

// ---------------------------------------------------------------- the synth track, under `lock`

int MelodeeApp::synth_note(unsigned at) const { return note_for(at, octave); }

void MelodeeApp::set_track(Track to) {
    if (page == Page::Chop || page == Page::ToPads) leave_chop();
    page = Page::Play;
    touched = -1;
    held.fill(false);
    menu_open = false;
    if (track == to) return;
    message_until = {}; // the other track's news
    track = to;
    audio_track.store(int(to), std::memory_order_relaxed);
    if (to == Track::Synth) refresh_synth();
    changed();
}

MelodeeApp::Knob MelodeeApp::synth_knob(unsigned which) const {
    if (!module || synth_page >= module->pages.size()) return {"", "", 0};
    const auto& keys = module->pages[synth_page].knobs;
    if (which >= keys.size()) return {"", "", 0};
    const ModuleParam* param = module->param(keys[which]);
    if (!param) return {"", "", 0};
    return {upper(param->name).substr(0, 16), upper(param->shown()).substr(0, 12), float(param->fraction())};
}

void MelodeeApp::turn_synth_knob(unsigned which, int delta, bool fine) {
    if (!module || synth_page >= module->pages.size()) return;
    const auto& keys = module->pages[synth_page].knobs;
    if (which >= keys.size()) return;
    if (keys[which] == module->preset_param) { preset_by(delta); return; }
    ModuleParam* param = module->param(keys[which]);
    if (!param || !delta) return;
    param->turn(delta, fine);
    synth_set(param->key, param->text());
    changed();
}

void MelodeeApp::synth_page_by(int way) {
    if (!module || module->pages.empty()) return;
    synth_page = unsigned(std::clamp(int(synth_page) + way, 0, int(module->pages.size()) - 1));
    refresh_synth();
}

void MelodeeApp::preset_by(int way) {
    if (!module) return;
    if (module->preset_param.empty()) { synth_page_by(way); return; }
    int last = module->preset_count > 0 ? module->preset_count - 1 : std::numeric_limits<int>::max();
    int to = std::clamp(module->preset + way, 0, last);
    if (to == module->preset) return;
    module->preset = to;
    if (auto* param = module->param(module->preset_param)) param->value = to;
    synth_set(module->preset_param, std::to_string(to));
    // A preset sets every parameter: ask for each again, to show and save.
    for (const auto& param : module->params)
        if (param.key != module->preset_param) synth.ask(param.key);
    if (!module->preset_name_param.empty()) synth.ask(module->preset_name_param);
    changed();
}

void MelodeeApp::refresh_synth() {
    if (!module || synth_page >= module->pages.size()) return;
    for (const auto& key : module->pages[synth_page].knobs) synth.ask(key);
}

void MelodeeApp::synth_set(const std::string& key, const std::string& value) {
    synth.set(key, value);
    set_at[key] = synth.last_serial();
}

void MelodeeApp::take_answers() {
    std::lock_guard<std::mutex> guard(lock);
    // A call that held the audio thread up: logged, with what it was.
    SynthPlayer::Slow slow;
    while (synth.slow(slow)) {
        ++slow_calls;
        slowest_us = std::max(slowest_us, slow.us);
        std::fprintf(stderr, "melodee-move: slow synth call: %s %s took %.1f ms\n", module ? module->info().id.c_str() : "-", slow.what,
                     slow.us / 1000.0);
    }
    SynthPlayer::Answer answer;
    while (synth.answer(answer)) {
        if (!module) continue;
        std::string key = answer.key, value = answer.value;
        // Asked before a later set of the same key: the module has the newer value.
        if (auto set = set_at.find(key); set != set_at.end() && answer.serial < set->second) continue;
        if (key == module->preset_name_param) module->preset_name = value;
        else if (key == module->preset_count_param) module->preset_count = std::atoi(value.c_str());
        else if (auto* param = module->param(key)) {
            param->take(value);
            if (key == module->preset_param) module->preset = int(param->value);
        }
    }
    // An items list, read whole: [{"index": 0, "label": "GM"}, ...] or names.
    std::string text;
    if (pick_reading && synth.long_answer(text)) {
        pick_reading = false;
        pick_items.clear();
        juce::var list;
        if (juce::JSON::parse(juce::String::fromUTF8(text.c_str()), list).wasOk())
            if (auto* entries = list.getArray())
                for (int n = 0; n < entries->size(); ++n) {
                    const auto& entry = entries->getReference(n);
                    if (entry.isString()) { pick_items.emplace_back(n, entry.toString().toStdString()); continue; }
                    auto* object = entry.getDynamicObject();
                    if (!object) continue;
                    int index = object->hasProperty("index") ? int(object->getProperty("index")) : n;
                    auto label = object->hasProperty("label") ? object->getProperty("label") : object->getProperty("name");
                    pick_items.emplace_back(index, label.toString().toStdString());
                }
        pick_key.clear();
    }
    if (!pick_key.empty() && !pick_reading) pick_reading = synth.ask_long(pick_key);
}

// ---------------------------------------------------------------- the module's menu, under `lock`

std::vector<MelodeeApp::MenuRow> MelodeeApp::menu_rows() const {
    std::vector<MenuRow> rows;
    if (!module || menu_path.empty()) return rows;
    const ModuleLevel* at = module->level(menu_path.back());
    if (!at) return rows;
    using Kind = MenuRow::Kind;
    if (!at->list_param.empty() && at->list_param == module->preset_param) rows.push_back({Kind::Preset, at->list_param, "PRESET"});
    if (!at->items_param.empty())
        for (const auto& [index, label] : pick_items) rows.push_back({Kind::Item, at->select_param, label, index});
    for (const auto& item : at->items) {
        if (item.kind == MenuItem::Kind::Level) {
            const ModuleLevel* to = module->level(item.key);
            if (to) rows.push_back({Kind::Level, item.key, item.label.empty() ? to->name : item.label});
            continue;
        }
        const ModuleParam* param = module->param(item.key);
        if (param && !param->selector) rows.push_back({Kind::Param, item.key, item.label.empty() ? (param->label.empty() ? param->name : param->label) : item.label});
    }
    if (rows.empty()) // a level that lists nothing: its knobs
        for (const auto& page : module->pages)
            if (page.level == module->level_index(at->id))
                for (const auto& key : page.knobs)
                    if (const ModuleParam* param = module->param(key)) rows.push_back({Kind::Param, key, param->label.empty() ? param->name : param->label});
    return rows;
}

std::string MelodeeApp::menu_text(const MenuRow& row, bool editing) const {
    constexpr size_t width = 16; // characters in a list row
    auto fit = [&](std::string label, std::string value) {
        if (editing) value = "[" + value + "]";
        value = value.substr(0, width - 2);
        label = label.substr(0, width - value.size() - 1);
        return label + std::string(width - label.size() - value.size(), ' ') + value;
    };
    using Kind = MenuRow::Kind;
    switch (row.kind) {
    case Kind::Preset: {
        char number[16];
        std::snprintf(number, sizeof number, "%03d", std::clamp(module->preset + 1, 0, 999));
        return fit(std::string(number) + " " + upper(module->preset_name), editing ? "<>" : "");
    }
    case Kind::Param: {
        const ModuleParam* param = module->param(row.key);
        return fit(upper(row.label), param ? upper(param->shown()) : "?");
    }
    case Kind::Level: return upper(row.label).substr(0, width - 2) + " >";
    case Kind::Item: {
        const ModuleParam* chosen = module->param(row.key);
        bool current = chosen && std::lround(chosen->value) == row.index;
        return (current ? "* " : "  ") + upper(row.label).substr(0, width - 2);
    }
    }
    return {};
}

void MelodeeApp::open_menu() {
    if (!module) return;
    menu_open = true;
    menu_path.clear();
    enter_level("root");
    // Where the knobs are: that level's menu.
    if (synth_page < module->pages.size() && module->pages[synth_page].level != 0)
        enter_level(module->levels[module->pages[synth_page].level].id);
}

void MelodeeApp::enter_level(const std::string& id) {
    const ModuleLevel* to = module ? module->level(id) : nullptr;
    if (!to) return;
    menu_path.push_back(id);
    menu_cursor = 0;
    menu_editing = false;
    pick_items.clear();
    pick_key = to->items_param;
    if (!pick_key.empty() && !pick_reading) pick_reading = synth.ask_long(pick_key);
    if (!to->select_param.empty()) synth.ask(to->select_param);
    // The knobs follow to the level's own page, where it has one.
    size_t index = module->level_index(id);
    bool here = synth_page < module->pages.size() && module->pages[synth_page].level == index;
    for (size_t n = 0; n < module->pages.size() && !here; ++n)
        if (module->pages[n].level == index) { synth_page = unsigned(n); here = true; }
    unsigned asked = 0;
    for (const auto& row : menu_rows())
        if (row.kind == MenuRow::Kind::Param && asked++ < 48) synth.ask(row.key);
}

void MelodeeApp::menu_turn(int delta, bool fine) {
    auto rows = menu_rows();
    if (rows.empty() || !delta) return;
    menu_cursor = std::clamp(menu_cursor, 0, int(rows.size()) - 1);
    if (!menu_editing) {
        menu_cursor = std::clamp(menu_cursor + delta, 0, int(rows.size()) - 1);
        return;
    }
    const auto& row = rows[size_t(menu_cursor)];
    if (row.kind == MenuRow::Kind::Preset) { preset_by(delta); return; }
    ModuleParam* param = module->param(row.key);
    if (row.kind != MenuRow::Kind::Param || !param) return;
    param->turn(delta, fine);
    synth_set(param->key, param->text());
    changed();
}

void MelodeeApp::menu_push() {
    auto rows = menu_rows();
    if (rows.empty()) return;
    menu_cursor = std::clamp(menu_cursor, 0, int(rows.size()) - 1);
    const auto row = rows[size_t(menu_cursor)];
    switch (row.kind) {
    case MenuRow::Kind::Preset:
    case MenuRow::Kind::Param: menu_editing = !menu_editing; break;
    case MenuRow::Kind::Level: enter_level(row.key); break;
    case MenuRow::Kind::Item: {
        if (ModuleParam* chosen = module->param(row.key)) chosen->value = row.index;
        synth_set(row.key, std::to_string(row.index));
        // A bank changes the presets: ask for them again.
        for (const auto* key : {&module->preset_param, &module->preset_count_param, &module->preset_name_param})
            if (!key->empty()) synth.ask(*key);
        changed();
        const ModuleLevel* at = module->level(menu_path.back());
        std::string to = at ? at->navigate_to : std::string();
        if (!to.empty() && module->level(to)) {
            menu_path.clear();
            if (to != "root") enter_level("root");
            enter_level(to);
        } else menu_back();
        break;
    }
    }
}

void MelodeeApp::menu_back() {
    if (menu_editing) { menu_editing = false; return; }
    if (menu_path.size() <= 1) { menu_open = false; return; }
    std::string left = menu_path.back();
    menu_path.pop_back();
    pick_items.clear();
    pick_key.clear();
    auto rows = menu_rows();
    menu_cursor = 0;
    for (size_t n = 0; n < rows.size(); ++n)
        if (rows[n].kind == MenuRow::Kind::Level && rows[n].key == left) menu_cursor = int(n);
}

void MelodeeApp::destroy_retired() {
    std::vector<std::unique_ptr<LoadedModule>> gone;
    {
        std::lock_guard<std::mutex> guard(lock);
        while (LoadedModule* old = synth.retired()) {
            auto at = std::find_if(leaving.begin(), leaving.end(), [old](const auto& held_module) { return held_module.get() == old; });
            if (at == leaving.end()) continue;
            gone.push_back(std::move(*at));
            leaving.erase(at);
        }
    }
    gone.clear(); // destroy_instance(), outside the lock
}

SynthFile MelodeeApp::synth_snapshot() const {
    SynthFile file;
    file.track = unsigned(track);
    file.octave = octave;
    if (!module) {
        // A module that would not load this time stays named, for when it is back.
        file.module = saved_synth.module;
        file.preset = saved_synth.preset;
        file.values = saved_synth.values;
        return file;
    }
    file.module = module->info().id;
    file.preset = module->preset_param.empty() ? -1 : module->preset;
    for (const auto& param : module->params)
        if (param.key != module->preset_param) file.values.emplace_back(param.key, param.text());
    return file;
}

// ---------------------------------------------------------------- MIDI thread

void MelodeeApp::surface(const move::SurfaceEvent& event) {
    using Kind = move::Input::Kind;
    using move::Button;
    const auto& input = event.input;
    std::lock_guard<std::mutex> guard(lock);
    bool fine = session().shift.load();
    switch (input.kind) {
    case Kind::Pad:
        if (track == Track::Synth) { if (input.index < pad_count) held[input.index] = input.pressed; break; }
        if (!input.pressed) break;
        if (page == Page::Chop || page == Page::ToPads) { if (input.index < slices.count) selected = input.index; }
        else select_pad(input.index);
        break;
    case Kind::Button: {
        if (!input.pressed) break;
        auto button = Button(input.index);
        if (button == Button::Row1 || button == Button::Row2) {
            set_track(button == Button::Row1 ? Track::Kit : Track::Synth);
            break;
        }
        if (track == Track::Synth) {
            if (button == Button::Back && page == Page::Play && menu_open) {
                menu_back();
            } else if (button == Button::MainPush && page == Page::Play) {
                if (menu_open) menu_push();
                else open_menu();
            } else if (button == Button::Menu || (button == Button::Back && page == Page::Load)) {
                menu_open = false;
                page = page == Page::Load ? Page::Play : Page::Load;
                if (page == Page::Load) {
                    catalog = scan_modules(modules_root);
                    module_cursor = std::clamp(module_cursor, 0, std::max(0, int(catalog.size()) - 1));
                }
            } else if (button == Button::MainPush && page == Page::Load && module_cursor < int(catalog.size())) {
                const auto& choice = catalog[size_t(module_cursor)];
                if (choice.api != 2) say("OLDER API: NOT YET");
                else { module_job = choice.id; say("LOADING"); }
            } else if (button == Button::Loop) {
                say("CHOP: KIT ONLY");
            } else if ((button == Button::Left || button == Button::Right) && page == Page::Play) {
                synth_page_by(button == Button::Left ? -1 : 1);
            } else if (button == Button::Up || button == Button::Down) {
                octave = std::clamp(octave + (button == Button::Up ? 1 : -1), lowest_octave, highest_octave);
                audio_octave.store(octave, std::memory_order_relaxed);
                say("OCTAVE " + note_name(synth_note(0)));
                changed();
            }
            break;
        }
        if (button == Button::Menu) {
            if (page == Page::Chop || page == Page::ToPads) leave_chop();
            page = page == Page::Load ? Page::Play : Page::Load;
            if (page == Page::Load) list_folder();
        } else if (button == Button::Loop) {
            if (page == Page::Chop || page == Page::ToPads) leave_chop();
            else enter_chop();
        } else if (button == Button::Back) {
            if (page == Page::Load) {
                if (place >= 0 && fs::path(folder) != fs::path(library)) { folder = fs::path(folder).parent_path().string(); cursor = 0; list_folder(); }
                else if (place >= 0 && places.size() > 1) { place = -1; cursor = 0; list_folder(); } // back to the places
                else page = Page::Play;
            } else if (page == Page::Chop) leave_chop();
            else if (page == Page::ToPads) page = Page::Chop;
        } else if (button == Button::MainPush) {
            if (page == Page::Load && place < 0) {
                open_place(cursor);
            } else if (page == Page::Load && cursor < int(entries.size())) {
                const auto& entry = entries[size_t(cursor)];
                auto path = (fs::path(folder) / entry.name).string();
                if (entry.folder) { folder = path; cursor = 0; list_folder(); }
                else { jobs.push_back({path, pad}); say("LOADING"); }
            } else if (page == Page::Chop && slices.count) {
                page = Page::ToPads;
            } else if (page == Page::ToPads) {
                slices_to_pads();
            }
        } else if (button == Button::Left || button == Button::Right) {
            int way = button == Button::Left ? -1 : 1;
            if (page == Page::Chop && slices.count) selected = unsigned(std::clamp(int(selected) + way, 0, int(slices.count) - 1));
            else if (page == Page::Play) select_pad(unsigned(std::clamp(int(pad) + way, 0, int(pad_count) - 1)));
        }
        break;
    }
    case Kind::MainKnob:
        if (track == Track::Synth) {
            if (page == Page::Load) module_cursor = std::clamp(module_cursor + input.delta, 0, std::max(0, int(catalog.size()) - 1));
            else if (menu_open) menu_turn(input.delta, fine);
            else preset_by(input.delta);
            break;
        }
        if (page == Page::Load) cursor = std::clamp(cursor + input.delta, 0, std::max(0, int(entries.size()) - 1));
        else if (page == Page::Chop && slices.count) selected = unsigned(std::clamp(int(selected) + input.delta, 0, int(slices.count) - 1));
        else if (page == Page::ToPads) to_pads = std::clamp(to_pads + input.delta, 0, 2);
        break;
    case Kind::Knob:
        if (track == Track::Synth) {
            if (page == Page::Load) break;
            turn_synth_knob(input.index, input.delta, fine);
        } else turn_knob(input.index, input.delta, fine);
        if (touched != int(input.index) || touched_until != Clock::time_point::max()) {
            touched = input.index;
            touched_until = Clock::now() + turn_linger;
        }
        break;
    case Kind::Touch:
        if (input.index >= move::knob_count) break;
        if (input.pressed) { touched = input.index; touched_until = Clock::time_point::max(); }
        else if (touched == int(input.index)) touched_until = Clock::now() + touch_linger;
        break;
    case Kind::MasterKnob:
        volume = std::clamp(volume.load() + input.delta * 0.01f, 0.0f, 1.0f);
        break;
    default: break;
    }
}

void MelodeeApp::lights(move::Lights& lights, Clock::time_point) {
    using namespace move;
    std::lock_guard<std::mutex> guard(lock);
    lights.button(Button::Row1, track == Track::Kit ? colour::azure : colour::dark_blue);
    lights.button(Button::Row2, track == Track::Synth ? colour::violet : module ? colour::deep_magenta : colour::dark_grey);
    lights.button(Button::Shift, session().shift ? white_bright : white_dim);
    if (track == Track::Synth) {
        // Every C in the track's colour, the other naturals dim, a pad held white.
        for (unsigned at = 0; at < pad_count; ++at) {
            int note = synth_note(at);
            lights.pad(at, held[at] ? colour::white : !module ? colour::off : note % 12 == 0 ? colour::violet : natural(note) ? colour::dark_grey : colour::off);
        }
        for (unsigned step = 0; step < step_count; ++step) lights.step(step, colour::off);
        bool paged = module && module->pages.size() > 1;
        lights.button(Button::Left, paged && synth_page > 0 ? white_dim : 0);
        lights.button(Button::Right, paged && synth_page + 1 < module->pages.size() ? white_dim : 0);
        lights.button(Button::Up, octave < highest_octave ? white_dim : 0);
        lights.button(Button::Down, octave > lowest_octave ? white_dim : 0);
        lights.button(Button::Loop, 0);
        lights.button(Button::Menu, page == Page::Load ? white_bright : white_dim);
        lights.button(Button::Back, page == Page::Play && !menu_open ? 0 : white_dim);
        return;
    }
    lights.button(Button::Left, page == Page::Play || page == Page::Chop ? white_dim : 0);
    lights.button(Button::Right, page == Page::Play || page == Page::Chop ? white_dim : 0);
    lights.button(Button::Up, 0);
    lights.button(Button::Down, 0);
    bool chop = page == Page::Chop || page == Page::ToPads;
    const Sample* sample = sample_of(pad);
    uint32_t frames = sample ? std::max<uint32_t>(1, sample->data->model_frames()) : 1;
    for (unsigned at = 0; at < pad_count; ++at) {
        uint8_t colour = colour::off;
        if (chop) {
            if (at < slices.count) colour = at == selected ? colour::white : at % 2 ? colour::brown_yellow : colour::orange;
        } else if (pads[at].used()) colour = at == pad ? colour::white : colour::azure;
        else if (at == pad) colour = colour::dark_grey;
        lights.pad(at, colour);
    }
    // Chop's steps: the sample in 16 parts, lit where a cut falls.
    for (unsigned step = 0; step < step_count; ++step) {
        bool cut = false;
        if (chop)
            for (unsigned n = 0; n < slices.count; ++n)
                cut |= slices.start[n] * step_count / frames == step;
        lights.step(step, cut ? (selected < slices.count && slices.start[selected] * step_count / frames == step ? colour::white : colour::orange)
                              : colour::off);
    }
    lights.button(Button::Loop, chop ? white_bright : sample ? white_dim : 0);
    lights.button(Button::Menu, page == Page::Load ? white_bright : white_dim);
    lights.button(Button::Back, page == Page::Play ? 0 : white_dim);
}

// ---------------------------------------------------------------- screen thread

void MelodeeApp::draw(juce::Graphics& g) {
    std::lock_guard<std::mutex> guard(lock);
    auto now = Clock::now();
    if (touched >= 0 && now < touched_until && !knob(unsigned(touched)).name.empty()) draw_touched(g);
    else if (track == Track::Synth) page == Page::Load ? draw_modules(g) : menu_open && module ? draw_menu(g) : draw_synth(g);
    else if (page == Page::Load) draw_load(g);
    else if (page == Page::Chop) draw_chop(g);
    else if (page == Page::ToPads) draw_to_pads(g);
    else draw_play(g);
    if (now < message_until) {
        ui::sheet(g, {ui::content_x, ui::detail_y - 2, ui::content_width, 10});
        move::draw_text_centred(g, ui::content_x, ui::detail_y, ui::content_width, message);
    }
}

void MelodeeApp::draw_play(juce::Graphics& g) {
    ui::rail(g, "KIT", "1", pad_label(pad));
    const Sample* sample = sample_of(pad);
    if (!sample) {
        move::draw_text_centred(g, ui::content_x, 18, ui::content_width, "EMPTY PAD", 2);
        ui::detail(g, "MENU: LOAD");
        return;
    }
    auto area = ui::content(0, ui::subject_height);
    ui::waveform(g, area, peaks_of(sample->overview));
    uint32_t frames = std::max<uint32_t>(1, sample->data->model_frames());
    const auto& sound = pads[pad];
    int start = int(int64_t(sound.start) * area.getWidth() / frames), end = int(int64_t(sound.end ? sound.end : frames) * area.getWidth() / frames);
    g.fillRect(area.getX() + std::min(start, area.getWidth() - 1), 0, 1, area.getHeight());
    g.fillRect(area.getX() + std::min(end, area.getWidth() - 1), 0, 1, area.getHeight());
    auto name = upper(fs::path(sample->name).stem().string()).substr(0, 10);
    ui::detail(g, name, seconds(frames).substr(0, 5));
}

void MelodeeApp::draw_load(juce::Graphics& g) {
    ui::rail(g, "LOAD", pad_label(pad), !jobs.empty() ? "..." : place < 0 ? "FROM" : places[size_t(place)].tag);
    if (entries.empty()) {
        move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "NO SOUND FILES");
        ui::detail(g, upper(fs::path(folder).filename().string()).substr(0, 16));
        return;
    }
    std::vector<std::string> rows;
    for (const auto& entry : entries) rows.push_back(entry.folder ? entry.name + " >" : entry.name);
    ui::list(g, ui::content(0, ui::height), rows, cursor, 1);
}

void MelodeeApp::draw_chop(juce::Graphics& g) {
    ui::rail(g, "SLC", two_digits(selected + 1), "/" + std::to_string(slices.count));
    const Sample* sample = sample_of(pad);
    if (!sample) return;
    auto area = ui::content(0, ui::subject_height);
    uint32_t frames = std::max<uint32_t>(1, sample->data->model_frames());
    auto column = [&](uint32_t frame) { return int(int64_t(frame) * area.getWidth() / frames); };
    int from = slices.count ? column(slices.start[selected]) : -1, to = slices.count ? column(slices.end(selected, frames)) - 1 : -1;
    ui::waveform(g, area, peaks_of(sample->overview), from, to);
    std::vector<int> cuts;
    for (unsigned n = 1; n < slices.count; ++n) cuts.push_back(column(slices.start[n]));
    ui::cuts(g, area, cuts, selected ? int(selected) - 1 : -1);
    auto amount = knob(3).value;
    std::string right;
    if (mode == ChopMode::Auto) right = melodee::sound_kind_name(sample->autos.kind);
    else if (sample->beats.found()) right = std::to_string(sample->beats.tempo / 10);
    ui::detail(g, upper(melodee::chop_mode_name(mode)).substr(0, 5) + " " + amount, right);
}

void MelodeeApp::draw_to_pads(juce::Graphics& g) {
    ui::rail(g, "SLC", two_digits(slices.count), "PADS");
    move::draw_text(g, ui::content_x + 1, 1, std::to_string(slices.count) + " SLICES TO PADS");
    ui::list(g, ui::content(11, 36), {to_pads_names[0], to_pads_names[1], to_pads_names[2]}, to_pads, 1);
    unsigned first = 0;
    while (first < pad_count && pads[first].used()) ++first;
    ui::detail(g, first < pad_count ? "FROM " + pad_label(first) : "NO EMPTY PADS", "PUSH");
}

void MelodeeApp::draw_touched(juce::Graphics& g) {
    auto shown = knob(unsigned(touched));
    bool chop = page == Page::Chop || page == Page::ToPads;
    std::string below = track == Track::Synth ? (module ? upper(module->info().abbrev).substr(0, 4) : "SYN") : chop ? "SLC" : pad_label(pad);
    ui::rail(g, "KNOB", std::to_string(touched + 1), below);
    ui::big_parameter(g, ui::content(4, 44), shown.name, shown.value, shown.fraction);
}

void MelodeeApp::draw_synth(juce::Graphics& g) {
    // The rail as docs/UI.md has a module's page: module, track, page.
    if (!module) {
        ui::rail(g, "SYN", "2", note_name(synth_note(0)));
        move::draw_text_centred(g, ui::content_x, 18, ui::content_width, module_loading ? "LOADING" : "NO SYNTH", 2);
        ui::detail(g, "MENU: MODULES");
        return;
    }
    const ModulePage* shown = synth_page < module->pages.size() ? &module->pages[synth_page] : nullptr;
    ui::rail(g, upper(module->info().abbrev).substr(0, 4), "2", std::to_string(synth_page + 1) + "/" + std::to_string(module->pages.size()));
    std::string title = upper(shown ? shown->name : module->info().name).substr(0, 12), lowest = note_name(synth_note(0));
    move::draw_text(g, ui::content_x + 1, 1, title);
    move::draw_text(g, ui::width - int(lowest.size()) * move::glyph_advance, 1, lowest); // the bottom-left pad's note
    std::array<std::string, 8> names;
    std::array<float, 8> values {};
    for (unsigned n = 0; shown && n < shown->knobs.size() && n < 8; ++n)
        if (const ModuleParam* param = module->param(shown->knobs[n])) {
            names[n] = cell_name(*param);
            values[n] = float(param->fraction());
        }
    ui::knob_cells(g, ui::content(11, ui::subject_height - 11), names, values);
    if (!module->preset_param.empty()) {
        char number[16];
        std::snprintf(number, sizeof number, "%03d", std::clamp(module->preset + 1, 0, 999));
        ui::detail(g, upper(module->preset_name.empty() ? module->info().name : module->preset_name).substr(0, 12), number);
    } else ui::detail(g, upper(module->info().name).substr(0, 16));
}

void MelodeeApp::draw_menu(juce::Graphics& g) {
    ui::rail(g, upper(module->info().abbrev).substr(0, 4), "2", "MENU");
    const ModuleLevel* at = menu_path.empty() ? nullptr : module->level(menu_path.back());
    move::draw_text(g, ui::content_x + 1, 1, upper(at ? at->name : module->info().name).substr(0, 16));
    auto rows = menu_rows();
    if (rows.empty()) {
        move::draw_text_centred(g, ui::content_x, 26, ui::content_width, pick_reading || !pick_key.empty() ? "READING" : "NOTHING HERE");
        return;
    }
    std::vector<std::string> texts;
    int cursor = std::clamp(menu_cursor, 0, int(rows.size()) - 1);
    for (size_t n = 0; n < rows.size(); ++n) texts.push_back(menu_text(rows[n], menu_editing && int(n) == cursor));
    ui::list(g, ui::content(10, ui::height - 10), texts, cursor, 1);
}

void MelodeeApp::draw_modules(juce::Graphics& g) {
    ui::rail(g, "LOAD", "2", "SYN");
    if (catalog.empty()) {
        move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "NO MODULES");
        ui::detail(g, "INSTALL VIA SCHWUNG");
        return;
    }
    std::vector<std::string> rows;
    for (const auto& choice : catalog) {
        std::string row = upper(choice.name);
        if (choice.api != 2) row += " (OLD)";
        else if (module && module->info().id == choice.id) row += " *";
        rows.push_back(row);
    }
    ui::list(g, ui::content(0, ui::height), rows, module_cursor, 1);
}

// ---------------------------------------------------------------- service thread

namespace {

// Reads a WAV and finds what Chop needs in it: its hits, beats and what
// Auto reads. Slow and allocating, so never on the audio thread.
template <class Sample> std::unique_ptr<Sample> read_sample(const std::string& path, std::string& error) {
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

} // namespace

std::string MelodeeApp::install(std::unique_ptr<Sample> sample, unsigned at) {
    if (samples.size() >= melodee::sample_slots) return "MEMORY FULL";
    auto slot = uint16_t(samples.size());
    if (!player.set_sample(slot, sample->data.get())) return "BUSY, TRY AGAIN";
    PadSound sound;
    sound.sample = slot;
    samples.push_back(std::move(sample));
    chops.resize(samples.size());
    set_pad(at, sound);
    say("ON " + pad_label(at));
    return {};
}

void MelodeeApp::put_sample(std::unique_ptr<Sample> sample, uint16_t slot, const melodee::SampleChop& chop) {
    if (slot >= melodee::sample_slots) return;
    if (slot >= samples.size()) { samples.resize(size_t(slot) + 1); chops.resize(samples.size()); }
    if (sample->data && !player.set_sample(slot, sample->data.get())) sample->data.reset(); // the queue was full: as missing
    samples[slot] = std::move(sample);
    chops[slot] = chop;
}

KitFile MelodeeApp::snapshot() const {
    KitFile kit;
    kit.pads = pads;
    kit.last_pad = pad;
    for (size_t slot = 0; slot < samples.size(); ++slot) {
        const auto& sample = samples[slot];
        if (!sample || sample->path.empty()) continue;
        kit.samples.push_back({uint16_t(slot), sample->path, sample->name, sample->data ? sample->data->model_frames() : 0, chops[slot]});
    }
    return kit;
}

std::string MelodeeApp::write(const KitFile& kit, const SynthFile& synth_file) {
    std::error_code error;
    auto projects = fs::path(project).parent_path();
    fs::create_directories(projects, error);
    give_to_owner_of(home, move_user_data);
    give_to_owner_of(projects.string(), move_user_data);
    auto failed = save_kit(kit, project);
    if (failed.empty()) failed = save_synth(synth_file, project);
    if (failed.empty())
        for (const char* beside : {"", ".chops", ".synth"}) give_to_owner_of(project + beside, move_user_data);
    return failed;
}

std::string MelodeeApp::save_now() {
    std::unique_lock<std::mutex> guard(lock);
    auto kit = snapshot();
    auto synth_file = synth_snapshot();
    dirty = false;
    guard.unlock();
    auto error = write(kit, synth_file);
    guard.lock();
    if (error.empty()) ++saves;
    else dirty = true;
    return error;
}

std::string MelodeeApp::restore_now() {
    bool kit_first = false, synth_first = false;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (restored) return {};
        restored = true;
        kit_first = !samples.empty(); // something was loaded first: that is the kit now
        synth_first = module != nullptr;
    }
    if (!synth_first) {
        SynthFile saved;
        load_synth(project, saved);
        {
            std::lock_guard<std::mutex> guard(lock);
            saved_synth = saved;
            octave = saved.octave;
            audio_octave.store(octave, std::memory_order_relaxed);
            track = Track(saved.track);
            audio_track.store(int(track), std::memory_order_relaxed);
        }
        if (!saved.module.empty()) {
            auto failed = load_module_now(saved.module, &saved);
            std::lock_guard<std::mutex> guard(lock);
            if (!failed.empty()) say(upper(saved.module + " MISSING").substr(0, 16));
        }
    }
    if (kit_first) return {};
    KitFile kit;
    auto error = load_kit(project, kit);
    if (error == "missing") return {}; // a first start: a new project
    if (!error.empty()) {
        std::lock_guard<std::mutex> guard(lock);
        say("PROJECT UNREADABLE");
        return error;
    }
    // Every sample read again, outside the lock; a file that has gone keeps
    // its place (and its pads), silent, so saving again keeps it too.
    std::vector<std::unique_ptr<Sample>> read;
    unsigned missing = 0;
    for (const auto& entry : kit.samples) {
        std::string failed;
        auto sample = read_sample<Sample>(entry.path, failed);
        if (!sample) {
            ++missing;
            sample = std::make_unique<Sample>();
            sample->name = entry.name;
            sample->path = entry.path;
        }
        read.push_back(std::move(sample));
    }
    std::lock_guard<std::mutex> guard(lock);
    for (size_t n = 0; n < kit.samples.size(); ++n) put_sample(std::move(read[n]), kit.samples[n].slot, kit.samples[n].chop);
    pads = kit.pads;
    pad = kit.last_pad;
    for (unsigned at = 0; at < pad_count; ++at) player.set_pad(at, pads[at]);
    dirty = false;
    say(missing ? std::to_string(missing) + " FILES MISSING" : "PROJECT LOADED");
    return {};
}

void MelodeeApp::service() {
    if (!restored) restore_now();
    take_answers();
    destroy_retired();
    std::string wanted;
    {
        std::lock_guard<std::mutex> guard(lock);
        wanted.swap(module_job);
    }
    if (!wanted.empty()) {
        auto failed = load_module_now(wanted);
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
        auto sample = read_sample<Sample>(job.path, error);
        std::lock_guard<std::mutex> guard(lock);
        if (!sample) { say(upper(error).substr(0, 16)); continue; }
        if (auto failed = install(std::move(sample), job.pad); !failed.empty()) say(failed);
    }
    std::unique_lock<std::mutex> guard(lock);
    loading = false;
    // Autosave: three seconds after the last change, as on the MPC.
    auto now = Clock::now();
    if (dirty && now - changed_at >= autosave_idle && now >= retry_at) {
        auto kit = snapshot();
        auto synth_file = synth_snapshot();
        dirty = false;
        guard.unlock();
        auto error = write(kit, synth_file);
        guard.lock();
        if (error.empty()) ++saves;
        else { dirty = true; retry_at = now + autosave_retry; say("NOT SAVED"); }
    }
}

std::string MelodeeApp::load_now(const std::string& path, unsigned at) {
    std::string error;
    auto sample = read_sample<Sample>(path, error);
    std::lock_guard<std::mutex> guard(lock);
    if (!sample) return error.empty() ? "unreadable" : error;
    return install(std::move(sample), at);
}

std::string MelodeeApp::load_module_now(const std::string& id, const SynthFile* saved) {
    ModuleInfo info;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto found = std::find_if(catalog.begin(), catalog.end(), [&](const ModuleInfo& choice) { return choice.id == id; });
        if (found == catalog.end()) return "no module " + id;
        info = *found;
        module_loading = true;
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
    std::unique_lock<std::mutex> guard(lock);
    module_loading = false;
    if (!loaded) return error.empty() ? "would not load" : error;
    if (!synth.set_module(loaded.get())) {
        guard.unlock();
        return "busy, try again"; // `loaded` destroyed here, never played
    }
    if (module) leaving.push_back(std::move(module));
    module = std::move(loaded);
    set_at.clear();
    synth_page = 0;
    menu_open = false;
    pick_items.clear();
    pick_key.clear();
    if (!saved) {
        saved_synth = {};
        if (track == Track::Synth && page == Page::Load) page = Page::Play; // to play it
        say(upper(info.name).substr(0, 16));
        changed();
    }
    return {};
}

std::string MelodeeApp::synth_value(const std::string& key) const {
    std::lock_guard<std::mutex> guard(lock);
    const ModuleParam* param = module ? module->param(key) : nullptr;
    return param ? param->text() : std::string();
}

unsigned MelodeeApp::slice_count() const {
    std::lock_guard<std::mutex> guard(lock);
    return slices.count;
}

std::string MelodeeApp::status() const {
    std::lock_guard<std::mutex> guard(lock);
    static const char* names[] {"play", "load", "chop", "to-pads"};
    unsigned used = 0, loaded = 0;
    for (const auto& sound : pads) used += sound.used();
    for (const auto& sample : samples) loaded += sample && sample->data;
    char text[420];
    std::snprintf(text, sizeof text,
                  "page=%s pad=%s samples=%u slices=%u pads=%u chop=%s loading=%d saves=%u unsaved=%d track=%s synth=%s octave=%d "
                  "menu=%s slow_calls=%u slowest_ms=%.1f",
                  names[unsigned(page)], pad_label(pad).c_str(), loaded, slices.count, used, melodee::chop_mode_name(mode), int(loading),
                  saves, int(dirty), track == Track::Synth ? "synth" : "kit", module ? module->info().id.c_str() : "-", octave, menu_open && !menu_path.empty() ? menu_path.back().c_str() : "-", slow_calls,
                  slowest_us / 1000.0);
    return text;
}

} // namespace app
