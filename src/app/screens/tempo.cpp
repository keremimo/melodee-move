// SPDX-License-Identifier: AGPL-3.0-only
// The Move's own transport settings, opened as its manual has them (10.1-10.3):
//
//   Tempo      Shift + Step 5: the BPM large; the jogwheel sets it, Shift +
//              the jogwheel in tenths; its push confirms. Knob 1 sets it too.
//   Metronome  Shift + Step 6 turns it on or off and shows it; the jogwheel
//              sets its level (Melodee's: the Move's manual gives none);
//              push confirms.
//   Groove     Shift + Step 7: the swing as the Move shows it, 0 % straight,
//              100 % triplet sixteenths (Melodee's swing 50 to 66.7 %),
//              above that more; the jogwheel sets it; push confirms.
//
// Back leaves each too. The steps count the bar's beats while the song plays.
#include "app/screens/common.h"
#include <cmath>

namespace app::screens {
namespace {

// The beats of the bar on the steps, the one now white.
void beat_steps(const Studio& studio, move::Lights& lights) {
    using namespace move;
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

// A setting: the jogwheel turns it, its push or Back confirms.
class Setting : public Screen {
public:
    using Screen::Screen;
    bool surface(const move::Input& input) override {
        using Kind = move::Input::Kind;
        using move::Button;
        if (input.kind == Kind::MainKnob) { turn(input.delta); return true; }
        if (input.kind == Kind::Knob) { if (input.index == 0) turn(input.delta); return true; }
        if (input.kind == Kind::Button && input.pressed && (Button(input.index) == Button::MainPush || Button(input.index) == Button::Back)) {
            studio.close();
            return true;
        }
        return false;
    }
    void lights(move::Lights& lights) override { beat_steps(studio, lights); }

protected:
    virtual void turn(int delta) = 0;
    void big(juce::Graphics& g, const std::string& value, const std::string& under) {
        move::draw_text(g, ui::content_x + 4, 4, value, 3);
        move::draw_text(g, ui::content_x + 4, 28, under);
    }
};

class Tempo final : public Setting {
public:
    using Setting::Setting;
    void draw(juce::Graphics& g) override {
        auto signature = studio.project().time_signature();
        ui::rail(g, "TMPO", "", std::to_string(signature.beats) + "/" + std::to_string(signature.unit));
        char bpm[16];
        std::snprintf(bpm, sizeof bpm, "%u.%u", studio.tempo() / 10, studio.tempo() % 10);
        big(g, bpm, "BPM");
        move::draw_text(g, ui::content_x + 4, 40, "SHIFT: FINER");
        ui::detail(g, "PUSH: OK", bar_beat(studio));
    }
    Knob knob(unsigned which) const override {
        if (which) return {};
        char text[16];
        std::snprintf(text, sizeof text, "%u.%u", studio.tempo() / 10, studio.tempo() % 10);
        return {"TEMPO", text, (studio.tempo() - 300) / 2700.0f};
    }
    std::string knob_owner() const override { return "TMPO"; }

private:
    void turn(int delta) override { studio.set_tempo(unsigned(std::clamp(int(studio.tempo()) + delta * (studio.shift ? 1 : 10), 300, 3000))); }
};

class Metronome final : public Setting {
public:
    using Setting::Setting;
    void draw(juce::Graphics& g) override {
        ui::rail(g, "MET", "", "");
        big(g, studio.click() ? "ON" : "OFF", "METRONOME");
        move::draw_text(g, ui::content_x + 4, 40, "LEVEL " + std::to_string(studio.click_level()));
        ui::detail(g, "PUSH: OK");
    }
    Knob knob(unsigned which) const override {
        if (which) return {};
        return {"CLICK LEVEL", std::to_string(studio.click_level()), studio.click_level() / 100.0f};
    }
    std::string knob_owner() const override { return "MET"; }

private:
    void turn(int delta) override { studio.set_click(studio.click(), unsigned(std::clamp(int(studio.click_level()) + delta, 0, 100))); }
};

// The Move's groove, 0 % straight to 100 % triplet sixteenths, as Melodee's
// swing (50 % straight, 66.7 % triplet): 6 % of groove a step of swing.
unsigned groove_of(unsigned swing) { return unsigned(std::lround((int(swing) - 50) * 100.0 / (200.0 / 12.0))); }

class Groove final : public Setting {
public:
    using Setting::Setting;
    void draw(juce::Graphics& g) override {
        ui::rail(g, "GRV", "", "");
        big(g, std::to_string(groove_of(studio.project().swing)) + "%", "GROOVE");
        move::draw_text(g, ui::content_x + 4, 40, "100%: TRIPLETS");
        ui::detail(g, "PUSH: OK");
    }
    Knob knob(unsigned which) const override {
        if (which) return {};
        return {"GROOVE", std::to_string(groove_of(studio.project().swing)) + "%", (studio.project().swing - 50) / 25.0f};
    }
    std::string knob_owner() const override { return "GRV"; }

private:
    void turn(int delta) override { studio.set_swing(unsigned(std::clamp(int(studio.project().swing) + delta, 50, 75))); }
};

RegisterScreen tempo("tempo", make<Tempo>);
RegisterScreen metronome("metronome", make<Metronome>);
RegisterScreen groove("groove", make<Groove>);

} // namespace
} // namespace app::screens
