// SPDX-License-Identifier: AGPL-3.0-only
// The undo list (Undo held 0.4 s, Kerem's decision in docs/MOVE_CONTROLS.md;
// Melodee's Shift + Minus held, shell/history_list.h): the steps kept,
// newest on top under the steps undone, then START, the state before them
// all. The cursor opens on the state in hand; the wheel (or Plus and Minus)
// moves it, the rows above it struck through (undone once it is reached);
// a push goes there, a step at a time; Back closes it. Any other button
// closes it and does its own job; Play, Shift and the pads leave it open.
#include "app/screens/common.h"
#include "ui/system_draw.h"

namespace app::screens {
namespace {

std::string age(Studio::Clock::time_point now, Studio::Clock::time_point at) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now - at).count();
    if (seconds < 60) return "NOW";
    if (seconds < 3600) return std::to_string(seconds / 60) + "M";
    return std::to_string(seconds / 3600) + "H";
}

class UndoList final : public Screen {
public:
    using Screen::Screen;

    void enter() override { cursor = int(studio.history.redos()); }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        using bindings::Control;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: move(use.delta); return true;
        case Job::ListPush: go(); return true;
        case Job::Back: studio.close(); return true;
        case Job::UndoList: return true;
        case Job::TurnKnob: case Job::TurnKnobFine: case Job::ShowKnob: return true; // the knobs wait
        case Job::PlayPad: case Job::PlayNote: case Job::PlayPitch: case Job::SelectPadQuietly: case Job::PlayStop: case Job::Restart:
        case Job::ShiftSteps: case Job::ShiftLock: case Job::Aftertouch:
            return false; // the list stays open
        default:
            if (use.control == Control::Plus || use.control == Control::Minus) { move(use.control == Control::Minus ? -1 : 1); return true; }
            studio.close(); // any other button: its own job, the list closed
            return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        if (auto* play = studio.screen(home_screen(studio.kind())); play && play != this) play->lights(lights);
        lights.button(Button::Undo, white_bright);
        lights.button(Button::Back, white_dim);
        lights.button(Button::Up, white_dim);
        lights.button(Button::Down, white_dim);
    }

    void draw(juce::Graphics& g) override {
        auto rows = this->rows();
        int now = int(studio.history.redos());
        ui::system::undo_list(g, rows, std::clamp(cursor, 0, int(rows.size()) - 1), now);
    }

    std::string status() const override {
        if (studio.top() != this) return {};
        return " undo_list=1 undo_cursor=" + std::to_string(cursor);
    }

private:
    int cursor = 0;

    // Top down: the steps undone (the furthest redo first), the steps done
    // (the newest first), START.
    std::vector<ui::system::UndoRow> rows() const {
        std::vector<ui::system::UndoRow> out;
        const auto& history = studio.history;
        auto now = Studio::Clock::now();
        for (size_t n = 0; n < history.redos(); ++n) {
            const auto& step = history.undone_step(n);
            out.push_back({step.what, step.track, age(now, step.at), true, false});
        }
        for (size_t n = history.size(); n-- > 0;) {
            const auto& step = history.kept(n);
            out.push_back({step.what, step.track, age(now, step.at), false, false});
        }
        ui::system::UndoRow start;
        start.start = true;
        out.push_back(start);
        return out;
    }
    void move(int delta) {
        int count = int(studio.history.redos() + studio.history.size()) + 1;
        cursor = std::clamp(cursor + delta, 0, count - 1); // a row a detent
    }
    // To the state after the step under the cursor, one step at a time.
    void go() {
        int redos = int(studio.history.redos());
        int steps = redos - cursor; // > 0: redo; < 0: undo
        studio.close();
        int done = 0;
        for (int n = 0; n < std::abs(steps); ++n) {
            if (!(steps > 0 ? studio.redo() : studio.undo())) break;
            ++done;
        }
        if (done == 1) studio.say((steps > 0 ? "REDO " : "UNDO ") + studio.last_step);
        else if (done > 1) studio.say((steps > 0 ? "REDO " : "UNDO ") + std::to_string(done) + " STEPS");
    }
};

RegisterScreen registered("undo-list", make<UndoList>);

} // namespace
} // namespace app::screens
