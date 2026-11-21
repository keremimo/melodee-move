// SPDX-License-Identifier: AGPL-3.0-only
// The AUTO SAMPLE sheet (docs/UI.md, "Instruments"; MELODEE.md, Print and
// freeze, Auto sample): AUTO SAMPLE at the end of a module track's menu
// (Shift + push; Kerem, 2026-11-20). It opens on the check (CHECKING, then
// what it found), then the sheet: knobs 1-8 LOW, HIGH, STEP, LAYERS, HOLD,
// TAIL, TAP, LOOP; Shift + the wheel its second page, LEVEL. Push samples;
// the steps fill as the keys are done; Back stops and deletes what was made,
// or leaves. Done, the program is on a new keygroup track: push goes to it.
// The pads play the module meanwhile, to try the sound.
#include "app/auto_sampler.h"
#include "app/screens/note_pages.h"
#include "melodee/model/scales.h"

namespace app::screens {
namespace {

class AutoSample final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        held.fill(false);
        page = 0;
        settings.found = {};
        track = studio.track();
        studio.auto_sampled = -1;
        auto& sampler = studio.auto_sampler();
        if (sampler.running()) return;
        sampler.reset();
        if (auto failed = studio.auto_sample(track, settings, false); !failed.empty()) studio.say(failed);
    }
    void leave() override {
        auto& sampler = studio.auto_sampler();
        if (sampler.phase_now() == AutoSampler::Phase::Opening || sampler.phase_now() == AutoSampler::Phase::Checking) sampler.cancel();
    }

    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void input(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Pad && input.index < pad_count) held[input.index] = input.pressed;
    }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        auto& sampler = studio.auto_sampler();
        switch (job) {
        case Job::TurnKnob: case Job::TurnKnobFine: if (!sampler.running()) turn(unsigned(use.index), use.delta, job == Job::TurnKnobFine); return true;
        case Job::Page: page = use.delta > 0 ? 1 : 0; return true;
        case Job::ListTurn: case Job::ListTurnFine: page = use.delta > 0 ? 1 : 0; return true;
        case Job::ListPush: {
            if (studio.auto_sampled >= 0) { studio.select(unsigned(studio.auto_sampled)); return true; } // to the new track
            if (sampler.running()) return true;
            settings.found = sampler.findings();
            if (auto failed = studio.auto_sample(track, settings, true); !failed.empty()) studio.say(failed);
            return true;
        }
        case Job::Back:
            if (sampler.running()) { sampler.cancel(); studio.say("STOPPED"); return true; }
            studio.close();
            return true;
        case Job::PlayNote: return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        key_pads(studio, lights, held);
        auto& sampler = studio.auto_sampler();
        bool sampling = sampler.phase_now() == AutoSampler::Phase::Sampling || sampler.phase_now() == AutoSampler::Phase::Writing;
        unsigned lit = sampling && sampler.total() ? sampler.done() * step_count / sampler.total() : 0;
        for (unsigned n = 0; n < step_count; ++n) // the steps are its progress
            lights.step(n, !sampling ? colour::off : n < lit ? colour::white : n == lit ? colour::red : colour::dark_grey);
        lights.button(Button::Back, white_dim);
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu}) lights.button(button, 0);
    }

    void draw(juce::Graphics& g) override {
        auto& sampler = studio.auto_sampler();
        const auto phase = sampler.phase_now();
        const std::string number = std::to_string(track + 1);
        if (studio.auto_sampled >= 0) { // done: on its track
            ui::rail(g, "AUTO", number, "DONE");
            const auto* sample = studio.keys_sample(unsigned(studio.auto_sampled));
            move::draw_text(g, ui::content_x + 2, 4, sample ? upper(sample->name).substr(0, 16) : "PROGRAM", 1);
            move::draw_text(g, ui::content_x + 2, 16, sample && sample->data->multi ? std::to_string(sample->data->multi->parts ? sample->data->multi->part_count : 0) + " SAMPLES" : "", 1);
            move::draw_text(g, ui::content_x + 2, 26, "ON TRACK " + std::to_string(studio.auto_sampled + 1), 1);
            move::draw_text(g, ui::content_x + 2, 36, "KEYGROUPS/", 1);
            ui::detail(g, "PUSH: GO TO IT");
            return;
        }
        if (phase == AutoSampler::Phase::Opening || phase == AutoSampler::Phase::Checking) {
            ui::rail(g, "AUTO", number, "CHK");
            std::vector<std::string> rows {phase == AutoSampler::Phase::Opening ? "OPENING..." : "CHECKING..."};
            for (const auto& row : sampler.heard()) rows.push_back(row);
            ui::list(g, ui::content(0, 50), rows, 0, 1);
            ui::detail(g, module_name(), "BACK: NO");
            return;
        }
        if (phase == AutoSampler::Phase::Sampling || phase == AutoSampler::Phase::Writing) {
            ui::rail(g, "AUTO", number, std::to_string(sampler.done()) + "/" + std::to_string(sampler.total()));
            move::draw_text(g, ui::content_x + 1, 1, phase == AutoSampler::Phase::Writing ? "WRITING" : "SAMPLING");
            auto left = seconds_left(sampler);
            move::draw_text(g, ui::width - move::text_width(left), 1, left);
            draw_keys(g, sampler.done() / std::max(1u, settings.layer_count() * settings.take_count()));
            g.drawRect(ui::content_x + 1, 30, ui::content_width - 4, 6);
            g.fillRect(ui::content_x + 2, 31, int((ui::content_width - 6) * sampler.done() / std::max(1u, sampler.total())), 4);
            ui::detail(g, "", "BACK: STOP");
            return;
        }
        // The sheet: what the check found, the keys to sample, eight knobs (page 2: LEVEL).
        auto found = sampler.findings();
        settings.found = found;
        ui::rail(g, "AUTO", number, page ? "2/2" : "1/2");
        std::string kind = !found.heard ? "-" : found.kind == melodee::AutoKind::Drums ? "DRUMS" : found.kind == melodee::AutoKind::Mono ? "MONO" : "KEYS";
        if (phase == AutoSampler::Phase::Failed) kind = upper(sampler.error()).substr(0, 10);
        move::draw_text(g, ui::content_x + 1, 1, kind);
        auto count = std::to_string(settings.roots().size() * settings.layer_count() * settings.take_count()) + " SMP";
        move::draw_text(g, ui::width - move::text_width(count), 1, count);
        draw_keys(g, 0);
        std::array<std::string, 8> names;
        std::array<float, 8> values {};
        for (unsigned n = 0; n < 8; ++n) {
            auto k = knob(n);
            names[n] = k.name;
            values[n] = k.fraction;
        }
        ui::knob_cells(g, ui::content(21, 32), names, values);
        float seconds = settings.seconds_expected();
        std::string time = seconds <= 0 ? "PUSH: GO" : seconds < 60 ? std::to_string(int(seconds + 0.5f)) + " S" : std::to_string(int(seconds / 60.0f + 0.5f)) + " MIN";
        ui::detail(g, time, megabytes());
    }

    Knob knob(unsigned n) const override {
        auto key = [](int k) { return note_name(k); };
        if (page == 1) {
            if (n == 0) return {"LEVEL", settings.normalize ? "-1 DB" : "AS PLAYED", settings.normalize ? 1.0f : 0.0f};
            return {};
        }
        char text[16];
        switch (n) {
        case 0: return {"LOW", key(settings.low), settings.low / 127.0f};
        case 1: return {"HIGH", key(settings.high), settings.high / 127.0f};
        case 2: return {"STEP", std::to_string(settings.step), settings.step / 24.0f};
        case 3: return {"LAYERS", settings.layers ? std::to_string(settings.layers) : "AUTO", settings.layers / 8.0f};
        case 4:
            std::snprintf(text, sizeof text, "%.1f S", settings.hold);
            return {"HOLD", settings.hold_auto ? "AUTO" : text, settings.hold_auto ? 0.0f : settings.hold / 20.0f};
        case 5:
            std::snprintf(text, sizeof text, "%.1f S", settings.tail);
            return {"TAIL", text, settings.tail / 30.0f};
        case 6: return {"TAP", settings.post_fx ? "POST FX" : "PRE FX", settings.post_fx ? 1.0f : 0.0f};
        case 7: return {"LOOP", settings.loop_auto ? "AUTO" : settings.loop ? "ON" : "OFF", settings.loop_auto ? 0.5f : settings.loop ? 1.0f : 0.0f};
        default: return {};
        }
    }
    std::string knob_owner() const override { return "AUTO"; }

    std::string status() const override {
        if (studio.top() != this) return {};
        auto& sampler = const_cast<Studio&>(studio).auto_sampler();
        return std::string(" auto_sheet=") + phase_name(sampler.phase_now()) + " auto_page=" + std::to_string(page + 1);
    }

private:
    melodee::AutoSampleSettings settings;
    unsigned track = 0;
    int page = 0;
    std::array<bool, pad_count> held {};

    std::string module_name() const {
        auto* module = const_cast<Studio&>(studio).module_track(track);
        return module && module->module ? upper(module->module->info().abbrev).substr(0, 6) : ""; // room for BACK: NO
    }
    std::string megabytes() const {
        double frames = 0;
        for (uint8_t root : settings.roots()) frames += double(settings.layer_count() * settings.take_count()) * (settings.hold_seconds(root) + settings.tail) * 44100.0;
        return std::to_string(int(frames * 2 * 3 / 1e6 + 0.5)) + " MB"; // 24-bit stereo at most
    }
    std::string seconds_left(const AutoSampler& sampler) const {
        float total = settings.seconds_expected();
        if (total <= 0 || !sampler.total()) return "";
        int left = int(total * float(sampler.total() - sampler.done()) / float(sampler.total()));
        return std::to_string(left / 60) + ":" + two_digits(unsigned(left % 60));
    }
    // The roots over the keys (C1-C8), a mark each, the done ones filled.
    void draw_keys(juce::Graphics& g, unsigned done) const {
        auto area = ui::content(10, 9).reduced(1, 0);
        auto x_of = [&](int k) { return area.getX() + (std::clamp(k, 24, 108) - 24) * area.getWidth() / 85; };
        g.setColour(juce::Colours::white);
        g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);
        for (int c = 24; c <= 108; c += 12) g.fillRect(x_of(c), area.getBottom() - 3, 1, 2);
        unsigned n = 0;
        for (uint8_t root : settings.roots()) {
            int x = x_of(root);
            if (n++ < done) g.fillRect(x - 1, area.getY(), 3, area.getHeight() - 4);
            else g.drawRect(x - 1, area.getY(), 3, area.getHeight() - 4);
        }
    }
    void turn(unsigned n, int delta, bool fine) {
        if (!delta) return;
        auto step = [&](auto& field, int low, int high) { field = std::decay_t<decltype(field)>(std::clamp(int(field) + delta, low, high)); };
        if (page == 1) { if (n == 0) settings.normalize = delta > 0; return; }
        switch (n) {
        case 0: step(settings.low, 0, settings.high); break;
        case 1: step(settings.high, settings.low, 127); break;
        case 2: step(settings.step, 1, 24); break;
        case 3: step(settings.layers, 0, 8); break;
        case 4: {
            float hold = settings.hold_auto ? 0.0f : settings.hold;
            hold = std::clamp(hold + delta * (fine ? 0.1f : 0.5f), 0.0f, 20.0f);
            settings.hold_auto = hold <= 0.0f;
            settings.hold = std::max(hold, 0.1f);
            break;
        }
        case 5: settings.tail = std::clamp(settings.tail + delta * (fine ? 0.1f : 0.5f), 0.5f, 30.0f); break;
        case 6: settings.post_fx = delta > 0; break;
        case 7: { // Auto, On, Off
            int at = settings.loop_auto ? 0 : settings.loop ? 1 : 2;
            at = std::clamp(at + delta, 0, 2);
            settings.loop_auto = at == 0;
            settings.loop = at == 1;
            break;
        }
        default: break;
        }
    }
};

RegisterScreen registered("auto-sample", make<AutoSample>); // a module track's menu: AUTO SAMPLE

} // namespace
} // namespace app::screens
