// SPDX-License-Identifier: AGPL-3.0-only
// Chop (Loop on a kit track): the last pad's sample, cut by Melodee's Auto,
// Beats, Regions or Threshold (knob 3 the type, knob 4 the amount). The pads
// play the slices, the steps show where the cuts fall, the jogwheel or
// Left / Right picks a slice, knobs 1 and 2 move its start and end. Push the
// jogwheel for To Pads (Poly, Mono, Thru), onto the first empty pads. Back
// or Loop leaves Chop without changing the pads.
#include "app/screens/common.h"
#include "app/pad_layout.h"
#include "melodee/shell/chop.h"

namespace app::screens {
namespace {

using melodee::ChopMode;
using melodee::PadSound;
constexpr unsigned default_tempo_tenths = 1200; // Beats' fallback where a sample has no steady beat
constexpr std::array<ChopMode, 4> chop_types {ChopMode::Auto, ChopMode::Beats, ChopMode::Regions, ChopMode::Threshold};
constexpr const char* to_pads_names[] {"POLY", "MONO", "THRU"};

class Chop final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        const Sample* sample = studio.sample_of_pad();
        if (!sample) { studio.say("EMPTY PAD"); studio.close(); return; }
        track = studio.track();
        pad = studio.pad();
        selected = 0;
        sounding = true;
        studio.pads_play_slices(true); // the pads its slices, whatever kit bank is in view
        // The sample's chop as it was left, where it was of this same region.
        const auto& sound = studio.current().pads[pad];
        uint32_t frames = sample->data->model_frames();
        const auto* kept = studio.chop_of(sound.sample);
        if (kept && kept->used() && kept->from == sound.start && (kept->to ? kept->to : frames) == (sound.end ? sound.end : frames)) {
            mode = kept->mode;
            amounts[unsigned(mode)] = kept->amount;
            slices = kept->slices;
            play_slices();
            return;
        }
        chop_again();
    }

    void leave() override {
        if (sounding) studio.sound_pads(track); // the pads back as the kit has them
        sounding = false;
        studio.pads_play_slices(false);
    }

    bindings::Screen screen_kind() const override { return bindings::Screen::Chop; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::PlaySlice:
            if (unsigned slice = logical_pad(uint8_t(use.index), pad_layout()); slice < slices.count) selected = slice; // the pad layout's number
            return true;
        case Job::TurnKnob: case Job::TurnKnobFine: turn(unsigned(use.index), use.delta); return true;
        case Job::ChopAmount: turn(3, use.delta); return true; // the wheel: the amount, as the Move's the number of slices
        case Job::SliceStep:
            if (slices.count) selected = unsigned(std::clamp(int(selected) + (use.control == bindings::Control::Left ? -1 : 1), 0, int(slices.count) - 1));
            return true;
        case Job::ToPads: if (slices.count) studio.push("to-pads"); return true;
        case Job::LeaveChop: studio.close(); return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        const Sample* sample = studio.sample_of(track, pad);
        uint32_t frames = sample ? std::max<uint32_t>(1, sample->data->model_frames()) : 1;
        for (unsigned at = 0; at < pad_count; ++at) // `at` the slice; its pad in the layout
            lights.pad(physical_pad(uint8_t(at), pad_layout()), at < slices.count ? (at == selected ? colour::white : at % 2 ? colour::brown_yellow : colour::orange) : colour::off);
        // The sample in 16 parts, lit where a cut falls.
        for (unsigned step = 0; step < step_count; ++step) {
            bool cut = false;
            for (unsigned n = 0; n < slices.count; ++n) cut |= slices.start[n] * step_count / frames == step;
            lights.step(step, cut ? (selected < slices.count && slices.start[selected] * step_count / frames == step ? colour::white : colour::orange) : colour::off);
        }
        lights.button(Button::Left, white_dim);
        lights.button(Button::Right, white_dim);
        lights.button(Button::Up, 0);
        lights.button(Button::Down, 0);
        lights.button(Button::Loop, 0);
        lights.button(Button::Menu, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "SLC", two_digits(selected + 1), "/" + std::to_string(slices.count));
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample) return;
        auto area = ui::content(0, ui::subject_height);
        uint32_t frames = std::max<uint32_t>(1, sample->data->model_frames());
        auto column = [&](uint32_t frame) { return int(int64_t(frame) * area.getWidth() / frames); };
        int from = slices.count ? column(slices.start[selected]) : -1, to = slices.count ? column(slices.end(selected, frames)) - 1 : -1;
        ui::waveform(g, area, peaks_of(sample->overview), from, to);
        std::vector<int> cuts;
        for (unsigned n = 1; n < slices.count; ++n) cuts.push_back(column(slices.start[n]));
        ui::cuts(g, area, cuts, selected ? int(selected) - 1 : -1);
        std::string right;
        if (mode == ChopMode::Auto) right = melodee::sound_kind_name(sample->autos.kind);
        else if (sample->beats.found()) right = std::to_string(sample->beats.tempo / 10);
        ui::detail(g, upper(melodee::chop_mode_name(mode)).substr(0, 5) + " " + knob(3).value, right);
    }

    Knob knob(unsigned which) const override {
        const Sample* sample = studio.sample_of(track, pad);
        uint32_t frames = sample ? std::max<uint32_t>(1, sample->data->model_frames()) : 1;
        unsigned n = std::min(selected, slices.count ? slices.count - 1 : 0);
        char text[24];
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
            return {mode == ChopMode::Beats ? "DIVISION" : mode == ChopMode::Threshold ? "THRESHOLD" : "SLICES", text, 0};
        }
        default: return {};
        }
    }
    std::string knob_owner() const override { return "SLC"; }

    std::string status() const override {
        return " slices=" + std::to_string(slices.count) + " chop=" + melodee::chop_mode_name(mode);
    }

    // To Pads: the slices onto the first empty pads, as Poly, Mono or Thru.
    void to_pads(int how) {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample || !slices.count) return;
        uint32_t frames = sample->data->model_frames();
        auto play = melodee::SlicePlay(std::clamp(how, 0, 2));
        const auto& kit = studio.project().tracks[track];
        uint8_t group = melodee::slice_group(kit.pads[pad], kit.pads);
        PadSound parent = kit.pads[pad];
        unsigned placed = 0, first = pad_count;
        std::vector<std::pair<unsigned, PadSound>> made;
        for (unsigned at = 0; at < pad_count && placed < slices.count; ++at) {
            if (kit.pads[at].used()) continue;
            if (first == pad_count) first = at;
            made.emplace_back(at, melodee::slice_pad(parent, slices, placed++, frames, play, group));
        }
        if (!placed) { studio.say("NO EMPTY PADS"); return; }
        for (const auto& [at, sound] : made) studio.project().tracks[track].pads[at] = sound;
        studio.home(); // Chop left: the pads sound as the kit has them now
        studio.select_pad(first);
        studio.changed();
        studio.say(std::to_string(placed) + " SLICES FROM " + pad_label(first));
    }
    unsigned slice_count() const { return slices.count; }
    unsigned first_empty() const {
        unsigned first = 0;
        while (first < pad_count && studio.project().tracks[track].pads[first].used()) ++first;
        return first;
    }

private:
    void chop_again() {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample) return;
        const auto& data = *sample->data;
        const auto& sound = studio.project().tracks[track].pads[pad];
        uint32_t frames = data.model_frames();
        uint32_t from = sound.start, to = sound.end ? sound.end : frames;
        if (mode == ChopMode::Auto)
            slices = melodee::auto_chop(sample->autos, melodee::auto_slice_count(amounts[unsigned(mode)]), from, to, frames, sample->hits,
                                        sample->hit_count, sample->beats);
        else {
            int amount = mode == ChopMode::Beats ? int(melodee::division_sixteenths(unsigned(amounts[unsigned(mode)]), 16)) : amounts[unsigned(mode)];
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
        remember();
    }

    // The pads play the slices while Chop is open: the engine's pads only.
    void play_slices() {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample) return;
        uint32_t frames = sample->data->model_frames();
        const auto& parent = studio.project().tracks[track].pads[pad];
        for (unsigned at = 0; at < pad_count; ++at)
            studio.sound_pad(track, at, at < slices.count ? melodee::slice_pad(parent, slices, at, frames, melodee::SlicePlay::Poly, 0) : PadSound {});
    }

    // The sample's chop, kept in the project (CHOP), as Melodee keeps it.
    void remember() {
        const auto& sound = studio.project().tracks[track].pads[pad];
        auto* chop = studio.chop_of(sound.sample);
        if (!chop) return;
        chop->from = sound.start;
        chop->to = sound.end;
        chop->mode = mode;
        chop->amount = int16_t(amounts[unsigned(mode)]);
        chop->slices = slices;
        chop->hand = {};
        studio.changed();
    }

    void turn(unsigned which, int delta) {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample || !slices.count) return;
        uint32_t frames = sample->data->model_frames();
        uint32_t step = std::max<uint32_t>(1, frames / (studio.shift ? 8192 : 256));
        auto move_by = [&](uint32_t value, uint32_t low, uint32_t high) {
            int64_t moved = int64_t(value) + int64_t(delta) * int64_t(step);
            return uint32_t(std::clamp<int64_t>(moved, low, std::max(low, high)));
        };
        unsigned n = selected;
        if (which == 0) { // the selected slice's start; the slice before follows where they touch
            uint32_t low = n ? slices.start[n - 1] + melodee::min_slice : studio.project().tracks[track].pads[pad].start;
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
        remember();
    }

    unsigned track = 0, pad = 0;
    bool sounding = false;
    ChopMode mode = ChopMode::Auto;
    std::array<int, unsigned(ChopMode::Count)> amounts {50, 16, 1, 0, 1}; // Threshold %, Regions, Beats division, -, Auto size
    melodee::Slices slices;
    unsigned selected = 0;
};

// To Pads, a sheet over Chop: Poly, Mono or Thru.
class ToPads final : public Screen {
public:
    using Screen::Screen;
    Chop& chop() const { return static_cast<Chop&>(*studio.screen("chop")); }

    bindings::Screen screen_kind() const override { return bindings::Screen::ToPads; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ToPadsChoose: how = std::clamp(how + use.delta, 0, 2); return true;
        case Job::ToPadsPlace: chop().to_pads(how); return true;
        case Job::Back: studio.close(); return true;
        case Job::PlaySlice: case Job::TurnKnob: case Job::TurnKnobFine: return true; // the slices sound; nothing changes
        default: return false;
        }
    }
    void lights(move::Lights& lights) override { chop().lights(lights); }
    void draw(juce::Graphics& g) override {
        unsigned count = chop().slice_count();
        ui::rail(g, "SLC", two_digits(count), "PADS");
        move::draw_text(g, ui::content_x + 1, 1, std::to_string(count) + " SLICES TO PADS");
        ui::list(g, ui::content(11, 36), {to_pads_names[0], to_pads_names[1], to_pads_names[2]}, how, 1);
        unsigned first = chop().first_empty();
        ui::detail(g, first < pad_count ? "FROM " + pad_label(first) : "NO EMPTY PADS", "PUSH");
    }
    Knob knob(unsigned which) const override { return chop().knob(which); }
    std::string knob_owner() const override { return "SLC"; }

private:
    int how = 0;
};

RegisterScreen chop("chop", make<Chop>); // from Sample Options (Shift + push on a kit pad's sample)
RegisterScreen to_pads("to-pads", make<ToPads>);

} // namespace
} // namespace app::screens
