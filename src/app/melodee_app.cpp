// SPDX-License-Identifier: AGPL-3.0-only
#include "app/melodee_app.h"
#include "move/display.h"
#include "move/font.h"
#include "ui/widgets.h"
#include "melodee/shell/chop.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace app {
namespace {

using melodee::ChopMode;
using melodee::PadSound;
namespace fs = std::filesystem;

constexpr const char* move_library = "/data/UserData/UserLibrary/Samples";
constexpr const char* move_user_data = "/data/UserData";
constexpr const char* move_home = "/data/UserData/Melodee";
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
bool is_wav(const fs::path& path) {
    auto extension = upper(path.extension().string());
    return extension == ".WAV";
}

} // namespace

MelodeeApp::MelodeeApp(std::string library_folder, std::string home_folder)
    : library(std::move(library_folder)), home(std::move(home_folder)) {
    std::error_code error;
    if (library.empty()) library = fs::is_directory(move_library, error) ? move_library : fs::current_path(error).string();
    if (home.empty()) home = fs::is_directory(move_user_data, error) ? move_home : (fs::current_path(error) / "Melodee").string();
    folder = library;
    project = (fs::path(home) / "Projects" / "Move.mldp").string();
}

MelodeeApp::~MelodeeApp() {
    std::unique_lock<std::mutex> guard(lock);
    if (!dirty) return;
    auto kit = snapshot();
    dirty = false;
    guard.unlock();
    write(kit);
}

// ---------------------------------------------------------------- audio thread

void MelodeeApp::render(const move::AudioBlock&, move::AudioBlock& output) {
    player.render(output, volume.load(std::memory_order_relaxed));
}

void MelodeeApp::audio_opened(const move::AudioFormat& format) {
    player.opened(format.rate);
}

void MelodeeApp::audio_event(const move::SurfaceEvent& event) {
    const auto& input = event.input;
    if (input.kind != move::Input::Kind::Pad) return; // no pressure in Melodee's sampler
    if (input.pressed) player.hit(input.index, input.value);
    else player.release(input.index);
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

void MelodeeApp::list_folder() {
    entries.clear();
    std::error_code error;
    for (const auto& item : fs::directory_iterator(folder, fs::directory_options::skip_permission_denied, error)) {
        auto name = item.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (item.is_directory(error)) entries.push_back({name, true});
        else if (is_wav(item.path())) entries.push_back({name, false});
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

// ---------------------------------------------------------------- MIDI thread

void MelodeeApp::surface(const move::SurfaceEvent& event) {
    using Kind = move::Input::Kind;
    using move::Button;
    const auto& input = event.input;
    std::lock_guard<std::mutex> guard(lock);
    bool fine = session().shift.load();
    switch (input.kind) {
    case Kind::Pad:
        if (!input.pressed) break;
        if (page == Page::Chop || page == Page::ToPads) { if (input.index < slices.count) selected = input.index; }
        else select_pad(input.index);
        break;
    case Kind::Button: {
        if (!input.pressed) break;
        auto button = Button(input.index);
        if (button == Button::Menu) {
            if (page == Page::Chop || page == Page::ToPads) leave_chop();
            page = page == Page::Load ? Page::Play : Page::Load;
            if (page == Page::Load) list_folder();
        } else if (button == Button::Loop) {
            if (page == Page::Chop || page == Page::ToPads) leave_chop();
            else enter_chop();
        } else if (button == Button::Back) {
            if (page == Page::Load) {
                if (fs::path(folder) != fs::path(library)) { folder = fs::path(folder).parent_path().string(); cursor = 0; list_folder(); }
                else page = Page::Play;
            } else if (page == Page::Chop) leave_chop();
            else if (page == Page::ToPads) page = Page::Chop;
        } else if (button == Button::MainPush) {
            if (page == Page::Load && cursor < int(entries.size())) {
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
        if (page == Page::Load) cursor = std::clamp(cursor + input.delta, 0, std::max(0, int(entries.size()) - 1));
        else if (page == Page::Chop && slices.count) selected = unsigned(std::clamp(int(selected) + input.delta, 0, int(slices.count) - 1));
        else if (page == Page::ToPads) to_pads = std::clamp(to_pads + input.delta, 0, 2);
        break;
    case Kind::Knob:
        turn_knob(input.index, input.delta, fine);
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
    lights.button(Button::Shift, session().shift ? white_bright : white_dim);
    lights.button(Button::Row1, colour::azure);
}

// ---------------------------------------------------------------- screen thread

void MelodeeApp::draw(juce::Graphics& g) {
    std::lock_guard<std::mutex> guard(lock);
    auto now = Clock::now();
    if (touched >= 0 && now < touched_until && !knob(unsigned(touched)).name.empty()) draw_touched(g);
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
    ui::rail(g, "LOAD", pad_label(pad), jobs.empty() ? "SMP" : "...");
    if (entries.empty()) {
        move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "NO WAV FILES");
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
    ui::rail(g, "KNOB", std::to_string(touched + 1), chop ? "SLC" : pad_label(pad));
    ui::big_parameter(g, ui::content(4, 44), shown.name, shown.value, shown.fraction);
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
    error = melodee::read_wav(path, *sample->data, 60);
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

void MelodeeApp::place(std::unique_ptr<Sample> sample, uint16_t slot, const melodee::SampleChop& chop) {
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

std::string MelodeeApp::write(const KitFile& kit) {
    std::error_code error;
    auto projects = fs::path(project).parent_path();
    fs::create_directories(projects, error);
    give_to_owner_of(home, move_user_data);
    give_to_owner_of(projects.string(), move_user_data);
    auto failed = save_kit(kit, project);
    if (failed.empty()) {
        give_to_owner_of(project, move_user_data);
        give_to_owner_of(project + ".chops", move_user_data);
    }
    return failed;
}

std::string MelodeeApp::save_now() {
    std::unique_lock<std::mutex> guard(lock);
    auto kit = snapshot();
    dirty = false;
    guard.unlock();
    auto error = write(kit);
    guard.lock();
    if (error.empty()) ++saves;
    else dirty = true;
    return error;
}

std::string MelodeeApp::restore_now() {
    {
        std::lock_guard<std::mutex> guard(lock);
        if (restored) return {};
        restored = true;
        if (!samples.empty()) return {}; // something was loaded first: that is the kit now
    }
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
    for (size_t n = 0; n < kit.samples.size(); ++n) place(std::move(read[n]), kit.samples[n].slot, kit.samples[n].chop);
    pads = kit.pads;
    pad = kit.last_pad;
    for (unsigned at = 0; at < pad_count; ++at) player.set_pad(at, pads[at]);
    dirty = false;
    say(missing ? std::to_string(missing) + " FILES MISSING" : "PROJECT LOADED");
    return {};
}

void MelodeeApp::service() {
    if (!restored) restore_now();
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
        dirty = false;
        guard.unlock();
        auto error = write(kit);
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
    char text[200];
    std::snprintf(text, sizeof text, "page=%s pad=%s samples=%u slices=%u pads=%u chop=%s loading=%d saves=%u unsaved=%d",
                  names[unsigned(page)], pad_label(pad).c_str(), loaded, slices.count, used, melodee::chop_mode_name(mode), int(loading),
                  saves, int(dirty));
    return text;
}

} // namespace app
