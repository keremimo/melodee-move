// SPDX-License-Identifier: AGPL-3.0-only
// Session (the Move's manual, 17; docs/UI.md, "Sequencer"): Note/Session
// shows the Patterns grid. A row of pads a track (the bank in view's four,
// the first on top), a column a pattern number (eight at a time).
//
//   Pad                  launches that track's pattern (the next bar while
//                        playing, at once stopped) and selects it
//   Pad held + pads of its row   a chain: each plays once, round again (Melodee's CHAN)
//   Copy + Pad, then a Pad       the pattern copied there (asks before replacing)
//   Delete + Pad         the pattern cleared at once (17.1.4)
//   Plus / Minus         the bank of four tracks; Left / Right eight patterns
//   Wheel                the playhead by the pattern's Q; stopped, Play continues there
//   Wheel push           the selected pattern's column, every track (Melodee's sequence), its tempo on knob 1,
//                        its name on knob 2, its colour on knob 3
//   Steps 1-8            (lights only) each column's sequence in its colour, the sequence in hand bright
//   Note/Session or Back back to Play; Note/Session held peeks (the sequencer lets it go)
#include "app/screens/common.h"
#include "app/sequencer.h"
#include "ui/seq_widgets.h"

namespace app::screens {
namespace {

using bindings::Job;
using Clock = std::chrono::steady_clock;

unsigned bank_first(const Studio& studio) { return ui::bank_of(studio.track()) * ui::tracks_a_bank; }

// A sequence's colour (melodee-sample's issue 19, Scene::colour): 0 none, 1-16
// a track's colour, named as the projects' colours are.
constexpr const char* scene_colour_names[16] {"AZURE", "ORANGE", "VIOLET", "GREEN", "YELLOW", "MAGENTA", "CYAN", "RED",
                                              "ROYAL", "PINK", "MINT", "PURPLE", "LIME", "ROSE", "SKY", "TAN"};
std::string scene_colour_name(uint8_t colour) { return colour && colour <= 16 ? scene_colour_names[colour - 1] : "NONE"; }

class Session final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::Session; }
    void enter() override {
        if (keeping) { keeping = false; return; } // back after picking a track: as it was
        auto& seq = studio.sequencer();
        first = seq.number(studio.track()) / 8 * 8;
        copied_track = copied = -1;
        chain_row = -1;
        chain.clear();
    }
    bool job(Job job, const bindings::Use& use) override {
        auto& seq = studio.sequencer();
        int row = 3 - int(move::pad_row(unsigned(use.index))), column = int(move::pad_column(unsigned(use.index)));
        unsigned t = bank_first(studio) + unsigned(row), n = first + unsigned(column);
        switch (job) {
        case Job::LaunchPattern:
            if (!studio.project().tracks[t].used()) { studio.say("EMPTY TRACK"); return true; }
            pick(t);
            chain_row = row;
            chain = {uint8_t(n)};
            held_pad = use.index;
            seq.launch(t, n);
            return true;
        case Job::ChainPattern: // a pad held, then more of its row: a chain, in one hold
            if (chain_row != row || chain.empty()) return true;
            if (chain.size() < melodee::max_chain) chain.push_back(uint8_t(n));
            studio.say(chain_text());
            return true;
        case Job::CopyPattern: {
            if (copied < 0) { copied_track = int(t); copied = int(n); studio.say("PATTERN " + std::to_string(n + 1) + " COPIED"); return true; }
            target_track = t;
            target = n;
            if (!seq.pattern(t, n).empty()) { studio.push("session-copy"); return true; }
            seq.copy_pattern(unsigned(copied_track), unsigned(copied), t, n);
            studio.say("PASTED ON " + std::to_string(n + 1));
            return true;
        }
        case Job::DeletePattern:
            seq.clear_pattern(t, n);
            studio.say("PATTERN " + std::to_string(n + 1) + " DELETED");
            return true;
        case Job::TrackBank: {
            int bank = int(ui::bank_of(studio.track())) + (use.control == bindings::Control::Plus ? -1 : 1);
            if (bank < 0 || bank >= int(melodee::track_count / ui::tracks_a_bank)) return true;
            pick(unsigned(bank) * ui::tracks_a_bank + studio.track() % ui::tracks_a_bank);
            return true;
        }
        case Job::PatternPage: {
            int to = int(first) + (use.control == bindings::Control::Right ? 8 : -8);
            if (to >= 0 && to < int(melodee::pattern_count)) first = unsigned(to);
            studio.say("PATTERNS " + std::to_string(first + 1) + "-" + std::to_string(first + 8));
            return true;
        }
        case Job::Playhead: seq.move_playhead(use.delta); return true;
        case Job::LaunchScene: studio.push("scene"); return true;
        case Job::ToggleSession: case Job::Back: studio.close(); return true;
        case Job::PeekSession: return true;
        default: return false;
        }
    }
    void input(const move::Input& input) override {
        using Kind = move::Input::Kind;
        if (input.kind == Kind::Pad && !input.pressed && int(input.index) == held_pad) {
            held_pad = -1;
            if (chain.size() >= 2) {
                studio.sequencer().chain(bank_first(studio) + unsigned(chain_row), chain);
                studio.say("CHAIN " + chain_text());
            }
            chain.clear();
            chain_row = -1;
        }
        if (input.kind == Kind::Button && move::Button(input.index) == move::Button::Copy && !input.pressed) copied = copied_track = -1;
    }
    void lights(move::Lights& lights) override {
        using namespace move;
        auto& seq = studio.sequencer();
        bool blink = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count() / 250 % 2;
        unsigned bank = bank_first(studio);
        for (unsigned pad = 0; pad < pad_count; ++pad) {
            unsigned row = 3 - pad_row(pad), column = pad_column(pad), t = bank + row, n = first + column;
            auto colour = ui::track_colour(t);
            auto cell = cell_of(t, n);
            uint8_t shown = cell == ui::seq::Cell::Empty ? colour::off : cell == ui::seq::Cell::Notes ? colour.dim
                          : cell == ui::seq::Cell::Playing ? colour.bright : cell == ui::seq::Cell::Queued ? (blink ? colour::green : colour::off)
                          : (blink ? colour.bright : colour.dim);
            if (t == studio.track() && n == seq.number(t) && blink && cell != ui::seq::Cell::Queued) shown = colour::white; // selected: pulsing white
            lights.pad(pad, shown);
        }
        // The steps under the eight columns: each sequence in its colour (none: dark), the sequence in hand bright.
        for (unsigned s = 0; s < step_count; ++s) {
            const unsigned n = first + s;
            const uint8_t c = s < 8 && n < melodee::pattern_count ? studio.project().scenes[n].colour : 0;
            if (!c || c > 16) { lights.step(s, colour::off); continue; }
            const auto shade = ui::track_colours[size_t(c - 1)];
            lights.step(s, n == studio.project().scene % melodee::pattern_count ? shade.bright : shade.dim);
        }
        lights.button(Button::Menu, white_bright);
        lights.button(Button::Back, white_dim);
        lights.button(Button::Left, first ? white_dim : 0);
        lights.button(Button::Right, first + 8 < melodee::pattern_count ? white_dim : 0);
        lights.button(Button::Up, ui::bank_of(studio.track()) ? white_dim : 0);
        lights.button(Button::Down, ui::bank_of(studio.track()) + 1 < melodee::track_count / ui::tracks_a_bank ? white_dim : 0);
        lights.button(Button::Copy, white_dim);
        lights.button(Button::Delete, white_dim);
    }
    void draw(juce::Graphics& g) override {
        auto& seq = studio.sequencer();
        unsigned bank = bank_first(studio);
        if (held_pad >= 0 && chain.size() >= 2) { // a chain in the making (Melodee's CHAN)
            ui::rail(g, "CHN", std::to_string(bank + unsigned(chain_row) + 1), "TRK");
            move::draw_text(g, ui::content_x + 2, 4, chain_text().substr(0, 5), 3);
            if (chain_text().size() > 5) move::draw_text(g, ui::content_x + 2, 24, chain_text().substr(5, 16));
            move::draw_text(g, ui::content_x + 2, 30, "EACH PLAYS ONCE,");
            move::draw_text(g, ui::content_x + 2, 40, "THEN ROUND AGAIN");
            ui::detail(g, "A TAP ENDS IT");
            return;
        }
        unsigned selected = seq.number(studio.track());
        ui::rail(g, "PATT", std::to_string(selected + 1), std::to_string(first + 1) + "-" + std::to_string(first + 8));
        std::array<ui::seq::GridRow, 4> rows {};
        for (unsigned r = 0; r < 4; ++r) {
            unsigned t = bank + r;
            if (!studio.project().tracks[t].used()) continue;
            rows[r].tag = std::to_string(t + 1);
            for (unsigned c = 0; c < 8; ++c) {
                rows[r].cells[c] = cell_of(t, first + c);
                if (seq.pattern(t, first + c).follow.on()) rows[r].follows |= uint8_t(1u << c);
            }
            int position = studio.playing() ? studio.host().position_of(t) : -1;
            const auto& playing = seq.pattern(t, studio.host().pattern_of(t));
            if (position >= 0 && playing.length) rows[r].progress = uint8_t(1 + uint64_t(position) * 9 / playing.length);
        }
        int column = int(selected) - int(first);
        ui::seq::pattern_grid(g, ui::content(1, 50), rows, first, int(studio.track() - bank), column >= 0 && column < 8 ? column : -1);
        const auto& track = studio.current();
        std::string where = seq.play_from_shown() ? "@" + std::to_string(seq.play_from_shown() / std::max(1u, seq.pattern().q()) + 1) : "P" + std::to_string(selected + 1);
        ui::detail(g, std::to_string(studio.track() + 1) + " " + kind_tag(studio.kind()) + " " + upper(track.name).substr(0, 7), where);
    }
    std::string status() const override { return " session_first=" + std::to_string(first + 1); }

    unsigned first_shown() const { return first; }
    // For the copy question.
    int copied_track = -1, copied = -1;
    unsigned target_track = 0, target = 0;

private:
    unsigned first = 0; // the first pattern number of the eight in view
    int held_pad = -1, chain_row = -1;
    bool keeping = false;
    std::vector<uint8_t> chain;

    void pick(unsigned t) {
        if (t == studio.track()) return;
        studio.select(t);      // back to Play ...
        keeping = true;
        studio.open("session"); // ... and here again, on the new track
    }
    ui::seq::Cell cell_of(unsigned t, unsigned n) const {
        auto& seq = studio.sequencer();
        bool notes = !seq.pattern(t, n).empty();
        bool playing = studio.playing() && studio.host().pattern_of(t) == n;
        bool chosen = seq.number(t) == n;
        if (playing && !chosen) return ui::seq::Cell::Stopping; // another waits for the bar
        if (playing) return ui::seq::Cell::Playing;
        if (chosen && studio.playing()) return ui::seq::Cell::Queued;
        return notes ? ui::seq::Cell::Notes : ui::seq::Cell::Empty;
    }
    std::string chain_text() const {
        std::string text;
        for (auto n : chain) text += (text.empty() ? "" : ">") + std::to_string(n + 1);
        return text;
    }
};

// Copy + a pad onto a pattern with notes: asked first.
class SessionCopy final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(Job job, const bindings::Use&) override {
        auto* session = dynamic_cast<Session*>(studio.screen("session"));
        switch (job) {
        case Job::ListPush:
            if (session && session->copied >= 0) {
                studio.sequencer().copy_pattern(unsigned(session->copied_track), unsigned(session->copied), session->target_track, session->target);
                studio.say("PATTERN REPLACED");
            }
            studio.close();
            return true;
        case Job::Back: studio.close(); return true;
        default: return true; // a question: nothing else meanwhile
        }
    }
    void lights(move::Lights& lights) override { lights.button(move::Button::Back, move::white_dim); }
    void draw(juce::Graphics& g) override {
        auto* session = dynamic_cast<Session*>(studio.screen("session"));
        if (!session) return;
        unsigned from = unsigned(std::max(0, session->copied)) + 1, to = session->target + 1;
        ui::rail(g, "COPY", std::to_string(from), "> " + std::to_string(to));
        move::draw_text(g, ui::content_x + 2, 4, "PATTERN " + std::to_string(from) + " TO " + std::to_string(to));
        auto notes = studio.sequencer().pattern(session->target_track, session->target).events.size();
        move::draw_text(g, ui::content_x + 2, 18, std::to_string(to) + " HAS " + std::to_string(notes) + " NOTES:");
        move::draw_text(g, ui::content_x + 2, 28, "REPLACE THEM?");
        ui::detail(g, "PUSH YES", "BACK NO");
    }
};

// The wheel's push in Session: the selected pattern's column on every track
// (Melodee's sequence). Knob 1 its tempo, knob 2 its name (Melodee's ready
// names); pads of other columns tapped here chain the sequences (each played
// once, round again); push launches, Back leaves.
class Scene final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override { chain.clear(); }
    bool job(Job job, const bindings::Use& use) override {
        auto& seq = studio.sequencer();
        unsigned n = seq.number(studio.track());
        auto* session = dynamic_cast<Session*>(studio.screen("session"));
        switch (job) {
        case Job::ListPush:
            if (chain.size() >= 2) {
                for (unsigned t = 0; t < melodee::track_count; ++t) {
                    if (!studio.project().tracks[t].used()) continue;
                    bool any = t == studio.track();
                    for (auto k : chain) any = any || !seq.pattern(t, k).empty();
                    if (any) seq.chain(t, chain);
                }
                studio.say("SEQUENCES " + chain_text());
            } else {
                seq.launch_scene(n);
                studio.say("SEQUENCE " + std::to_string(n + 1));
            }
            studio.close();
            return true;
        case Job::PlayPad: case Job::PlayNote: case Job::PlayPitch: { // a column: chained after the selected one
            unsigned column = (session ? session->first_shown() : n / 8 * 8) + move::pad_column(unsigned(use.index));
            if (chain.empty()) chain.push_back(uint8_t(n));
            if (chain.size() < melodee::max_chain) chain.push_back(uint8_t(column));
            return true;
        }
        case Job::TurnKnob: case Job::TurnKnobFine:
            if (use.index == 0) seq.set_scene_tempo(n, use.delta);
            if (use.index == 1) name(n, use.delta);
            if (use.index == 2) colour(n, use.delta);
            return true;
        case Job::ListTurn: seq.set_scene_tempo(n, use.delta); return true;
        case Job::Back: studio.close(); return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        auto* session = studio.screen("session");
        if (session) session->lights(lights); // the grid, its columns to chain
        lights.button(move::Button::Back, move::white_dim);
    }
    void draw(juce::Graphics& g) override {
        unsigned n = studio.sequencer().number(studio.track());
        const auto& scene = studio.project().scenes[n];
        ui::rail(g, "SCNE", std::to_string(n + 1), "");
        ui::seq::title(g, "SEQUENCE " + std::to_string(n + 1), upper(scene.name).substr(0, 6));
        char tempo[16];
        if (scene.tempo) std::snprintf(tempo, sizeof tempo, "%u.%u", scene.tempo / 10, scene.tempo % 10);
        else std::snprintf(tempo, sizeof tempo, "--");
        ui::seq::value_cells(g, ui::content(11, 40), {"BPM", "NAM", "COL", "", "", "", "", ""},
                             {tempo, scene.name[0] ? upper(scene.name).substr(0, 4) : "-", scene.colour ? scene_colour_name(scene.colour).substr(0, 4) : "-", "", "", "", "", ""});
        if (chain.size() >= 2) move::draw_text(g, ui::content_x + 2, 33, "CHAIN " + chain_text());
        else move::draw_text(g, ui::content_x + 2, 33, "PATTERN " + std::to_string(n + 1) + " OF EACH");
        move::draw_text(g, ui::content_x + 2, 42, chain.size() >= 2 ? "EACH ONCE, ROUND" : "TRACK, NEXT BAR");
        ui::detail(g, "PUSH: LAUNCH");
    }
    Knob knob(unsigned which) const override {
        const auto& scene = studio.project().scenes[studio.sequencer().number(studio.track())];
        if (which == 1) return {"SEQUENCE NAME", scene.name[0] ? upper(scene.name) : "NONE", 0.0f};
        if (which == 2) return {"SEQUENCE COLOUR", scene_colour_name(scene.colour), scene.colour / 16.0f};
        if (which) return {};
        return {"SEQUENCE TEMPO", scene.tempo ? std::to_string(scene.tempo / 10) + "." + std::to_string(scene.tempo % 10) : "NONE", scene.tempo / 3000.0f};
    }
    std::string status() const override { return " scene_chain=" + std::to_string(chain.size()); }

private:
    std::vector<uint8_t> chain;
    // Melodee's ready names for a sequence (its markers').
    void name(unsigned n, int delta) {
        static const char* const names[] {"", "Intro", "Verse", "Pre", "Chorus", "Bridge", "Break", "Build", "Drop", "Outro", "A", "B", "C", "D"};
        constexpr int count = int(sizeof names / sizeof names[0]);
        auto& scene = studio.project().scenes[n];
        int at = 0;
        for (int k = 0; k < count; ++k) if (std::string(scene.name) == names[k]) at = k;
        scene.set_name(names[std::clamp(at + (delta > 0 ? 1 : -1), 0, count - 1)]);
        studio.changed();
    }
    std::string chain_text() const {
        std::string text;
        for (auto k : chain) text += (text.empty() ? "" : ">") + std::to_string(k + 1);
        return text;
    }
    // None, then the sixteen track colours, a detent each; its cell and step light in it.
    void colour(unsigned n, int delta) {
        auto& scene = studio.project().scenes[n];
        scene.colour = uint8_t(std::clamp(int(scene.colour) + (delta > 0 ? 1 : -1), 0, 16));
        studio.name_step("COLOUR SEQ " + std::to_string(n + 1));
        studio.changed();
    }
};

RegisterScreen session("session", make<Session>, {{bindings::Job::ToggleSession}});
RegisterScreen session_copy("session-copy", make<SessionCopy>);
RegisterScreen scene("scene", make<Scene>);

} // namespace
} // namespace app::screens
