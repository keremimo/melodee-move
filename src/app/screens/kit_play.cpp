// SPDX-License-Identifier: AGPL-3.0-only
// A kit track's Play screen (docs/UI.md, docs/MOVE_CONTROLS.md): the Move's
// Device View. Along the top the kit's items, KIT, the last pad, FX1, FX2;
// the wheel walks them, its push opens the one it is on (KIT and the pad:
// Load, kits and samples), Shift + push Sample Options (Chop), Shift + turn
// the pad pages (kit_pages.cpp). Under them the last pad's waveform with start and end;
// knobs 1-8 tune, level, pan, start, end, attack, decay, cutoff (Shift:
// finer; Shift + a touch, its fine view). The pads play the kit and pick the
// pad edited (Shift + a pad picks it quietly), two of its eight banks, Plus
// and Minus moving them 16 pads; Shift + Step 8 turns the right 16 into the
// last pad's 16 Pitches, Plus and Minus then their octave. Copy held, a pad
// then another copies the first onto the second (asking when it holds a
// sample); Delete held, a pad loses its notes, its sound, or its gap; Mute
// held, a pad mutes. A pad held + the Volume knob its level. Past the pad,
// the wheel walks the kit's effects (device_fx.h).
#include "app/pad_layout.h"
#include "app/screens/common.h"
#include "app/screens/device_fx.h"
#include "app/screens/kit_common.h"
#include "melodee/shell/pad_gap.h"

namespace app::screens {
namespace {

using melodee::PadSound;

class KitPlay final : public Screen {
public:
    using Screen::Screen;

    void enter() override { fine = -1; }
    void leave() override { fine = -1; }

    void input(const move::Input& input) override {
        using Kind = move::Input::Kind;
        fx.input(input);
        if (input.kind == Kind::Button && move::Button(input.index) == move::Button::Copy) {
            copy_down = input.pressed;
            if (!input.pressed) copy_from = -1;
        }
        if (input.kind == Kind::Touch && !input.pressed && int(input.index) == fine) fine = -1;
    }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        // The track item's mix and the effects (device_fx.h): the knobs, the
        // push and Shift + push are theirs there.
        auto effects = fx.track_items();
        focus = std::min(focus, 1 + int(effects.size()));
        if (focus >= 2 && fx.job(effects[size_t(focus - 2)], job, use)) { focus += fx.take_moved(); return true; }
        if (focus == 0 && fx.mix_job(studio.track(), job, use)) return true;
        switch (job) {
        case Job::PlayPad: case Job::SelectPadQuietly:
            if (int kit = studio.kit_pad_at(unsigned(use.index)); kit >= 0) studio.select_pad(unsigned(kit));
            return true;
        case Job::PlayPitch: return true; // the audio thread plays it
        case Job::MutePad: // Mute held + pad (16.5)
            if (int kit = studio.kit_pad_at(unsigned(use.index)); kit >= 0) mixing::toggle_pad_mute(studio, studio.track(), unsigned(kit));
            return true;
        case Job::SoloPad: // Shift + Mute held + pad
            if (int kit = studio.kit_pad_at(unsigned(use.index)); kit >= 0) mixing::solo_pad(studio, studio.track(), unsigned(kit));
            return true;
        case Job::TurnKnob: turn(unsigned(use.index), use.delta, false); return true;
        case Job::TurnKnobFine: turn(unsigned(use.index), use.delta, true); return true;
        case Job::SecondParameter: fine = use.index; return true;
        case Job::PadLevel: turn(1, use.delta, false); return true;
        case Job::Focus: focus = std::clamp(focus + (use.delta > 0 ? 1 : -1), 0, 1 + int(effects.size())); return true;
        case Job::ShowFocus: // the item in focus, named in full
            studio.say(focus == 0 ? std::string("TRACK MIX") : focus == 1 ? "PAD " + pad_label(studio.pad()) : fx.focus_name(effects[size_t(focus - 2)]));
            return true;
        case Job::OpenBrowser: // KIT or the pad: Load, its kits (MPC programs), banks and samples, until the track types' sheet
            studio.open("load-samples");
            return true;
        case Job::SampleOptions:
            if (studio.sample_of_pad()) studio.open("sample-options");
            else studio.say("EMPTY PAD");
            return true;
        case Job::Page:
            studio.open("kit-pages");
            if (auto* pages = studio.top(); pages && pages->id() == "kit-pages") pages->job(job, use);
            return true;
        case Job::PadBank:
            studio.set_pad_bank(int(studio.pad_bank()) + (use.control == bindings::Control::Plus ? 1 : -1));
            studio.say("PADS " + banks_in_view(studio));
            return true;
        case Job::ScaleStep: // Shift + Plus / Minus on a kit: four pads (the Move's Control Live)
            studio.set_pad_first(int(studio.pad_first()) + (use.control == bindings::Control::Plus ? 4 : -4));
            studio.say("PADS " + banks_in_view(studio));
            return true;
        case Job::PitchesOctave: {
            int octave = studio.current().octave + (use.control == bindings::Control::Plus ? 1 : -1);
            studio.set_pitches_octave(octave);
            studio.say("OCTAVE " + signed_octave());
            return true;
        }
        case Job::SixteenPitches:
            studio.set_pitches(!studio.pitches());
            studio.say(studio.pitches() ? "16 PITCHES ON" : "16 PITCHES OFF");
            return true;
        case Job::CopyPad: copy(use); return true;
        case Job::ClearPad: clear(use); return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        kit_pads(studio, lights, copy_from);
        steps_playhead(studio, lights);
        // Plus and Minus where they move the banks (or 16 Pitches' octave).
        bool pitches = studio.pitches();
        int octave = studio.current().octave, first = int(studio.pad_first());
        int top = int(melodee::sampler_pads - studio.pads_in_view());
        lights.button(Button::Up, (pitches ? octave < melodee::kit_highest_octave : first < top) ? white_dim : 0);
        lights.button(Button::Down, (pitches ? octave > melodee::kit_lowest_octave : first > 0) ? white_dim : 0);
        lights.button(Button::Copy, copy_down ? white_bright : white_dim);
        lights.button(Button::Delete, white_dim);
        // Left / Right (bars), Loop (Loop Mode) and Note/Session (Patterns) wait for wave 2.
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu, Button::Back}) lights.button(button, 0);
    }

    void draw(juce::Graphics& g) override {
        if (fine >= 0 && draw_fine(g)) return;
        bool pitches = studio.pitches();
        if (copy_down) { draw_copy(g); return; }
        auto effects = fx.track_items();
        if (studio.desk.show_effects) { focus = 2; studio.desk.show_effects = false; } // from the Mixer's push
        focus = std::min(focus, 1 + int(effects.size()));
        ui::rail(g, pitches ? "PTCH" : "KIT", track_number(studio),
                 focus == 0 ? fx.mix_bottom() : focus >= 2 ? fx.rail_bottom(effects[size_t(focus - 2)]) : pad_label(studio.pad()));
        fx.draw_row(g, item(0), item(1), effects, focus);
        if (focus == 0) { fx.draw_mix(g, studio.track()); return; }
        if (focus >= 2) { fx.draw(g, effects[size_t(focus - 2)]); return; }
        const Sample* sample = studio.sample_of_pad();
        std::string where = bar_beat(studio);
        if (!sample) {
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "EMPTY", 2);
            ui::detail(g, pitches ? key_name() : focus == 1 ? "PUSH: LOAD" : "", pitches ? "OCT " + signed_octave() : where);
            return;
        }
        auto area = ui::content(ui::header_height + 1, ui::subject_height - ui::header_height - 1);
        ui::waveform(g, area, peaks_of(sample->overview));
        uint32_t frames = std::max<uint32_t>(1, sample->data->model_frames());
        const auto& sound = studio.current().pads[studio.pad()];
        int start = int(int64_t(sound.start) * area.getWidth() / frames), end = int(int64_t(sound.end ? sound.end : frames) * area.getWidth() / frames);
        g.fillRect(area.getX() + std::min(start, area.getWidth() - 1), area.getY(), 1, area.getHeight());
        g.fillRect(area.getX() + std::min(end, area.getWidth() - 1), area.getY(), 1, area.getHeight());
        if (pitches) { ui::detail(g, key_name(), "OCT " + signed_octave()); return; }
        auto name = upper(sample->name.substr(0, sample->name.rfind('.'))).substr(0, 10);
        ui::detail(g, name, where.empty() ? length_of(frames) : where);
    }

    bool knob_lane(unsigned which, unsigned& param, unsigned& pad) const override {
        if (focus == 0) return fx.mix_lane(studio.track(), which, param, pad); // not the pad's LEVEL or PAN by name
        if (focus >= 2) {
            auto effects = fx.track_items();
            pad = 0;
            return size_t(focus - 2) < effects.size() && fx.lane(effects[size_t(focus - 2)], which, param);
        }
        return false; // the sound's knobs: the kit's lanes by their names (sequencer.cpp)
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
    // A touched START or END: the place close up under its value.
    std::string draw_knob_place(juce::Graphics& g, unsigned which, juce::Rectangle<int> area, bool fine_view) const override {
        const Sample* sample = studio.sample_of_pad();
        if (focus != 1 || (which != 3 && which != 4) || !sample) return {};
        const PadSound& sound = studio.current().pads[studio.pad()];
        uint32_t end = sound.end ? sound.end : sample->data->model_frames();
        return draw_place(g, area, *sample, which == 3 ? sound.start : end, sound.start, end, place_zoom(fine_view));
    }
    std::string status() const override {
        return " bank=" + std::to_string(studio.pad_bank()) + " first=" + pad_label(studio.pad_first()) + " pitches=" + std::to_string(int(studio.pitches())) +
               " copy_from=" + std::to_string(copy_from);
    }
    // Delete + a knob's touch with no motion: the value as a new pad has it (Melodee's Erase + pot).
    bool reset_knob(unsigned which) override {
        if (focus != 1 || !studio.sample_of_pad()) return false; // the pad's own knobs, not the mix's or an effect's
        PadSound sound = studio.current().pads[studio.pad()];
        const PadSound fresh {};
        switch (which) {
        case 0: sound.pitch = fresh.pitch; sound.fine = fresh.fine; break;
        case 1: sound.level = fresh.level; break;
        case 2: sound.pan = fresh.pan; break;
        case 3: sound.start = fresh.start; break;
        case 4: sound.end = fresh.end; break;
        case 5: sound.attack = fresh.attack; break;
        case 6: sound.decay = fresh.decay; break;
        case 7: sound.cutoff = fresh.cutoff; sound.filter = fresh.filter; break;
        default: return false;
        }
        studio.set_pad(studio.track(), studio.pad(), sound);
        return true;
    }

private:
    int focus = 1; // the Device View's item the wheel is on: 0 the kit (its mix), 1 the pad, 2 on its effects
    int fine = -1; // a knob Shift + touched: its fine view
    bool copy_down = false;
    int copy_from = -1; // Copy held: the kit pad picked to copy
    DeviceFx fx {studio};
    std::string item(int which) const {
        switch (which) {
        case 0: return "KIT";
        case 1: return pad_label(studio.pad());
        default: return "FX" + std::to_string(which - 1);
        }
    }
    std::string signed_octave() const {
        int octave = studio.current().octave - melodee::kit_level_octave;
        return (octave > 0 ? "+" : "") + std::to_string(octave);
    }
    std::string key_name() const {
        const auto& project = studio.project();
        return upper(std::string(melodee::note_letter(project.root)) + " " + melodee::scales()[project.scale % melodee::scales().size()].name).substr(0, 10);
    }
    static std::string length_of(uint32_t frames) { // seconds, or minutes for a long sample
        if (frames < 60u * 44100u) return seconds(frames).substr(0, 5);
        unsigned total = frames / 44100u;
        char text[16];
        std::snprintf(text, sizeof text, "%u:%02u", total / 60, total % 60);
        return text;
    }

    void copy(const bindings::Use& use) {
        int kit = studio.kit_pad_at(unsigned(use.index));
        if (kit < 0) return;
        if (copy_from < 0) {
            if (studio.current().pads[unsigned(kit)].used()) copy_from = kit;
            else studio.say("EMPTY PAD");
            return;
        }
        if (kit == copy_from) return;
        pending_from = unsigned(copy_from);
        pending_to = unsigned(kit);
        if (studio.current().pads[unsigned(kit)].used()) studio.push("copy-pad"); // asks first: it holds a sample
        else copy_now();
    }
    void clear(const bindings::Use& use) {
        int kit = studio.kit_pad_at(unsigned(use.index));
        if (kit < 0) return;
        std::string label = pad_label(unsigned(kit));
        switch (studio.clear_pad(studio.track(), unsigned(kit))) {
        case Studio::Cleared::Notes: studio.say(label + " NOTES GONE"); break;
        case Studio::Cleared::Emptied: studio.say(label + " EMPTIED"); break;
        case Studio::Cleared::GapClosed:
            studio.say(pad_label(unsigned(kit) + 1) + "-" + pad_label(melodee::gap_end(unsigned(kit)) - 1) + " DOWN ONE");
            break;
        case Studio::Cleared::Nothing: studio.say(label + " EMPTY"); break;
        }
    }

public:
    unsigned pending_from = 0, pending_to = 0;
    void copy_now() {
        if (studio.copy_pad(studio.track(), pending_from, pending_to)) {
            studio.say(pad_label(pending_from) + " TO " + pad_label(pending_to));
            studio.select_pad(pending_to);
        }
    }

private:
    void draw_copy(juce::Graphics& g) {
        ui::rail(g, "COPY", copy_from < 0 ? "" : pad_label(unsigned(copy_from)), "PAD");
        if (copy_from < 0) {
            move::draw_text_centred(g, ui::content_x, 14, ui::content_width, "PRESS THE PAD");
            move::draw_text_centred(g, ui::content_x, 26, ui::content_width, "TO COPY");
            return;
        }
        move::draw_text_centred(g, ui::content_x, 6, ui::content_width, pad_label(unsigned(copy_from)), 2);
        move::draw_text_centred(g, ui::content_x, 26, ui::content_width, "ONTO A PAD");
        if (const Sample* sample = studio.sample_of(studio.track(), unsigned(copy_from)))
            ui::detail(g, upper(sample->name.substr(0, sample->name.rfind('.'))).substr(0, 16));
    }

    // Shift + a knob touched: START and END close up at their place (the
    // Move's Sample Start zoom), TUNE in cents; the rest as touched.
    bool draw_fine(juce::Graphics& g) {
        const Sample* sample = studio.sample_of_pad();
        const PadSound& sound = studio.current().pads[studio.pad()];
        if (fine == 0) {
            char text[16];
            std::snprintf(text, sizeof text, "%+d CT", sound.fine);
            ui::rail(g, "KNOB", "1", "FINE");
            ui::big_parameter(g, ui::content(4, 44), "FINE TUNE", text, (sound.fine + 50) / 100.0f);
            return true;
        }
        if ((fine != 3 && fine != 4) || !sample) return false;
        uint32_t end = sound.end ? sound.end : sample->data->model_frames(), at = fine == 3 ? sound.start : end;
        auto zoom = draw_place(g, ui::place_area(), *sample, at, sound.start, end, place_zoom(true));
        if (zoom.empty()) return false;
        ui::rail(g, "KNOB", std::to_string(fine + 1), fine == 3 ? "STRT" : "END");
        ui::place_value(g, seconds(at));
        ui::detail(g, fine == 3 ? "START" : "END", zoom);
        return true;
    }
    void turn(unsigned which, int delta, bool fine_turn) {
        const Sample* sample = studio.sample_of_pad();
        if (!sample) return;
        uint32_t frames = sample->data->model_frames();
        uint32_t step = std::max<uint32_t>(1, frames / (fine_turn ? 8192 : 256));
        auto move_by = [&](uint32_t value, uint32_t low, uint32_t high) {
            int64_t moved = int64_t(value) + int64_t(delta) * int64_t(step);
            return uint32_t(std::clamp<int64_t>(moved, low, std::max(low, high)));
        };
        PadSound sound = studio.current().pads[studio.pad()];
        switch (which) {
        case 0:
            if (fine_turn) sound.fine = int8_t(std::clamp(sound.fine + delta * 5, -50, 50));
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
            sound.filter = sound.cutoff < 100 ? (sound.filter == melodee::PadFilter::Off ? melodee::PadFilter::Lpf4 : sound.filter) : melodee::PadFilter::Off;
            break;
        default: return;
        }
        studio.set_pad(studio.track(), studio.pad(), sound);
    }
};

RegisterScreen registered("kit-play", make<KitPlay>, {}, kind_bit(TrackKind::Kit));

// The question before copying onto a pad that holds a sample: push yes, Back no.
class CopyPad final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(bindings::Job job, const bindings::Use&) override {
        using bindings::Job;
        switch (job) {
        case Job::ListPush: studio.close(); play().copy_now(); return true;
        case Job::Back: studio.close(); return true;
        case Job::ListTurn: case Job::ListTurnFine: case Job::TurnKnob: case Job::TurnKnobFine: case Job::CopyPad: return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        using namespace move;
        kit_pads(studio, lights, int(play().pending_from));
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }
    void draw(juce::Graphics& g) override {
        auto& kit = play();
        std::string to = pad_label(kit.pending_to), what;
        if (const Sample* sample = studio.sample_of(studio.track(), kit.pending_to)) what = upper(sample->name.substr(0, sample->name.rfind('.')));
        ui::rail(g, "COPY", pad_label(kit.pending_from), to);
        move::draw_text_centred(g, ui::content_x, 6, ui::content_width, "REPLACE", 2);
        move::draw_text_centred(g, ui::content_x, 26, ui::content_width, (to + " " + what).substr(0, 15) + "?");
        ui::detail(g, "PUSH YES", "BACK NO");
    }

private:
    KitPlay& play() { return static_cast<KitPlay&>(*studio.screen("kit-play")); }
};

RegisterScreen copy_pad("copy-pad", make<CopyPad>);

} // namespace
} // namespace app::screens
