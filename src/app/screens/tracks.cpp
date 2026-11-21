// SPDX-License-Identifier: AGPL-3.0-only
// Picking among the sixteen tracks (docs/UI.md "Tracks", design A, chosen on
// 2026-11-20): a track button held shows the sixteen as a grid, a row a bank
// (1-4, 5-8, 9-12, 13-16), and turns the 16 steps into them, each lit in its
// track's colour. A step picks its track; an empty one opens the new-track
// sheet for it. Copy held, a step then another copies the first track onto
// the second (asking when the second has something); Delete held, a step
// asks to delete it. The app shows this screen while a track button is held
// (MelodeeApp), over whatever screen is on top.
#include "app/screens/common.h"
#include "app/prints.h"
#include "app/song_desk.h"
#include "ui/song_view.h"
#include "ui/system_draw.h"

namespace app::screens {
namespace {

class Tracks final : public Screen {
public:
    using Screen::Screen;

    void enter() override { copy_down = delete_down = moving = false; copy_from = -1; }

    // Latched by Shift + Note/Session it is a screen of its own; held under a
    // track button the app hands it the steps.
    bindings::Screen screen_kind() const override { return bindings::Screen::Tracks; }
    void input(const move::Input& input) override {
        if (input.kind != move::Input::Kind::Button) return;
        auto button = move::Button(input.index);
        if (button == move::Button::Copy) { copy_down = input.pressed; if (!input.pressed) { copy_from = -1; moving = false; } }
        if (button == move::Button::Delete) delete_down = input.pressed;
    }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        if (job == Job::Back) { studio.close(); return true; }
        if (job != Job::PickTrack && job != Job::CopyTrack && job != Job::ClearTrack && job != Job::MoveTrack) return false;
        if (use.index < 0 || unsigned(use.index) >= melodee::track_count) return true;
        unsigned t = unsigned(use.index);
        bool used = studio.kind(t) != TrackKind::Empty;
        if (job == Job::ClearTrack) {
            if (!used) return true;
            pending_from = pending_to = int(t);
            studio.open("delete-track");
            return true;
        }
        if (job == Job::MoveTrack) { // Melodee's Move: a track, then where it goes; one there trades places
            if (copy_from < 0) { if (used) { copy_from = int(t); moving = true; } return true; }
            if (int(t) == copy_from) return true;
            int from = copy_from;
            copy_from = -1;
            moving = false;
            if (studio.move_track(unsigned(from), t)) {
                studio.name_step("MOVE TRACK " + std::to_string(from + 1), int(t));
                studio.say("TRACK " + std::to_string(from + 1) + " TO " + std::to_string(t + 1));
            }
            return true;
        }
        if (job == Job::CopyTrack) {
            if (copy_from < 0) { if (used) copy_from = int(t); return true; }
            if (int(t) == copy_from) return true;
            pending_from = copy_from;
            pending_to = int(t);
            copy_from = -1;
            if (used) studio.open("copy-track"); // asks first: it has something
            else copy();
            return true;
        }
        studio.select(t);
        if (!used) studio.open("new-track");
        return true;
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        for (unsigned t = 0; t < melodee::track_count; ++t) {
            uint8_t colour = studio.kind(t) == TrackKind::Empty ? colour::dark_grey : studio.colour_of(t).bright;
            if (studio.song_desk().printer().frozen(t)) colour = move::nearest_colour({150, 215, 255}); // frozen: ice
            if (int(t) == copy_from) colour = colour::white;
            else if (t == studio.track() && copy_from < 0) colour = colour::white;
            lights.step(t, colour);
        }
        lights.button(Button::Copy, copy_down ? white_bright : white_dim);
        lights.button(Button::Delete, delete_down ? white_bright : white_dim);
    }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "TRKS", track_number(studio), "/16");
        auto area = ui::content(0, 52);
        int cell_w = area.getWidth() / 4, cell_h = area.getHeight() / 4;
        for (unsigned t = 0; t < melodee::track_count; ++t) {
            int x = area.getX() + int(t % 4) * cell_w, y = area.getY() + int(t / 4) * cell_h;
            bool used = studio.kind(t) != TrackKind::Empty;
            bool marked = t == studio.track() || int(t) == copy_from;
            g.setColour(juce::Colours::white);
            if (marked) g.fillRect(x, y, cell_w - 1, cell_h - 1);
            else if (used) g.drawRect(x, y, cell_w - 1, cell_h - 1);
            g.setColour(marked ? juce::Colours::black : juce::Colours::white);
            if (studio.song_desk().printer().frozen(t)) ui::snowflake(g, x + (cell_w - 8) / 2, y + (cell_h - 8) / 2); // frozen: its snowflake
            else move::draw_text_centred(g, x, y + (cell_h - 7) / 2, cell_w - 1, used ? tag(t) : std::to_string(t + 1));
        }
        g.setColour(juce::Colours::white);
        if (delete_down) ui::detail(g, "DELETE: A STEP");
        else if (copy_down && moving) ui::detail(g, "MOVE " + std::to_string(copy_from + 1) + " TO: A STEP");
        else if (copy_down && studio.shift) ui::detail(g, "MOVE: A STEP");
        else if (copy_down) ui::detail(g, copy_from < 0 ? "COPY: A STEP" : "COPY " + std::to_string(copy_from + 1) + " TO: A STEP");
        else ui::detail(g, two_digits(studio.track() + 1) + " " + tag(studio.track()) + " " + upper(studio.current().name).substr(0, 7));
    }

    // What a cell says: a synth's module, or the track's kind.
    std::string tag(unsigned t) const {
        auto* track = const_cast<Studio&>(studio).module_track(t);
        if (track && track->module) return upper(track->module->info().abbrev).substr(0, 4);
        return kind_tag(studio.kind(t));
    }

    void copy() {
        if (pending_from < 0 || pending_to < 0) return;
        if (studio.copy_track(unsigned(pending_from), unsigned(pending_to))) {
            studio.say("TRACK " + std::to_string(pending_from + 1) + " TO " + std::to_string(pending_to + 1));
            studio.name_step("COPY TRACK " + std::to_string(pending_from + 1), pending_to);
        }
        pending_from = pending_to = -1;
    }
    void erase() {
        if (pending_to < 0) return;
        studio.delete_track(unsigned(pending_to));
        studio.say("TRACK " + std::to_string(pending_to + 1) + " DELETED");
        studio.name_step("DELETE TRACK " + std::to_string(pending_to + 1), pending_to);
        pending_from = pending_to = -1;
    }
    int pending_from = -1, pending_to = -1;

private:
    bool copy_down = false, delete_down = false, moving = false;
    int copy_from = -1;
};

Tracks& tracks_of(Studio& studio) { return static_cast<Tracks&>(*studio.screen("tracks")); }

// The question before copying onto a track that has something, or deleting one.
class Confirm : public Screen {
public:
    Confirm(Studio& studio, bool deleting) : Screen(studio), deleting(deleting) {}

    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(bindings::Job job, const bindings::Use&) override {
        using bindings::Job;
        switch (job) {
        case Job::ListPush:
            studio.close();
            if (deleting) tracks_of(studio).erase();
            else tracks_of(studio).copy();
            return true;
        case Job::Back: studio.close(); return true;
        case Job::ListTurn: case Job::ListTurnFine: case Job::TurnKnob: case Job::TurnKnobFine:
        case Job::PlayPad: case Job::PlayNote: return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        // Melodee's one question sheet (ui::system::question): what goes, and that undo brings it back.
        auto& tracks = tracks_of(studio);
        int to = std::max(tracks.pending_to, 0), from = std::max(tracks.pending_from, 0);
        const auto& target = studio.project().tracks[unsigned(to)];
        ui::system::Question q;
        q.undoable = true;
        if (deleting) {
            q.rail_top = "DEL"; q.rail_big = std::to_string(to + 1); q.rail_bottom = kind_tag(studio.kind(unsigned(to)));
            q.asked = "DELETE TRACK " + std::to_string(to + 1) + "?";
            q.lines = {upper(target.name).substr(0, 17), "SOUND, KIT, PATTERNS"};
        } else {
            q.rail_top = "COPY"; q.rail_big = std::to_string(from + 1); q.rail_bottom = "> " + std::to_string(to + 1);
            q.asked = "REPLACE TRACK " + std::to_string(to + 1) + "?";
            q.lines = {upper(target.name).substr(0, 17), "WITH TRACK " + std::to_string(from + 1)};
        }
        ui::system::question(g, q);
    }

private:
    bool deleting;
};

class DeleteTrack final : public Confirm {
public:
    explicit DeleteTrack(Studio& studio) : Confirm(studio, true) {}
};
class CopyTrack final : public Confirm {
public:
    explicit CopyTrack(Studio& studio) : Confirm(studio, false) {}
};

RegisterScreen tracks("tracks", make<Tracks>);
RegisterScreen delete_track("delete-track", make<DeleteTrack>);
RegisterScreen copy_track("copy-track", make<CopyTrack>);

} // namespace
} // namespace app::screens
