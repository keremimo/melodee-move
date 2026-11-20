// SPDX-License-Identifier: AGPL-3.0-only
#include "spike/spike_app.h"
#include "move/display.h"
#include "move/font.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace move;

namespace {

constexpr double two_pi = 6.283185307179586;
constexpr double c3 = 130.8127826502993;
// Idle pad colours, bottom row to top; the row buttons beside them match.
constexpr std::array<uint8_t, pad_rows> row_colours {colour::dark_blue, colour::dark_teal, colour::brown_yellow, colour::deep_magenta};
constexpr std::array<Button, pad_rows> row_buttons {Button::Row4, Button::Row3, Button::Row2, Button::Row1};

} // namespace

SpikeApp::SpikeApp(std::string previews_directory) : previews_directory(std::move(previews_directory)) {
    for (auto& value : knob_values) value = 64;
}

void SpikeApp::audio_opened(const AudioFormat& format) {
    rate = format.rate;
    meter.set_rate(format.rate);
}

void SpikeApp::audio_event(const SurfaceEvent& event) {
    const auto& input = event.input;
    if (input.kind == Input::Kind::Pad) {
        pad_level[input.index].store(input.pressed ? input.value / 127.0f : 0.0f, std::memory_order_relaxed);
        if (!input.pressed) pad_pressure[input.index].store(0, std::memory_order_relaxed);
    } else if (input.kind == Input::Kind::PadPressure) {
        pad_pressure[input.index].store(input.value / 127.0f, std::memory_order_relaxed);
    }
}

void SpikeApp::render(const AudioBlock& input, AudioBlock& output) {
    std::array<float, pad_count> target, pressure;
    for (unsigned pad = 0; pad < pad_count; ++pad) {
        target[pad] = pad_level[pad].load(std::memory_order_relaxed);
        pressure[pad] = pad_pressure[pad].load(std::memory_order_relaxed);
    }
    bool run = playing.load(std::memory_order_relaxed);
    double frames_per_step = rate * 60.0 / std::clamp(tempo.load(std::memory_order_relaxed), 40.0f, 240.0f) / 4.0;
    uint32_t pattern = steps_on.load(std::memory_order_relaxed);
    float gain = volume.load(std::memory_order_relaxed), base_overtone = overtone.load(std::memory_order_relaxed);
    if (run && !was_playing) { step = -1; step_frames = frames_per_step; }
    if (!run && was_playing) playhead.store(-1, std::memory_order_relaxed);
    was_playing = run;

    for (unsigned frame = 0; frame < audio_frames; ++frame) {
        if (run && (step_frames += 1.0) >= frames_per_step) {
            step_frames -= frames_per_step;
            step = (step + 1) % int(step_count);
            playhead.store(step, std::memory_order_relaxed);
            if (pattern & (1u << step)) { click_level = 1.0f; click_frequency = step % 4 == 0 ? 1500.0f : 1000.0f; click_phase = 0; }
        }
        float sample = 0;
        for (unsigned pad = 0; pad < pad_count; ++pad) {
            auto& voice = voices[pad];
            voice.level += (target[pad] - voice.level) * 0.002f;  // about 10 ms: no clicks
            voice.pressure += (pressure[pad] - voice.pressure) * 0.002f;
            if (voice.level < 1e-5f) continue;
            double phase = two_pi * voice.phase;
            float octave = std::min(1.0f, base_overtone + voice.pressure);
            sample += float(std::sin(phase) + 0.5 * octave * std::sin(2 * phase)) * voice.level * 0.08f;
            voice.phase += c3 * std::exp2(pad / 12.0) / rate;
            voice.phase -= std::floor(voice.phase);
        }
        if (click_level > 1e-4f) {
            sample += float(std::sin(two_pi * click_phase)) * click_level * 0.25f;
            click_phase += click_frequency / rate;
            click_phase -= std::floor(click_phase);
            click_level *= 0.9985f;
        }
        int32_t value = to_s32(std::clamp(sample * gain, -0.95f, 0.95f));
        output[2 * frame] = output[2 * frame + 1] = value;
    }
    meter.process(input, output, measuring.load(std::memory_order_relaxed));
}

void SpikeApp::log(const char* text) {
    LogLine line;
    std::snprintf(line.text.data(), line.text.size(), "%s", text);
    log_lines.push(line);
}

void SpikeApp::surface(const SurfaceEvent& event) {
    const auto& input = event.input;
    char text[32];
    switch (input.kind) {
    case Input::Kind::Pad: // the sound already started in audio_event()
        if (input.pressed) std::snprintf(text, sizeof text, "PAD %u ON %u", input.index + 1u, unsigned(input.value));
        else std::snprintf(text, sizeof text, "PAD %u OFF", input.index + 1u);
        break;
    case Input::Kind::PadPressure:
        std::snprintf(text, sizeof text, "PRESS %u %u", input.index + 1u, unsigned(input.value));
        if (input.value % 16) return; // pressure is a stream: log only some of it
        break;
    case Input::Kind::Step:
        if (input.pressed) steps_on.fetch_xor(1u << input.index);
        std::snprintf(text, sizeof text, "STEP %u %s", input.index + 1u, input.pressed ? "ON" : "OFF");
        break;
    case Input::Kind::Button:
        if (input.pressed && input.index == uint8_t(Button::Play)) playing = !playing;
        if (input.pressed && input.index == uint8_t(Button::MainPush) && page >= first_preview_page) stretch = !stretch;
        if (input.pressed && input.index == uint8_t(Button::MainPush) && page == latency_page) measuring = !measuring;
        std::snprintf(text, sizeof text, "%s %s", buttons[input.index].name, input.pressed ? "ON" : "OFF");
        break;
    case Input::Kind::Knob: {
        int value = std::clamp(knob_values[input.index] + input.delta, 0, 127);
        knob_values[input.index] = value;
        if (input.index == 0) tempo = std::clamp(tempo + float(input.delta), 40.0f, 240.0f);
        if (input.index == 1) overtone = value / 127.0f;
        std::snprintf(text, sizeof text, "KNOB %u %+d", input.index + 1u, input.delta);
        break;
    }
    case Input::Kind::MasterKnob:
        volume = std::clamp(volume + input.delta * 0.02f, 0.0f, 1.0f);
        std::snprintf(text, sizeof text, "MASTER %+d", input.delta);
        break;
    case Input::Kind::MainKnob: {
        int last = first_preview_page + preview_count.load() - 1;
        page = std::clamp(page + input.delta, home_page, std::max(latency_page, last));
        if (page != latency_page) measuring = false;
        std::snprintf(text, sizeof text, "JOG %+d", input.delta);
        break;
    }
    case Input::Kind::Touch:
        std::snprintf(text, sizeof text, "TOUCH %u %s", input.index + 1u, input.pressed ? "ON" : "OFF");
        break;
    case Input::Kind::AudioInSense:
        std::snprintf(text, sizeof text, "IN %s", input.value ? "LINE" : "MIC");
        break;
    case Input::Kind::AudioOutSense:
        std::snprintf(text, sizeof text, "OUT %s", input.value ? "LINE" : "SPEAKER");
        break;
    case Input::Kind::None:
        std::snprintf(text, sizeof text, "RAW %02X %02X %02X", event.bytes[0], event.bytes[1], event.bytes[2]);
        break;
    }
    log(text);
}

void SpikeApp::lights(Lights& lights, Clock::time_point) {
    uint32_t held = session().held_pads.load();
    for (unsigned pad = 0; pad < pad_count; ++pad)
        lights.pad(pad, held & (1u << pad) ? colour::white : row_colours[pad_row(pad)]);
    uint32_t pattern = steps_on.load();
    int head = playhead.load();
    for (unsigned step_index = 0; step_index < step_count; ++step_index)
        lights.step(step_index, int(step_index) == head ? colour::white : pattern & (1u << step_index) ? colour::orange : colour::off);
    for (unsigned row = 0; row < pad_rows; ++row) lights.button(row_buttons[row], row_colours[row]);
    lights.button(Button::Play, playing ? colour::bright_green : colour::very_dark_green);
    lights.button(Button::Shift, session().shift ? white_bright : white_dim);
    lights.button(Button::Back, session().shift ? white_bright : white_dim);
}

void SpikeApp::service() {
    if (previews_directory.empty()) return;
    if (!previews_listed) {
        previews_listed = true;
        auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(previews_directory.c_str()));
        for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*.png"))
            if (!entry.getFile().getFileName().contains("@2x")) preview_files.push_back(entry.getFile());
        std::sort(preview_files.begin(), preview_files.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
        previews.reserve(preview_files.size());
    }
    for (int loaded = 0; loaded < 4 && next_preview_file < preview_files.size(); ++loaded) {
        const auto& file = preview_files[next_preview_file++];
        auto image = juce::ImageFileFormat::loadFrom(file);
        if (!image.isValid()) continue;
        previews.push_back({file.getFileNameWithoutExtension(), crush(image, Fit::Letterbox), crush(image, Fit::Stretch)});
        preview_count.store(int(previews.size()), std::memory_order_release);
    }
}

void SpikeApp::draw(juce::Graphics& g) {
    input_meter = std::max(session().input_peak.load(), input_meter * 0.9f);
    LogLine line;
    while (log_lines.pop(line)) {
        std::rotate(recent.begin(), recent.begin() + 1, recent.end());
        recent.back() = line.text.data();
    }
    int shown = page.load();
    if (shown == home_page) draw_home(g);
    else if (shown == inputs_page) draw_inputs(g);
    else if (shown == latency_page) draw_latency(g);
    else draw_preview(g, shown - first_preview_page);
}

void SpikeApp::draw_latency(juce::Graphics& g) {
    char text[32];
    g.fillRect(0, 0, screen_width, 9);
    g.setColour(juce::Colours::black);
    draw_text_centred(g, 0, 1, screen_width, "LATENCY");
    g.setColour(juce::Colours::white);
    auto ms = [this](int frames) { return frames * 1000.0 / double(rate); };
    if (meter.count()) std::snprintf(text, sizeof text, "LOOP %.2fMS BEST %.2f", ms(meter.last()), ms(meter.best()));
    else std::snprintf(text, sizeof text, "LOOP -");
    draw_text(g, 0, 11, text);
    std::snprintf(text, sizeof text, "%u TIMED %u MISSED", meter.count(), meter.missed());
    draw_text(g, 0, 20, text);
    std::snprintf(text, sizeof text, "PAD>RENDER %lluUS", static_cast<unsigned long long>(session().pad_to_render_ns.load() / 1000));
    draw_text(g, 0, 29, text);
    std::snprintf(text, sizeof text, "%s RT %s LATE %llu", session().backend.load(), session().realtime ? "YES" : "NO",
                  static_cast<unsigned long long>(session().xruns.load()));
    draw_text(g, 0, 38, text);
    draw_text(g, 0, 48, measuring ? "TIMING CLICKS" : "CABLE OUT>IN, OR");
    draw_text(g, 0, 56, measuring ? "PUSH JOG: STOP" : "SPEAKER>MIC. PUSH JOG");
}

void SpikeApp::draw_home(juce::Graphics& g) {
    char text[32];
    g.fillRect(0, 0, screen_width, 9);
    g.setColour(juce::Colours::black);
    draw_text_centred(g, 0, 1, screen_width, "MELODEE MOVE SPIKE");
    g.setColour(juce::Colours::white);

    std::snprintf(text, sizeof text, "%.1fK %uF XR%llu IN%3d", session().rate.load() / 1000.0, session().period.load(),
                  static_cast<unsigned long long>(session().xruns.load()), int(std::lround(input_meter * 100)));
    draw_text(g, 0, 11, text);
    int last = session().last_pad.load();
    uint32_t held = session().held_pads.load();
    int held_count = 0;
    for (uint32_t bits = held; bits; bits &= bits - 1) ++held_count;
    if (last >= 0) std::snprintf(text, sizeof text, "HELD %d LAST %d V%d", held_count, last + 1, session().velocity.load());
    else std::snprintf(text, sizeof text, "HELD %d  TAP A PAD", held_count);
    draw_text(g, 0, 20, text);
    std::snprintf(text, sizeof text, "%3.0f BPM %s VOL%3d", double(tempo.load()), playing ? "PLAY" : "STOP", int(std::lround(volume * 100)));
    draw_text(g, 0, 29, text);

    uint32_t pattern = steps_on.load();
    int head = playhead.load();
    for (int n = 0; n < int(step_count); ++n) {
        int x = n * 8;
        if (pattern & (1u << n)) g.fillRect(x + 1, 39, 6, 5);
        else g.drawRect(x + 1, 39, 6, 5);
        if (n == head) g.fillRect(x + 1, 45, 6, 1);
    }
    for (unsigned knob = 0; knob < knob_count; ++knob) {
        int x = int(knob) * 16 + 2, height = 1 + knob_values[knob].load() * 14 / 127;
        g.drawRect(x, 48, 12, 16);
        g.fillRect(x + 2, 64 - 1 - height, 8, height);
    }
}

void SpikeApp::draw_inputs(juce::Graphics& g) {
    g.fillRect(0, 0, screen_width, 9);
    g.setColour(juce::Colours::black);
    draw_text_centred(g, 0, 1, screen_width, "INPUTS");
    g.setColour(juce::Colours::white);
    for (size_t n = 0; n < recent.size(); ++n) draw_text(g, 0, 11 + int(n) * 9, recent[n]);
}

void SpikeApp::draw_preview(juce::Graphics& g, int index) {
    if (index < 0 || index >= preview_count.load(std::memory_order_acquire)) {
        draw_text_centred(g, 0, 28, screen_width, previews_directory.empty() ? "NO PREVIEWS" : "LOADING");
        return;
    }
    const auto& preview = previews[size_t(index)];
    g.drawImageAt(stretch ? preview.stretched : preview.letterbox, 0, 0);
    if (session().shift) {
        char text[32];
        std::snprintf(text, sizeof text, "%d/%d %s", index + 1, preview_count.load(), preview.name.toRawUTF8());
        g.setColour(juce::Colours::black);
        g.fillRect(0, 0, screen_width, 9);
        g.setColour(juce::Colours::white);
        draw_text(g, 1, 1, text);
    }
}

std::string SpikeApp::status() const {
    char text[160];
    std::snprintf(text, sizeof text, "page=%d playing=%d tempo=%.0f steps=%04x previews=%d loop_frames=%d/%d timed=%u missed=%u",
                  page.load(), int(playing.load()), double(tempo.load()), unsigned(steps_on.load()), preview_count.load(),
                  meter.last(), meter.best(), meter.count(), meter.missed());
    return text;
}
