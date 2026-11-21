// SPDX-License-Identifier: AGPL-3.0-only
// An audio track (guide/audio-tracks.md, docs/ui/sampling.png): its Play
// screen and its INPUT. Sampling on an audio track opens INPUT (the Move's
// Sampling arms an audio track): the wheel walks SOURCE (the input as the
// jack has it), MONITOR and GAIN, its push steps the row on, knobs 1-3 set
// them; Rec arms the track (again: lets go; while it records: the take ends,
// kept), and the next pass of its pattern from the top is recorded (Play
// starts it when stopped). The Play screen shows the take of the pattern
// playing; knobs 1-6 its CLIP (level, start, end, fade in, fade out,
// reverse), Shift + the wheel its WARP (warp, BPM, semitones, cents). The
// takes themselves are src/app/audio_takes.cpp's.
#include "app/sampling.h"
#include "app/screens/common.h"
#include "melodee/model/audio_track.h"
#include <cmath>

namespace app::screens {
namespace {

using melodee::InputSource;

std::string row(const std::string& name, const std::string& value) {
    std::string text = name;
    while (text.size() + value.size() < 16) text += ' ';
    return text + value;
}
float meter_of(float level) { return level <= 0 ? 0.0f : std::clamp((20.0f * std::log10(level) + 72.0f) / 72.0f, 0.0f, 1.0f); }
std::string source_label(InputSource s, const Sampling& sampling) {
    bool known = sampling.sensed();
    switch (s) {
    case InputSource::Mic: return "MIC";
    case InputSource::RearL: return known ? "LINE L" : "INPUT L";
    case InputSource::RearR: return known ? "LINE R" : "INPUT R";
    default: return known ? "LINE" : "INPUT";
    }
}
// The pattern's length in bars, as the take's frame numbers it.
unsigned bars_of(Studio& studio, unsigned track) {
    const auto& pattern = studio.patterns().at[track][studio.project().tracks[track].pattern % melodee::pattern_count];
    return std::max(1u, (pattern.length + studio.project().bar_ticks() - 1) / std::max(1u, studio.project().bar_ticks()));
}
// A take's picture over `area`: its peaks from `offset` for `frames` (44.1 kHz frames), or the take recording now.
std::vector<float> take_peaks(const std::vector<uint8_t>& peaks, uint32_t offset, uint32_t frames, int width) {
    std::vector<float> out(size_t(std::max(width, 0)), 0.0f);
    if (peaks.empty() || !frames) return out;
    for (int x = 0; x < width; ++x) {
        size_t a = (offset + uint64_t(frames) * unsigned(x) / unsigned(width)) / melodee::take_peak_frames;
        size_t b = std::max(a + 1, size_t((offset + uint64_t(frames) * unsigned(x + 1) / unsigned(width)) / melodee::take_peak_frames));
        uint8_t most = 0;
        for (size_t c = a; c < b && c < peaks.size(); ++c) most = std::max(most, peaks[c]);
        out[size_t(x)] = most / 255.0f;
    }
    return out;
}

void record_light(juce::Graphics& g) {
    g.setColour(juce::Colours::black);
    ui::dot(g, 12, 30, 5);
    g.setColour(juce::Colours::white);
}
bool pulse_on() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 300 % 2 == 0; }

// Rec on an audio track, and its light: pulsing while armed, red while it records.
void rec_light(Studio& studio, move::Lights& lights) {
    auto& s = studio.sampling();
    unsigned t = studio.track();
    lights.button(move::Button::Rec, s.audio_recording() == int(t) ? move::colour::red : s.audio_armed(t) ? (pulse_on() ? move::colour::red : move::colour::off)
                                                                                              : move::colour::dark_grey);
}

// The take recording now, over the pattern's bars.
void draw_recording(Studio& studio, juce::Graphics& g) {
    auto& s = studio.sampling();
    unsigned t = studio.track();
    unsigned bars = bars_of(studio, t);
    ui::rail(g, "REC", "", "TRK" + std::to_string(t + 1));
    record_light(g);
    auto area = ui::content(10, 40);
    g.drawRect(area);
    for (unsigned bar = 1; bar < bars && bars <= 16; ++bar) g.fillRect(area.getX() + int(bar * unsigned(area.getWidth()) / bars), area.getY(), 1, 3);
    for (unsigned bar = 0; bar < bars && bars <= 4; ++bar) move::draw_text(g, area.getX() + 2 + int(bar * unsigned(area.getWidth()) / bars), 1, std::to_string(bar + 1));
    const auto& pattern = studio.patterns().at[t][studio.project().tracks[t].pattern % melodee::pattern_count];
    uint32_t length = uint32_t(melodee::ticks_to_frames(pattern.length, studio.project().tempo));
    auto inner = area.reduced(1);
    uint32_t heard = std::min(s.recording_frames, std::max(length, 1u));
    int shown = int(uint64_t(heard) * unsigned(inner.getWidth()) / std::max(length, 1u));
    auto peaks = take_peaks(s.recording_peaks, 0, heard, std::max(shown, 1));
    peaks.resize(size_t(inner.getWidth()), 0.0f);
    ui::waveform(g, inner, peaks);
    unsigned bar_now = length ? std::min(bars, unsigned(uint64_t(heard) * bars / length) + 1) : 1;
    ui::detail(g, "PAT " + std::to_string(studio.project().tracks[t].pattern + 1), "BAR " + std::to_string(bar_now) + "/" + std::to_string(bars));
}

class AudioInput final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        if (studio.kind() != TrackKind::Audio) studio.close();
        cursor = 0;
    }
    bindings::Screen screen_kind() const override { return bindings::Screen::Sampling; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        auto& s = studio.sampling();
        unsigned t = studio.track();
        switch (job) {
        case Job::RecordAudio: case Job::Record: s.arm_audio(t); return true;
        case Job::InputTurn: cursor = std::clamp(cursor + use.delta, 0, 2); return true;
        case Job::InputPush: step(cursor, 1, true); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: if (use.index < 3) { cursor = use.index; step(use.index, use.delta, false); } return true;
        case Job::SamplingMode: case Job::Back: studio.close(); return true;
        case Job::RecordPad: case Job::RecordPadUp: return true; // an audio track's pads play nothing
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, colour::off);
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
        lights.button(Button::Sample, colour::red);
        rec_light(studio, lights);
    }

    void draw(juce::Graphics& g) override {
        auto& s = studio.sampling();
        unsigned t = studio.track();
        if (s.audio_recording() == int(t)) { draw_recording(studio, g); return; }
        const auto& setting = studio.current().audio;
        unsigned pattern = studio.current().pattern % melodee::pattern_count;
        unsigned bars = bars_of(studio, t);
        if (s.audio_armed(t)) {
            ui::rail(g, "AUD", track_number(studio), "ARM");
            move::draw_text_centred(g, ui::content_x, 2, ui::content_width, "ARMED", 2);
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "PATTERN " + std::to_string(pattern + 1) + ", " + std::to_string(bars) + (bars == 1 ? " BAR" : " BARS"));
            level(g, 36);
            ui::detail(g, studio.playing() ? "NEXT PASS" : "PLAY: RECORD");
            return;
        }
        ui::rail(g, "AUD", track_number(studio), "IN");
        ui::list(g, ui::content(0, 36),
                 {row("SOURCE", source_label(setting.source, s)), row("MONITOR", setting.monitor == melodee::TrackMonitor::On ? "ON" : "OFF"),
                  row("GAIN", s.gain_text())},
                 cursor, 1);
        level(g, 38);
        ui::detail(g, "REC: ARM", "PAT " + std::to_string(pattern + 1));
    }

    Knob knob(unsigned which) const override {
        const auto& setting = studio.current().audio;
        auto& s = studio.sampling();
        switch (which) {
        case 0: return {"SOURCE", source_label(setting.source, s), 0};
        case 1: return {"MONITOR", setting.monitor == melodee::TrackMonitor::On ? "ON" : "OFF", setting.monitor == melodee::TrackMonitor::On ? 1.0f : 0.0f};
        case 2: return {"GAIN", s.gain_text(), (s.input.gain + 12) / 36.0f};
        default: return {};
        }
    }
    std::string knob_owner() const override { return "IN"; }
    std::string status() const override {
        auto& s = studio.sampling();
        return " audio_arm=" + std::to_string(s.audio_recording() >= 0 ? -2 : -1) + " audio_armed=" + std::to_string(int(s.audio_armed(studio.track())));
    }

private:
    void level(juce::Graphics& g, int y) {
        auto& s = studio.sampling();
        ui::meter(g, {ui::content_x + 1, y, ui::content_width - 4, 10}, meter_of(s.level_left()), meter_of(s.level_right()), -1);
    }
    void step(int which, int delta, bool wraps) {
        auto& s = studio.sampling();
        auto& setting = studio.current().audio;
        switch (which) {
        case 0: {
            auto list = s.audio_sources();
            int at = 0;
            for (size_t n = 0; n < list.size(); ++n) if (list[n].source == setting.source) at = int(n);
            if (wraps && at + delta >= int(list.size())) delta = -at;
            s.step_audio_source(studio.track(), delta);
            break;
        }
        case 1:
            if (!delta) return;
            setting.monitor = wraps ? (setting.monitor == melodee::TrackMonitor::On ? melodee::TrackMonitor::Off : melodee::TrackMonitor::On)
                                    : delta > 0 ? melodee::TrackMonitor::On : melodee::TrackMonitor::Off;
            studio.changed();
            break;
        case 2: s.step_gain(wraps ? 1 : delta); break;
        default: break;
        }
    }
    int cursor = 0;
};

// The audio track's Play screen: the take of the pattern playing, its CLIP and WARP.
class AudioPlay final : public Screen {
public:
    using Screen::Screen;
    void enter() override { page = 0; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::Record: studio.sampling().arm_audio(studio.track()); return true; // Rec: the take, as the Move records into the clip
        case Job::Page: page = page ? 0 : 1; return true;
        case Job::TurnKnob: turn(unsigned(use.index), use.delta, false); return true;
        case Job::TurnKnobFine: turn(unsigned(use.index), use.delta, true); return true;
        case Job::OpenBrowser: studio.open("audio-input"); return true; // the wheel's push: its INPUT
        case Job::Focus: return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, move::colour::off);
        steps_playhead(studio, lights);
        for (auto button : {move::Button::Left, move::Button::Right, move::Button::Up, move::Button::Down, move::Button::Loop, move::Button::Menu, move::Button::Back})
            lights.button(button, 0);
        rec_light(studio, lights);
    }

    void draw(juce::Graphics& g) override {
        auto& s = studio.sampling();
        unsigned t = studio.track();
        if (s.audio_recording() == int(t)) { draw_recording(studio, g); return; }
        const auto* region = current();
        if (!region || !region->used()) {
            ui::rail(g, "AUD", track_number(studio), s.audio_armed(t) ? "ARM" : "IN");
            move::draw_text(g, ui::content_x + 1, 1, upper(studio.current().name).substr(0, 16));
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "NO TAKE");
            ui::detail(g, s.audio_armed(t) ? "PLAY: RECORD" : "REC: ARM", "PAT " + std::to_string(pattern() + 1));
            return;
        }
        ui::rail(g, "AUD", track_number(studio), page ? "2/2" : "1/2");
        move::draw_text(g, ui::content_x + 2, 1, page ? "WARP" : "CLIP", 2);
        std::array<std::string, 8> names {"LVL", "STA", "END", "IN", "OUT", "REV", "", ""};
        if (page) names = {"WRP", "BPM", "ST", "CT", "", "", "", ""};
        std::array<float, 8> values {};
        for (unsigned n = 0; n < 8; ++n) values[n] = knob(n).fraction;
        // The take's picture behind the title's right, from its start to its end.
        if (const auto* data = s.take_data(region->take)) {
            auto peaks = take_peaks(data->peaks, region->offset, region->frames, 50);
            ui::waveform(g, {ui::content_x + 52, 0, 50, 16}, peaks);
        }
        ui::knob_cells(g, ui::content(18, 34), names, values);
        char bpm[16];
        std::snprintf(bpm, sizeof bpm, "%.1f", region->tempo / 10.0);
        std::string right = bar_beat(studio);
        ui::detail(g, page ? (region->warp ? (region->mode == melodee::WarpMode::Stretch ? "STRETCH" : "REPITCH") : "WARP OFF") : "TAKE " + std::to_string(region->take),
                   page ? std::string(bpm) : right.empty() ? std::to_string(bars_of(studio, t)) + " BARS" : right);
    }

    Knob knob(unsigned which) const override {
        const auto* region = current();
        if (!region || !region->used()) return {};
        char text[24];
        uint32_t whole = std::max(region->take_frames, 1u);
        unsigned n = which + page * 8;
        switch (n) {
        case 0: std::snprintf(text, sizeof text, "%+.1f DB", region->gain / 10.0); return {"LEVEL", text, (region->gain + 600) / 720.0f};
        case 1: return {"START", seconds(region->offset), float(region->offset) / float(whole)};
        case 2: return {"END", seconds(region->offset + region->frames), float(region->offset + region->frames) / float(whole)};
        case 3: return {"FADE IN", seconds(region->fade_in), float(region->fade_in) / float(std::max(region->frames, 1u))};
        case 4: return {"FADE OUT", seconds(region->fade_out), float(region->fade_out) / float(std::max(region->frames, 1u))};
        case 5: return {"REVERSE", region->reverse ? "ON" : "OFF", region->reverse ? 1.0f : 0.0f};
        case 8: return {"WARP", region->warp ? (region->mode == melodee::WarpMode::Stretch ? "STRETCH" : "REPITCH") : "OFF",
                        region->warp ? (region->mode == melodee::WarpMode::Stretch ? 0.5f : 1.0f) : 0.0f};
        case 9: std::snprintf(text, sizeof text, "%.1f", region->tempo / 10.0); return {"BPM", text, (region->tempo - 300) / 2700.0f};
        case 10: std::snprintf(text, sizeof text, "%+d ST", region->semi); return {"SEMI", text, (region->semi + 24) / 48.0f};
        case 11: std::snprintf(text, sizeof text, "%+d CT", region->fine); return {"FINE", text, (region->fine + 50) / 100.0f};
        default: return {};
        }
    }
    std::string knob_owner() const override { return "TAKE"; }
    // The take's START or END touched: the place close up under its value.
    std::string draw_knob_place(juce::Graphics& g, unsigned which, juce::Rectangle<int> area, bool fine) const override {
        const auto* region = current();
        if (!region || !region->used() || page || (which != 1 && which != 2)) return {};
        const auto* data = studio.sampling().take_data(region->take);
        if (!data || data->left.empty() || !region->take_frames) return {};
        uint32_t end = region->offset + region->frames;
        return draw_place(g, area, region->take_frames, which == 1 ? region->offset : end, region->offset, end, place_zoom(fine),
                          [&](int64_t from, uint32_t span, int width) { return close_peaks(data->left, data->right, data->rate, from, span, width); });
    }
    std::string status() const override {
        const auto* region = current();
        if (studio.kind() != TrackKind::Audio) return {};
        char text[96];
        std::snprintf(text, sizeof text, " take=%u take_gain=%d take_reverse=%d take_warp=%d", region && region->used() ? unsigned(region->take) : 0u,
                      region ? int(region->gain) : 0, region ? int(region->reverse) : 0, region ? int(region->warp) + int(region->mode) : 0);
        return text;
    }

private:
    unsigned pattern() const { return studio.current().pattern % melodee::pattern_count; }
    const melodee::AudioRegion* current() const {
        if (studio.kind() != TrackKind::Audio) return nullptr;
        return &studio.patterns().at[studio.track()][pattern()].audio;
    }
    void turn(unsigned which, int delta, bool fine) {
        auto& region = studio.patterns().at[studio.track()][pattern()].audio;
        if (!region.used() || !delta) return;
        uint32_t whole = region.take_frames, step = std::max<uint32_t>(1, whole / (fine ? 8192 : 256));
        auto moved = [&](uint32_t value, uint32_t lo, uint32_t hi) {
            return uint32_t(std::clamp<int64_t>(int64_t(value) + int64_t(delta) * step, lo, std::max(lo, hi)));
        };
        switch (which + page * 8) {
        case 0: region.gain = int16_t(std::clamp(region.gain + delta * (fine ? 1 : 5), melodee::region_gain_min, melodee::region_gain_max)); break;
        case 1: {
            uint32_t end = region.offset + region.frames;
            region.offset = moved(region.offset, 0, end - std::min(end, melodee::min_slice));
            region.frames = end - region.offset;
            break;
        }
        case 2: region.frames = moved(region.offset + region.frames, region.offset + melodee::min_slice, whole) - region.offset; break;
        case 3: region.fade_in = moved(region.fade_in, 0, region.frames / 2); break;
        case 4: region.fade_out = moved(region.fade_out, 0, region.frames / 2); break;
        case 5: region.reverse = delta > 0; break;
        case 8: { // Off, Stretch, Repitch
            int at = region.warp ? (region.mode == melodee::WarpMode::Stretch ? 1 : 2) : 0;
            at = std::clamp(at + (delta > 0 ? 1 : -1), 0, 2);
            region.warp = at != 0;
            region.mode = at == 2 ? melodee::WarpMode::Repitch : melodee::WarpMode::Stretch;
            break;
        }
        case 9: region.tempo = uint16_t(std::clamp(int(region.tempo) + delta * (fine ? 1 : 10), 300, 3000)); break;
        case 10: region.semi = int8_t(std::clamp(region.semi + delta, -24, 24)); break;
        case 11: region.fine = int8_t(std::clamp(region.fine + delta, -50, 50)); break;
        default: return;
        }
        studio.sampling().audio_changed(studio.track());
        studio.changed();
    }
    int page = 0;
};

RegisterScreen input("audio-input", make<AudioInput>, {{bindings::Job::SamplingMode, kind_bit(TrackKind::Audio)}});
RegisterScreen play("audio-play", make<AudioPlay>, {}, kind_bit(TrackKind::Audio));

} // namespace
} // namespace app::screens
