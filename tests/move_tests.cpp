// SPDX-License-Identifier: AGPL-3.0-only
// Unit tests for the Move runtime's pure parts: control decoding, lights,
// the screen format, the crush, the reblocker and the hand-off queues.
#include "move/audio.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/lights.h"
#include "move/queues.h"
#include "move/tools.h"
#include "spike/spike_app.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace move;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)

Input decode3(uint8_t a, uint8_t b, uint8_t c) { const uint8_t bytes[] {a, b, c}; return decode(bytes, 3); }

void test_controls() {
    auto pad = decode3(0x9f, 68, 100);
    CHECK(pad.kind == Input::Kind::Pad && pad.index == 0 && pad.pressed && pad.value == 100);
    auto top_right = decode3(0x9f, 99, 1);
    CHECK(top_right.kind == Input::Kind::Pad && top_right.index == 31 && pad_row(31) == 3 && pad_column(31) == 7);
    CHECK(!decode3(0x8f, 70, 64).pressed && decode3(0x8f, 70, 64).kind == Input::Kind::Pad);
    CHECK(!decode3(0x9f, 70, 0).pressed); // note-on with velocity 0 lets go
    auto pressure = decode3(0xaf, 70, 64);
    CHECK(pressure.kind == Input::Kind::PadPressure && pressure.index == 2 && pressure.value == 64);
    auto step = decode3(0x9f, 31, 127);
    CHECK(step.kind == Input::Kind::Step && step.index == 15 && step.pressed);
    auto touch = decode3(0x9f, 9, 127);
    CHECK(touch.kind == Input::Kind::Touch && touch.index == touch_main && touch.pressed);
    CHECK(!decode3(0x9f, 0, 40).pressed); // 0-63 is let go
    CHECK(decode3(0xbf, 14, 1).kind == Input::Kind::MainKnob && decode3(0xbf, 14, 1).delta == 1);
    CHECK(decode3(0xbf, 14, 127).delta == -1);
    auto knob = decode3(0xbf, 78, 65);
    CHECK(knob.kind == Input::Kind::Knob && knob.index == 7 && knob.delta == -63);
    CHECK(decode3(0xbf, 79, 2).kind == Input::Kind::MasterKnob && decode3(0xbf, 79, 2).delta == 2);
    auto shift = decode3(0xbf, 49, 127);
    CHECK(shift.kind == Input::Kind::Button && shift.index == uint8_t(Button::Shift) && shift.pressed);
    CHECK(!decode3(0xbf, 49, 0).pressed);
    CHECK(decode3(0xbf, 3, 127).index == uint8_t(Button::MainPush));
    CHECK(decode3(0xbf, 118, 127).index == uint8_t(Button::Sample));
    CHECK(decode3(0xbf, 114, 127).kind == Input::Kind::AudioInSense && decode3(0xbf, 114, 127).value == 127);
    CHECK(decode3(0xbf, 1, 127).kind == Input::Kind::None);
    CHECK(decode3(0x9f, 50, 127).kind == Input::Kind::None);
    const uint8_t short_message[] {0x9f, 68};
    CHECK(decode(short_message, 2).kind == Input::Kind::None);
}

void test_lights() {
    Lights lights;
    std::vector<std::array<uint8_t, 3>> sent;
    auto collect = [&](const uint8_t* bytes) { sent.push_back({bytes[0], bytes[1], bytes[2]}); return true; };
    CHECK(lights.update(collect) == 0); // nothing set, nothing sent
    lights.pad(3, colour::white);
    lights.step(0, colour::orange);
    lights.button(Button::Play, colour::bright_green);
    lights.button(Button::MainPush, 127); // no light: ignored
    CHECK(lights.update(collect) == 3);
    bool pad_sent = false, step_sent = false, play_sent = false;
    for (const auto& message : sent) {
        pad_sent |= message == std::array<uint8_t, 3> {0x9f, 71, colour::white};
        step_sent |= message == std::array<uint8_t, 3> {0x9f, 16, colour::orange};
        play_sent |= message == std::array<uint8_t, 3> {0xbf, 85, colour::bright_green};
    }
    CHECK(pad_sent && step_sent && play_sent);
    sent.clear();
    CHECK(lights.update(collect) == 0 && !lights.pending()); // unchanged
    lights.clear_all();
    unsigned total = 0;
    while (lights.pending()) total += lights.update(collect);
    CHECK(total == 256);
    for (const auto& message : sent) CHECK((message[0] == 0x9f || message[0] == 0xbf) && message[2] == 0);
    // A full port stops the pass; the rest go next time.
    lights.pad(0, 5); lights.pad(1, 5);
    unsigned accepted = 0;
    CHECK(lights.update([&](const uint8_t*) { return accepted++ < 1; }) == 1);
    CHECK(lights.pending());
    CHECK(nearest_colour({255, 0, 0}) == colour::red);
    CHECK(nearest_colour({0, 0, 0}) == colour::off);
    CHECK(nearest_colour({255, 255, 255}) == colour::white);
    CHECK(nearest_colour({1, 1, 1}) != colour::off);
}

void test_display() {
    Frame frame {};
    set_pixel(frame, 0, 0, true);
    CHECK(frame[0] == 0x01);
    set_pixel(frame, 127, 63, true);
    CHECK(frame[1023] == 0x80);
    set_pixel(frame, 5, 9, true);
    CHECK(frame[128 + 5] == 0x02 && pixel(frame, 5, 9) && !pixel(frame, 5, 8));
    set_pixel(frame, 200, 9, true); // off screen: ignored
    auto message = display_message(frame);
    CHECK(message.size() == 1032 && std::string(message.begin(), message.begin() + 8) == "MOVEDISP" && message[8] == 0x01);

    juce::Image image(juce::Image::ARGB, screen_width, screen_height, true);
    {
        juce::Graphics g(image);
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        g.fillRect(10, 20, 1, 1);
        g.setColour(juce::Colour(0xff404040)); // dark grey stays off
        g.fillRect(30, 30, 1, 1);
    }
    auto packed = pack(image);
    CHECK(pixel(packed, 10, 20) && !pixel(packed, 11, 20) && !pixel(packed, 30, 30));

    juce::Image white(juce::Image::RGB, 320, 240, false);
    white.clear(white.getBounds(), juce::Colours::white);
    auto stretched = pack(crush(white, Fit::Stretch));
    int lit = 0;
    for (int y = 0; y < screen_height; ++y) for (int x = 0; x < screen_width; ++x) lit += pixel(stretched, x, y);
    CHECK(lit == screen_width * screen_height);
    auto boxed = pack(crush(white, Fit::Letterbox)); // 85 x 64, from x 21
    CHECK(!pixel(boxed, 0, 0) && !pixel(boxed, 20, 32) && pixel(boxed, 21, 32) && pixel(boxed, 105, 32) && !pixel(boxed, 106, 32));
    juce::Image grey(juce::Image::RGB, 320, 240, false);
    grey.clear(grey.getBounds(), juce::Colour(0xff808080));
    auto dithered = pack(crush(grey, Fit::Stretch, Tone::Dither));
    lit = 0;
    for (int y = 0; y < screen_height; ++y) for (int x = 0; x < screen_width; ++x) lit += pixel(dithered, x, y);
    CHECK(lit > 3500 && lit < 4700); // about half lit
    juce::Image dark(juce::Image::RGB, 320, 240, false);
    dark.clear(dark.getBounds(), juce::Colour(0xff10151b)); // Melodee's background
    auto thresholded = pack(crush(dark, Fit::Stretch));
    lit = 0;
    for (int y = 0; y < screen_height; ++y) for (int x = 0; x < screen_width; ++x) lit += pixel(thresholded, x, y);
    CHECK(lit == 0);

    juce::Image text(juce::Image::ARGB, screen_width, screen_height, true);
    {
        juce::Graphics g(text);
        g.setColour(juce::Colours::white);
        CHECK(draw_text(g, 0, 0, "Ab") == 12);
    }
    auto glyphs = pack(text);
    lit = 0;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 12; ++x) lit += pixel(glyphs, x, y);
    CHECK(lit > 10 && !pixel(glyphs, 0, 7)); // 'A' fills its 5 x 7 cell and no lower
    CHECK(text_width("Ab") == 11 && text_width("") == 0);
}

void test_reblocker() {
    auto identity = [](const AudioBlock& in, AudioBlock& out) { out = in; };
    for (unsigned period : {128u, 256u, 64u, 200u, 1000u}) {
        auto reblocker = std::make_unique<Reblocker>();
        reblocker->reset(period);
        unsigned expected = period % 128 == 0 ? 0 : 128;
        CHECK(reblocker->latency_frames() == expected);
        std::vector<float> in(period), out_left(period), out_right(period);
        long impulse_at = -1, total = 0;
        for (unsigned call = 0; call < 20; ++call) {
            std::fill(in.begin(), in.end(), 0.0f);
            if (call == 1) in[period / 3] = 0.5f;
            reblocker->process(in.data(), in.data(), out_left.data(), out_right.data(), period, identity);
            for (unsigned n = 0; n < period; ++n)
                if (std::fabs(out_left[n] - 0.5f) < 1e-6f) impulse_at = total + n;
            total += period;
        }
        CHECK(impulse_at == long(period) + long(period / 3) + long(expected));
        CHECK(reblocker->underruns() == 0);
    }
    // Periods that change length every call never run short.
    auto reblocker = std::make_unique<Reblocker>();
    reblocker->reset(100);
    std::mt19937 random(7);
    std::vector<float> buffer(1024);
    for (int call = 0; call < 500; ++call) {
        unsigned period = 1 + random() % 127;
        reblocker->process(buffer.data(), buffer.data(), buffer.data(), buffer.data(), period, identity);
    }
    CHECK(reblocker->underruns() == 0);
    CHECK(to_s32(2.0f) == 2147483647 && to_s32(-2.0f) == -2147483647 && to_s32(0.0f) == 0);
    CHECK(std::fabs(from_s32(to_s32(0.25f)) - 0.25f) < 1e-6f);
}

void test_queues() {
    SpscRing<int, 4> ring;
    CHECK(ring.empty() && ring.push(1) && ring.push(2) && ring.push(3) && !ring.push(4));
    int value = 0;
    CHECK(ring.pop(value) && value == 1 && ring.push(4));
    CHECK(ring.pop(value) && value == 2 && ring.pop(value) && value == 3 && ring.pop(value) && value == 4 && !ring.pop(value));
    TripleBuffer<int> triple;
    CHECK(!triple.take());
    triple.back() = 1; triple.publish();
    triple.back() = 2; triple.publish();
    CHECK(triple.take() && triple.front() == 2 && !triple.take());
    triple.back() = 3; triple.publish();
    CHECK(triple.take() && triple.front() == 3);
}

void test_spike() {
    // The spike's surface hooks without the runtime: a step toggles, Play
    // starts the clicks, a pad sounds.
    SpikeApp app;
    Session session;
    app.attach(session);
    app.audio_opened(AudioFormat {});
    app.surface(make_event(0xbf, button_cc(Button::Play), 127));
    AudioBlock in {}, out {};
    int32_t peak = 0;
    for (int block = 0; block < 50; ++block) {
        app.render(in, out);
        for (int32_t sample : out) peak = std::max(peak, std::abs(sample));
    }
    CHECK(peak > 0);
    Lights lights;
    app.lights(lights, App::Clock::now());
    std::vector<std::array<uint8_t, 3>> sent;
    lights.update([&](const uint8_t* bytes) { sent.push_back({bytes[0], bytes[1], bytes[2]}); return true; }, 256);
    bool play_lit = false;
    for (const auto& message : sent) play_lit |= message == std::array<uint8_t, 3> {0xbf, 85, colour::bright_green};
    CHECK(play_lit);
}

} // namespace

int main() {
    test_controls();
    test_lights();
    test_display();
    test_reblocker();
    test_queues();
    test_spike();
    std::cout << "move-tests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
