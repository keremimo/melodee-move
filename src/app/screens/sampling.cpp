// SPDX-License-Identifier: AGPL-3.0-only
// Sampling (docs/UI.md "Sampling", docs/ui/sampling.png): the Move's
// Sampling button as Melodee's Sample Record, on a kit track. Press a pad to
// record onto it; the pad again, Sampling, the wheel's push or Play stopping
// ends the take (held 0.4 s, the pad ends it as it is let go); another pad
// carries the take on onto that pad. The wheel walks the input's rows
// (source, gain, monitor, start, length) as the Move's walks gain,
// monitoring and source, its push steps the row on (gain and start: push,
// then the wheel); knobs 1-6 set them; Shift + the wheel turns to the
// SAMPLING settings. Back leaves, keeping a take. The take itself is
// src/app/sampling.cpp's; Shift + Sampling (Audio Recall) is the "recall"
// view at the end of this file.
#include "app/sampling.h"
#include "app/screens/common.h"
#include <cmath>

namespace app::screens {
namespace {

using Clock = std::chrono::steady_clock;
using melodee::InputSource;
using Phase = Sampling::Phase;
constexpr auto rows_linger = std::chrono::milliseconds(2500); // the rows stay this long after the wheel
constexpr auto done_linger = std::chrono::milliseconds(3000); // the take kept, shown this long

// A level (linear peak) on the meter: -72 dB to 0.
float meter_of(float level) { return level <= 0 ? 0.0f : std::clamp((20.0f * std::log10(level) + 72.0f) / 72.0f, 0.0f, 1.0f); }
std::string row(const std::string& name, const std::string& value) {
    std::string text = name;
    while (text.size() + value.size() < 16) text += ' ';
    return text + value;
}
std::string clock_text(uint32_t frames) { // m:ss.d
    unsigned tenths = unsigned(uint64_t(frames) * 10 / 44100);
    char text[16];
    std::snprintf(text, sizeof text, "%u:%02u.%u", tenths / 600, tenths / 10 % 60, tenths % 10);
    return text;
}
void record_light(juce::Graphics& g) {
    g.setColour(juce::Colours::black);
    ui::dot(g, 12, 30, 5);
    g.setColour(juce::Colours::white);
}
bool pulse_on() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count() / 300 % 2 == 0; }

class SamplingScreen final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        if (studio.kind() != TrackKind::Kit) { studio.say("SAMPLING: KIT TRACKS"); studio.close(); return; }
        page = 0;
        editing = false;
        rows_until = {};
        seen_take = sampling().last_name;
    }
    void leave() override {
        if (sampling().taking()) sampling().stop(); // the take ends, kept
        editing = false;
    }

    bindings::Screen screen_kind() const override { return bindings::Screen::Sampling; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        auto& s = sampling();
        auto now = Clock::now();
        switch (job) {
        case Job::RecordPad:
            pressed_at[size_t(use.index) % pad_count] = now;
            done_until = {};
            s.press_pad(unsigned(use.index));
            return true;
        case Job::RecordPadUp: s.release_pad(unsigned(use.index), now - pressed_at[size_t(use.index) % pad_count]); return true;
        case Job::SamplingMode:
            if (s.taking()) s.stop(); // Sampling while taking: the take ends; again, Sampling Mode is left
            else studio.close();
            return true;
        case Job::AudioRecall: studio.push("recall"); return true;
        case Job::InputTurn:
            if (page == 1) { settings_cursor = std::clamp(settings_cursor + use.delta, 0, int(settings_rows().size()) - 1); return true; }
            if (editing) step_row(cursor, use.delta);
            else cursor = std::clamp(cursor + use.delta, 0, int(input_rows().size()) - 1);
            rows_until = now + rows_linger;
            return true;
        case Job::InputPush:
            if (s.taking()) { s.stop(); return true; } // Melodee's Push ends a take
            if (page == 1) { step_setting(settings_cursor, 1, true); return true; }
            rows_until = now + rows_linger;
            if (cursor == 1 || cursor == 3) editing = !editing; // gain, start: push, then the wheel
            else step_row(cursor, 1, true);
            return true;
        case Job::ShowSource: studio.say(s.source_name() + " " + s.gain_text()); return true;
        case Job::TurnKnob: case Job::TurnKnobFine:
            if (page == 1) step_setting(use.index, use.delta, false);
            else if (use.index < int(input_rows().size())) { step_row(use.index, use.delta); rows_until = now + rows_linger; cursor = use.index; }
            return true;
        case Job::Page: page = page ? 0 : 1; editing = false; return true;
        case Job::Back:
            if (editing) { editing = false; return true; }
            if (page == 1) { page = 0; return true; }
            studio.close(); // leave(): a take ends, kept
            return true;
        case Job::PlayPad: case Job::SelectPadQuietly: case Job::Focus: case Job::OpenBrowser: case Job::SampleOptions: return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        auto& s = sampling();
        bool pulse = pulse_on();
        const auto& kit = studio.current();
        auto colour = ui::track_colour(studio.track());
        if (s.pads_play_source()) { // the pads are the source's: its notes, or the kit
            unsigned source = s.input.source == InputSource::Track ? s.input.track : studio.track();
            auto kind = studio.kind(source);
            auto source_colour = ui::track_colour(source).bright;
            for (unsigned at = 0; at < pad_count; ++at) {
                if (kind == TrackKind::Kit) {
                    lights.pad(at, studio.project().tracks[source].pads[at].used() ? source_colour : colour::off);
                    continue;
                }
                int note = studio.note_for(at, studio.octave(source));
                lights.pad(at, note % 12 == 0 ? source_colour : natural(note) ? colour::dark_grey : colour::off);
            }
        } else {
            // Every pad can take a sample: the empty ones pink, the rest in the kit's colour (recorded over).
            const auto& parts = s.take_pads();
            for (unsigned at = 0; at < pad_count; ++at) {
                auto in_take = std::find(parts.begin(), parts.end(), uint8_t(at));
                uint8_t shown = kit.pads[at].used() ? colour.dim : pink;
                if (in_take != parts.end()) shown = in_take + 1 == parts.end() && pulse ? colour::off : colour::red; // the one taking pulses
                if (in_take != parts.end() && in_take + 1 == parts.end() && !s.taking()) shown = colour::red;
                lights.pad(at, shown);
            }
        }
        steps_playhead(studio, lights);
        lights.button(Button::Sample, s.taking() && pulse ? colour::off : colour::red);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        auto& s = sampling();
        auto now = Clock::now();
        if (s.last_name != seen_take) { seen_take = s.last_name; done_until = now + done_linger; }
        auto phase = s.phase();
        if (page == 1) { draw_settings(g); return; }
        if (phase == Phase::Recording) { draw_taking(g); return; }
        if (phase == Phase::Armed || phase == Phase::Cued) { draw_armed(g, phase == Phase::Cued); return; }
        if (phase == Phase::Saving) {
            ui::rail(g, "REC", "", pad_label(unsigned(std::max(s.take_pad(), 0))));
            move::draw_text_centred(g, ui::content_x, 18, ui::content_width, "SAVING", 2);
            return;
        }
        if (editing || now < rows_until) { draw_rows(g); return; }
        if (now < done_until && !s.last_pads.empty()) { draw_done(g); return; }
        ui::rail(g, "SMPL", "", s.source_tag());
        record_light(g);
        bool own = melodee::internal_source(s.input.source);
        move::draw_text_centred(g, ui::content_x, 2, ui::content_width, "PRESS", 2);
        move::draw_text_centred(g, ui::content_x, 20, ui::content_width, "A PAD", 2);
        level(g, 38);
        ui::detail(g, own ? s.source_name() : s.source_tag() + " " + s.gain_text(), s.length_text());
    }

    Knob knob(unsigned which) const override {
        auto& s = sampling();
        if (page == 1) {
            auto rows = settings_rows();
            if (which >= rows.size()) return {};
            return {rows[which].first, rows[which].second, 0};
        }
        switch (which) {
        case 0: return {"SOURCE", s.source_name(), 0};
        case 1: return {"GAIN", s.gain_text(), (s.input.gain + 12) / 36.0f};
        case 2: return {"MONITOR", s.monitor_text(), float(s.input.monitor) / 2.0f};
        case 3: return {"START", s.threshold_text(), s.input.at_once() ? 0.0f : (s.input.threshold + 72) / 72.0f};
        case 4: return {"LENGTH", s.length_text(), float(s.input.length) / float(melodee::rec_lengths - 1)};
        case 5: if (s.input.source == InputSource::Track) return {"TAP", s.input.post ? "POST FX" : "PRE FX", s.input.post ? 1.0f : 0.0f};
            return {};
        default: return {};
        }
    }
    std::string knob_owner() const override { return "SMPL"; }

private:
    Sampling& sampling() const { return studio.sampling(); }

    std::vector<std::string> input_rows() const {
        auto& s = sampling();
        std::vector<std::string> rows {row("SOURCE", s.source_name()), row("GAIN", s.gain_text()), row("MONITOR", s.monitor_text()),
                                       row("START", s.threshold_text()), row("LENGTH", s.length_text())};
        if (s.input.source == InputSource::Track) rows.push_back(row("TAP", s.input.post ? "POST FX" : "PRE FX"));
        if (editing && cursor < int(rows.size())) {
            auto value = cursor == 1 ? s.gain_text() : s.threshold_text();
            rows[size_t(cursor)] = row(cursor == 1 ? "GAIN" : "START", "[" + value + "]");
        }
        return rows;
    }
    // A row stepped: by the wheel, a knob, or the push (`wraps`: past the end back to the start).
    void step_row(int which, int delta, bool wraps = false) {
        auto& s = sampling();
        switch (which) {
        case 0: {
            auto list = s.sources();
            int at = s.source_index();
            if (wraps && at + delta >= int(list.size())) delta = -at;
            s.step_source(delta);
            break;
        }
        case 1: s.step_gain(delta); break;
        case 2: s.step_monitor(wraps && int(s.input.monitor) + delta > 2 ? -int(s.input.monitor) : delta); break;
        case 3: s.step_threshold(delta); break;
        case 4: s.step_length(wraps && int(s.input.length) + delta >= int(melodee::rec_lengths) ? -int(s.input.length) : delta); break;
        case 5: if (s.input.source == InputSource::Track && delta) { s.input.post = !s.input.post; s.changed(); } break;
        default: break;
        }
    }

    // The SAMPLING settings (Melodee's Settings page): Audio Recall's cut, a Free take's own end.
    std::vector<std::pair<std::string, std::string>> settings_rows() const {
        const auto& p = sampling().prefs;
        static const char* grabs[] {"PHRASE", "FIRST", "WHOLE"};
        static const char* ends[] {"OFF", "SYNTHS", "ALL"};
        char gap[16], after[16], silence[16];
        std::snprintf(gap, sizeof gap, "%g S", double(melodee::SamplingPrefs::gap_seconds[p.gap % melodee::SamplingPrefs::gap_count]));
        std::snprintf(after, sizeof after, "%g S", double(melodee::SamplingPrefs::end_seconds[p.end_after % melodee::SamplingPrefs::end_count]));
        std::snprintf(silence, sizeof silence, "%d DB", p.silence_db());
        return {{"RECALL", grabs[unsigned(p.grab) % 3]}, {"RECALL GAP", gap}, {"TRIM END", p.trim_end ? "ON" : "OFF"},
                {"AUTO END", ends[unsigned(p.auto_end) % 3]}, {"END AFTER", after}, {"SILENCE", silence},
                {"PREVIEW", sampling().preview ? "ON" : "OFF"}};
    }
    void step_setting(int which, int delta, bool wraps) {
        auto& s = sampling();
        auto& p = s.prefs;
        auto step = [&](unsigned value, unsigned count) {
            int next = int(value) + delta;
            if (wraps) next = (next % int(count) + int(count)) % int(count);
            return unsigned(std::clamp(next, 0, int(count) - 1));
        };
        switch (which) {
        case 0: p.grab = melodee::RecallGrab(step(unsigned(p.grab), 3)); break;
        case 1: p.gap = uint8_t(step(p.gap, melodee::SamplingPrefs::gap_count)); break;
        case 2: if (delta) p.trim_end = wraps ? !p.trim_end : delta > 0; break;
        case 3: p.auto_end = melodee::AutoEnd(step(unsigned(p.auto_end), 3)); break;
        case 4: p.end_after = uint8_t(step(p.end_after, melodee::SamplingPrefs::end_count)); break;
        case 5: p.silence = uint8_t(step(p.silence, melodee::SamplingPrefs::silence_count)); break;
        case 6: if (delta) s.preview = wraps ? !s.preview : delta > 0; break;
        default: return;
        }
        p.clamp();
        studio.changed();
    }

    void level(juce::Graphics& g, int y, float threshold = -1) {
        auto& s = sampling();
        ui::meter(g, {ui::content_x + 1, y, ui::content_width - 4, 10}, meter_of(s.level_left()), meter_of(s.level_right()), threshold);
    }

    void draw_rows(juce::Graphics& g) {
        auto& s = sampling();
        ui::rail(g, "SMPL", "", s.source_tag());
        record_light(g);
        ui::list(g, ui::content(0, 36), input_rows(), cursor, 1);
        level(g, 40);
    }

    void draw_settings(juce::Graphics& g) {
        ui::rail(g, "SMPL", "2/2", "SET");
        std::vector<std::string> rows;
        for (const auto& [name, value] : settings_rows()) rows.push_back(row(name, value));
        ui::list(g, ui::content(0, ui::height), rows, settings_cursor, 1);
    }

    void draw_armed(juce::Graphics& g, bool cued) {
        auto& s = sampling();
        std::string target = pad_label(unsigned(std::max(s.take_pad(), 0)));
        ui::rail(g, "REC", "", s.source_tag());
        record_light(g);
        if (s.pads_play_source()) { // Melodee's own sound: the pads play it, the first note starts the take
            std::string what = s.input.source == InputSource::Track ? "TRACK " + std::to_string(s.input.track + 1) : "THE KIT";
            if (s.input.source == InputSource::Track)
                if (const auto* module = studio.module_track(s.input.track); module && module->module)
                    what = upper(module->module->info().name).substr(0, 8);
            move::draw_text_centred(g, ui::content_x, 2, ui::content_width, "PLAY", 2);
            move::draw_text_centred(g, ui::content_x, 20, ui::content_width, what, what.size() <= 8 ? 2 : 1);
            level(g, 38);
            ui::detail(g, "ONTO " + target, cued ? "NEXT LOOP" : s.input.at_once() ? "" : "1ST NOTE");
            return;
        }
        float threshold = s.input.at_once() ? -1.0f : (s.input.threshold + 72) / 72.0f;
        level(g, 6, threshold);
        std::string length = s.input.length == melodee::rec_free ? std::string() : s.length_text() + " ";
        move::draw_text(g, ui::content_x + 2, 26, length + "ONTO " + target);
        move::draw_text(g, ui::content_x + 2, 38, "THE PAD: AT ONCE");
        ui::detail(g, cued ? "NEXT LOOP" : "WAITS " + s.threshold_text());
    }

    void draw_taking(juce::Graphics& g) {
        auto& s = sampling();
        ui::rail(g, "REC", "", pad_label(unsigned(std::max(s.take_pad(), 0))));
        record_light(g);
        uint32_t frames = s.take_frames(), length = s.take_length();
        move::draw_text(g, ui::content_x + 2, 1, clock_text(frames), 2);
        auto area = ui::content(20, 30);
        // The take so far: with a length, across it; free, across a view that doubles as it grows (8 s first).
        uint32_t window = length ? length : 60 * 44100;
        uint32_t view = length ? length : 8 * 44100;
        while (!length && view < frames && view < window) view *= 2;
        std::vector<float> peaks(size_t(area.getWidth()), 0.0f);
        for (int x = 0; x < area.getWidth(); ++x) {
            uint64_t at = uint64_t(x) * view / unsigned(area.getWidth());
            if (at >= frames) break;
            unsigned column = unsigned(at % window * melodee::TakeRecorder::columns / window);
            peaks[size_t(x)] = s.take_peak(column) / 255.0f;
        }
        ui::waveform(g, area, peaks);
        std::vector<int> cut_columns;
        for (size_t n = 1; n < s.take_cuts().size(); ++n) cut_columns.push_back(int(uint64_t(s.take_cuts()[n]) * unsigned(area.getWidth()) / view));
        ui::cuts(g, area, cut_columns, int(cut_columns.size()) - 1);
        std::string right = bar_beat(studio);
        if (!right.empty()) right = "BAR " + right.substr(0, right.find('.'));
        else if (length) right = clock_text(length).substr(0, 4);
        if (s.take_pads().size() > 1) right = pad_label(s.take_pads()[s.take_pads().size() - 2]) + " > " + pad_label(s.take_pads().back());
        ui::detail(g, s.length_text(), right);
    }

    void draw_done(juce::Graphics& g) {
        auto& s = sampling();
        ui::rail(g, "SMPL", "", s.source_tag());
        record_light(g);
        auto area = ui::content(0, 36);
        ui::waveform(g, area, peaks_of(s.last_overview));
        std::vector<int> columns;
        for (size_t n = 1; n < s.last_cuts.size(); ++n) columns.push_back(int(uint64_t(s.last_cuts[n]) * unsigned(area.getWidth()) / std::max<uint32_t>(1, s.last_frames)));
        ui::cuts(g, area, columns, -1);
        level(g, 40);
        ui::detail(g, upper(s.last_name).substr(0, 10), seconds(s.last_frames).substr(0, 5));
    }

    int page = 0, cursor = 0, settings_cursor = 0;
    bool editing = false;
    Clock::time_point rows_until {}, done_until {};
    std::string seen_take;
    std::array<Clock::time_point, pad_count> pressed_at {};
    const uint8_t pink = move::nearest_colour({255, 120, 190});
};

// Audio Recall (Shift + Sampling): the last 25 s of the input onto the next
// empty pad, at once; this shows what it kept, then goes.
class Recall final : public Screen {
public:
    using Screen::Screen;
    void enter() override {
        shown_until = Clock::now() + std::chrono::milliseconds(2000);
        if (!studio.sampling().recall()) studio.close();
    }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(bindings::Job job, const bindings::Use&) override {
        using bindings::Job;
        switch (job) {
        case Job::Back: case Job::ListPush: studio.close(); return true;
        case Job::AudioRecall: // again: the ring as it is now
            if (!studio.sampling().recall_waiting) { studio.sampling().recall(); shown_until = Clock::now() + std::chrono::milliseconds(2000); }
            return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        auto& s = studio.sampling();
        if (!s.recall_waiting && Clock::now() > shown_until) { studio.close(); return; }
        const auto& kit = studio.current();
        auto colour = ui::track_colour(studio.track());
        for (unsigned at = 0; at < pad_count; ++at)
            lights.pad(at, int(at) == s.recall_pad ? move::colour::white : kit.pads[at].used() ? colour.dim : move::colour::off);
        lights.button(move::Button::Back, move::white_dim);
    }
    void draw(juce::Graphics& g) override {
        auto& s = studio.sampling();
        ui::rail(g, "RCL", "25", "SEC");
        if (s.recall_waiting || s.recall_peaks.empty()) move::draw_text_centred(g, ui::content_x, 20, ui::content_width, "...", 2);
        else ui::waveform(g, ui::content(0, ui::subject_height), s.recall_peaks);
        ui::detail(g, s.recall_pad >= 0 ? "NEXT EMPTY: " + pad_label(unsigned(s.recall_pad)) : "");
    }

private:
    Clock::time_point shown_until {};
};

RegisterScreen sampling("sampling", make<SamplingScreen>,
                        {{bindings::Job::SamplingMode, kind_bit(TrackKind::Kit) | kind_bit(TrackKind::Empty) | kind_bit(TrackKind::Synth)
                                                            | kind_bit(TrackKind::Keys) | kind_bit(TrackKind::Midi) | kind_bit(TrackKind::Plugin)}});
RegisterScreen recall("recall", make<Recall>, {{bindings::Job::AudioRecall}});

} // namespace
} // namespace app::screens
