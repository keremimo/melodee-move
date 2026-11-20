// SPDX-License-Identifier: AGPL-3.0-only
// A kit track's Play screen (docs/UI.md): the last pad's sample, its
// waveform with start and end; knobs 1-8 tune, level, pan, start, end,
// attack, decay, cutoff (Shift: finer); the pads play the kit and pick the
// pad edited; Left / Right the pad before / after.
#include "app/screens/common.h"

namespace app::screens {
namespace {

using melodee::PadSound;

class KitPlay final : public Screen {
public:
    using Screen::Screen;

    bool surface(const move::Input& input) override {
        using Kind = move::Input::Kind;
        using move::Button;
        switch (input.kind) {
        case Kind::Pad:
            if (input.pressed) studio.select_pad(input.index);
            return true;
        case Kind::Knob: turn(input.index, input.delta, studio.shift); return true;
        case Kind::Button:
            if (!input.pressed) return false;
            if (Button(input.index) == Button::Left || Button(input.index) == Button::Right) {
                int way = Button(input.index) == Button::Left ? -1 : 1;
                studio.select_pad(unsigned(std::clamp(int(studio.pad()) + way, 0, int(pad_count) - 1)));
                return true;
            }
            return false;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        const auto& track = studio.current();
        auto colour = ui::track_colour(studio.track()).bright;
        for (unsigned at = 0; at < pad_count; ++at)
            lights.pad(at, track.pads[at].used() ? (at == studio.pad() ? colour::white : colour) : at == studio.pad() ? colour::dark_grey : colour::off);
        steps_playhead(studio, lights);
        lights.button(Button::Left, white_dim);
        lights.button(Button::Right, white_dim);
        lights.button(Button::Up, 0);
        lights.button(Button::Down, 0);
        lights.button(Button::Loop, studio.sample_of_pad() ? white_dim : 0);
        lights.button(Button::Menu, white_dim);
        lights.button(Button::Back, 0);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "KIT", track_number(studio), pad_label(studio.pad()));
        const Sample* sample = studio.sample_of_pad();
        if (!sample) {
            move::draw_text_centred(g, ui::content_x, 18, ui::content_width, "EMPTY PAD", 2);
            ui::detail(g, "MENU: LOAD", bar_beat(studio));
            return;
        }
        auto area = ui::content(0, ui::subject_height);
        ui::waveform(g, area, peaks_of(sample->overview));
        uint32_t frames = std::max<uint32_t>(1, sample->data->model_frames());
        const auto& sound = studio.current().pads[studio.pad()];
        int start = int(int64_t(sound.start) * area.getWidth() / frames), end = int(int64_t(sound.end ? sound.end : frames) * area.getWidth() / frames);
        g.fillRect(area.getX() + std::min(start, area.getWidth() - 1), 0, 1, area.getHeight());
        g.fillRect(area.getX() + std::min(end, area.getWidth() - 1), 0, 1, area.getHeight());
        auto name = upper(sample->name.substr(0, sample->name.rfind('.'))).substr(0, 10);
        auto where = bar_beat(studio);
        ui::detail(g, name, where.empty() ? seconds(frames).substr(0, 5) : where);
    }

    Knob knob(unsigned which) const override {
        const Sample* sample = studio.sample_of_pad();
        uint32_t frames = sample ? std::max<uint32_t>(1, sample->data->model_frames()) : 1;
        const PadSound& sound = studio.current().pads[studio.pad()];
        char text[24];
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
        default: return {};
        }
    }
    std::string knob_owner() const override { return pad_label(studio.pad()); }

private:
    void turn(unsigned which, int delta, bool fine) {
        const Sample* sample = studio.sample_of_pad();
        if (!sample) return;
        uint32_t frames = sample->data->model_frames();
        uint32_t step = std::max<uint32_t>(1, frames / (fine ? 8192 : 256));
        auto move_by = [&](uint32_t value, uint32_t low, uint32_t high) {
            int64_t moved = int64_t(value) + int64_t(delta) * int64_t(step);
            return uint32_t(std::clamp<int64_t>(moved, low, std::max(low, high)));
        };
        PadSound sound = studio.current().pads[studio.pad()];
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
        studio.set_pad(studio.track(), studio.pad(), sound);
    }
};

RegisterScreen registered("kit-play", make<KitPlay>, {}, kind_bit(TrackKind::Kit));

} // namespace
} // namespace app::screens
