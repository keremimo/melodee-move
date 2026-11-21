// SPDX-License-Identifier: AGPL-3.0-only
// The Looper (docs/UI.md, "Looper"; docs/ui/looper.png), Shift + Step 12:
// four loops L1-L4 on loop tracks of their own, Melodee's Looper in the
// engine host (app/loop_desk.h asks it).
//
//   Off            asks first: push turns it on (its memory made meanwhile)
//   LOOPS          the four as rows, L1 on top as the track buttons and the
//                  pad rows sit; knobs 1-8 the selected loop's source,
//                  length, level, feedback, pan, send, and all four's
//                  quantize and after take
//   MIX            Shift + the wheel: the four strips; knobs 1-4 their
//                  levels, 5-8 their pans
//   Pads           a row a loop: record / overdub, play / stop on the
//                  boundary (Shift: at once), undo / redo, held to clear;
//                  the right 16 play the selected track
//   Track buttons  pick the loop; Mute (+ Shift) with one mutes (solos) it;
//                  held with the Volume knob, its level (the app's)
//   Rec            record, close, overdub, play the selected loop (a loop at
//                  another tempo is fitted first)
//   The wheel      picks the loop; its push opens the loop's sheet (MORE)
//   Back           back to Play; the loops play on
#include "app/screens/common.h"
#include "melodee/engine/engine_info.h"
#include "ui/looper_view.h"
#include <cmath>

namespace app::screens {
namespace {

namespace looper = melodee::looper;
using S = melodee::Looper::State;
using Do = looper::Do;
using bindings::Job;

std::string loop_tag(unsigned loop) { return "L" + std::to_string(loop % LoopDesk::loops + 1); }

const char* quantize_word(unsigned q) { static const char* const w[] {"BAR", "BEAT", "OFF"}; return w[q % 3]; }
const char* then_word(unsigned t) { static const char* const w[] {"PLAY", "OVERDUB", "STOP"}; return w[t % 3]; }

std::string level_db(int level) { return melodee::decibels(level) + " DB"; }
std::string pan_text(int pan) { return pan == 0 ? "C" : (pan < 0 ? "L" : "R") + std::to_string(std::abs(pan)); }

// A loop's state as its row says it: the word, inverted while it writes.
std::string state_word(const melodee::Looper::View& v) {
    if (v.fitting) return "FIT";
    return looper::state_word(v.state);
}
bool going(const melodee::Looper::View& v) { return v.state == S::Armed || v.state == S::Recording || v.state == S::Playing || v.state == S::Overdub; }

// The loop's length as its row says it: the set length before and during a
// take, its bars after, the tempo it was taken at when that is not now's.
std::string length_word(const Studio& studio, const melodee::Looper::View& v, unsigned set_bars) {
    if (v.state == S::Empty || v.state == S::Armed || v.state == S::Recording || v.ticks <= 0)
        return set_bars ? std::to_string(set_bars) + "B" : "FREE";
    if (v.stretched && v.tempo) return "@" + std::to_string((v.tempo + 5) / 10);
    unsigned bar = std::max(1u, studio.project().bar_ticks());
    return std::to_string(std::max(1u, unsigned(std::lround(v.ticks / bar)))) + "B";
}

ui::looper::Row row_of(const Studio& studio, unsigned loop, bool selected, bool blink_on) {
    const auto& desk = studio.loops();
    auto v = desk.view(loop);
    ui::looper::Row row;
    row.selected = selected;
    row.muted = desk.mix(loop).mute;
    row.length = length_word(studio, v, studio.project().looper.loops[loop].bars);
    if (v.state == S::Empty) return row;
    row.state = v.state == S::Armed && !blink_on ? "" : state_word(v);
    if (row.state.empty() && v.state == S::Armed) row.state = " "; // the word blinks; the row stays a loop's
    row.writing = v.state == S::Recording || v.state == S::Overdub;
    row.peaks.resize(looper::peak_columns);
    for (unsigned c = 0; c < looper::peak_columns; ++c) row.peaks[c] = studio.host().looper().peak(loop, c) / 255.0f;
    if (v.state == S::Armed) { row.peaks.clear(); return row; }
    if (v.state == S::Recording) {
        // A set take fills its strip as it runs; a free one is all strip, growing.
        row.span = v.bars && v.span ? std::clamp(float(v.position) / float(v.span), 0.0f, 1.0f) : 1.0f;
        row.head = v.bars ? row.span : -1;
        return row;
    }
    if (v.length && v.state != S::Stopped) row.head = float(v.position % v.length) / float(v.length);
    return row;
}

// The steps: the selected loop across its length, the play head white; a
// take running, the bar's sixteenths in red.
void loop_steps(const Studio& studio, move::Lights& lights) {
    using namespace move;
    const auto& desk = studio.loops();
    unsigned loop = desk.selected;
    auto v = desk.view(loop);
    auto colour = ui::looper::loop_colour(loop);
    int at = -1;
    bool taking = v.state == S::Recording;
    if (taking) {
        unsigned bar = std::max(1u, studio.project().bar_ticks());
        double tick = std::fmod(studio.host().looper().beat_tick(), double(bar));
        at = int(tick / bar * step_count) % int(step_count);
    } else if (v.length && (v.state == S::Playing || v.state == S::Overdub)) {
        at = int(uint64_t(v.position % v.length) * step_count / v.length);
    }
    static const uint8_t dim_red = nearest_colour({90, 0, 0});
    for (unsigned n = 0; n < step_count; ++n) {
        uint8_t shown = colour::off;
        if (at >= 0 && int(n) < at) shown = taking ? dim_red : colour.dim;
        if (int(n) == at) shown = taking ? colour::red : colour::white;
        lights.step(n, shown);
    }
}

// The track buttons as L1-L4, each in its colour, the selected one bright;
// an empty loop dark grey; muted grey, soloed blue, as the Move shows tracks.
void loop_buttons(const Studio& studio, move::Lights& lights) {
    using namespace move;
    static const Button rows[4] {Button::Row1, Button::Row2, Button::Row3, Button::Row4};
    const auto& desk = studio.loops();
    for (unsigned n = 0; n < LoopDesk::loops; ++n) {
        auto colour = ui::looper::loop_colour(n);
        const auto& m = desk.mix(n);
        bool selected = n == desk.selected, empty = desk.view(n).state == S::Empty;
        uint8_t shown = m.solo ? colour::blue : m.mute ? colour::dark_grey : empty ? (selected ? colour::light_grey : colour::dark_grey)
                      : selected ? colour.bright : colour.dim;
        lights.button(rows[n], shown);
    }
}

// The pads, by their logical numbers (app/pad_layout.h): the loops' rows
// (L1 on top), and the selected track's 16 (LoopDesk::pad_at()).
void loop_pads(const Studio& studio, move::Lights& lights, const std::array<bool, pad_count>& held, bool blink_on) {
    using namespace move;
    const auto& desk = studio.loops();
    auto layout = pad_layout();
    static const uint8_t dim_red = nearest_colour({90, 0, 0});
    auto now = Studio::Clock::now();
    for (unsigned loop = 0; loop < LoopDesk::loops; ++loop) {
        auto v = desk.view(loop);
        auto colour = ui::looper::loop_colour(loop);
        auto at = [&](unsigned job) { return physical_pad(LoopDesk::loop_pad(loop, job, layout), layout); };
        bool audio = v.length > 0 && v.state != S::Empty;
        // Record: red recording (blinking armed), orange overdubbing, the loop's colour dim otherwise.
        uint8_t record = v.state == S::Recording ? colour::red : v.state == S::Armed ? (blink_on ? colour::red : colour::off)
                       : v.state == S::Overdub ? colour::orange : v.fitting ? (blink_on ? colour.bright : colour.dim) : colour.dim;
        // Play / stop: the loop's colour playing, white on the bar; dim stopped; blinking with a change waiting.
        uint8_t play = colour::off;
        if (audio) {
            bool on_bar = false;
            if (v.state == S::Playing || v.state == S::Overdub) {
                unsigned bar = std::max(1u, studio.project().bar_ticks());
                on_bar = studio.playing() && std::fmod(studio.host().looper().beat_tick(), double(bar)) < bar / 16.0;
            }
            play = v.pending != melodee::Looper::Pending::None ? (blink_on ? colour.bright : colour::off)
                 : v.state == S::Playing || v.state == S::Overdub ? (on_bar ? colour::white : colour.bright) : colour.dim;
        }
        // Undo: white when there is one, dim grey for redo.
        uint8_t undo = v.undo == looper::Undo::Undo ? colour::light_grey : v.undo == looper::Undo::Redo ? colour::dark_grey : colour::off;
        // Clear: dim red with audio, red while it is held.
        uint8_t clear = !audio ? colour::off : desk.clearing() == int(loop) ? (desk.clear_progress(now) > 0.5f || blink_on ? colour::red : dim_red) : dim_red;
        lights.pad(at(LoopDesk::RecordPad), record);
        lights.pad(at(LoopDesk::PlayPad), play);
        lights.pad(at(LoopDesk::UndoPad), undo);
        lights.pad(at(LoopDesk::ClearPad), clear);
    }
    // The right 16: a kit's bank of 16 (the last pad's), each with a sample in
    // the track's colour, the last white; a note track 16 notes up from C.
    auto kind = studio.kind();
    auto colour = ui::track_colour(studio.track());
    const auto& track = studio.current();
    unsigned bank = track.last_pad / 16 % 2;
    for (unsigned at = 0; at < 16; ++at) {
        unsigned pad = physical_pad(LoopDesk::track_pad(at, layout), layout);
        uint8_t shown = colour::off;
        if (kind == TrackKind::Kit) {
            unsigned kit = bank * 16 + at;
            shown = kit == track.last_pad ? colour::white : track.pads[kit].used() ? colour.bright : colour::off;
        } else if (kind == TrackKind::Synth || kind == TrackKind::Keys || kind == TrackKind::Midi || kind == TrackKind::Plugin) {
            shown = at % 12 == 0 ? colour.bright : natural(int(at)) ? colour::dark_grey : colour::off;
        }
        if (held[pad]) shown = colour::white;
        lights.pad(pad, shown);
    }
}

// Where a physical pad sits on the Looper, in the layout chosen.
LoopDesk::PadAt looper_pad(unsigned physical) {
    auto layout = pad_layout();
    return LoopDesk::pad_at(logical_pad(uint8_t(physical), layout), layout);
}

void looper_buttons(const Studio& studio, move::Lights& lights, bool blink_on) {
    using namespace move;
    const auto& desk = studio.loops();
    const auto& looper = studio.host().looper();
    for (auto b : {Button::Left, Button::Right, Button::Loop, Button::Menu, Button::Capture, Button::Copy, Button::Delete, Button::Undo})
        lights.button(b, 0);
    lights.button(Button::Back, white_dim);
    lights.button(Button::Mute, desk.mix(desk.selected).mute ? white_bright : white_dim);
    bool notes = studio.kind() != TrackKind::Kit && studio.kind() != TrackKind::Empty && studio.kind() != TrackKind::Audio;
    lights.button(Button::Up, notes ? white_dim : 0);
    lights.button(Button::Down, notes ? white_dim : 0);
    // Rec: red while a loop records or overdubs, blinking while one waits; dim when it can.
    lights.button(Button::Rec, looper.recording() ? colour::red : looper.armed() ? (blink_on ? colour::red : colour::off)
                             : desk.on() ? colour::dark_grey : colour::off);
    lights.button(Button::Sample, 0);
}

bool blink() { return std::chrono::duration_cast<std::chrono::milliseconds>(Studio::Clock::now().time_since_epoch()).count() / 250 % 2 == 0; }

// A knob on a list of choices moves one a few detents.
struct Detents {
    int left = 0;
    int step(int delta) {
        left += delta;
        int moved = left / 3;
        left -= moved * 3;
        return moved;
    }
};

// What the Looper's screens share: the loop's knobs, its track buttons,
// mute and solo, the pads' MIDI-thread side.
class LooperBase : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::Looper; }

    void input(const move::Input& in) override {
        using Kind = move::Input::Kind;
        if (in.kind == Kind::Pad && in.index < pad_count) {
            held[in.index] = in.pressed;
            auto at = looper_pad(in.index);
            if (!in.pressed && at.loop >= 0 && at.job == LoopDesk::ClearPad) // a clear let go early does nothing
                studio.loops().pad(unsigned(at.loop), LoopDesk::ClearPad, false, Studio::Clock::now());
        }
    }

    bool job(Job job, const bindings::Use& use) override {
        auto& desk = studio.loops();
        switch (job) {
        case Job::Looper: studio.home(); return true; // Shift + Step 12 again: back to Play
        case Job::LoopPad: case Job::LoopPadNow: { // the audio thread did the rest, in the cycle it was read
            auto at = looper_pad(unsigned(use.index));
            if (at.loop >= 0) desk.pad(unsigned(at.loop), at.job, true, Studio::Clock::now());
            return true;
        }
        case Job::PlayPad: { // the track's 16 on a kit: the pad played becomes the last pad
            auto at = looper_pad(unsigned(use.index));
            if (studio.kind() == TrackKind::Kit && at.track >= 0) studio.select_pad(studio.current().last_pad / 16 % 2 * 16 + unsigned(at.track));
            return true;
        }
        case Job::PlayNote: return true;
        case Job::SelectLoop: desk.select(unsigned(use.index)); return true;
        case Job::MuteLoop: {
            unsigned loop = use.control == bindings::Control::TrackButton ? unsigned(use.index) : desk.selected;
            desk.set_mute(loop, !desk.mix(loop).mute);
            return true;
        }
        case Job::SoloLoop: desk.set_solo(unsigned(use.index), !desk.mix(unsigned(use.index)).solo); return true;
        case Job::LoopRecord:
            if (!desk.on()) { studio.say("LOOPER IS OFF"); return true; }
            desk.ask(Do::Primary, desk.selected);
            return true;
        case Job::TurnKnob: case Job::TurnKnobFine: turn(unsigned(use.index), use.delta); return true;
        default:
            // A loop's pad with Mute, Copy, Delete or a step held: nothing (the Play screen's jobs are for its own pads).
            return use.control == bindings::Control::Pad && looper_pad(unsigned(use.index)).loop >= 0;
        }
    }

    Knob knob(unsigned which) const override {
        last_knob = which;
        const auto& desk = studio.loops();
        unsigned loop = desk.selected;
        const auto& lp = studio.project().looper;
        const auto& m = desk.mix(loop);
        std::string tag = loop_tag(loop) + " ";
        if (mix_page()) {
            unsigned n = which % 4;
            const auto& strip = desk.mix(n);
            if (which < 4) return {loop_tag(n) + " LEVEL", level_db(strip.level), strip.level / 127.0f};
            return {loop_tag(n) + " PAN", pan_text(strip.pan), (strip.pan + 50) / 100.0f};
        }
        switch (which) {
        case 0: return {tag + "SOURCE", LoopDesk::source_name(lp.loops[loop].source), lp.loops[loop].source / float(looper::source_count - 1)};
        case 1: {
            unsigned bars = lp.loops[loop].bars;
            auto at = std::find(looper::lengths.begin(), looper::lengths.end(), bars) - looper::lengths.begin();
            return {tag + "LENGTH", LoopDesk::length_name(bars), float(at) / float(looper::lengths.size() - 1)};
        }
        case 2: return {tag + "LEVEL", level_db(m.level), m.level / 127.0f};
        case 3: return {tag + "FEEDBACK", std::to_string(lp.loops[loop].feedback) + "%", lp.loops[loop].feedback / 100.0f};
        case 4: return {tag + "PAN", pan_text(m.pan), (m.pan + 50) / 100.0f};
        case 5: return {tag + "SEND", std::to_string(m.send) + " " + std::string(1, char('A' + m.bus)), m.send / 100.0f};
        case 6: return {"QUANTIZE", quantize_word(lp.quantize), lp.quantize / 2.0f};
        case 7: return {"AFTER TAKE", then_word(lp.then), lp.then / 2.0f};
        default: return {};
        }
    }
    std::string knob_owner() const override {
        if (mix_page()) return loop_tag(last_knob % 4);
        return last_knob >= 6 ? "ALL" : loop_tag(studio.loops().selected);
    }

protected:
    virtual bool mix_page() const { return false; }
    std::array<bool, pad_count> held {};
    mutable unsigned last_knob = 0;

    void turn(unsigned which, int delta) {
        auto& desk = studio.loops();
        unsigned loop = desk.selected;
        const auto& m = desk.mix(loop);
        if (mix_page()) {
            unsigned n = which % 4;
            if (which < 4) desk.set_level(n, desk.mix(n).level + delta);
            else desk.set_pan(n, desk.mix(n).pan + delta);
            return;
        }
        int steps = which == 0 || which == 1 || which >= 6 ? detents[which].step(delta) : 0;
        const auto& lp = studio.project().looper;
        switch (which) {
        case 0: if (steps) desk.step_source(loop, steps); break;
        case 1: if (steps) desk.step_length(loop, steps); break;
        case 2: desk.set_level(loop, m.level + delta); break;
        case 3: desk.set_feedback(loop, lp.loops[loop].feedback + delta); break;
        case 4: desk.set_pan(loop, m.pan + delta); break;
        case 5: desk.set_send(loop, m.send + delta); break;
        case 6: if (steps) desk.set_quantize(std::clamp(int(lp.quantize) + steps, 0, 2)); break;
        case 7: if (steps) desk.set_then(std::clamp(int(lp.then) + steps, 0, 2)); break;
        default: break;
        }
    }

    void base_lights(move::Lights& lights) {
        bool on = blink();
        loop_buttons(studio, lights);
        loop_pads(studio, lights, held, on);
        loop_steps(studio, lights);
        looper_buttons(studio, lights, on);
    }

private:
    std::array<Detents, 8> detents {};
};

// ---------------------------------------------------------------- the Looper

class Looper final : public LooperBase {
public:
    using LooperBase::LooperBase;

    bool job(Job job, const bindings::Use& use) override {
        auto& desk = studio.loops();
        switch (job) {
        case Job::PickLoop:
            if (desk.on()) desk.select(unsigned(std::clamp(int(desk.selected) + use.delta, 0, int(LoopDesk::loops) - 1)));
            return true;
        case Job::LoopSheet:
            if (!desk.on()) desk.set_on(true);
            else studio.push("looper-more");
            return true;
        case Job::Page: page = std::clamp(page + use.delta, 0, 1); return true;
        case Job::Back: studio.close(); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: if (!desk.on()) return true; break;
        default: break;
        }
        return LooperBase::job(job, use);
    }

    void lights(move::Lights& lights) override {
        base_lights(lights);
        if (!studio.loops().on()) {
            for (unsigned loop = 0; loop < LoopDesk::loops; ++loop)
                for (unsigned job = 0; job < LoopDesk::PadJobs; ++job) lights.pad(physical_pad(LoopDesk::loop_pad(loop, job, pad_layout()), pad_layout()), 0);
            for (unsigned n = 0; n < move::step_count; ++n) lights.step(n, 0);
        }
    }

    void draw(juce::Graphics& g) override {
        auto& desk = studio.loops();
        if (!desk.on()) {
            ui::rail(g, "LOOP", "", "OFF");
            move::draw_text(g, ui::content_x + 2, 2, "LOOPER", 2);
            move::draw_text(g, ui::content_x + 2, 22, "4 LOOPS, L1-L4");
            move::draw_text(g, ui::content_x + 2, 32, "45 MB EACH");
            ui::detail(g, "PUSH: TURN ON");
            return;
        }
        unsigned loop = desk.selected;
        const auto& looper = studio.host().looper();
        if (mix_page()) {
            ui::rail(g, "MIX", loop_tag(loop), "2/2");
            std::array<ui::looper::Strip, 4> strips {};
            for (unsigned n = 0; n < LoopDesk::loops; ++n) {
                const auto& m = desk.mix(n);
                strips[n] = {m.level / 127.0f, std::min(1.0f, studio.host().loop_meter(n)), m.pan / 50.0f, m.mute, m.solo, n == loop};
            }
            ui::looper::strips(g, ui::content(0, ui::subject_height), strips);
            const auto& m = desk.mix(loop);
            ui::detail(g, level_db(m.level), "SEND " + std::to_string(m.send));
            return;
        }
        bool on = blink();
        std::array<ui::looper::Row, 4> rows;
        for (unsigned n = 0; n < LoopDesk::loops; ++n) rows[n] = row_of(studio, n, n == loop, on);
        ui::rail(g, "LOOP", loop_tag(loop), quantize_word(looper.quantize()));
        ui::looper::rows(g, ui::content(0, ui::subject_height), rows);
        auto v = desk.view(loop);
        std::string left = LoopDesk::source_name(v.source), right = v.layers > 1 ? "x" + std::to_string(v.layers) : bar_beat(studio);
        if (!desk.memory_ready()) { left = "MAKING MEMORY"; right = std::to_string(desk.memory_made() / 2) + "/4"; }
        else if (v.stretched && !v.fitting && (v.state == S::Playing || v.state == S::Stopped)) left = "REC: FIT, DUB";
        else if (looper.armed() && !studio.playing()) { left = "PLAY: COUNT-IN"; right.clear(); }
        ui::detail(g, left, right);
    }

    std::string status() const override {
        return std::string(" looper_page=") + (studio.loops().on() ? (page ? "mix" : "loops") : "off");
    }

protected:
    bool mix_page() const override { return page == 1; }

private:
    int page = 0;
};

// ---------------------------------------------------------------- the loop's sheet (MORE)

class LoopSheet final : public LooperBase {
public:
    using LooperBase::LooperBase;
    enum Item { Toggle, Now, Commit, CommitNote, Bus, Clear, Off, Delay, Items };

    void enter() override { cursor = 0; }

    bool job(Job job, const bindings::Use& use) override {
        switch (job) {
        case Job::PickLoop: cursor = std::clamp(cursor + use.delta, 0, int(Items) - 1); return true;
        case Job::LoopSheet: act(Item(cursor)); return true;
        case Job::Page: return true;
        case Job::Back: studio.close(); return true;
        default: return LooperBase::job(job, use);
        }
    }

    void lights(move::Lights& lights) override { base_lights(lights); }

    void draw(juce::Graphics& g) override {
        unsigned loop = studio.loops().selected;
        auto v = studio.loops().view(loop);
        const char* word = looper::state_word(v.state);
        ui::rail(g, "MORE", loop_tag(loop), *word ? word : "----");
        ui::list(g, ui::content(0, ui::height), items(), cursor, 1);
    }

    std::string status() const override { return " loop_sheet=" + std::to_string(cursor); }

private:
    int cursor = 0;

    std::vector<std::string> items() const {
        const auto& desk = studio.loops();
        unsigned loop = desk.selected;
        auto v = desk.view(loop);
        std::string when = studio.host().looper().quantize() == 0 ? " NEXT BAR" : studio.host().looper().quantize() == 1 ? " NEXT BEAT" : " AT ONCE";
        std::string toggle = v.pending != melodee::Looper::Pending::None ? "CANCEL" : std::string(going(v) ? "STOP" : "PLAY") + when;
        std::string now = going(v) ? "STOP NOW" : "PLAY NOW";
        int pad = desk.commit_pad();
        std::string commit = studio.kind() != TrackKind::Kit ? "COMMIT: NO KIT" : pad < 0 ? "COMMIT: KIT FULL" : "COMMIT TO " + pad_label(unsigned(pad));
        std::string delay = desk.measuring_now() ? "MEASURING " + std::to_string(desk.measured()) + "/4" : "IN DELAY " + std::to_string(desk.latency());
        return {toggle, now, commit, "COMMIT + NOTE", "SEND TO BUS " + std::string(1, char('A' + desk.mix(loop).bus)), "CLEAR " + loop_tag(loop), "LOOPER OFF", delay};
    }

    void act(Item item) {
        auto& desk = studio.loops();
        unsigned loop = desk.selected;
        auto v = desk.view(loop);
        bool audio = v.length > 0 && v.state != S::Empty;
        switch (item) {
        case Toggle:
            if (!audio) { studio.say(loop_tag(loop) + " IS EMPTY"); return; }
            desk.ask(Do::Toggle, loop);
            return;
        case Now:
            if (going(v)) desk.ask(Do::Halt, loop); // a take closes on its whole bars
            else if (audio) desk.ask(Do::Now, loop);
            else studio.say(loop_tag(loop) + " IS EMPTY");
            return;
        case Commit: case CommitNote: desk.commit(loop, item == CommitNote); return;
        case Bus: desk.set_bus(loop, (desk.mix(loop).bus + 1) % melodee::bus_count); return;
        case Clear:
            if (v.state == S::Empty) { studio.say(loop_tag(loop) + " IS EMPTY"); return; }
            asking(LoopAsk::ClearLoop);
            return;
        case Off:
            if (desk.holding()) { asking(LoopAsk::TurnOff); return; }
            desk.set_on(false);
            studio.close();
            return;
        case Delay: desk.measure(); return; // a cable from the output into the line in: clicks out, heard back
        default: return;
        }
    }

public:
    // What the question asks (the "looper-ask" screen reads it).
    enum class LoopAsk { ClearLoop, TurnOff };
    static LoopAsk asked;
    void asking(LoopAsk what) { asked = what; studio.push("looper-ask"); }
};
LoopSheet::LoopAsk LoopSheet::asked = LoopSheet::LoopAsk::ClearLoop;

// ---------------------------------------------------------------- the question

class LoopQuestion final : public LooperBase {
public:
    using LooperBase::LooperBase;

    bool job(Job job, const bindings::Use& use) override {
        auto& desk = studio.loops();
        switch (job) {
        case Job::LoopSheet: // yes
            if (LoopSheet::asked == LoopSheet::LoopAsk::ClearLoop) {
                if (desk.ask(Do::Clear, desk.selected)) studio.say(loop_tag(desk.selected) + " CLEARED");
                studio.close();
            } else {
                desk.set_on(false);
                studio.home();
            }
            return true;
        case Job::Back: studio.close(); return true; // no
        case Job::PickLoop: case Job::Page: case Job::SelectLoop: return true;
        default: return LooperBase::job(job, use);
        }
    }

    void lights(move::Lights& lights) override { base_lights(lights); }

    void draw(juce::Graphics& g) override {
        auto& desk = studio.loops();
        unsigned loop = desk.selected;
        if (LoopSheet::asked == LoopSheet::LoopAsk::ClearLoop) {
            auto v = desk.view(loop);
            const char* word = looper::state_word(v.state);
            ui::rail(g, "CLR", loop_tag(loop), *word ? word : "----");
            unsigned bars = std::max(1u, unsigned(std::lround(v.ticks / std::max(1u, studio.project().bar_ticks()))));
            std::string what = v.ticks > 0 ? std::to_string(bars) + (bars == 1 ? " BAR, " : " BARS, ") + std::to_string(v.layers) + (v.layers == 1 ? " LAYER" : " LAYERS") : "ITS TAKE";
            ui::looper::question(g, "CLEAR " + loop_tag(loop) + "?", what, "UNDO BRINGS NONE");
        } else {
            ui::rail(g, "LOOP", "", "OFF");
            unsigned n = desk.holding();
            ui::looper::question(g, "TURN IT OFF?", std::to_string(n) + (n == 1 ? " LOOP IS LOST" : " LOOPS ARE LOST"), "THEIR FILES TOO");
        }
    }
};

RegisterScreen looper_screen("looper", make<Looper>, {{bindings::Job::Looper}}); // Shift + Step 12
RegisterScreen loop_sheet("looper-more", make<LoopSheet>);
RegisterScreen loop_question("looper-ask", make<LoopQuestion>);

} // namespace
} // namespace app::screens
