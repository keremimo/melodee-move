// SPDX-License-Identifier: AGPL-3.0-only
// A module's user presets (My Presets › Preset on its pages; user_presets.h),
// in Melodee's look with Schwung's behaviour (its shadow_ui_presets.mjs): the
// one thing here is choosing one. The wheel walks them, the push loads it and
// goes back to the pages, Back goes back without. Nothing is heard until the
// push (Schwung's Audition is off by default). Save, Save As and Delete are
// on the My Presets page itself.
#include "app/screens/common.h"

namespace app::screens {
namespace {

class ModulePresets final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        presets.clear();
        cursor = 0;
        ModuleTrack* track = studio.module_track(studio.pages_target.track);
        if (!track || !track->module) return;
        presets = user_presets(user_presets_folder(), track->module->info().id);
        for (size_t n = 0; n < presets.size(); ++n)
            if (presets[n].name == track->user_preset) cursor = int(n);
    }

    bindings::Screen screen_kind() const override { return bindings::Screen::List; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (!presets.empty()) cursor = std::clamp(cursor + (use.delta > 0 ? 1 : -1), 0, int(presets.size()) - 1);
            return true;
        case Job::ListPush:
            if (!presets.empty()) {
                studio.load_user_preset(studio.pages_target, presets[size_t(cursor)]);
                studio.close();
            }
            return true;
        case Job::Back: studio.close(); return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, move::colour::off);
        lights.button(move::Button::Back, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        ModuleTrack* track = studio.module_track(studio.pages_target.track);
        std::string abbrev = track && track->module ? upper(track->module->info().abbrev).substr(0, 4) : "SYN";
        ui::rail(g, abbrev, std::to_string(studio.pages_target.track + 1), "PRST");
        g.setColour(juce::Colours::white);
        g.fillRect(ui::content_x, 0, ui::content_width, ui::header_height);
        g.setColour(juce::Colours::black);
        move::draw_text(g, ui::content_x + 2, 1, "PRESETS");
        g.setColour(juce::Colours::white);
        if (presets.empty()) {
            move::draw_text_centred(g, ui::content_x, 24, ui::content_width, "NO PRESETS SAVED");
            ui::hints(g, {{"BACK", "EXIT"}});
            return;
        }
        std::vector<std::string> rows;
        for (const auto& preset : presets)
            rows.push_back((track && preset.name == track->user_preset ? "* " : "  ") + upper(preset.name).substr(0, 15));
        ui::list(g, juce::Rectangle<int>(ui::content_x, ui::header_height + 2, ui::content_width, ui::subject_height - ui::header_height - 2), rows, cursor, 1);
        ui::hints(g, {{"JOG", "SEL"}, {"CLK", "LOAD"}, {"BACK", "EXIT"}});
    }

    std::string status() const override { return " presets=" + std::to_string(presets.size()) + " preset_cursor=" + std::to_string(cursor); }

private:
    std::vector<UserPreset> presets;
    int cursor = 0;
};

RegisterScreen registered("module-presets", make<ModulePresets>);

} // namespace
} // namespace app::screens
