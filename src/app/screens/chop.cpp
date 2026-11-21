// SPDX-License-Identifier: AGPL-3.0-only
// Chop (Sample Options' first row, Shift + push on a kit pad's sample): the
// last pad's sample, cut by Melodee's Auto, Beats, Regions, Threshold or
// Manual (knob 3 the type, knob 4 or the wheel the amount). The pads play the
// slices, 32 at a time (Plus and Minus: slices 33-64); the steps show where
// the cuts fall and play the sample from their sixteenth; Left / Right pick a
// slice; knobs 1 and 2 move its start and end, the view closing in on the cut
// as they turn, and a moved cut plays its slice once it is still; Shift +
// the wheel jumps the start from hit to hit; Delete + a pad takes its cut
// out. Manual is a take: the red pad cuts next, as the sample plays from the
// first pad (docs/ui/sampling.png). Push the jogwheel for To Pads (Poly,
// Mono, Thru, Kit), onto the first empty pads; Shift + push for the slice
// options (Extract, Split, Merge, Remove all cuts). Back leaves Chop without
// changing the pads.
#include "app/sampling.h"
#include "app/pad_layout.h"
#include "app/screens/common.h"
#include "melodee/shell/chop.h"
#include <cmath>

namespace app::screens {
namespace {

using Clock = std::chrono::steady_clock;
using melodee::ChopMode;
using melodee::PadSound;
using melodee::SlicePlay;
constexpr unsigned default_tempo_tenths = 1200; // Beats' fallback where a sample has no steady beat
constexpr std::array<ChopMode, 5> chop_types {ChopMode::Auto, ChopMode::Beats, ChopMode::Regions, ChopMode::Threshold, ChopMode::Manual};
constexpr const char* to_pads_names[] {"POLY", "MONO", "THRU", "KIT"};
constexpr unsigned bank_size = pad_count; // 32 slices on the pads at a time
constexpr auto close_linger = std::chrono::milliseconds(1200); // the cut close up, after the last turn
constexpr auto still_before_play = std::chrono::milliseconds(150); // a moved cut plays its slice once still this long
constexpr uint32_t close_frames = 5292; // about 120 ms across, as Melodee's chop_follow

bool pulse_on() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count() / 300 % 2 == 0; }

class Chop final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        const Sample* sample = studio.sample_of_pad();
        if (!sample) { studio.say("EMPTY PAD"); studio.close(); return; }
        track = studio.track();
        pad = studio.pad();
        selected = 0;
        bank = 0;
        sounding = true;
        studio.pads_play_slices(true); // the pads its slices, whatever kit bank is in view
        manual_next = -1;
        manual_playing = false;
        close_until = {};
        audition_due = {};
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
        stop_audition();
        if (sounding) studio.sound_pads(track); // the pads back as the kit has them
        sounding = false;
        studio.pads_play_slices(false);
        manual_playing = false;
    }

    bindings::Screen screen_kind() const override { return bindings::Screen::Chop; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        if (job != Job::PlaySlice && manual_playing) stop_short(); // any other control: a manual take stops short
        switch (job) {
        case Job::PlaySlice: { // the pad's number in the pad layout is the slice's in the bank
            unsigned n = bank * bank_size + logical_pad(uint8_t(use.index), pad_layout()) % bank_size;
            if (mode == ChopMode::Manual && manual_next >= 0) { manual_tap(n); return true; }
            if (n < slices.count) selected = n;
            return true;
        }
        case Job::TurnKnob: case Job::TurnKnobFine: turn(unsigned(use.index), use.delta); return true;
        case Job::ChopAmount: turn(3, use.delta); return true; // the wheel: the amount, as the Move's the number of slices
        case Job::JumpHit: jump(use.delta); return true;
        case Job::SliceStep:
            if (slices.count) select(unsigned(std::clamp(int(selected) + (use.control == bindings::Control::Left ? -1 : 1), 0, int(slices.count) - 1)));
            return true;
        case Job::SlicePage:
            if (slices.count <= bank_size) { studio.say("32 SLICES OR FEWER"); return true; }
            bank = use.control == bindings::Control::Plus ? 1 : 0;
            if (selected / bank_size != bank) selected = std::min(bank * bank_size, slices.count - 1);
            play_slices();
            return true;
        case Job::PlayFromPart: play_part(unsigned(use.index)); return true;
        case Job::RemoveCut: remove_cut(bank * bank_size + logical_pad(uint8_t(use.index), pad_layout()) % bank_size); return true;
        case Job::ToPads: if (slices.count) studio.push("to-pads"); return true;
        case Job::SliceOptions: if (slices.count) studio.push("slice-options"); return true;
        case Job::LeaveChop: studio.close(); return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        const Sample* sample = studio.sample_of(track, pad);
        uint32_t frames = sample ? std::max<uint32_t>(1, sample->data->model_frames()) : 1;
        tick(frames);
        bool pulse = pulse_on();
        for (unsigned at = 0; at < pad_count; ++at) {
            unsigned n = bank * bank_size + at;
            uint8_t shown = n < slices.count ? (n == selected && manual_next < 0 ? colour::white : n % 2 ? colour::brown_yellow : colour::orange) : colour::off;
            if (manual_next >= 0 && int(n) == manual_next) shown = pulse ? colour::red : colour::off; // it cuts next
            lights.pad(physical_pad(uint8_t(at), pad_layout()), shown); // slice pad `at`, where the pad layout puts it
        }
        // The sample in 16 parts, lit where a cut falls.
        for (unsigned step = 0; step < step_count; ++step) {
            bool cut = false;
            for (unsigned n = 0; n < slices.count; ++n) cut |= slices.start[n] * step_count / frames == step;
            lights.step(step, cut ? (selected < slices.count && slices.start[selected] * step_count / frames == step ? colour::white : colour::orange) : colour::off);
        }
        bool banks = slices.count > bank_size;
        lights.button(Button::Left, white_dim);
        lights.button(Button::Right, white_dim);
        lights.button(Button::Up, banks ? (bank == 1 ? white_bright : white_dim) : 0);
        lights.button(Button::Down, banks ? (bank == 0 ? white_bright : white_dim) : 0);
        lights.button(Button::Loop, 0);
        lights.button(Button::Menu, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample) return;
        uint32_t frames = std::max<uint32_t>(1, sample->data->model_frames());
        if (mode == ChopMode::Manual && manual_next >= 0) { draw_take(g, *sample, frames); return; }
        if (Clock::now() < close_until && slices.count) { draw_close(g, *sample, frames); return; }
        ui::rail(g, "SLC", two_digits(selected + 1), "/" + std::to_string(slices.count));
        auto area = ui::content(0, ui::subject_height);
        auto column = [&](uint32_t frame) { return int(int64_t(frame) * area.getWidth() / frames); };
        int from = slices.count ? column(slices.start[selected]) : -1, to = slices.count ? column(slices.end(selected, frames)) - 1 : -1;
        ui::waveform(g, area, peaks_of(sample->overview), from, to);
        std::vector<int> cuts;
        for (unsigned n = 1; n < slices.count; ++n) cuts.push_back(column(slices.start[n]));
        ui::cuts(g, area, cuts, selected ? int(selected) - 1 : -1);
        std::string right;
        if (mode == ChopMode::Auto) right = melodee::sound_kind_name(sample->autos.kind);
        else if (sample->beats.found()) right = std::to_string(sample->beats.tempo / 10);
        if (bank == 1) { ui::detail(g, "PADS 33-" + std::to_string(slices.count), upper(melodee::chop_mode_name(mode)).substr(0, 6)); return; }
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
            else if (mode == ChopMode::Manual) std::snprintf(text, sizeof text, "%u", slices.count);
            else std::snprintf(text, sizeof text, "%d%%", amount);
            return {mode == ChopMode::Beats ? "DIVISION" : mode == ChopMode::Threshold ? "THRESHOLD" : "SLICES", text, 0};
        }
        default: return {};
        }
    }
    std::string knob_owner() const override { return "SLC"; }

    std::string status() const override {
        return " slices=" + std::to_string(slices.count) + " chop=" + melodee::chop_mode_name(mode) + " slice=" + std::to_string(selected + 1) +
               " bank=" + std::to_string(bank + 1) + " manual_next=" + std::to_string(manual_next + 1);
    }

    // To Pads: the slices onto the first empty pads, as Poly, Mono, Thru, or Kit (a pad a sound).
    void to_pads(int how) {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample || !slices.count) return;
        uint32_t frames = sample->data->model_frames();
        const auto& kit = studio.project().tracks[track];
        uint8_t group = melodee::slice_group(kit.pads[pad], kit.pads);
        PadSound parent = kit.pads[pad];
        std::vector<unsigned> chosen;
        if (how == 3) for (const auto& sound : melodee::kit_sounds(slices, frames, sample->hits, sample->hit_count, sample->autos)) chosen.push_back(sound.slice);
        else for (unsigned n = 0; n < slices.count; ++n) chosen.push_back(n);
        auto play = how == 3 ? SlicePlay::Poly : SlicePlay(std::clamp(how, 0, 2));
        unsigned placed = 0, first = pad_count;
        std::vector<std::pair<unsigned, PadSound>> made;
        for (unsigned at = 0; at < pad_count && placed < chosen.size(); ++at) {
            if (kit.pads[at].used()) continue;
            if (first == pad_count) first = at;
            made.emplace_back(at, melodee::slice_pad(parent, slices, chosen[placed++], frames, play, group));
        }
        if (!placed) { studio.say("NO EMPTY PADS"); return; }
        for (const auto& [at, sound] : made) studio.project().tracks[track].pads[at] = sound;
        studio.home(); // Chop left: the pads sound as the kit has them now
        studio.select_pad(first);
        studio.changed();
        studio.say(std::to_string(placed) + (how == 3 ? " SOUNDS FROM " : " SLICES FROM ") + pad_label(first));
    }
    unsigned slice_count() const { return slices.count; }
    unsigned kit_count() const {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample) return 0;
        return unsigned(melodee::kit_sounds(slices, sample->data->model_frames(), sample->hits, sample->hit_count, sample->autos).size());
    }
    unsigned first_empty() const {
        unsigned first = 0;
        while (first < pad_count && studio.project().tracks[track].pads[first].used()) ++first;
        return first;
    }
    unsigned selected_slice() const { return selected; }

    // The slice options (Shift + push).
    void extract() {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample || selected >= slices.count) return;
        studio.sampling().extract(track, pad, slices.start[selected], slices.end(selected, sample->data->model_frames()), selected);
    }
    void split() {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample || selected >= slices.count) return;
        uint32_t frames = sample->data->model_frames();
        uint32_t a = slices.start[selected], b = slices.end(selected, frames);
        if (!slices.split(selected, frames)) { studio.say(slices.count >= melodee::max_slices ? "64 SLICES AT MOST" : "TOO SHORT"); return; }
        // The new cut on the strongest hit of the slice's middle half, on a zero crossing.
        uint32_t middle = a + (b - a) / 2, cut = middle;
        uint8_t best = 0;
        for (unsigned h = 0; h < sample->hit_count; ++h)
            if (sample->hits[h].frame > a + (b - a) / 4 && sample->hits[h].frame < b - (b - a) / 4 && sample->hits[h].strength > best) {
                best = sample->hits[h].strength;
                cut = sample->hits[h].frame;
            }
        slices.move_start(selected + 1, melodee::zero_crossing(*sample->data, cut), frames);
        edited();
    }
    void merge() {
        if (selected + 1 >= slices.count) { studio.say("THE LAST SLICE"); return; }
        slices.remove(selected + 1);
        edited();
    }
    void remove_all() {
        const auto& sound = studio.project().tracks[track].pads[pad];
        slices.whole(sound.start, sound.end);
        mode = ChopMode::Manual;
        selected = 0;
        bank = 0;
        manual_next = 0; // a manual take, from the first pad
        manual_playing = false;
        edited();
    }

private:
    void chop_again() {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample) return;
        const auto& data = *sample->data;
        const auto& sound = studio.project().tracks[track].pads[pad];
        uint32_t frames = data.model_frames();
        uint32_t from = sound.start, to = sound.end ? sound.end : frames;
        manual_next = -1;
        manual_playing = false;
        if (mode == ChopMode::Manual) { // no cut yet: the take begins on the first pad
            slices.whole(from, sound.end);
            manual_next = 0;
        } else if (mode == ChopMode::Auto)
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
        bank = std::min(bank, slices.count > bank_size ? 1u : 0u);
        play_slices();
        remember();
    }

    // The pads play the slices of the bank in view while Chop is open (the
    // engine's pads only); in a manual take each plays on to the region's end.
    void play_slices() {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample) return;
        uint32_t frames = sample->data->model_frames();
        const auto& parent = studio.project().tracks[track].pads[pad];
        auto play = mode == ChopMode::Manual && manual_next >= 0 ? SlicePlay::Thru : SlicePlay::Poly;
        for (unsigned at = 0; at < pad_count; ++at) {
            unsigned n = bank * bank_size + at;
            studio.sound_pad(track, at, n < slices.count ? melodee::slice_pad(parent, slices, n, frames, play, 0) : PadSound {});
        }
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
    void edited() {
        selected = std::min(selected, slices.count ? slices.count - 1 : 0);
        if (slices.count <= bank_size) bank = 0;
        play_slices();
        remember();
    }
    void select(unsigned n) {
        selected = n;
        if (n / bank_size != bank) { bank = n / bank_size; play_slices(); }
    }

    // ------------------------------------------------- Manual: a take
    // Where the sample plays now, from when the last pad sounded (its rate by the pad's tune).
    uint32_t heard_at() const {
        const auto& sound = studio.project().tracks[track].pads[pad];
        double rate = std::pow(2.0, (sound.pitch + sound.fine / 100.0) / 12.0);
        double seconds = std::chrono::duration<double>(Clock::now() - played_at).count();
        return played_from + uint32_t(seconds * 44100.0 * rate);
    }
    void manual_tap(unsigned n) {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample) return;
        uint32_t frames = sample->data->model_frames();
        auto now = Clock::now();
        if (int(n) == manual_next && n == 0) { // the first pad: the region plays (its own sound), the take begins
            played_from = slices.start[0];
            played_at = now;
            manual_playing = true;
            manual_next = 1;
            selected = 0;
            return;
        }
        if (int(n) == manual_next && !manual_playing) { // stopped short: the red pad plays on from the last cut
            played_from = slices.start[slices.count - 1];
            played_at = now;
            manual_playing = true;
            audition(played_from, slices.end(slices.count - 1, frames));
            return;
        }
        if (int(n) == manual_next) { // the red pad cuts where the sound is, pulled onto the strongest hit
            uint32_t at = melodee::zero_crossing(*sample->data, melodee::snap_to_hit(heard_at(), sample->hits, sample->hit_count));
            if (slices.count >= melodee::max_slices || !slices.add(at, frames)) return;
            selected = slices.count - 1;
            manual_next = int(slices.count);
            if (manual_next >= int(melodee::max_slices)) manual_next = -1;
            if (manual_next >= 0 && unsigned(manual_next) / bank_size != bank) bank = unsigned(manual_next) / bank_size; // on to slices 33-64
            play_slices();
            remember();
            return;
        }
        if (n < slices.count) { // a lit pad: plays again from its slice, the cuts after it gone (the MPC 3's way)
            while (slices.count > n + 1) slices.remove(slices.count - 1);
            selected = n;
            manual_next = int(n + 1);
            played_from = slices.start[n];
            played_at = now;
            manual_playing = true;
            play_slices();
            remember();
        }
    }
    void stop_short() {
        manual_playing = false; // the red stays on the next pad
        stop_audition();
    }

    // ------------------------------------------------- hearing it
    void audition(uint32_t from, uint32_t to) {
        const auto& sound = studio.project().tracks[track].pads[pad];
        melodee::Command play {melodee::Command::Type::Audition, uint8_t(track), 0, 0, 100};
        play.pad.sample = sound.sample;
        play.pad.start = from;
        play.pad.end = to;
        studio.send(play);
    }
    void stop_audition() {
        melodee::Command stop {melodee::Command::Type::Audition, uint8_t(track)};
        stop.pad.sample = melodee::no_sample;
        studio.send(stop);
    }
    // A step: the region from its sixteenth on.
    void play_part(unsigned step) {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample) return;
        const auto& sound = studio.project().tracks[track].pads[pad];
        uint32_t frames = sample->data->model_frames(), from = sound.start, to = sound.end ? sound.end : frames;
        audition(from + uint32_t(uint64_t(to - from) * step / move::step_count), to);
    }
    // Every lights(): a manual take played to the end; a moved cut still long enough plays its slice.
    void tick(uint32_t frames) {
        auto now = Clock::now();
        if (manual_playing && slices.count && heard_at() >= slices.end(slices.count - 1, frames)) {
            manual_playing = false;
            manual_next = -1; // played to the end: no red
            play_slices();
        }
        if (audition_due != Clock::time_point {} && now >= audition_due && selected < slices.count) {
            audition_due = {};
            uint32_t start = slices.start[selected], end = slices.end(selected, frames);
            if (close_which == 0) audition(start > 13230 ? start - 13230 : 0, end); // from 300 ms before the start into the slice
            else audition(end > 44100 && end - 44100 > start ? end - 44100 : start, end); // the slice's last second
        }
    }

    // ------------------------------------------------- knobs and the wheel
    void moved(unsigned which) {
        close_which = which;
        close_until = Clock::now() + close_linger;
        audition_due = Clock::now() + still_before_play;
    }
    void turn(unsigned which, int delta) {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample || !slices.count) return;
        uint32_t frames = sample->data->model_frames();
        uint32_t step = std::max<uint32_t>(1, frames / (studio.shift ? 8192 : 256));
        unsigned n = selected;
        if (which == 0) { // the selected slice's start; the slice before follows where they touch
            slices.move_start(n, uint32_t(std::max<int64_t>(0, int64_t(slices.start[n]) + int64_t(delta) * step)), frames);
            if (n == 0) slices.start[0] = std::max(slices.start[0], studio.project().tracks[track].pads[pad].start);
            moved(0);
        } else if (which == 1) { // its end; the slice after follows where they touch
            slices.move_end(n, uint32_t(std::max<int64_t>(0, int64_t(slices.end(n, frames)) + int64_t(delta) * step)), frames);
            if (slices.stop[n] == frames) slices.stop[n] = 0;
            moved(1);
        } else if (which == 2 && delta) {
            auto at = std::find(chop_types.begin(), chop_types.end(), mode) - chop_types.begin();
            mode = chop_types[size_t((at + (delta > 0 ? 1 : int(chop_types.size()) - 1)) % chop_types.size())];
            chop_again();
            return;
        } else if (which == 3) {
            int& amount = amounts[unsigned(mode)];
            switch (mode) {
            case ChopMode::Auto: amount = std::clamp(amount + delta, 0, int(melodee::auto_counts) - 1); break;
            case ChopMode::Beats: amount = std::clamp(amount + delta, 0, int(melodee::beat_divisions) - 1); break;
            case ChopMode::Regions: amount = std::clamp(amount + delta, 2, int(melodee::max_slices)); break;
            case ChopMode::Manual: return; // a take's cuts are the hand's
            default: amount = std::clamp(amount + delta, 0, 100); break;
            }
            chop_again();
            return;
        } else return;
        play_slices();
        remember();
    }
    // Shift + the wheel: the start from hit to hit, within its room (20 ms clear of its neighbours).
    void jump(int delta) {
        const Sample* sample = studio.sample_of(track, pad);
        if (!sample || !slices.count || !delta) return;
        uint32_t frames = sample->data->model_frames();
        unsigned n = selected;
        uint32_t lo = n ? slices.start[n - 1] + melodee::min_slice : studio.project().tracks[track].pads[pad].start;
        uint32_t hi = slices.end(n, frames) > melodee::min_slice ? slices.end(n, frames) - melodee::min_slice : 0;
        uint8_t floor = melodee::mark_floor(amounts[unsigned(ChopMode::Threshold)]);
        uint32_t at = slices.start[n];
        for (int k = 0; k < std::abs(delta); ++k) at = melodee::next_hit_within(at, delta, sample->hits, sample->hit_count, floor, lo, hi);
        if (at == slices.start[n]) return;
        slices.move_start(n, melodee::zero_crossing(*sample->data, at), frames);
        moved(0);
        play_slices();
        remember();
    }
    void remove_cut(unsigned n) {
        if (n >= slices.count || slices.count < 2) return;
        slices.remove(n ? n : 1); // into the one before; the first takes the one after
        if (selected >= n && selected) --selected;
        edited();
        studio.say("CUT OUT");
    }

    // ------------------------------------------------- drawing
    // The cut close up: about 120 ms round it, from the frames themselves; the hits marked at the foot.
    void draw_close(juce::Graphics& g, const Sample& sample, uint32_t frames) {
        unsigned n = selected;
        uint32_t cut = close_which == 0 ? slices.start[n] : slices.end(n, frames);
        ui::rail(g, "SLC", two_digits(n + 1), close_which == 0 ? "STRT" : "END");
        auto area = ui::content(0, ui::subject_height - 4);
        int width = area.getWidth();
        int64_t from = int64_t(cut) - close_frames / 2;
        const auto& data = *sample.data;
        std::vector<float> peaks(size_t(width), 0.0f);
        for (int x = 0; x < width; ++x) {
            int64_t a = from + int64_t(x) * close_frames / width, b = from + int64_t(x + 1) * close_frames / width;
            float most = 0;
            for (int64_t f = std::max<int64_t>(a, 0); f < b && f < int64_t(frames); ++f) {
                uint32_t own = data.own(uint32_t(f));
                if (own < data.left.size()) most = std::max({most, std::fabs(data.left[own]), std::fabs(data.right[own])});
            }
            peaks[size_t(x)] = most;
        }
        ui::waveform(g, area, peaks);
        ui::cuts(g, area, {width / 2}, 0);
        std::vector<int> marks;
        uint8_t floor = melodee::mark_floor(amounts[unsigned(ChopMode::Threshold)]);
        for (unsigned h = 0; h < sample.hit_count; ++h)
            if (sample.hits[h].strength >= floor && int64_t(sample.hits[h].frame) >= from && int64_t(sample.hits[h].frame) < from + close_frames)
                marks.push_back(int((int64_t(sample.hits[h].frame) - from) * width / close_frames));
        for (int x : marks) g.fillRect(area.getX() + x, area.getBottom() + 1, 1, 2);
        const auto& sound = studio.project().tracks[track].pads[pad];
        char offset[16], zoom[16];
        std::snprintf(offset, sizeof offset, "+%.2fS", (cut - sound.start) / 44100.0);
        uint32_t region = (sound.end ? sound.end : frames) - sound.start;
        std::snprintf(zoom, sizeof zoom, "X%u", std::max<uint32_t>(1, region / close_frames));
        ui::detail(g, offset, zoom);
    }
    // A manual take: the cuts so far, where it plays now, the pad that cuts next.
    void draw_take(juce::Graphics& g, const Sample& sample, uint32_t frames) {
        ui::rail(g, "TAKE", two_digits(slices.count ? slices.count - 1 : 0), "TAP");
        auto area = ui::content(0, ui::subject_height);
        auto column = [&](uint32_t frame) { return int(int64_t(frame) * area.getWidth() / frames); };
        ui::waveform(g, area, peaks_of(sample.overview));
        std::vector<int> cuts;
        for (unsigned n = 1; n < slices.count; ++n) cuts.push_back(column(slices.start[n]));
        ui::cuts(g, area, cuts, int(cuts.size()) - 1);
        if (manual_playing) ui::playhead(g, area, std::min(column(heard_at()), area.getWidth() - 1));
        ui::detail(g, "NEXT: " + pad_label(unsigned(manual_next) % pad_count), "MANUAL");
    }

    unsigned track = 0, pad = 0;
    bool sounding = false;
    ChopMode mode = ChopMode::Auto;
    std::array<int, unsigned(ChopMode::Count)> amounts {50, 16, 1, 0, 1}; // Threshold %, Regions, Beats division, Manual, Auto size
    melodee::Slices slices;
    unsigned selected = 0, bank = 0;
    int manual_next = -1;          // the slice the red pad cuts (-1: no take)
    bool manual_playing = false;
    uint32_t played_from = 0;      // where the sound began (44.1 kHz frames)
    Clock::time_point played_at {};
    Clock::time_point close_until {}, audition_due {};
    unsigned close_which = 0;      // 0 the start, 1 the end
};

Chop& chop_of(Studio& studio) { return static_cast<Chop&>(*studio.screen("chop")); }

// To Pads, a sheet over Chop: Poly, Mono, Thru or Kit.
class ToPads final : public Screen {
public:
    using Screen::Screen;
    Chop& chop() const { return chop_of(studio); }

    bindings::Screen screen_kind() const override { return bindings::Screen::ToPads; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ToPadsChoose: how = std::clamp(how + use.delta, 0, 3); return true;
        case Job::ToPadsPlace: chop().to_pads(how); return true;
        case Job::Back: studio.close(); return true;
        case Job::PlaySlice: case Job::TurnKnob: case Job::TurnKnobFine: return true; // the slices sound; nothing changes
        default: return false;
        }
    }
    void lights(move::Lights& lights) override { chop().lights(lights); }
    void draw(juce::Graphics& g) override {
        unsigned count = how == 3 ? chop().kit_count() : chop().slice_count();
        ui::rail(g, "SLC", two_digits(count), "PADS");
        move::draw_text(g, ui::content_x + 1, 1, std::to_string(count) + (how == 3 ? " SOUNDS TO PADS" : " SLICES TO PADS"));
        ui::list(g, ui::content(11, 36), {to_pads_names[0], to_pads_names[1], to_pads_names[2], to_pads_names[3]}, how, 1);
        unsigned first = chop().first_empty();
        ui::detail(g, first < pad_count ? "FROM " + pad_label(first) : "NO EMPTY PADS", "PUSH");
    }
    Knob knob(unsigned which) const override { return chop().knob(which); }
    std::string knob_owner() const override { return "SLC"; }

private:
    int how = 0;
};

// The slice options (Shift + push in Chop): Extract, Split, Merge, Remove all cuts.
class SliceOptions final : public Screen {
public:
    using Screen::Screen;
    void enter() override { cursor = 0; }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        auto& chop = chop_of(studio);
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: cursor = std::clamp(cursor + use.delta, 0, 3); return true;
        case Job::ListPush:
            studio.close(); // back to Chop first
            if (cursor == 0) chop.extract();
            else if (cursor == 1) chop.split();
            else if (cursor == 2) chop.merge();
            else chop.remove_all();
            return true;
        case Job::Back: studio.close(); return true;
        case Job::PlayPad: case Job::TurnKnob: case Job::TurnKnobFine: return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override { chop_of(studio).lights(lights); }
    void draw(juce::Graphics& g) override {
        ui::rail(g, "OPT", two_digits(chop_of(studio).selected_slice() + 1), "SLC");
        ui::list(g, ui::content(0, ui::height), {"EXTRACT", "SPLIT", "MERGE", "REMOVE ALL CUTS"}, cursor, 1);
    }

private:
    int cursor = 0;
};

RegisterScreen chop("chop", make<Chop>); // from Sample Options (Shift + push on a kit pad's sample)
RegisterScreen to_pads("to-pads", make<ToPads>);
RegisterScreen slice_options("slice-options", make<SliceOptions>);

} // namespace
} // namespace app::screens
