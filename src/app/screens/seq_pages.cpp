// SPDX-License-Identifier: AGPL-3.0-only
// Melodee's SEQ pages (docs/UI.md, "Sequencer"; Kerem, 2026-11-20: Shift +
// Loop tapped opens them, Shift + the wheel turns the page): what Melodee's
// sequencer has beyond the Move's. The steps, the pads and a held step work
// as on Play under them.
//
//   PATTERN  length, Q, time signature, the track's speed, this pattern's
//            BPM, free length; push: Half and Double Seq and Speed, the variations
//   ENTRY    step entry (Melodee's Step Edit PLAY): pads written at the
//            caret, which moves on as they are let go; the wheel the caret,
//            push a rest, Delete back a step erasing it; length, velocity, chance
//   ROLL     the notes over the bar, the wheel the note in focus; knobs its
//            length, velocity, chance, nudge, ratchet, octave, place, key;
//            push: the jobs (marks, mute, pull to the grid, legato, split,
//            reverse, the arp as notes, loop the bar, erase); Copy copies the
//            marked notes, again pastes them at the focus; Delete erases
//   MOTION   the pattern's lanes, the wheel one; push turns it off or on, Delete clears it
//   MOD      eight modulators (the wheel), an LFO or a follower; push the target slot
//   MACROS   eight macros a knob each; push: the lanes the last one turned moves
//   FEEL     Time Correct heard live: strength, swing, humanize, velocity, a
//            groove from another pattern, its amount; push lays it on the others
//   FOLLOW   the follow action; push puts it on the pattern's column
//   LANE     a kit pad's own lane length; Euclid's hits, steps, rotation; push lays it
#include "app/screens/common.h"
#include "app/sequencer.h"
#include "ui/seq_widgets.h"
#include "melodee/engine/arp.h"
#include "melodee/model/generators.h"
#include "melodee/model/note_edits.h"
#include "melodee/model/trig.h"
#include <set>

namespace app::screens {
namespace {

using bindings::Job;
using melodee::Pattern;

enum class Page : unsigned { Pattern, Entry, Roll, Motion, Mod, Macros, Feel, Follow, Lane, Count };
constexpr unsigned page_count = unsigned(Page::Count);
const char* const page_tags[page_count] {"SEQ", "ENTR", "ROLL", "MOTN", "MOD", "MACR", "FEEL", "FOLW", "LANE"};

std::string tenths(unsigned value) { return std::to_string(value / 10) + "." + std::to_string(value % 10); }
std::string key_name(unsigned key) { return note_name(int(key)); }
std::string padded(const std::string& left, const std::string& right) {
    return left + std::string(size_t(std::max(1, 16 - int(left.size()) - int(right.size()))), ' ') + right;
}

class SeqPages final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::Play; }
    void enter() override { set_entry(); }
    void leave() override { studio.sequencer().entry = false; }

    bool job(Job job, const bindings::Use& use) override {
        auto& seq = studio.sequencer();
        switch (job) {
        case Job::SeqPages: case Job::Back: seq.entry = false; studio.close(); return true;
        case Job::Page: turn_page(use.delta); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: knob_turn(unsigned(use.index), use.delta); touched = use.index; return true;
        case Job::Focus: wheel(use.delta); return true;
        case Job::OpenBrowser: push(); return true;
        case Job::DeleteCurrentPattern: return erase(); // Delete on a page: its own
        case Job::DuplicatePattern: return copy();       // Copy on Roll: the marked notes
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        if (auto* play = studio.screen(home_screen(studio.kind())); play && play != this) play->lights(lights); // the pads as they play
        for (unsigned k = 0; k < 8; ++k) {} // the knobs' lights are the app's
        lights.button(move::Button::Back, move::white_dim);
        lights.button(move::Button::Loop, move::white_bright);
    }
    void draw(juce::Graphics& g) override;
    Knob knob(unsigned which) const override;
    std::string knob_owner() const override { return page_tags[unsigned(page)]; }
    std::string status() const override {
        return std::string(" seq_page_name=") + page_tags[unsigned(page)] + " roll_focus=" + std::to_string(focus) + " marks=" + std::to_string(marks.size());
    }

    // ------------------------------------------------------------ for the sheets and tests
    Page page = Page::Pattern;
    int focus = 0;                 // Roll: the note in focus (an index into the pattern's events)
    std::set<size_t> marks;        // Roll: the marked notes
    std::vector<melodee::Event> clipboard;
    unsigned vary_amount = 50;     // the variations' amount, percent
    unsigned mod = 0, mod_target = 0; // MOD: the modulator, its target slot
    unsigned macro = 0;            // MACROS: the one turned last
    int motion_row = 0;
    unsigned hits = 4, steps = 16, rotate = 0; // LANE: Euclid's

    void go(Page to) {
        page = to;
        set_entry();
    }
    // The notes Roll's jobs act on: the marked ones, else the one in focus.
    std::vector<size_t> chosen() {
        auto& p = studio.sequencer().pattern();
        std::vector<size_t> at;
        for (size_t n : marks) if (n < p.events.size()) at.push_back(n);
        if (at.empty() && focus >= 0 && size_t(focus) < p.events.size()) at.push_back(size_t(focus));
        return at;
    }
    void edited() {
        auto& seq = studio.sequencer();
        auto& p = seq.pattern();
        focus = std::clamp(focus, 0, std::max(0, int(p.events.size()) - 1));
        std::set<size_t> kept;
        for (size_t n : marks) if (n < p.events.size()) kept.insert(n);
        marks = kept;
        seq.sync(studio.track(), seq.number(studio.track()));
    }
    // Lanes of the pattern, as Motion lists them.
    struct LaneRow { unsigned param, pad, points, locks; };
    std::vector<LaneRow> lanes() const {
        auto& p = studio.sequencer().pattern();
        std::vector<LaneRow> rows;
        for (const auto& m : p.motion) {
            auto at = std::find_if(rows.begin(), rows.end(), [&](const LaneRow& r) { return r.param == m.param && r.pad == m.pad; });
            if (at == rows.end()) { rows.push_back({m.param, m.pad, 0, 0}); at = rows.end() - 1; }
            if (m.lock()) ++at->locks; else ++at->points;
        }
        return rows;
    }
    std::string lane_name(unsigned param, unsigned pad) const {
        for (const auto& lane : studio.sequencer().lanes_of(studio.track()))
            if (lane.param == param && (lane.pad == pad || melodee::pad_param_of(param) < 0)) return lane.name;
        if (int which = melodee::pad_param_of(param); which >= 0) return "PAD " + pad_label(pad) + " " + std::to_string(which);
        return "LANE " + std::to_string(param);
    }

private:
    int touched = -1;

    void set_entry() { studio.sequencer().entry = page == Page::Entry; }
    void turn_page(int delta) {
        int to = std::clamp(int(page) + (delta > 0 ? 1 : -1), 0, int(page_count) - 1);
        if (Page(to) == Page::Lane && !studio.sequencer().kit()) to = delta > 0 ? int(Page::Follow) : int(Page::Follow);
        go(Page(to));
        studio.say(page_tags[unsigned(page)]);
    }
    void wheel(int delta) {
        auto& seq = studio.sequencer();
        auto& p = seq.pattern();
        switch (page) {
        case Page::Entry: seq.caret = unsigned(std::max(0, int(seq.caret) + delta)); break;
        case Page::Roll: if (!p.events.empty()) focus = std::clamp(focus + (delta > 0 ? 1 : -1), 0, int(p.events.size()) - 1); break;
        case Page::Motion: motion_row = std::clamp(motion_row + (delta > 0 ? 1 : -1), 0, std::max(0, int(lanes().size()) - 1)); break;
        case Page::Mod: mod = unsigned(std::clamp(int(mod) + (delta > 0 ? 1 : -1), 0, int(melodee::modulator_count) - 1)); break;
        default: break;
        }
    }
    void push() {
        auto& seq = studio.sequencer();
        switch (page) {
        case Page::Pattern: studio.push("seq-jobs"); break;
        case Page::Entry: seq.entry_rest(); break;
        case Page::Roll: studio.push("roll-jobs"); break;
        case Page::Motion: {
            auto rows = lanes();
            if (rows.empty()) { studio.say("NO MOTION"); break; }
            const auto& row = rows[size_t(std::min<int>(motion_row, int(rows.size()) - 1))];
            seq.toggle_lane(studio.track(), seq.number(studio.track()), row.param, row.pad);
            studio.say(seq.lane_off(studio.track(), seq.number(studio.track()), row.param, row.pad) ? "LANE OFF" : "LANE ON");
            break;
        }
        case Page::Mod: mod_target = (mod_target + 1) % melodee::modulator_targets; studio.say("TARGET " + std::to_string(mod_target + 1)); break;
        case Page::Macros: studio.push("macro-lanes"); break;
        case Page::Feel: seq.feel_all(); break;
        case Page::Follow: seq.follow_column(); break;
        case Page::Lane: seq.lay_euclid(hits, steps, rotate); studio.say("E(" + std::to_string(hits) + "," + std::to_string(steps) + ")"); break;
        case Page::Count: break;
        }
    }
    bool erase() {
        auto& seq = studio.sequencer();
        auto& p = seq.pattern();
        switch (page) {
        case Page::Entry: seq.entry_erase(); return true;
        case Page::Roll: {
            auto at = chosen();
            if (at.empty()) return true;
            melodee::take_events(p, at);
            marks.clear();
            edited();
            studio.say("ERASED");
            return true;
        }
        case Page::Motion: {
            auto rows = lanes();
            if (rows.empty()) return true;
            const auto& row = rows[size_t(std::min<int>(motion_row, int(rows.size()) - 1))];
            p.clear_lane(row.param, row.pad);
            seq.sync(studio.track(), seq.number(studio.track()));
            studio.say("LANE CLEARED");
            return true;
        }
        default: return false; // the pattern, as on Play
        }
    }
    bool copy() {
        if (page != Page::Roll) return false;
        auto& p = studio.sequencer().pattern();
        if (clipboard.empty()) {
            for (size_t n : chosen()) clipboard.push_back(p.events[n]);
            studio.say(clipboard.empty() ? "NOTHING MARKED" : std::to_string(clipboard.size()) + " COPIED");
            return true;
        }
        // Pasted at the note in focus: the first copied lands there.
        uint32_t at = focus >= 0 && size_t(focus) < p.events.size() ? p.events[size_t(focus)].tick : 0;
        uint32_t from = clipboard.front().tick;
        for (auto e : clipboard) { e.tick = e.tick - from + at; p.add(e); }
        studio.say(std::to_string(clipboard.size()) + " PASTED");
        clipboard.clear();
        edited();
        return true;
    }
    void knob_turn(unsigned k, int delta) {
        auto& seq = studio.sequencer();
        auto& p = seq.pattern();
        auto& project = studio.project();
        unsigned t = studio.track(), n = seq.number(t);
        switch (page) {
        case Page::Pattern:
            switch (k) {
            case 0: seq::set_length(p, uint32_t(std::max(1, int(seq::bars_of(p, seq.bar_ticks())) + delta)) * seq.bar_ticks(), project.max_length()); seq.sync(t, n); break;
            case 1: seq.set_division(delta); break;
            case 2: seq.set_signature(delta); break;
            case 3: seq.set_speed(delta); break;
            case 4: seq.set_pattern_tempo(delta); break;
            case 5: if (p.empty()) { p.free = delta > 0; seq.sync(t, n); } break;
            default: break;
            }
            break;
        case Page::Entry:
            if (k == 0) seq.entry_length = unsigned(std::clamp(int(seq.entry_length) + delta, 1, 64));
            if (k == 1) seq.entry_velocity = uint8_t(std::clamp(int(seq.entry_velocity) + delta, 0, 127));
            if (k == 2) seq.entry_chance = uint8_t(std::clamp(int(seq.entry_chance) + delta, 0, 100));
            break;
        case Page::Roll: roll_turn(k, delta); break;
        case Page::Mod: mod_turn(k, delta); break;
        case Page::Macros: macro = k; seq.turn_macro(k, delta); break;
        case Page::Feel: {
            auto& f = p.feel;
            switch (k) {
            case 0: f.strength = uint8_t(std::clamp(int(f.strength) + delta, 0, 100)); break;
            case 1: f.swing = uint8_t(std::clamp(int(f.swing) + delta, 50, 75)); break;
            case 2: f.humanize = uint8_t(std::clamp(int(f.humanize) + delta, 0, 100)); break;
            case 3: f.humanize_velocity = uint8_t(std::clamp(int(f.humanize_velocity) + delta, 0, 100)); break;
            case 4: seq.set_groove_from(delta); return;
            case 5: f.groove_amount = uint8_t(std::clamp(int(f.groove_amount) + delta, 0, 100)); break;
            default: return;
            }
            seq.sync(t, n);
            break;
        }
        case Page::Follow: {
            auto& f = p.follow;
            int actions = int(melodee::FollowAction::Count);
            switch (k) {
            case 0: f.action = uint8_t(std::clamp(int(f.action) + delta, 0, actions - 1)); break;
            case 1: f.count = uint8_t(std::clamp(int(f.count) + delta, 1, int(melodee::follow_most))); break;
            case 2: f.chance = uint8_t(std::clamp(int(f.chance) + delta, 0, 100)); break;
            case 3: f.other = uint8_t(std::clamp(int(f.other) + delta, 0, actions - 1)); break;
            case 4: f.plays = delta > 0; break;
            default: return;
            }
            seq.sync(t, n);
            break;
        }
        case Page::Lane:
            if (k == 0) seq.set_lane_steps(delta);
            if (k == 1) hits = unsigned(std::clamp(int(hits) + delta, 0, int(steps)));
            if (k == 2) { steps = unsigned(std::clamp(int(steps) + delta, 1, int(melodee::euclid_max_steps))); hits = std::min(hits, steps); }
            if (k == 3) rotate = unsigned(std::clamp(int(rotate) + delta, 0, int(steps) - 1));
            break;
        default: break;
        }
    }
    void roll_turn(unsigned k, int delta) {
        auto& seq = studio.sequencer();
        auto& p = seq.pattern();
        auto at = chosen();
        if (at.empty()) return;
        uint32_t q = p.q();
        bool kit = seq.kit();
        auto notes = melodee::take_events(p, at);
        for (auto& e : notes) {
            switch (k) {
            case 0: e.length = uint16_t(std::clamp(int(e.length) + delta * int(q) / 4, 1, 65535)); break;
            case 1: e.velocity = uint8_t(std::clamp(int(e.velocity) + delta, 1, 127)); break;
            case 2: e.chance = uint8_t(std::clamp(int(e.chance) + delta, 0, 100)); break;
            case 3: e.tick = uint32_t(std::max(0, int(e.tick) + delta)); break;
            case 4: e.ratchet = uint8_t(std::clamp(int(e.ratchet) + delta, 1, int(melodee::max_ratchet))); break;
            case 5:
                if (kit) e.pitch = int8_t(std::clamp(int(e.pitch) + 12 * delta, -melodee::max_pitch_ramp, melodee::max_pitch_ramp));
                else e.key = uint8_t(std::clamp(int(e.key) + 12 * delta, 0, 127));
                break;
            case 6: e.tick = uint32_t(std::max<int64_t>(0, int64_t(e.tick) + int64_t(delta) * q)); break;
            case 7: e.key = uint8_t(std::clamp(int(e.key) + delta, 0, 127)); break;
            default: break;
            }
        }
        // Back in, the focus following its note.
        auto focused = notes.front();
        melodee::put_events(p, notes);
        marks.clear();
        for (size_t n = 0; n < p.events.size(); ++n)
            if (p.events[n].tick == focused.tick && p.events[n].key == focused.key) { focus = int(n); break; }
        if (at.size() > 1) // the marks follow their notes
            for (const auto& e : notes)
                for (size_t n = 0; n < p.events.size(); ++n)
                    if (p.events[n] == e) { marks.insert(n); break; }
        edited();
    }
    void mod_turn(unsigned k, int delta) {
        auto& seq = studio.sequencer();
        auto& m = studio.project().modulators[mod];
        auto& target = m.targets[mod_target];
        auto lanes = seq.lanes_of(studio.track());
        switch (k) {
        case 0: m.kind = melodee::ModKind(std::clamp(int(m.kind) + delta, 0, int(melodee::ModKind::Count) - 1)); break;
        case 1: m.shape = melodee::LfoShape(std::clamp(int(m.shape) + delta, 0, int(melodee::LfoShape::Count) - 1)); break;
        case 2:
            if (m.sync) m.division = uint8_t(std::clamp(int(m.division) + delta, 0, int(melodee::lfo_divisions) - 1));
            else m.rate = uint8_t(std::clamp(int(m.rate) + delta, 0, 100));
            break;
        case 3: m.sync = delta > 0; break;
        case 4: target.depth = int8_t(std::clamp(int(target.depth) + delta, -100, 100)); break;
        case 5: {
            // The target: none, then each lane of the selected track.
            int at = -1;
            for (size_t n = 0; n < lanes.size(); ++n)
                if (target.used() && target.track == studio.track() && lanes[n].param == target.param && lanes[n].pad == target.pad) at = int(n);
            at = std::clamp(at + (delta > 0 ? 1 : -1), -1, int(lanes.size()) - 1);
            if (at < 0) target = melodee::ModTarget {};
            else target = {uint8_t(studio.track()), uint8_t(lanes[size_t(at)].param), uint8_t(lanes[size_t(at)].pad), target.depth,
                           int16_t(lanes[size_t(at)].low), int16_t(lanes[size_t(at)].high)};
            break;
        }
        case 6: target.low = int16_t(target.low + delta); break;
        case 7: target.high = int16_t(target.high + delta); break;
        default: return;
        }
        seq.send_modulator(mod);
    }
};

void SeqPages::draw(juce::Graphics& g) {
    auto& seq = studio.sequencer();
    auto& p = seq.pattern();
    auto& project = studio.project();
    unsigned t = studio.track(), n = seq.number(t);
    std::string place = std::to_string(unsigned(page) + 1) + "/" + std::to_string(page_count);
    std::string pattern_name = "PATTERN " + std::to_string(n + 1);
    std::array<std::string, 8> names {}, values {};
    // The cells' names, three letters each as the mockups have them; the touched knob shows the whole.
    static const char* const short_names[page_count][8] {
        {"LEN", "Q", "SIG", "SPD", "BPM", "FRE", "", ""}, {"LEN", "VEL", "CHN", "", "", "", "", ""},
        {"LEN", "VEL", "CHN", "NDG", "RTC", "OCT", "MOV", "KEY"}, {"", "", "", "", "", "", "", ""},
        {"KND", "SHP", "RAT", "SYN", "DPT", "TGT", "LOW", "HI"}, {"M1", "M2", "M3", "M4", "M5", "M6", "M7", "M8"},
        {"STR", "SWG", "HUM", "HVL", "GRV", "AMT", "", ""}, {"DO", "AFT", "CHN", "OR", "BY", "", "", ""},
        {"LAN", "HIT", "STP", "ROT", "", "", "", ""}};
    for (unsigned k = 0; k < 8; ++k) {
        auto shown = knob(k);
        names[k] = shown.name.empty() ? "" : short_names[unsigned(page)][k];
        values[k] = shown.value;
    }
    switch (page) {
    case Page::Pattern:
        ui::rail(g, "SEQ", std::to_string(n + 1), place);
        ui::seq::title(g, pattern_name, "PAT");
        ui::seq::value_cells(g, ui::content(11, 40), names, values, touched);
        ui::detail(g, "PUSH: HALF, DBL..");
        break;
    case Page::Entry: {
        ui::rail(g, "ENTR", std::to_string(seq.caret % seq.page_steps() + 1), place);
        ui::seq::title(g, "STEP " + std::to_string(seq.caret + 1), "BAR " + std::to_string(seq::step_tick(p, seq.caret) / seq.bar_ticks() + 1));
        unsigned first = seq.caret / 16 * 16;
        std::vector<bool> hits;
        for (unsigned s = 0; s < 16; ++s) hits.push_back(seq::any_in(p, seq::step_span(p, first + s), -1));
        auto row = ui::content(12, 7).reduced(1, 0);
        ui::seq::step_row(g, row, hits, 16);
        int cell = row.getWidth() / 16;
        g.fillRect(row.getX() + int(seq.caret - first) * cell, row.getBottom() + 1, cell - 1, 2); // the caret
        ui::seq::value_cells(g, ui::content(22, 40), names, values, touched);
        ui::detail(g, "PUSH:REST X:BACK");
        break;
    }
    case Page::Roll: {
        ui::rail(g, "ROLL", std::to_string(n + 1), place);
        std::vector<ui::seq::RollNote> notes;
        int low = 127, high = 0;
        uint32_t bar = seq.bar_ticks();
        uint32_t from = focus >= 0 && size_t(focus) < p.events.size() ? p.events[size_t(focus)].tick / bar * bar : 0;
        for (size_t k = 0; k < p.events.size(); ++k) {
            const auto& e = p.events[k];
            notes.push_back({e.tick, e.length, e.key, marks.count(k) != 0});
            if (e.tick >= from && e.tick < from + bar) { low = std::min(low, int(e.key)); high = std::max(high, int(e.key)); }
        }
        if (low > high) { low = 48; high = 59; }
        if (high - low < 11) { int middle = (low + high) / 2; low = std::max(0, middle - 6); high = low + 11; }
        int playhead = studio.playing() ? studio.host().position_of(t) : -1;
        ui::seq::roll(g, ui::content(1, 50), notes, from, bar, low, high, focus, playhead, bar);
        if (focus >= 0 && size_t(focus) < p.events.size()) {
            const auto& e = p.events[size_t(focus)];
            char text[32];
            std::snprintf(text, sizeof text, "%s V%u %u/%u", key_name(e.key).c_str(), unsigned(e.velocity), unsigned(e.length), p.q());
            ui::detail(g, text, "B" + std::to_string(e.tick / bar + 1));
        } else {
            ui::detail(g, "NO NOTES");
        }
        break;
    }
    case Page::Motion: {
        ui::rail(g, "MOTN", std::to_string(n + 1), place);
        auto rows = lanes();
        std::vector<std::string> lines;
        for (const auto& row : rows) {
            bool off = seq.lane_off(t, n, row.param, row.pad);
            std::string right = off ? "OFF" : row.locks && !row.points ? std::to_string(row.locks) + " LCK" : std::to_string(row.points) + " PTS";
            lines.push_back(padded(lane_name(row.param, row.pad).substr(0, 10), right));
        }
        if (lines.empty()) move::draw_text_centred(g, ui::content_x, 20, ui::content_width, "NO MOTION");
        else ui::list(g, ui::content(0, 50), lines, std::min<int>(motion_row, int(lines.size()) - 1), 1);
        ui::detail(g, "PUSH:OFF X:CLEAR");
        break;
    }
    case Page::Mod: {
        ui::rail(g, "MOD", std::to_string(mod + 1), place);
        ui::seq::lfo_shape(g, ui::content(1, 9).reduced(2, 0), unsigned(project.modulators[mod].shape));
        ui::seq::value_cells(g, ui::content(11, 40), names, values, touched);
        ui::detail(g, "WHEEL: MOD 1-8", "T" + std::to_string(mod_target + 1));
        break;
    }
    case Page::Macros:
        ui::rail(g, "MACR", std::to_string(macro + 1), place);
        ui::seq::title(g, "MACROS", std::to_string(project.macros[macro].target_count()) + " LN");
        ui::seq::value_cells(g, ui::content(11, 40), names, values, touched);
        ui::detail(g, "PUSH: LANES");
        break;
    case Page::Feel:
        ui::rail(g, "FEEL", std::to_string(n + 1), place);
        ui::seq::title(g, pattern_name, "LIVE");
        ui::seq::value_cells(g, ui::content(11, 40), names, values, touched);
        ui::detail(g, "PUSH: TO ALL");
        break;
    case Page::Follow:
        ui::rail(g, "FOLW", std::to_string(n + 1), place);
        ui::seq::title(g, pattern_name, melodee::follow_name(p.follow.action));
        ui::seq::value_cells(g, ui::content(11, 40), names, values, touched);
        ui::detail(g, "PUSH: COLUMN");
        break;
    case Page::Lane: {
        ui::rail(g, "LANE", pad_label(studio.pad()), place);
        auto rhythm = melodee::euclid(hits, steps, rotate);
        ui::seq::step_row(g, ui::content(2, 7).reduced(1, 0), rhythm, unsigned(rhythm.size()));
        ui::seq::value_cells(g, ui::content(11, 40), names, values, touched);
        ui::detail(g, "PUSH: LAY E(" + std::to_string(hits) + "," + std::to_string(steps) + ")");
        break;
    }
    case Page::Count: break;
    }
}

Knob SeqPages::knob(unsigned k) const {
    auto& seq = studio.sequencer();
    auto& p = seq.pattern();
    const auto& project = studio.project();
    unsigned t = studio.track(), n = seq.number(t);
    char text[24];
    auto fraction = [](int value, int low, int high) { return float(value - low) / float(std::max(1, high - low)); };
    switch (page) {
    case Page::Pattern:
        switch (k) {
        case 0: return {"LENGTH", p.is_free() ? "FREE" : std::to_string(seq::bars_of(p, seq.bar_ticks())) + "BR", seq::bars_of(p, seq.bar_ticks()) / 128.0f};
        case 1: return {"Q", melodee::division_name(p.division), fraction(int(p.division), 0, int(melodee::Division::Count) - 1)};
        case 2: { auto sig = project.time_signature(); return {"SIGNATURE", std::to_string(sig.beats) + "/" + std::to_string(sig.unit), fraction(project.signature, 0, int(melodee::time_signature_count) - 1)}; }
        case 3: return {"SPEED", k == 3 && project.tracks[t].speed == melodee::speed_normal ? "X1" : std::string("X") + (project.tracks[t].speed == 0 ? "1/2" : project.tracks[t].speed == 1 ? "3/4" : project.tracks[t].speed == 3 ? "1.5" : "2"), fraction(project.tracks[t].speed, 0, 4)};
        case 4: return {"BPM", project.pattern_tempo[n] ? tenths(project.pattern_tempo[n]) : "--", project.pattern_tempo[n] / 3000.0f};
        case 5: return {"FREE", p.is_free() ? "ON" : "OFF", p.is_free() ? 1.0f : 0.0f};
        default: return {};
        }
    case Page::Entry:
        switch (k) {
        case 0: return {"LENGTH", std::to_string(seq.entry_length), seq.entry_length / 64.0f};
        case 1: return {"VELOCITY", seq.entry_velocity ? std::to_string(seq.entry_velocity) : "HIT", seq.entry_velocity / 127.0f};
        case 2: return {"CHANCE", std::to_string(seq.entry_chance) + "%", seq.entry_chance / 100.0f};
        default: return {};
        }
    case Page::Roll: {
        if (focus < 0 || size_t(focus) >= p.events.size()) return {};
        const auto& e = p.events[size_t(focus)];
        switch (k) {
        case 0: std::snprintf(text, sizeof text, "%u", unsigned(e.length)); return {"LENGTH", text, e.length / 384.0f};
        case 1: return {"VELOCITY", std::to_string(e.velocity), e.velocity / 127.0f};
        case 2: return {"CHANCE", std::to_string(e.chance) + "%", e.chance / 100.0f};
        case 3: return {"NUDGE", std::to_string(int(e.tick % p.q())), float(e.tick % p.q()) / float(p.q())};
        case 4: return {"RATCHET", std::to_string(e.ratchet), e.ratchet / 8.0f};
        case 5: return {"OCTAVE", seq.kit() ? std::to_string(int(e.pitch)) : key_name(e.key), seq.kit() ? fraction(e.pitch, -24, 24) : e.key / 127.0f};
        case 6: return {"MOVE", "Q", 0.5f};
        case 7: return {"KEY", key_name(e.key), e.key / 127.0f};
        default: return {};
        }
    }
    case Page::Mod: {
        const auto& m = project.modulators[mod];
        const auto& target = m.targets[mod_target];
        switch (k) {
        case 0: return {"KIND", melodee::mod_kind_name(m.kind), fraction(int(m.kind), 0, 2)};
        case 1: return {"SHAPE", melodee::lfo_shape_name(m.shape), fraction(int(m.shape), 0, 6)};
        case 2: return {"RATE", m.sync ? upper(melodee::lfo_division_name(m.division)) : std::to_string(m.rate), m.sync ? m.division / 9.0f : m.rate / 100.0f};
        case 3: return {"SYNC", m.sync ? "ON" : "OFF", m.sync ? 1.0f : 0.0f};
        case 4: return {"DEPTH", std::to_string(target.depth), fraction(target.depth, -100, 100)};
        case 5: return {"TARGET", target.used() ? lane_name(target.param, target.pad).substr(0, 4) : "-", 0.0f};
        case 6: return {"LOW", std::to_string(target.low), 0.0f};
        case 7: return {"HIGH", std::to_string(target.high), 1.0f};
        default: return {};
        }
    }
    case Page::Macros: {
        const auto& m = project.macros[k % melodee::macro_count];
        return {"M" + std::to_string(k + 1), m.target_count() ? std::to_string(m.value) : "-", m.value / 127.0f};
    }
    case Page::Feel: {
        const auto& f = p.feel;
        switch (k) {
        case 0: return {"STRENGTH", std::to_string(f.strength), f.strength / 100.0f};
        case 1: return {"SWING", std::to_string(f.swing), fraction(f.swing, 50, 75)};
        case 2: return {"HUMANIZE", std::to_string(f.humanize), f.humanize / 100.0f};
        case 3: return {"HUMAN VEL", std::to_string(f.humanize_velocity), f.humanize_velocity / 100.0f};
        case 4: return {"GROOVE", f.groove_from ? "P" + std::to_string(melodee::groove_pattern(f.groove_from) + 1) : "-", 0.0f};
        case 5: return {"AMOUNT", std::to_string(f.groove_amount), f.groove_amount / 100.0f};
        default: return {};
        }
    }
    case Page::Follow: {
        const auto& f = p.follow;
        switch (k) {
        case 0: return {"DO", melodee::follow_name(f.action), fraction(f.action, 0, 5)};
        case 1: return {"AFTER", std::to_string(f.count), f.count / 64.0f};
        case 2: return {"CHANCE", std::to_string(f.chance) + "%", f.chance / 100.0f};
        case 3: return {"OR", melodee::follow_name(f.other), fraction(f.other, 0, 5)};
        case 4: return {"BY", f.plays ? "PLAY" : "BARS", f.plays ? 1.0f : 0.0f};
        default: return {};
        }
    }
    case Page::Lane: {
        uint8_t key = studio.current().step_note();
        switch (k) {
        case 0: return {"LANE", p.own_lane(key) ? std::to_string(p.lane_length(key) / p.q()) : "ALL", float(p.lane_length(key)) / float(std::max<uint32_t>(p.length, 1))};
        case 1: return {"HITS", std::to_string(hits), float(hits) / float(std::max(1u, steps))};
        case 2: return {"STEPS", std::to_string(steps), steps / 64.0f};
        case 3: return {"ROTATE", std::to_string(rotate), float(rotate) / float(std::max(1u, steps))};
        default: return {};
        }
    }
    case Page::Motion: case Page::Count: break;
    }
    return {};
}

// ---------------------------------------------------------------- the jobs sheets

// The pages under a sheet.
SeqPages* pages_of(const Studio& studio) { return dynamic_cast<SeqPages*>(studio.screen("seq-pages")); }

// PATTERN's push: Half and Double Seq and Speed, then the variations into
// the next empty pattern (knob 1 the amount).
class PatternJobs final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(Job job, const bindings::Use& use) override {
        auto& seq = studio.sequencer();
        auto* pages = pages_of(studio);
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: row = std::clamp(row + (use.delta > 0 ? 1 : -1), 0, int(rows().size()) - 1); return true;
        case Job::TurnKnob: case Job::TurnKnobFine:
            if (use.index == 0 && pages) pages->vary_amount = unsigned(std::clamp(int(pages->vary_amount) + use.delta, 0, 100));
            return true;
        case Job::ListPush:
            switch (row) {
            case 0: seq.half_seq(); break;
            case 1: {
                auto& p = seq.pattern();
                if (melodee::double_pattern(p, studio.project().max_length())) { p.free = false; seq.sync(studio.track(), seq.number(studio.track())); }
                break;
            }
            case 2: seq.half_speed(true); break;
            case 3: seq.half_speed(false); break;
            default: seq.vary(unsigned(row - 4), (pages ? pages->vary_amount : 50) / 100.0); break;
            }
            studio.close();
            return true;
        case Job::Back: studio.close(); return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override { lights.button(move::Button::Back, move::white_dim); }
    void draw(juce::Graphics& g) override {
        auto& seq = studio.sequencer();
        ui::rail(g, "SEQ", std::to_string(seq.number(studio.track()) + 1), "1/9");
        ui::list(g, ui::content(0, 50), rows(), row, 1);
        ui::detail(g, row < 4 ? "PUSH: DO IT" : "PUSH: TO P" + std::to_string(seq.next_empty(studio.track(), seq.number(studio.track())) + 1),
                   row < 4 ? "" : std::to_string(pages_of(studio) ? pages_of(studio)->vary_amount : 50) + "%");
    }
    Knob knob(unsigned which) const override {
        if (which || !pages_of(studio)) return {};
        return {"AMOUNT", std::to_string(pages_of(studio)->vary_amount) + "%", pages_of(studio)->vary_amount / 100.0f};
    }
    std::string status() const override { return " seq_jobs_row=" + std::to_string(row); }

private:
    int row = 0;
    std::vector<std::string> rows() const {
        std::vector<std::string> list {"HALF SEQ", "DOUBLE SEQ", "HALF SPEED", "DOUBLE SPEED"};
        for (unsigned v = 0; v < unsigned(melodee::Variation::Count); ++v) list.push_back(upper(melodee::variation_name(melodee::Variation(v))));
        return list;
    }
};

// ROLL's push: what to do with the marked notes (or the one in focus).
class RollJobs final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(Job job, const bindings::Use& use) override {
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: row = std::clamp(row + (use.delta > 0 ? 1 : -1), 0, int(names.size()) - 1); return true;
        case Job::ListPush: act(); studio.close(); return true;
        case Job::Back: studio.close(); return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override { lights.button(move::Button::Back, move::white_dim); }
    void draw(juce::Graphics& g) override {
        ui::rail(g, "ROLL", pages_of(studio) ? std::to_string(pages_of(studio)->marks.size()) : "", "MRK");
        ui::list(g, ui::content(0, 50), names, row, 1);
        ui::detail(g, "PUSH: DO IT");
    }

private:
    int row = 0;
    const std::vector<std::string> names {"MARK NOTE", "MARK THE BAR", "CLEAR MARKS", "MUTE", "PULL TO GRID", "LEGATO", "SPLIT", "REVERSE",
                                          "ARP AS NOTES", "LOOP THE BAR", "LOOP OFF", "ERASE"};
    void act() {
        auto* pages = pages_of(studio);
        if (!pages) return;
        auto& seq = studio.sequencer();
        auto& p = seq.pattern();
        uint32_t bar = seq.bar_ticks();
        int focus = pages->focus;
        bool any = focus >= 0 && size_t(focus) < p.events.size();
        switch (row) {
        case 0: if (any && !pages->marks.erase(size_t(focus))) pages->marks.insert(size_t(focus)); return;
        case 1:
            if (!any) return;
            for (size_t n = 0; n < p.events.size(); ++n)
                if (p.events[n].tick / bar == p.events[size_t(focus)].tick / bar) pages->marks.insert(n);
            return;
        case 2: pages->marks.clear(); return;
        case 9: { // the bar in view loops (Melodee's: loop the bars in view)
            if (!any) return;
            unsigned b = p.events[size_t(focus)].tick / bar;
            melodee::Command c {melodee::Command::Type::EditLoop, uint8_t(studio.track()), 0, uint16_t(bar & 0xffff), int16_t(uint16_t(bar >> 16))};
            c.position = b * bar;
            studio.send(c);
            studio.say("LOOP BAR " + std::to_string(b + 1));
            return;
        }
        case 10: {
            melodee::Command c {melodee::Command::Type::EditLoop, uint8_t(studio.track())};
            studio.send(c);
            studio.say("LOOP OFF");
            return;
        }
        default: break;
        }
        auto at = pages->chosen();
        if (at.empty()) return;
        auto notes = melodee::take_events(p, at);
        switch (row) {
        case 3: for (auto& e : notes) e.flags ^= melodee::step_mute; break;
        case 4: melodee::quantize_events(notes, p.q(), p.length, 100); break;
        case 5: melodee::legato_events(notes, p, false); break;
        case 6: melodee::split_events(notes, p.q()); break;
        case 7: melodee::reverse_events(notes); break;
        case 8: {
            const auto& track = studio.current();
            int top = 0;
            for (const auto& e : notes) top = std::max(top, int(e.key));
            notes = melodee::arp_events(notes, track.arp_mode, track.arp_rate, track.arp_octaves, track.arp_gate, p.length, -1, top);
            break;
        }
        case 11: notes.clear(); break;
        default: break;
        }
        melodee::put_events(p, notes);
        pages->marks.clear();
        pages->edited();
        studio.say(names[size_t(row)]);
    }
};

// MACROS' push: the lanes of the selected track the last macro moves, a push each in or out.
class MacroLanes final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(Job job, const bindings::Use& use) override {
        auto lanes = studio.sequencer().lanes_of(studio.track());
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: row = std::clamp(row + (use.delta > 0 ? 1 : -1), 0, int(lanes.size()) - 1); return true;
        case Job::ListPush: toggle(lanes[size_t(row)]); return true;
        case Job::Back: studio.close(); return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override { lights.button(move::Button::Back, move::white_dim); }
    void draw(juce::Graphics& g) override {
        unsigned which = pages_of(studio) ? pages_of(studio)->macro : 0;
        const auto& macro = studio.project().macros[which];
        auto lanes = studio.sequencer().lanes_of(studio.track());
        std::vector<std::string> lines;
        for (const auto& lane : lanes) lines.push_back(padded(lane.name, find(macro, lane) >= 0 ? "IN" : ""));
        ui::rail(g, "MACR", std::to_string(which + 1), "LANE");
        ui::list(g, ui::content(0, 50), lines, row, 1);
        ui::detail(g, "PUSH: IN / OUT");
    }

private:
    int row = 0;
    int find(const melodee::Macro& macro, const Sequencer::Lane& lane) const {
        for (unsigned n = 0; n < melodee::macro_targets; ++n) {
            const auto& t = macro.targets[n];
            if (t.used() && t.track == studio.track() && t.param == lane.param && t.pad == lane.pad) return int(n);
        }
        return -1;
    }
    void toggle(const Sequencer::Lane& lane) {
        unsigned which = pages_of(studio) ? pages_of(studio)->macro : 0;
        auto& macro = studio.project().macros[which];
        if (int at = find(macro, lane); at >= 0) { macro.targets[size_t(at)] = melodee::MacroTarget {}; studio.changed(); return; }
        for (auto& t : macro.targets)
            if (!t.used()) {
                t = {uint8_t(studio.track()), uint8_t(lane.param), uint8_t(lane.pad), int16_t(lane.low), int16_t(lane.high)};
                studio.changed();
                return;
            }
        studio.say("EIGHT LANES AT MOST");
    }
};

RegisterScreen pages("seq-pages", make<SeqPages>, {{bindings::Job::SeqPages}}); // Shift + Loop
RegisterScreen pattern_jobs("seq-jobs", make<PatternJobs>);
RegisterScreen roll_jobs("roll-jobs", make<RollJobs>);
RegisterScreen macro_lanes("macro-lanes", make<MacroLanes>);

} // namespace
} // namespace app::screens
