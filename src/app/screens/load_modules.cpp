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
        studio.rescan_modules();
        cursor = std::clamp(cursor, 0, std::max(0, int(studio.catalog.size()) - 1));
    }

    bindings::Screen screen_kind() const override { return bindings::Screen::Browser; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::BrowseTurn: cursor = std::clamp(cursor + use.delta, 0, std::max(0, int(studio.catalog.size()) - 1)); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: return true;
        case Job::Back: studio.close(); return true;
        case Job::BrowsePush:
            if (cursor < int(studio.catalog.size())) {
                const auto& choice = studio.catalog[size_t(cursor)];
                auto* track = studio.module_track();
                if (choice.api != 2) studio.say("OLDER API: NOT YET");
                else if (track && track->module && track->module->info().id != choice.id) {
                    // Replacing a module asks first (Melodee's); one undo brings the old one back, as it was.
                    Studio::Question q;
                    q.rail_top = "SYN";
                    q.rail_big = std::to_string(studio.track() + 1);
                    q.rail_bottom = "TRK";
                    q.asked = "REPLACE " + upper(track->module->info().abbrev.empty() ? track->module->info().name : track->module->info().abbrev) + "?";
                    q.line1 = "WITH " + upper(choice.name);
                    q.line2 = "ITS SOUND GOES";
                    q.undoable = true;
                    Studio* where = &studio;
                    std::string id = choice.id;
                    q.yes = [where, id] {
                        if (auto* module = where->module_track()) { module->job = id; module->job_restores = false; where->say("LOADING"); }
                    };
                    studio.ask(q);
                } else if (track) { track->job = choice.id; track->job_restores = false; studio.say("LOADING"); }
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
        lights.button(Button::Menu, 0);
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

RegisterScreen registered("load-modules", make<LoadModules>); // the wheel's push on a synth's module (the Device View)

} // namespace
} // namespace app::screens
