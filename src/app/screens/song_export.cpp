// SPDX-License-Identifier: AGPL-3.0-only
// EXPORT (Sampling on Song; docs/UI.md "Song", Kerem's decision in
// docs/MOVE_CONTROLS.md): the song as a mixdown (32-bit float, 24 or 16-bit
// dithered), stems, a MIDI file or an MPC 3 project under Melodee's folder,
// or into a new pattern. The wheel picks, its push exports (named after the
// project, numbered past what is there). A mixdown and stems play the song
// once through, as Melodee renders them: the screen shows how far, Back (or
// Play) stops it and drops the files. Then where it went; push or Back goes
// back to Song. The work is exports.h's.
#include "app/screens/common.h"
#include "app/exports.h"
#include "app/song_desk.h"
#include "ui/song_view.h"
#include <cmath>

namespace app::screens {
namespace {

using bindings::Job;
using Kind = Exports::Kind;

std::string clock_of(double seconds) {
    unsigned s = unsigned(std::lround(std::max(0.0, seconds)));
    char text[16];
    std::snprintf(text, sizeof text, "%u:%02u", s / 60, s % 60);
    return text;
}

class SongExport final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override { started = false; studio.desk().exports().seen(); }

    bool job(Job job, const bindings::Use& use) override {
        auto& exports = studio.desk().exports();
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (!started) row = std::clamp(row + use.delta, 0, int(Kind::Count) - 1);
            return true;
        case Job::ListPush:
            if (exports.busy()) return true;
            if (started) { exports.seen(); studio.close(); return true; }
            if (auto why = exports.start(Kind(row)); !why.empty()) { studio.say(why); return true; }
            started = true;
            return true;
        case Job::Back: case Job::SongExport:
            if (exports.busy()) { exports.cancel(); return true; }
            exports.seen();
            studio.close();
            return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        bool busy = studio.desk().exports().busy();
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Copy, Button::Delete}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
        lights.button(Button::Sample, busy ? colour::red : colour::light_grey);
    }

    void draw(juce::Graphics& g) override {
        const auto& desk = studio.desk();
        auto shown = desk.exports().shown();
        if (!started) { // the list
            ui::rail(g, "EXPT", "", "SONG");
            std::vector<std::string> rows;
            for (unsigned k = 0; k < unsigned(Kind::Count); ++k) rows.push_back(Exports::kind_name(Kind(k)));
            ui::list(g, ui::content(0, 52), rows, row, 1);
            std::string name = upper(studio.project().name);
            if (name.empty() || name == "UNTITLED") name = "SONG";
            ui::detail(g, Kind(row) == Kind::ToPattern ? "INTO A PATTERN" : "AS " + name.substr(0, 9), clock_of(desk.seconds_at(desk.song().length())));
            return;
        }
        if (shown.busy) {
            int percent = int(std::lround(shown.fraction * 100));
            ui::rail(g, "EXPT", shown.kind == Kind::Mpc3 ? "" : std::to_string(std::min(percent, 99)), shown.kind == Kind::Mpc3 ? "" : "%");
            move::draw_text(g, ui::content_x + 2, 1, Exports::kind_name(shown.kind));
            auto slash = shown.file.find('/');
            move::draw_text(g, ui::content_x + 2, 12, upper(slash == std::string::npos ? shown.name : shown.file.substr(slash + 1)).substr(0, 16));
            if (shown.kind == Kind::Mpc3) {
                move::draw_text(g, ui::content_x + 2, 28, "WRITING...");
            } else {
                ui::progress(g, {ui::content_x + 2, 26, ui::content_width - 4, 9}, shown.fraction);
                std::string clock = shown.tail > 0 ? "TAIL " + clock_of(shown.tail) : clock_of(shown.seconds) + " OF " + clock_of(shown.total);
                move::draw_text(g, ui::content_x + 2, 40, clock);
            }
            ui::detail(g, "BACK: STOP");
            return;
        }
        if (shown.done) {
            ui::rail(g, "EXPT", "OK", "");
            auto slash = shown.file.find('/');
            if (slash != std::string::npos) {
                move::draw_text(g, ui::content_x + 2, 1, upper(shown.file.substr(0, slash + 1)));
                move::draw_text(g, ui::content_x + 2, 12, upper(shown.file.substr(slash + 1)).substr(0, 16));
            }
            char line[40];
            if (!shown.said.empty()) move::draw_text(g, ui::content_x + 2, 26, upper(shown.said).substr(0, 16));
            else if (shown.kind == Kind::Midi) {
                std::snprintf(line, sizeof line, "%llu BYTES", (unsigned long long)shown.bytes);
                move::draw_text(g, ui::content_x + 2, 26, line);
            } else {
                std::snprintf(line, sizeof line, "%s  %.1f MB", clock_of(shown.seconds).c_str(), shown.bytes / 1048576.0);
                move::draw_text(g, ui::content_x + 2, 26, line);
                std::snprintf(line, sizeof line, "TAIL %.1f S", shown.tail);
                move::draw_text(g, ui::content_x + 2, 37, line);
            }
            ui::detail(g, "PUSH: OK");
            return;
        }
        ui::rail(g, "EXPT", "", "");
        move::draw_text(g, ui::content_x + 2, 4, "NOT EXPORTED");
        move::draw_text(g, ui::content_x + 2, 16, upper(shown.error).substr(0, 16));
        ui::detail(g, "PUSH: OK");
    }

    std::string status() const override { return studio.top() == this ? " export_row=" + std::to_string(row) : std::string(); }

private:
    int row = 0;
    bool started = false;
};

RegisterScreen song_export("song-export", make<SongExport>); // Sampling on Song

} // namespace
} // namespace app::screens
