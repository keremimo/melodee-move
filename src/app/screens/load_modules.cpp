// SPDX-License-Identifier: AGPL-3.0-only
// Load modules (Menu on a synth track): Schwung's sound generators, the ones
// the Move's Schwung install has (/data/UserData/schwung/modules/
// sound_generators, or --modules DIR). The jogwheel picks, its push loads
// one onto the track; Menu or Back leaves.
#include "app/screens/common.h"

namespace app::screens {
namespace {

class LoadModules final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        if (auto* track = studio.module_track()) track->menu_open = false;
        studio.rescan_modules();
        cursor = std::clamp(cursor, 0, std::max(0, int(studio.catalog.size()) - 1));
    }

    bool surface(const move::Input& input) override {
        using Kind = move::Input::Kind;
        using move::Button;
        if (input.kind == Kind::MainKnob) {
            cursor = std::clamp(cursor + input.delta, 0, std::max(0, int(studio.catalog.size()) - 1));
            return true;
        }
        if (input.kind == Kind::Knob) return true;
        if (input.kind != Kind::Button || !input.pressed) return false;
        switch (Button(input.index)) {
        case Button::Menu:
        case Button::Back: studio.close(); return true;
        case Button::MainPush:
            if (cursor < int(studio.catalog.size())) {
                const auto& choice = studio.catalog[size_t(cursor)];
                auto* track = studio.module_track();
                if (choice.api != 2) studio.say("OLDER API: NOT YET");
                else if (track) { track->job = choice.id; track->job_restores = false; studio.say("LOADING"); }
            }
            return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, colour::off);
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop}) lights.button(button, 0);
        lights.button(Button::Menu, white_bright);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "LOAD", track_number(studio), "SYN");
        if (studio.catalog.empty()) {
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "NO MODULES");
            ui::detail(g, "INSTALL VIA SCHWUNG");
            return;
        }
        auto* track = studio.module_track();
        std::vector<std::string> rows;
        for (const auto& choice : studio.catalog) {
            std::string row = upper(choice.name);
            if (choice.api != 2) row += " (OLD)";
            else if (track && track->module && track->module->info().id == choice.id) row += " *";
            rows.push_back(row);
        }
        ui::list(g, ui::content(0, ui::height), rows, cursor, 1);
    }

private:
    int cursor = 0;
};

RegisterScreen registered("load-modules", make<LoadModules>, {{move::Button::Menu, false, kind_bit(TrackKind::Synth)}});

} // namespace
} // namespace app::screens
