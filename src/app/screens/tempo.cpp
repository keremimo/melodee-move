// SPDX-License-Identifier: AGPL-3.0-only
// Tempo (Shift + Play, docs/UI.md "Tracks"): the BPM large; the jogwheel
// or knob 1 the tempo (Shift: tenths), knob 2 the swing, knob 3 the
// metronome's level; the jogwheel's push the metronome on or off. The steps
// count the beats while the song plays. Back or Shift + Play leaves.
#include "app/screens/common.h"

namespace app::screens {
namespace {

class Tempo final : public Screen {
public:
    using Screen::Screen;

    bool surface(const move::Input& input) override {
        using Kind = move::Input::Kind;
        using move::Button;
        switch (input.kind) {
        case Kind::MainKnob: tempo_by(input.delta); return true;
        case Kind::Knob:
            if (input.index == 0) tempo_by(input.delta);
            else if (input.index == 1) studio.set_swing(unsigned(std::clamp(int(studio.project().swing) + input.delta, 50, 75)));
            else if (input.index == 2) studio.set_click(studio.click(), unsigned(std::clamp(int(studio.click_level()) + input.delta, 0, 100)));
            return true;
        case Kind::Button:
            if (!input.pressed) return false;
            switch (Button(input.index)) {
            case Button::MainPush: studio.set_click(!studio.click(), studio.click_level()); return true;
            case Button::Back: studio.close(); return true;
            case Button::Play: studio.close(); return true; // Shift + Play again
            default: return false;
            }
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        // The beats of the bar on the steps, the one now white.
        unsigned beat_ticks = std::max(1u, studio.project().beat_ticks()), beats = std::min(16u, studio.project().bar_ticks() / beat_ticks);
        int now = studio.playing() ? int(studio.host().ticks() % studio.project().bar_ticks() / beat_ticks) : -1;
        unsigned spacing = std::max(1u, 16u / std::max(beats, 1u));
        for (unsigned step = 0; step < step_count; ++step) {
            bool beat = step % spacing == 0 && step / spacing < beats;
            lights.step(step, !beat ? colour::off : int(step / spacing) == now ? colour::white : colour::light_grey);
        }
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        auto signature = studio.project().time_signature();
        ui::rail(g, "TMPO", "", std::to_string(signature.beats) + "/" + std::to_string(signature.unit));
        char bpm[16];
        std::snprintf(bpm, sizeof bpm, "%u.%u", studio.tempo() / 10, studio.tempo() % 10);
        move::draw_text(g, ui::content_x + 4, 4, bpm, 3);
        move::draw_text(g, ui::content_x + 4, 28, "BPM");
        move::draw_text(g, ui::content_x + 4, 40, "SWING " + std::to_string(studio.project().swing) + "%");
        int click_x = ui::width - move::text_width("CLICK");
        move::draw_text(g, click_x, 40, "CLICK");
        if (studio.click()) g.fillRect(click_x, 48, move::text_width("CLICK") - 1, 1); // on: underlined
        ui::detail(g, studio.click() ? "PUSH: CLICK OFF" : "PUSH: CLICK ON", bar_beat(studio));
    }

    Knob knob(unsigned which) const override {
        char text[16];
        switch (which) {
        case 0: std::snprintf(text, sizeof text, "%u.%u", studio.tempo() / 10, studio.tempo() % 10); return {"TEMPO", text, (studio.tempo() - 300) / 2700.0f};
        case 1: return {"SWING", std::to_string(studio.project().swing) + "%", (studio.project().swing - 50) / 25.0f};
        case 2: return {"CLICK LEVEL", std::to_string(studio.click_level()), studio.click_level() / 100.0f};
        default: return {};
        }
    }
    std::string knob_owner() const override { return "TMPO"; }

private:
    void tempo_by(int delta) { studio.set_tempo(unsigned(std::clamp(int(studio.tempo()) + delta * (studio.shift ? 1 : 10), 300, 3000))); }
};

RegisterScreen registered("tempo", make<Tempo>, {{move::Button::Play, true, 0}});

} // namespace
} // namespace app::screens
