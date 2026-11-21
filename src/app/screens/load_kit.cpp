// SPDX-License-Identifier: AGPL-3.0-only
// Kits and banks from the browser (docs/UI.md "Kits", docs/ui/kit.png).
//
// LOAD KIT: the wheel's push on an MPC drum program (.xpm, .xty) in the
// samples browser, as Melodee's LOAD KIT sheet: WHOLE KIT loads the program
// as the selected kit; each INTO row is a bank of the kit, its push stepping
// through the program's filled banks (then none), the lower bank in view
// starting on the program's first; LOAD BANKS puts only those banks' filled
// pads in (Melodee's shell/project_kits.h). Pads of two to four layers take
// them as their own layers; more, an instrument, stay silent for now.
//
// Browser options: Shift held in the samples browser (the Move's Options),
// Save Sample (the pad's region, named) and Save Bank for the two banks in
// view (Samples/Banks/<name>.mldb), which the browser lists tagged BANK.
#include "app/screens/kit_common.h"
#include "app/new_project.h"
#include "melodee/shell/mpc_program.h"
#include "melodee/shell/project_kits.h"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace app::screens {
namespace {

namespace fs = std::filesystem;

class LoadKit final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        cursor = 0;
        program = app::new_project();
        std::ifstream in(studio.program_path, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        melodee::MpcImport report;
        error = bytes.empty() ? "UNREADABLE" : upper(melodee::import_mpc_program(bytes, studio.program_path, *program, report));
        if (!error.empty()) return;
        melodee::layers_from_programs(*program); // two to four layers: the pad's own
        filled = melodee::kit_bank_pads(program->tracks[0]);
        picks.fill(-1);
        int first = next_filled(-1);
        picks[studio.pad_bank() % melodee::sampler_banks] = first;
    }
    void leave() override { program.reset(); }

    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: cursor = std::clamp(cursor + use.delta, 0, int(melodee::sampler_banks) + 1); return true;
        case Job::ListPush: push(); return true;
        case Job::Back: studio.close(); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        kit_pads(studio, lights);
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "KIT", track_number(studio), "LOAD");
        if (!error.empty()) {
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, error.substr(0, 16));
            ui::detail(g, "BACK: OUT");
            return;
        }
        std::vector<std::string> rows {"WHOLE KIT"};
        for (unsigned bank = 0; bank < melodee::sampler_banks; ++bank) {
            std::string row = "INTO " + std::string(1, char('A' + bank)) + "  ";
            row += picks[bank] < 0 ? "--" : "PROG " + std::string(1, char('A' + picks[bank]));
            rows.push_back(row);
        }
        rows.push_back("LOAD BANKS");
        ui::list(g, ui::content(0, ui::height), rows, cursor, 1);
    }
    std::string status() const override { return studio.top() == this ? " load_kit=" + (error.empty() ? std::string("ok") : error) : std::string(); }

private:
    std::unique_ptr<melodee::Project> program;
    std::array<uint8_t, melodee::sampler_banks> filled {};
    std::array<int, melodee::sampler_banks> picks {}; // each kit bank: the program's bank going into it, -1 none
    std::string error;
    int cursor = 0;

    int next_filled(int after) const {
        for (int bank = after + 1; bank < int(melodee::sampler_banks); ++bank)
            if (filled[size_t(bank)]) return bank;
        return -1;
    }
    void push() {
        if (!error.empty() || !program) return;
        if (cursor == 0) { // the whole program as the kit
            if (auto failed = studio.load_program(*program, -1, 0); !failed.empty()) studio.say(failed);
            studio.close();
            return;
        }
        if (cursor <= int(melodee::sampler_banks)) { // a bank: the program's next filled bank, then none
            auto& pick = picks[size_t(cursor - 1)];
            pick = next_filled(pick);
            return;
        }
        unsigned loaded = 0;
        for (unsigned bank = 0; bank < melodee::sampler_banks; ++bank) {
            if (picks[bank] < 0) continue;
            if (auto failed = studio.load_program(*program, picks[bank], bank); !failed.empty()) { studio.say(failed); return; }
            ++loaded;
        }
        if (!loaded) { studio.say("NO BANK PICKED"); return; }
        studio.close();
    }
};

RegisterScreen load_kit("load-kit", make<LoadKit>);

class BrowserOptions final : public Screen {
public:
    using Screen::Screen;
    void enter() override { cursor = 0; }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: cursor = std::clamp(cursor + use.delta, 0, 2); return true;
        case Job::ListPush: push(); return true;
        case Job::Back: case Job::BrowserOptions: studio.close(); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        using namespace move;
        kit_pads(studio, lights, int(studio.pad()));
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }
    void draw(juce::Graphics& g) override {
        ui::rail(g, "OPT", pad_label(studio.pad()), "LOAD");
        unsigned bank = studio.pad_bank();
        ui::list(g, ui::content(0, ui::height),
                 {"SAVE SAMPLE", "SAVE BANK " + std::string(1, char('A' + bank)), "SAVE BANK " + std::string(1, char('A' + bank + 1))}, cursor, 1);
    }

private:
    int cursor = 0;
    void push() {
        unsigned track = studio.track(), pad = studio.pad(), bank = studio.pad_bank() + unsigned(cursor - 1);
        if (cursor == 0) { // the pad's region, as Sample Options' Save Sample
            const Sample* sample = studio.sample_of_pad();
            if (!sample) { studio.say("EMPTY PAD"); return; }
            auto folder = studio.samples_folder();
            studio.close();
            studio.ask_text("SAVE SAMPLE", upper(sample->name.substr(0, sample->name.rfind('.'))), [this, track, pad, folder](const std::string& typed) {
                studio.file_jobs.push_back({false, track, pad, 0, (fs::path(folder) / (safe(typed) + ".wav")).string()});
                studio.say("SAVING");
            });
            return;
        }
        std::string name = upper(std::string(studio.current().name)) + " " + std::string(1, char('A' + bank));
        studio.close();
        studio.ask_text("SAVE BANK", name, [this, bank](const std::string& typed) {
            if (auto failed = studio.save_bank_file(bank, safe(typed)); !failed.empty()) studio.say(upper(failed).substr(0, 16));
        });
    }
    static std::string safe(const std::string& typed) {
        std::string name;
        for (char c : typed) name += std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' ? c : '_';
        while (!name.empty() && name.back() == ' ') name.pop_back();
        return name.empty() ? "UNTITLED" : name;
    }
};

RegisterScreen browser_options("browser-options", make<BrowserOptions>);

} // namespace
} // namespace app::screens
