// SPDX-License-Identifier: AGPL-3.0-only
// A kit track's Play screen (docs/UI.md, docs/MOVE_CONTROLS.md): the Move's
// Device View. Along the top the kit's items, KIT, the last pad, FX1, FX2;
// the wheel walks them, its push opens the one it is on (the pad: Load
// samples), Shift + push Sample Options (Chop). Under them the last pad's
// waveform with start and end; knobs 1-8 tune, level, pan, start, end,
// attack, decay, cutoff (Shift: finer); the pads play the kit and pick the
// pad edited (Shift + a pad picks it quietly); a pad held + the Volume knob
// its level.
#include "app/pad_layout.h"
#include "app/screens/common.h"
#include "app/screens/device_fx.h"

namespace app::screens {
namespace {

using melodee::PadSound;

class KitPlay final : public Screen {
public:
    using Screen::Screen;

    void input(const move::Input& input) override { fx.input(input); }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        // The track item's mix and the effects (device_fx.h): the knobs, the
        // push and Shift + push are theirs there.
        auto effects = fx.track_items();
        focus = std::min(focus, 1 + int(effects.size()));
        if (focus >= 2 && fx.job(effects[size_t(focus - 2)], job, use)) return true;
        if (focus == 0 && fx.mix_job(studio.track(), job, use)) return true;
        switch (job) {
        case Job::PlayPad: case Job::SelectPadQuietly: studio.select_pad(unsigned(use.index)); return true;
        case Job::MutePad: mixing::toggle_pad_mute(studio, studio.track(), logical_pad(uint8_t(use.index), pad_layout())); return true; // Mute held + pad (16.5)
        case Job::SoloPad: mixing::solo_pad(studio, studio.track(), logical_pad(uint8_t(use.index), pad_layout())); return true;        // Shift + Mute held + pad
        case Job::TurnKnob: turn(unsigned(use.index), use.delta, false); return true;
        case Job::TurnKnobFine: turn(unsigned(use.index), use.delta, true); return true;
        case Job::PadLevel: turn(1, use.delta, false); return true;
        case Job::Focus: focus = std::clamp(focus + (use.delta > 0 ? 1 : -1), 0, 1 + int(effects.size())); return true;
        case Job::ShowFocus: studio.say(item(focus)); return true;
        case Job::OpenBrowser:
            if (focus == 1) studio.open("load-samples");
            else studio.say("KITS: NOT YET");
            return true;
        case Job::SampleOptions:
            if (studio.sample_of_pad()) studio.open("sample-options");
            else studio.say("EMPTY PAD");
            return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        const auto& track = studio.current();
        auto colour = ui::track_colour(studio.track()).bright;
        static const uint8_t dim_white = move::nearest_colour({96, 96, 96});
        for (unsigned at = 0; at < pad_count; ++at)
            lights.pad(physical_pad(uint8_t(at), pad_layout()), track.pad_muted(at) ? dim_white : track.pads[at].used() ? (at == studio.pad() ? colour::white : colour)
                         : at == studio.pad() ? colour::dark_grey : colour::off); // a muted pad dim white, as on the Move
        steps_playhead(studio, lights);
        // Left / Right (bars), Plus / Minus (pad banks), Loop (Loop Mode) and
        // Note/Session (Patterns) wait for wave 2.
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu, Button::Back})
            lights.button(button, 0);
    }

    void draw(juce::Graphics& g) override {
        auto effects = fx.track_items();
        if (studio.desk.show_effects) { focus = 2; studio.desk.show_effects = false; } // from the Mixer's push
        focus = std::min(focus, 1 + int(effects.size()));
        ui::rail(g, "KIT", track_number(studio), focus == 0 ? fx.mix_bottom() : focus >= 2 ? fx.rail_bottom(effects[size_t(focus - 2)]) : pad_label(studio.pad()));
        fx.draw_row(g, item(0), item(1), effects, focus);
        if (focus == 0) { fx.draw_mix(g, studio.track()); return; }
        if (focus >= 2) { fx.draw(g, effects[size_t(focus - 2)]); return; }
        const Sample* sample = studio.sample_of_pad();
        if (!sample) {
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "EMPTY", 2);
            ui::detail(g, focus == 1 ? "PUSH: LOAD" : "", bar_beat(studio));
            return;
        }
        auto area = ui::content(ui::header_height + 1, ui::subject_height - ui::header_height - 1);
        ui::waveform(g, area, peaks_of(sample->overview));
        uint32_t frames = std::max<uint32_t>(1, sample->data->model_frames());
        const auto& sound = studio.current().pads[studio.pad()];
        int start = int(int64_t(sound.start) * area.getWidth() / frames), end = int(int64_t(sound.end ? sound.end : frames) * area.getWidth() / frames);
        g.fillRect(area.getX() + std::min(start, area.getWidth() - 1), area.getY(), 1, area.getHeight());
        g.fillRect(area.getX() + std::min(end, area.getWidth() - 1), area.getY(), 1, area.getHeight());
        auto name = upper(sample->name.substr(0, sample->name.rfind('.'))).substr(0, 10);
        auto where = bar_beat(studio);
        ui::detail(g, name, where.empty() ? seconds(frames).substr(0, 5) : where);
    }

    Knob knob(unsigned which) const override {
        if (focus == 0) return fx.mix_knob(studio.track(), which);
        if (focus >= 2) {
            auto effects = fx.track_items();
            return size_t(focus - 2) < effects.size() ? fx.knob(effects[size_t(focus - 2)], which) : Knob {};
        }
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
    int focus = 1; // the Device View's item the wheel is on: 0 the kit (its mix), 1 the pad, 2 on its effects
    DeviceFx fx {studio};
    std::string item(int which) const {
        switch (which) {
        case 0: return "KIT";
        case 1: return pad_label(studio.pad());
        default: return "FX" + std::to_string(which - 1);
        }
    }
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
