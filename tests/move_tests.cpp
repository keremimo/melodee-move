// SPDX-License-Identifier: AGPL-3.0-only
// Unit tests for the Move runtime's pure parts: control decoding, lights,
// the screen format, the crush, the reblocker and the hand-off queues.
#include "move/audio.h"
#include "move/controls.h"
#include "move/display.h"
#include "move/font.h"
#include "move/host.h"
#include "move/latency.h"
#include "move/lights.h"
#include "move/queues.h"
#include "move/spi_device.h"
#include "move/spi_protocol.h"
#include "move/tools.h"
#include "spike/spike_app.h"
#include "app/canvas_page.h"
#include <juce_core/juce_core.h>
#include "app/melodee_app.h"
#include "app/pad_layout.h"
#include "app/project_store.h"
#include "melodee/model/project_file.h"
#include "melodee/engine/engine_info.h"
#include "melodee/engine/sampler.h"
#include "melodee/model/scales.h"
#include <algorithm>
#include <iterator>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace move;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)

Input decode3(uint8_t a, uint8_t b, uint8_t c) { const uint8_t bytes[] {a, b, c}; return decode(bytes, 3); }

// The Move's controls as a hand uses them (docs/MOVE_CONTROLS.md): a button
// pressed and let go, Shift held around something, a pad or step tapped.
void press_button(App& app, Button button) {
    app.surface(make_event(0xb0, button_cc(button), 127));
    app.surface(make_event(0xb0, button_cc(button), 0));
}
template <class Do> void with_shift(App& app, Do&& what) {
    app.surface(make_event(0xb0, button_cc(Button::Shift), 127));
    what();
    app.surface(make_event(0xb0, button_cc(Button::Shift), 0));
}
void tap_pad(App& app, unsigned pad, uint8_t velocity = 100) {
    app.surface(make_event(0x90, pad_note(pad), velocity));
    app.surface(make_event(0x80, pad_note(pad), 0));
}
void tap_step(App& app, unsigned step) {
    app.surface(make_event(0x90, step_note(step), 127));
    app.surface(make_event(0x80, step_note(step), 0));
}
void turn_wheel(App& app, int detents) { app.surface(make_event(0xb0, main_knob_cc, uint8_t(detents < 0 ? 128 + detents : detents))); }
// Chop: Shift + push on the pad's sample (Sample Options), its first row.
void open_chop(App& app) {
    with_shift(app, [&] { press_button(app, Button::MainPush); });
    press_button(app, Button::MainPush);
}

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
    lights.step_icon(1, white_bright);    // Setup's icon, under Step 2
    lights.step_icon(16, white_bright);   // no step 17: ignored
    CHECK(lights.update(collect) == 4);
    bool pad_sent = false, step_sent = false, play_sent = false, icon_sent = false;
    for (const auto& message : sent) {
        pad_sent |= message == std::array<uint8_t, 3> {0x9f, 71, colour::white};
        step_sent |= message == std::array<uint8_t, 3> {0x9f, 16, colour::orange};
        play_sent |= message == std::array<uint8_t, 3> {0xbf, 85, colour::bright_green};
        icon_sent |= message == std::array<uint8_t, 3> {0xbf, 17, white_bright};
    }
    CHECK(pad_sent && step_sent && play_sent && icon_sent);
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
    // Brightness keeps each light's colour: it goes down that colour's own
    // rungs (colour k's dim at 63 + 2k, dark at 64 + 2k; the greys' ramp),
    // or stays as it is where there are none, never to another colour.
    auto shown = [](uint8_t level, uint8_t index, Button button = Button::Count) {
        Lights dim;
        dim.set_brightness(level);
        if (button == Button::Count) dim.pad(0, index);
        else dim.button(button, index);
        uint8_t value = 0;
        dim.update([&](const uint8_t* bytes) { value = bytes[2]; return true; });
        return value;
    };
    CHECK(shown(3, colour::bright_red) == colour::bright_red);
    CHECK(shown(2, colour::bright_red) == 65 && shown(1, colour::bright_red) == 65 && shown(0, colour::bright_red) == 66);
    CHECK(shown(1, colour::orange) == 69 && shown(0, colour::orange) == 70); // the palette's unlisted ones
    CHECK(shown(1, colour::cyan) == 91 && shown(0, colour::cyan) == 92);
    CHECK(shown(1, colour::azure) == 93 && shown(0, colour::azure) == 94);
    CHECK(shown(1, 65) == 66 && shown(0, 66) == 66); // a dim one darker; a dark one as it is
    CHECK(shown(1, colour::white) == 123 && shown(0, colour::white) == colour::dark_grey);
    CHECK(shown(0, colour::light_grey) == colour::dark_grey && shown(0, colour::dark_grey) == colour::dark_grey);
    CHECK(shown(0, colour::green) == colour::green && shown(0, 47) == 47); // no darker version
    CHECK(shown(0, colour::light_grey, Button::Row1) == colour::dark_grey); // a track button dims
    // Play, Rec and Sampling keep their colours, as on the Move.
    CHECK(shown(0, colour::green, Button::Play) == colour::green && shown(1, colour::dark_grey, Button::Play) == colour::dark_grey);
    CHECK(shown(0, colour::red, Button::Rec) == colour::red && shown(0, colour::light_grey, Button::Sample) == colour::light_grey);
    auto rung = [](unsigned from, unsigned to) {
        if (to == from) return true;
        if (from >= 1 && from <= 26) return to == 63 + 2 * from || to == 64 + 2 * from;
        if (from >= 65 && from <= 116 && from % 2) return to == from + 1;
        const unsigned greys[] {120, 122, 118, 123, 124};
        auto at = [&](unsigned index) { return std::find(std::begin(greys), std::end(greys), index) - std::begin(greys); };
        return at(from) < 5 && at(to) < 5 && at(to) > at(from);
    };
    for (uint8_t level = 0; level < 3; ++level)
        for (unsigned n = 1; n < 128; ++n) CHECK(rung(n, shown(level, uint8_t(n))));
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

void put_event(uint8_t* page, unsigned slot, uint8_t header, uint8_t status, uint8_t data1, uint8_t data2, uint32_t timestamp) {
    uint8_t* event = page + spi::in_midi + 8 * slot;
    event[0] = header; event[1] = status; event[2] = data1; event[3] = data2;
    spi::DisplayServer::store32(event + 4, timestamp);
}

void test_spi_protocol() {
    std::vector<uint8_t> page(spi::page_size, 0);
    put_event(page.data(), 0, 0x09, 0x90, 68, 100, 5);
    put_event(page.data(), 1, 0x01, 0x00, 0, 0, 6);  // the XMOS heartbeat: no status byte
    put_event(page.data(), 2, 0x2b, 0xb0, 7, 64, 7);  // cable 2, external
    std::array<spi::MidiIn, spi::midi_in_slots> events;
    unsigned skipped = 0;
    unsigned count = spi::read_midi_in(page.data(), events, skipped);
    CHECK(count == 2 && skipped == 1);
    CHECK(events[0].cable == 0 && events[0].bytes == (std::array<uint8_t, 3> {0x90, 68, 100}) && events[0].size == 3 && events[0].timestamp == 5);
    CHECK(events[1].cable == 2 && events[1].bytes[0] == 0xb0 && events[1].bytes[2] == 64);
    spi::clear_midi_in(page.data());
    CHECK(spi::read_midi_in(page.data(), events, skipped) == 0);

    const uint8_t pad_light[] {0x90, 71, 120};
    uint32_t packet = spi::usb_midi_packet(0, pad_light, 3);
    CHECK(packet == (0x09u | 0x90u << 8 | 71u << 16 | 120u << 24));
    const uint8_t play_light[] {0xb0, 85, 8};
    CHECK((spi::usb_midi_packet(0, play_light, 3) & 0xff) == 0x0b);
    const uint8_t program[] {0xc0, 5};
    CHECK(spi::usb_midi_packet(2, program, 2) == (0x2cu | 0xc0u << 8 | 5u << 16));
    const uint8_t sysex[] {0xf0, 1, 2};
    CHECK(spi::usb_midi_packet(0, sysex, 3) == 0 && spi::usb_midi_packet(0, pad_light, 2) == 0);
    std::memset(page.data(), 0xaa, 80);
    uint32_t packets[] {packet, packet};
    spi::write_midi_out(page.data(), packets, 2);
    CHECK(spi::DisplayServer::load32(page.data() + 4) == packet && spi::DisplayServer::load32(page.data() + 8) == 0
          && spi::DisplayServer::load32(page.data() + 76) == 0);

    for (unsigned n = 0; n < 256; ++n) { page[spi::in_audio + 2 * n] = 0xe8; page[spi::in_audio + 2 * n + 1] = 0x03; } // 1000
    AudioBlock block {};
    spi::read_audio_in(page.data(), block);
    CHECK(block[0] == 1000 * 65536 && block[255] == 1000 * 65536);
    spi::Dither dither;
    block.fill(0);
    spi::write_audio_out(page.data(), block, dither);
    bool silent = true;
    for (unsigned n = 0; n < 512; ++n) silent &= page[spi::out_audio + n] == 0;
    CHECK(silent); // digital silence stays silence: no dither hiss when idle
    block.fill(to_s32(0.5f));
    block[1] = to_s32(1.0f);
    block[2] = to_s32(-1.0f);
    spi::write_audio_out(page.data(), block, dither);
    auto sample = [&](unsigned n) { return int16_t(uint16_t(page[spi::out_audio + 2 * n]) | uint16_t(page[spi::out_audio + 2 * n + 1]) << 8); };
    CHECK(std::abs(sample(0) - 16384) <= 1 && sample(1) == 32767 && sample(2) <= -32767);
    double sum = 0;
    float lowest = 0, highest = 0;
    for (int n = 0; n < 100000; ++n) {
        float noise = dither.next();
        sum += noise; lowest = std::min(lowest, noise); highest = std::max(highest, noise);
    }
    CHECK(std::fabs(sum / 100000) < 0.01 && lowest >= -1.0f && highest <= 1.0f && lowest < -0.9f && highest > 0.9f);

    spi::DisplayServer screen;
    Frame first {}, second {};
    first.fill(0x11); second.fill(0x22);
    std::fill(page.begin(), page.end(), 0);
    CHECK(screen.serve(page.data(), [&] { return first; }) == 0); // nothing asked
    spi::DisplayServer::store32(page.data() + spi::in_display_index, 1);
    CHECK(screen.serve(page.data(), [&] { return first; }) == 1);
    CHECK(spi::DisplayServer::load32(page.data() + spi::out_display_index) == 1 && page[spi::out_display_data] == 0x11
          && page[spi::out_display_data + 171] == 0x11);
    spi::DisplayServer::store32(page.data() + spi::in_display_index, 6);
    std::fill(page.begin() + spi::out_display_data, page.begin() + spi::out_display_data + 172, 0);
    CHECK(screen.serve(page.data(), [&] { return second; }) == 6); // chunk 6 keeps the picture chunk 1 latched
    CHECK(page[spi::out_display_data] == 0x11 && page[spi::out_display_data + 163] == 0x11 && page[spi::out_display_data + 164] == 0);
}

void test_latency_meter() {
    for (unsigned delay : {300u, 128u, 1000u}) { // never under a block: an input cannot hear its own block
        LoopbackMeter meter;
        meter.set_rate(44100);
        std::vector<int32_t> line(2 * 8192, 0);
        AudioBlock input {}, output {};
        uint64_t frame = 0;
        for (int block = 0; block < 44100 * 3 / 128; ++block) {
            for (unsigned n = 0; n < audio_frames; ++n) {
                uint64_t at = frame + n;
                int32_t back = at >= delay ? line[2 * ((at - delay) % 8192)] : 0;
                input[2 * n] = input[2 * n + 1] = back / 2 + int32_t(std::sin(at * 0.01) * 0.001 * 2147483647.0); // half level, a little noise
            }
            output.fill(0);
            meter.process(input, output, true);
            for (unsigned n = 0; n < audio_frames; ++n) line[2 * ((frame + n) % 8192)] = output[2 * n];
            frame += audio_frames;
        }
        CHECK(meter.count() >= 5 && meter.missed() == 0 && meter.last() == int(delay) && meter.best() == int(delay));
    }
    LoopbackMeter open_loop; // no cable: every click is missed
    AudioBlock silence {}, output {};
    for (int block = 0; block < 44100 * 2 / 128; ++block) { output.fill(0); open_loop.process(silence, output, true); }
    CHECK(open_loop.count() == 0 && open_loop.missed() >= 3);
    LoopbackMeter idle; // not measuring: the output is left alone
    output.fill(5);
    idle.process(silence, output, false);
    CHECK(output[0] == 5 && idle.count() == 0);
}

void test_spi_host() {
    // A pad hit, then its release, against the simulated XMOS, unpaced.
    auto owned = std::make_unique<FakeSpiDevice>(false, 300, "3:0x90,68,100;400:0x80,68,0;5:0xb0,49,127");
    FakeSpiDevice* device = owned.get();
    SpikeApp app;
    Session session;
    app.attach(session);
    auto host = make_spi_host(app, session, std::move(owned));
    host->start();
    // What the runtime's threads would send: a light and a screen.
    host->leds.push({0x9f, 71, colour::white});
    Frame picture {};
    picture[0] = 0x81; picture[1023] = 0x18; picture[500] = 0x42;
    host->display.back() = display_message(picture);
    host->display.publish();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    Frame blank {};
    host->display.back() = display_message(blank);
    host->display.publish();
    host->finish(std::chrono::milliseconds(500));
    const auto& seen = device->seen();
    CHECK(seen.transfers > 500 && host->alive());
    CHECK(seen.led_messages >= 1); // and on channel 1, cable 0:
    CHECK(seen.chunks_answered > 12 && seen.chunks_wrong == 0 && seen.frames_completed > 2);
    bool blank_last = true;
    for (uint8_t byte : seen.frame) blank_last &= byte == 0;
    CHECK(blank_last); // the screen is cleared on the way out
    CHECK(seen.output_peak > 1000);
    CHECK(session.pad_events.load() == 2 && session.midi_in.load() == 3 && session.pad_to_render_ns.load() > 0);
    CHECK(session.backend.load() == std::string("spi") && session.blocks.load() > 500);
    // The light went out as a USB-MIDI note-on, channel 1, on the controls' cable.
    auto run_once = std::make_unique<FakeSpiDevice>(false, 0, "");
    FakeSpiDevice* lights_device = run_once.get();
    Session quiet;
    SpikeApp other;
    other.attach(quiet);
    auto lights_host = make_spi_host(other, quiet, std::move(run_once));
    lights_host->start();
    lights_host->leds.push({0x9f, 71, colour::white});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    lights_host->finish(std::chrono::milliseconds(200));
    CHECK(lights_device->seen().last_led_packet == (0x09u | 0x90u << 8 | 71u << 16 | uint32_t(colour::white) << 24));
}

int32_t peak_of(const AudioBlock& block) {
    int32_t peak = 0;
    for (int32_t sample : block) peak = std::max(peak, sample == INT32_MIN ? INT32_MAX : std::abs(sample));
    return peak;
}

void test_melodee_app() {
    // Melodee's own sampler and Chop, through the Move app: load a drum
    // loop, play it, chop it, put the slices on pads.
    std::string folder = "build/test-samples", loop = folder + "/loop.wav";
    std::filesystem::create_directories(folder);
    CHECK(write_test_loop(loop));
    std::filesystem::remove_all("build/test-home");
    app::MelodeeApp melodee(folder, "build/test-home");
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    CHECK(melodee.load_now(loop, 0).empty());
    AudioBlock in {}, out {};
    auto play = [&](unsigned pad, int blocks) {
        melodee.audio_event(make_event(0x90, pad_note(pad), 110));
        int32_t peak = 0;
        for (int block = 0; block < blocks; ++block) { out.fill(0); melodee.render(in, out); peak = std::max(peak, peak_of(out)); }
        melodee.audio_event(make_event(0x80, pad_note(pad), 0));
        return peak;
    };
    // Lets a one-shot play out: true once a block comes out silent.
    auto quiet = [&] {
        for (int block = 0; block < 4000; ++block) { out.fill(0); melodee.render(in, out); if (!peak_of(out)) return true; }
        return false;
    };
    CHECK(play(5, 40) == 0);                // an empty pad is silent
    CHECK(play(0, 40) > to_s32(0.1f));      // the loop sounds from pad A01
    CHECK(quiet());
    open_chop(melodee); // Shift + push: Sample Options, Chop
    unsigned slices = melodee.slice_count();
    CHECK(slices >= 4 && slices <= 64);
    CHECK(melodee.status().find("page=chop ") != std::string::npos);
    CHECK(play(1, 20) > to_s32(0.05f));     // pad 2 plays slice 2
    CHECK(quiet());
    CHECK(play(slices, 20) == 0);           // past the last slice: nothing
    press_button(melodee, Button::MainPush); // To Pads
    press_button(melodee, Button::MainPush); // Poly, onto the empty pads
    CHECK(melodee.status().find("page=kit-play") != std::string::npos);
    CHECK(melodee.status().find("pad=A02") != std::string::npos); // the first empty pad after A01
    CHECK(play(1, 20) > to_s32(0.05f));     // A02 now holds slice 1
    CHECK(quiet());
    // Knob 1 tunes the last pad; touching it shows it; a turn of Regions' amount re-chops.
    melodee.surface(make_event(0xb0, knob_first_cc, 3));
    CHECK(melodee.status().find("page=kit-play") != std::string::npos);
    press_button(melodee, Button::Back);
    CHECK(melodee.load_now(folder + "/missing.wav", 3) != "");

    // An AIFF (much of the Move's Core Library), written by hand: a 220 Hz
    // tone, 16-bit stereo at 44.1 kHz, read by Melodee's read_audio().
    std::string aiff = folder + "/tone.aif";
    {
        constexpr uint32_t frames = 22050;
        std::ofstream file(aiff, std::ios::binary);
        auto be32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) file.put(char((v >> s) & 0xff)); };
        auto be16 = [&](uint16_t v) { file.put(char(v >> 8)); file.put(char(v & 0xff)); };
        file.write("FORM", 4); be32(4 + 26 + 16 + frames * 4); file.write("AIFF", 4);
        file.write("COMM", 4); be32(18); be16(2); be32(frames); be16(16);
        const unsigned char rate[10] {0x40, 0x0e, 0xac, 0x44, 0, 0, 0, 0, 0, 0}; // 44100 as an 80-bit float
        file.write(reinterpret_cast<const char*>(rate), 10);
        file.write("SSND", 4); be32(8 + frames * 4); be32(0); be32(0);
        for (uint32_t n = 0; n < frames; ++n) {
            auto sample = uint16_t(int16_t(std::sin(6.2831853 * 220.0 * n / 44100.0) * 20000));
            be16(sample); be16(sample);
        }
    }
    CHECK(melodee.load_now(aiff, 3).empty());
    CHECK(quiet());
    CHECK(play(3, 20) > to_s32(0.15f)); // 0.61 x velocity x centre pan's -3 dB x volume: about 0.3
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void test_saving() {
    // A kit saved as a Melodee project and read back by a new start: its
    // samples, pads, last pad and each sample's chop; a file gone missing
    // keeps its place.
    std::string folder = "build/test-samples", home = "build/test-home-saving";
    std::string loop = folder + "/loop.wav", other = folder + "/other.wav";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    CHECK(write_test_loop(loop) && write_test_loop(other));
    auto press = [](app::MelodeeApp& melodee, Button button) { press_button(melodee, button); };
    unsigned chopped = 0;
    std::string project;
    {
        app::MelodeeApp first(folder, home);
        Session session;
        first.attach(session);
        first.audio_opened(AudioFormat {});
        CHECK(first.restore_now().empty()); // no file yet: a new project
        CHECK(first.load_now(loop, 0).empty());
        CHECK(first.load_now(other, 20).empty());
        tap_pad(first, 0); // A01 again
        open_chop(first);
        first.surface(make_event(0xb0, uint8_t(knob_first_cc + 2), 1)); // type: Beats
        chopped = first.slice_count();
        CHECK(chopped > 1);
        press(first, Button::MainPush);
        press(first, Button::MainPush); // To Pads: Poly, from A02
        CHECK(has(first.status(), "pad=A02") && has(first.status(), "unsaved=1"));
        CHECK(first.save_now().empty());
        CHECK(has(first.status(), "saves=1") && has(first.status(), "unsaved=0"));
        project = first.project_path();
        CHECK(std::filesystem::exists(project) && !std::filesystem::exists(project + ".writing"));
    }
    std::filesystem::remove(other); // gone before the next start
    app::MelodeeApp second(folder, home);
    Session session;
    second.attach(session);
    second.audio_opened(AudioFormat {});
    CHECK(second.restore_now().empty());
    std::string status = second.status();
    unsigned expected_pads = 2 + std::min(chopped, 32u - 2u); // A01, the missing file's pad, the slices
    CHECK(has(status, "samples=1") && has(status, "pad=A02") && has(status, "pads=" + std::to_string(expected_pads)));
    AudioBlock in {}, out {};
    second.audio_event(make_event(0x90, pad_note(1), 110)); // A02: the first slice
    int32_t peak = 0;
    for (int block = 0; block < 20; ++block) { out.fill(0); second.render(in, out); peak = std::max(peak, peak_of(out)); }
    CHECK(peak > to_s32(0.05f));
    tap_pad(second, 0);
    open_chop(second); // A01's chop as it was left: Beats, the same slices
    CHECK(second.slice_count() == chopped && has(second.status(), "chop=Beats"));
    press(second, Button::Back);
    CHECK(second.save_now().empty());
    // The file is Melodee's own project: track 1 the kit, its two samples by
    // path (the missing one kept), A01's chop in CHOP.
    {
        auto project = std::make_unique<melodee::Project>();
        auto patterns = std::make_unique<melodee::PatternBank>();
        app::PluginStates states;
        CHECK(app::read_project_file(home + "/Projects/Move.mldp", *project, *patterns, states).empty());
        unsigned files = 0, chopped_files = 0;
        for (size_t slot = 0; slot < melodee::sample_slots; ++slot) {
            files += project->samples[slot].path[0] != 0;
            chopped_files += project->chops[slot].used();
        }
        CHECK(files == 2 && chopped_files == 1 && project->tracks[0].engine == melodee::EngineKind::Sampler);
        CHECK(app::read_project_file(home + "/nothing.mldp", *project, *patterns, states) == "missing");
    }
    // The chops' lines: what is written reads back; a damaged line is left out.
    app::KitFile original;
    original.samples.push_back({3, "/a.wav", "a.wav", 1000, {}});
    original.samples[0].chop.mode = melodee::ChopMode::Regions;
    original.samples[0].chop.amount = 4;
    original.samples[0].chop.slices.count = 2;
    original.samples[0].chop.slices.start = {0, 500};
    original.samples[0].chop.slices.stop = {500, 0};
    app::KitFile back;
    back.samples.push_back({3, "/a.wav", "a.wav", 1000, {}});
    app::read_chops(app::chops_text(original), back);
    CHECK(back.samples[0].chop.slices.same(original.samples[0].chop.slices) && back.samples[0].chop.mode == melodee::ChopMode::Regions
          && back.samples[0].chop.amount == 4);
    app::KitFile damaged;
    damaged.samples.push_back({3, "/a.wav", "a.wav", 1000, {}});
    app::read_chops("melodee-move chops 1\n3 0 0 1 4 2 500 0 0 500\n", damaged); // starts out of order
    CHECK(!damaged.samples[0].chop.used());
}

void test_main_volume() {
    // The Volume knob's main volume is the Move's: kept in Move.settings
    // (not the project's, so no UNSAVED) and back on the next start.
    std::string folder = "build/test-samples", home = "build/test-home-volume";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    auto percent = [](app::MelodeeApp& melodee) { return std::lround(melodee.studio().volume.load() * 100.0f); };
    {
        app::MelodeeApp first(folder, home);
        Session session;
        first.attach(session);
        CHECK(first.restore_now().empty());
        CHECK(first.save_now().empty()); // as a start leaves it: nothing unsaved
        CHECK(percent(first) == 80);
        for (int n = 0; n < 5; ++n) first.surface(make_event(0xb0, master_knob_cc, 127)); // five down
        CHECK(percent(first) == 75);
        CHECK(has(first.status(), "unsaved=0"));
        first.stopping(); // the run ending: Move.settings written though the project was not
    }
    std::ifstream file(home + "/Move.settings");
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(has(text, "\nvolume 75\n"));
    app::MelodeeApp second(folder, home);
    CHECK(percent(second) == 75);
}


void test_schwung_modules() {
    // Schwung's module API, against the tests' stand-in module
    // (tests/fake_module, built into build/test-modules by the Makefile).
    using Kind = app::ModuleParam::Kind;
    const std::string root = "build/test-modules";
    auto found = app::scan_modules(root);
    CHECK(found.size() == 1);
    if (found.empty()) return;
    const auto& info = found[0];
    CHECK(info.id == "fake" && info.name == "Fake Synth" && info.abbrev == "FAKE" && info.api == 2);
    CHECK(info.dsp == root + "/sound_generators/fake/dsp.so");
    std::string error;
    auto loaded = app::LoadedModule::load(info, error);
    CHECK(loaded && error.empty());
    if (!loaded) { std::cerr << "fake module: " << error << '\n'; return; }
    // Its parameters: chain_params' four (a file path is no knob), attack,
    // described in its menu instead, and the table chosen from a list, first.
    CHECK(loaded->params.size() == 7 && !loaded->param("sample") && !loaded->param("") && !loaded->param("browse"));
    CHECK(loaded->params.front().key == "table" && loaded->params.front().selector);
    // Its menu: root, then the levels root links to.
    CHECK(loaded->levels.size() == 3 && loaded->levels[0].name == "Fake" && loaded->levels[1].id == "env" && loaded->levels[2].id == "tables");
    CHECK(loaded->levels[0].items.size() == 4 && loaded->levels[0].items[2].kind == app::MenuItem::Kind::Level
          && loaded->levels[0].items[2].label == "Envelope");
    CHECK(loaded->levels[2].items_param == "table_list" && loaded->levels[2].navigate_to == "root");
    // Its knob pages: an empty knob kept in its place, the preset list no
    // knob (it has the jogwheel), a level without knobs no page.
    CHECK(loaded->pages.size() == 3 && loaded->pages[0].name == "Fake" && loaded->pages[1].name == "Envelope" && loaded->pages[1].level == 1);
    CHECK((loaded->pages[0].knobs == std::vector<std::string> {"level", "wave", "", "voices"}));
    // Its own page, drawn by its script, last (it does not ask to be first),
    // with root's knobs.
    CHECK(loaded->canvases.size() == 1 && loaded->canvases[0].as_page && loaded->canvases[0].enterable
          && loaded->canvases[0].script == info.dir + "/page.js" && loaded->canvases[0].extra_keys == std::vector<std::string> {"level"});
    CHECK(loaded->pages[2].canvas == 0 && loaded->pages[2].name == "Browse" && loaded->pages[2].knobs == loaded->pages[0].knobs);
    // An action, not a value.
    auto* go = loaded->param("go");
    CHECK(go && go->trigger() && go->shown() == "PUSH" && !loaded->param("level")->trigger());
    // Each value as the module reports it once created.
    auto* attack = loaded->param("attack");
    CHECK(attack && attack->short_name == "ATK" && attack->label == "Attack" && attack->max == 2 && std::abs(attack->value - 0.1) < 1e-6);
    auto* wave = loaded->param("wave");
    CHECK(wave && wave->label == "Waveform");
    CHECK(wave && wave->kind == Kind::Enum && wave->value == 1 && wave->shown() == "Square" && wave->text() == "1");
    if (wave) {
        wave->turn(-1, false);
        CHECK(wave->text() == "0" && wave->shown() == "Sine");
        wave->turn(-5, false);
        CHECK(wave->value == 0);
        wave->take("Square"); // an answer by name
        CHECK(wave->value == 1);
    }
    auto* level = loaded->param("level");
    CHECK(level && std::abs(level->value - 0.5) < 1e-6);
    if (level) {
        level->turn(10, false);
        CHECK(std::abs(level->value - 0.6) < 1e-9 && level->text() == "0.6" && level->shown() == "60%");
        level->turn(1, true); // Shift: a tenth of a step
        CHECK(std::abs(level->value - 0.601) < 1e-9);
    }
    auto* voices = loaded->param("voices");
    CHECK(voices && voices->kind == Kind::Int && voices->value == 4);
    if (voices) { voices->turn(10, false); CHECK(voices->value == 8); }
    CHECK(loaded->preset_param == "preset" && loaded->preset_count == 10 && loaded->preset_name == "Fake 0");
    auto* preset = loaded->param("preset");
    CHECK(preset && preset->max == 9); // its max_param, preset_count, less one
    // What the host tells it of the tempo, through get_bpm() and through
    // schwung_move_info(), which it finds in the process.
    app::set_module_tempo(97.0f);
    CHECK(loaded->get_now("bpm") == "97" && loaded->get_now("tempo") == "97");
    app::set_module_tempo(120.0f);

    // Played as the audio thread plays it: notes, pressure, a parameter set
    // and reads answered on the next block.
    app::SynthPlayer player;
    CHECK(player.set_module(loaded.get()));
    AudioBlock in {}, out {};
    out.fill(0);
    player.render(in, out, 1.0f);
    CHECK(player.live() && peak_of(out) == 0);
    player.note_on(60, 127);
    out.fill(0);
    player.render(in, out, 1.0f);
    int32_t loud = peak_of(out);
    CHECK(loud > to_s32(0.1f));
    player.pressure(60, 127);
    CHECK(player.ask("pressure") && player.ask("note"));
    out.fill(0);
    player.render(in, out, 1.0f);
    CHECK(peak_of(out) > loud);
    app::SynthPlayer::Answer answer;
    CHECK(player.answer(answer) && std::string(answer.key) == "pressure" && std::string(answer.value) == "127");
    CHECK(player.answer(answer) && std::string(answer.key) == "note" && std::string(answer.value) == "60");
    player.set("level", "0.25");
    player.ask("level");
    player.note_off(60);
    out.fill(0);
    player.render(in, out, 1.0f);
    CHECK(peak_of(out) == 0);
    CHECK(player.answer(answer) && std::string(answer.key) == "level" && std::string(answer.value) == "0.25");
    // A call that holds the audio thread up is reported, with what it was.
    app::SynthPlayer::Slow slow;
    CHECK(!player.slow(slow)); // nothing slow so far
    player.set("stall", "3");
    player.render(in, out, 1.0f);
    CHECK(player.slow(slow) && std::string(slow.what) == "set stall=3" && slow.us >= 3000);
    // A source (a stream, no MIDI) plays on the player's own thread, ahead:
    // a stall of its holds that thread up, never the audio thread.
    {
        app::SynthPlayer streaming;
        loaded->source = true;
        CHECK(streaming.set_module(loaded.get()));
        streaming.note_on(60, 127); // to its thread, in order
        int32_t heard = 0;
        for (int block = 0; block < 60; ++block) {
            out.fill(0);
            streaming.render(in, out, 1.0f);
            heard = std::max(heard, peak_of(out));
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        CHECK(heard > to_s32(0.05f)); // level 0.25, set above
        streaming.set("stall", "80"); // its thread waits 80 ms, past its 35 ms lead
        double slowest_ms = 0;
        // Until its lead has run out (a slow machine may take longer to get there).
        for (int block = 0; block < 30 || (block < 300 && !streaming.stream_late()); ++block) {
            auto begun = std::chrono::steady_clock::now();
            out.fill(0);
            streaming.render(in, out, 1.0f);
            slowest_ms = std::max(slowest_ms, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        CHECK(slowest_ms < 5);             // the audio thread was never held
        CHECK(streaming.stream_late() > 0); // and the stream ran dry for a moment instead
        app::SynthPlayer::Slow stalled;
        bool reported = false;
        bool small_reported = false;
        while (streaming.slow(stalled)) {
            reported |= std::string(stalled.what) == "set stall=80";
            small_reported |= stalled.us < app::SynthPlayer::source_slow_us;
        }
        CHECK(reported && !small_reported); // ahead of time, only a stall that eats into the lead
        // Let go: its thread hands the commands back, and the module.
        CHECK(streaming.set_module(nullptr));
        app::LoadedModule* back = nullptr;
        for (int tries = 0; tries < 100 && !back; ++tries) {
            streaming.render(in, out, 1.0f);
            back = streaming.retired();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(back == loaded.get());
        streaming.note_off(60);
        loaded->source = false;
    }
    loaded->api->on_midi(loaded->instance, std::array<uint8_t, 3> {0x80, 60, 0}.data(), 3, 0);

    // Let go: handed back, to be destroyed off the audio thread.
    CHECK(!player.retired());
    CHECK(player.set_module(nullptr));
    player.render(in, out, 1.0f);
    CHECK(!player.live() && player.retired() == loaded.get());
}

void test_canvas_page() {
    // A module's own page script, hosted as Schwung hosts it.
    struct Calls { std::vector<std::string> sets; std::string title, initial, opened; int changed = 0; } calls;
    app::CanvasPage::Host host;
    host.get = [](const std::string& key) { return key == "level" ? std::string("0.42") : std::string(); };
    host.set = [&](const std::string& key, const std::string& value) { calls.sets.push_back(key + "=" + value); };
    host.text_entry = [&](const std::string& title, const std::string& initial) { calls.title = title; calls.initial = initial; };
    host.open_file = [&](const std::string& path) { calls.opened = path; };
    host.changed = [&] { ++calls.changed; };
    std::string error;
    auto page = app::CanvasPage::load("tests/fake_module/page.js", host, error);
    CHECK(page && error.empty());
    if (!page) { std::cerr << "page.js: " << error << '\n'; return; }
    juce::Image screen(juce::Image::ARGB, 128, 64, true);
    {
        juce::Graphics g(screen);
        page->draw(g, {{"level", "0.5"}});
    }
    CHECK(page->pixel(0, 0) && page->pixel(9, 9) && !page->pixel(10, 10)); // its block
    CHECK(page->pixel(50, 30) && !page->pixel(42, 30));                     // the value it was given, not asked for
    CHECK(page->pixel(1, 41));                                              // QuickJS's std module, imported
    CHECK(screen.getPixelAt(0, 0).getBrightness() > 0.5f && screen.getPixelAt(64, 50).getBrightness() < 0.5f);
    page->midi(0xb0, 14, 1);
    page->midi(0xb0, 3, 127);
    CHECK((calls.sets == std::vector<std::string> {"level=0.9"}) && calls.title == "Search" && calls.initial == "ab");
    page->text_entered("drums", false);
    page->text_entered("ignored", true);
    CHECK(calls.sets.size() == 2 && calls.sets[1] == "searched=drums");
    CHECK(page->back());  // a level up
    CHECK(!page->back()); // at its top
    CHECK(calls.changed == 6); // each gesture, not the draw
    // What the page keeps in ctx.state goes with the module and comes back
    // onto a new page: here a level down, as it was left.
    page->midi(0xb0, 3, 127);
    auto kept = page->saved_state();
    CHECK(kept.find("\"depth\":1") != std::string::npos);
    auto again = app::CanvasPage::load("tests/fake_module/page.js", host, error);
    CHECK(again != nullptr);
    if (again) {
        CHECK(again->saved_state().empty()); // nothing kept yet
        again->restore_state("not json");
        again->restore_state("[1, 2]");
        again->restore_state(kept);
        CHECK(again->back() && !again->back());
        CHECK(again->saved_state() == "{\"depth\":0}");
    }
    // A script that throws is reported and not called again; one that does
    // not parse, or sets no page, is refused.
    std::filesystem::create_directories("build/test-pages");
    std::ofstream("build/test-pages/throws.js") << "let n = 0; globalThis.canvas_overlay = { drawPage(ctx) { n++; ctx.setParam('n', String(n)); throw new Error('boom'); } };\n";
    std::ofstream("build/test-pages/broken.js") << "globalThis.canvas_overlay = { drawPage( };\n";
    std::ofstream("build/test-pages/none.js") << "const x = 1;\n";
    calls.sets.clear();
    auto throws = app::CanvasPage::load("build/test-pages/throws.js", host, error);
    CHECK(throws != nullptr);
    if (throws) {
        juce::Graphics g(screen);
        throws->draw(g, {});
        throws->draw(g, {});
        CHECK((calls.sets == std::vector<std::string> {"n=1"}));
    }
    CHECK(!app::CanvasPage::load("build/test-pages/broken.js", host, error) && !error.empty());
    CHECK(!app::CanvasPage::load("build/test-pages/none.js", host, error) && has(error, "canvas_overlay"));
    CHECK(!app::CanvasPage::load("build/test-pages/missing.js", host, error));
}

void test_synth_track() {
    // The synth track in Melodee: Track 2; the wheel's push (on its preset,
    // with no module yet) lists the modules and a push loads one; pads play
    // it; knobs and presets set it; it is saved beside the project and read
    // back by the next start.
    std::string folder = "build/test-samples", home = "build/test-home-synth", modules = "build/test-modules";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    auto press = [](app::MelodeeApp& melodee, Button button) { press_button(melodee, button); };
    auto loudest = [](app::MelodeeApp& melodee, int blocks) {
        AudioBlock in {}, out {};
        int32_t peak = 0;
        for (int block = 0; block < blocks; ++block) { out.fill(0); melodee.render(in, out); peak = std::max(peak, peak_of(out)); }
        return peak;
    };
    std::string project;
    {
        app::MelodeeApp melodee(folder, home, modules);
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        press(melodee, Button::Row2);
        CHECK(has(melodee.status(), "track=2 kind=synth") && has(melodee.status(), "synth=-"));
        press(melodee, Button::MainPush); // no module: the push lists them
        CHECK(has(melodee.status(), "page=load-modules"));
        press(melodee, Button::MainPush);
        melodee.service(); // loads it
        CHECK(has(melodee.status(), "page=synth-play") && has(melodee.status(), "synth=fake"));
        // A pad plays a note, in the cycle it was read; let go, it stops.
        melodee.audio_event(make_event(0x90, pad_note(0), 100));
        CHECK(loudest(melodee, 1) > to_s32(0.05f));
        melodee.audio_event(make_event(0x80, pad_note(0), 0));
        CHECK(loudest(melodee, 1) == 0);
        // Loaded again: the wheel to the module, its push, the list's push. A
        // new instance; the one before is let go and destroyed.
        turn_wheel(melodee, -1);
        press(melodee, Button::MainPush);
        CHECK(has(melodee.status(), "page=load-modules"));
        press(melodee, Button::MainPush);
        melodee.service();
        loudest(melodee, 1);
        melodee.service();
        CHECK(has(melodee.status(), "synth=fake"));
        // The wheel to the preset, its push its presets, a turn the next one
        // (loaded as it turns), which sets the level; the module's values
        // asked for again and shown; push: done.
        turn_wheel(melodee, 1);
        press(melodee, Button::MainPush);
        turn_wheel(melodee, 1);
        press(melodee, Button::MainPush);
        loudest(melodee, 3);
        melodee.service();
        CHECK(melodee.synth_value("preset") == "1" && melodee.synth_value("level") == "0.1");
        with_shift(melodee, [&] { turn_wheel(melodee, 1); }); // past MACRO, Melodee's first page (Kerem, 2026-11-20), to the module's
        std::this_thread::sleep_for(std::chrono::milliseconds(350)); // the next Shift no double press (that locks it)
        melodee.surface(make_event(0xb0, knob_first_cc, 3)); // knob 1: level, up three
        CHECK(melodee.synth_value("level") == "0.13");
        // Shift + the wheel's push: the module's menu. Root's rows: the
        // preset, level, Waveform, Envelope >, Tables >.
        with_shift(melodee, [&] { press(melodee, Button::MainPush); });
        CHECK(has(melodee.status(), "menu=root"));
        melodee.surface(make_event(0xb0, main_knob_cc, 2)); // Waveform
        press(melodee, Button::MainPush);                   // edit it
        melodee.surface(make_event(0xb0, main_knob_cc, 127)); // one back: Sine
        CHECK(melodee.synth_value("wave") == "0");
        press(melodee, Button::MainPush); // done
        melodee.surface(make_event(0xb0, main_knob_cc, 2)); // Tables
        press(melodee, Button::MainPush);
        CHECK(has(melodee.status(), "menu=tables"));
        loudest(melodee, 1); // the list read on the audio thread
        melodee.service();
        melodee.surface(make_event(0xb0, main_knob_cc, 2)); // Organ
        press(melodee, Button::MainPush);                   // chosen: back to root, as the module asks
        CHECK(has(melodee.status(), "menu=root") && melodee.synth_value("table") == "2");
        loudest(melodee, 1);
        press(melodee, Button::Back);
        CHECK(has(melodee.status(), "menu=-"));
        press(melodee, Button::Up);
        CHECK(has(melodee.status(), "octave=1"));
        // Row 1: the kit again; its pads its own.
        press(melodee, Button::Row1);
        CHECK(has(melodee.status(), "track=1 kind=kit"));
        melodee.audio_event(make_event(0x90, pad_note(0), 100));
        CHECK(loudest(melodee, 2) == 0); // an empty kit
        melodee.audio_event(make_event(0x80, pad_note(0), 0));
        press(melodee, Button::Row2);
        CHECK(melodee.save_now().empty());
        project = melodee.project_path();
    }
    // The module's state, in its PLUG entry of the project.
    auto saved_state = [](const std::string& file) {
        auto read = std::make_unique<melodee::Project>();
        auto patterns = std::make_unique<melodee::PatternBank>();
        app::PluginStates states;
        app::SynthFile synth;
        if (app::read_project_file(file, *read, *patterns, states).empty() && read->tracks[1].engine == melodee::EngineKind::Plugin
            && read->tracks[1].plugin < melodee::max_plugins)
            app::read_synth_state(states[read->tracks[1].plugin], synth);
        return synth;
    };
    app::SynthFile saved = saved_state(project);
    CHECK(saved.module == "fake" && saved.preset == 1 && saved.octave == 1 && saved.track == 1);
    bool level_saved = false, table_saved = false, action_saved = false;
    for (const auto& [key, value] : saved.values) {
        level_saved |= key == "level" && value == "0.13";
        table_saved |= key == "table" && value == "2";
        action_saved |= key == "go";
    }
    CHECK(!action_saved); // an action has no value to keep
    CHECK(level_saved && table_saved && !saved.values.empty() && saved.values.front().first == "table"); // the choice first
    {
        app::MelodeeApp second(folder, home, modules);
        Session session;
        second.attach(session);
        second.audio_opened(AudioFormat {});
        CHECK(second.restore_now().empty());
        CHECK(has(second.status(), "track=2 kind=synth") && has(second.status(), "synth=fake octave=1"));
        CHECK(second.synth_value("level") == "0.13" && second.synth_value("preset") == "1" && second.synth_value("table") == "2");
        CHECK(second.synth_value("wave") == "0");
        second.audio_event(make_event(0x90, pad_note(0), 100));
        CHECK(loudest(second, 1) > 0);
        second.audio_event(make_event(0x80, pad_note(0), 0));
        loudest(second, 1);

        // The module's own page: Shift + the wheel to it, after MACRO, Fake and Envelope.
        with_shift(second, [&] { turn_wheel(second, 1); turn_wheel(second, 1); turn_wheel(second, 1); });
        CHECK(has(second.status(), "canvas=browse entered=0"));
        std::this_thread::sleep_for(std::chrono::milliseconds(60)); // no audio thread: the page's reads go to the module directly
        juce::Image screen(juce::Image::ARGB, 128, 64, true);
        { juce::Graphics g(screen); g.setColour(juce::Colours::white); second.draw(g); }
        CHECK(second.canvas_pixel(0, 0) && second.canvas_pixel(13, 30) && second.canvas_pixel(1, 41)); // its block, level 0.13, std
        press(second, Button::MainPush); // in: the host's push, not the page's
        CHECK(has(second.status(), "entered=1") && has(second.status(), "typing=0"));
        second.surface(make_event(0xb0, main_knob_cc, 1)); // the page's: the level to 0.9
        CHECK(second.synth_value("level") == "0.9");
        press(second, Button::MainPush); // the page's: a level down, and it asks for text
        CHECK(has(second.status(), "typing=1"));
        second.audio_event(make_event(0x90, pad_note(24), 100)); // a letter now, not a note
        CHECK(loudest(second, 1) == 0);
        second.audio_event(make_event(0x80, pad_note(24), 0));
        press(second, Button::Back);                           // "ab", given, less one: "a" (the Move's Back deletes)
        tap_pad(second, 25);                                   // b
        tap_pad(second, 26);                                   // c
        tap_step(second, 0);                                   // 1
        second.surface(make_event(0xb0, main_knob_cc, 63));   // the strip to its end, OK (the Move's own typing)
        press(second, Button::MainPush);                       // done
        CHECK(has(second.status(), "typing=0"));
        loudest(second, 4); // the page's sets reach the module (after the pages' asks, four a block)
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        CHECK(second.ask_module("searched") == "abc1" && second.ask_module("level") == "0.9");
        press(second, Button::Back); // up a level: the page keeps the jogwheel
        CHECK(has(second.status(), "entered=1"));
        press(second, Button::Back); // at its top: out
        CHECK(has(second.status(), "canvas=browse entered=0"));
        // The page's gestures made the project unsaved; saved, its ctx.state is the module's, the page's own key no value.
        CHECK(has(second.status(), "unsaved=1"));
        CHECK(second.save_now().empty());
        auto with_page = saved_state(project);
        CHECK(with_page.pages.size() == 1 && with_page.pages[0].first == "browse" && with_page.pages[0].second.find("\"depth\":0") != std::string::npos);
        CHECK(std::none_of(with_page.values.begin(), with_page.values.end(), [](const auto& value) { return value.first == "browse"; }));
    }
    {
        // Its module gone: the track keeps naming it, and saves it so.
        app::MelodeeApp third(folder, home, "build/test-no-modules");
        Session session;
        third.attach(session);
        CHECK(third.restore_now().empty());
        CHECK(has(third.status(), "synth=-"));
        CHECK(third.save_now().empty());
    }
    saved = saved_state(project);
    CHECK(saved.module == "fake" && saved.preset == 1);
    // The file's own lines: a value with spaces; a damaged file is none.
    app::SynthFile text;
    text.module = "obxd";
    text.values = {{"mode", "Saw Up"}};
    text.pages = {{"browse", "{\"provider\":\"sampledig\",\"stack\":[\"dig\"]}"}};
    app::SynthFile back;
    app::read_synth(app::synth_text(text), back);
    CHECK(back.module == "obxd" && back.values.size() == 1 && back.values[0].second == "Saw Up");
    CHECK(back.pages.size() == 1 && back.pages[0].first == "browse" && back.pages[0].second == text.pages[0].second);
    app::read_synth("something else\nmodule x\n", back);
    CHECK(back.module.empty());
}

void test_tracks() {
    // Sixteen tracks on Melodee's engine host: a project with a pattern opens
    // and plays it on Play; new, copy and delete; the track buttons; the
    // new-track sheet; tempo and metronome.
    std::string folder = "build/test-samples", home = "build/test-home-tracks", loop = folder + "/loop.wav";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home + "/Projects");
    CHECK(write_test_loop(loop));
    {   // A Melodee project, as the MPC would leave it: a kit on track 1, A01 the loop, four steps on A01 in pattern 1.
        auto project = std::make_unique<melodee::Project>();
        auto patterns = std::make_unique<melodee::PatternBank>();
        melodee::load_sound(project->tracks[0], melodee::EngineKind::Sampler, 0);
        project->tracks[0].pads[0].sample = 0;
        auto& ref = project->samples.edit(0);
        std::snprintf(ref.path, sizeof ref.path, "%s", loop.c_str());
        std::snprintf(ref.name, sizeof ref.name, "loop.wav");
        for (uint32_t beat = 0; beat < 4; ++beat) {
            melodee::Event step;
            step.tick = beat * melodee::ticks_per_quarter;
            step.key = uint8_t(melodee::sampler_first_note);
            step.length = 4;
            patterns->at[0][0].events.push_back(step);
        }
        app::PluginStates none;
        auto bytes = app::project_file_bytes(*project, *patterns, none);
        CHECK(app::write_whole(home + "/Projects/Move.mldp", bytes.data(), bytes.size()).empty());
    }
    auto press = [](app::MelodeeApp& melodee, Button button) {
        melodee.surface(make_event(0xb0, button_cc(button), 127));
        melodee.surface(make_event(0xb0, button_cc(button), 0));
    };
    AudioBlock in {}, out {};
    auto loudest = [&](app::MelodeeApp& melodee, int blocks) {
        int32_t peak = 0;
        for (int block = 0; block < blocks; ++block) { out.fill(0); melodee.render(in, out); peak = std::max(peak, peak_of(out)); }
        return peak;
    };
    app::MelodeeApp melodee(folder, home, "build/test-no-modules");
    // Until a long while is silent (the loop has quiet gaps between its hits).
    auto ring_out = [&] { for (int block = 0, quiet = 0; block < 8000 && quiet < 200; ++block) quiet = loudest(melodee, 1) ? 0 : quiet + 1; };
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    CHECK(melodee.restore_now().empty());
    CHECK(has(melodee.status(), "track=1 kind=kit tracks=1") && has(melodee.status(), "samples=1 pads=1"));
    CHECK(loudest(melodee, 40) == 0); // stopped: silent
    press(melodee, Button::Play);
    CHECK(loudest(melodee, 40) > to_s32(0.05f)); // the pattern plays A01
    melodee.service();
    CHECK(has(melodee.status(), "playing=1") && !has(melodee.status(), "tick=0 "));
    uint32_t before = 0;
    {   // Restarted (the Move's Shift + Play): from the top again.
        std::lock_guard<std::mutex> guard(melodee.studio().lock);
        before = melodee.studio().host().ticks();
        melodee.studio().restart();
    }
    loudest(melodee, 1);
    CHECK(before > 10 && melodee.studio().host().ticks() < 3 && has(melodee.status(), "playing=1"));
    loudest(melodee, 40);
    {   // The same from the Move's keys: Shift + Play.
        std::lock_guard<std::mutex> guard(melodee.studio().lock);
        before = melodee.studio().host().ticks();
    }
    with_shift(melodee, [&] { press(melodee, Button::Play); });
    loudest(melodee, 1);
    CHECK(before > 10 && melodee.studio().host().ticks() < 3 && has(melodee.status(), "playing=1 ") && has(melodee.status(), "job=Restart"));
    press(melodee, Button::Play);
    loudest(melodee, 2);
    CHECK(has(melodee.status(), "playing=0"));
    // The Move's settings: Shift + Step 5 the tempo (the jogwheel two up, push
    // confirms); Shift + Step 6 the metronome on; Shift + Step 7 the groove.
    auto shift_step = [&](unsigned n) { with_shift(melodee, [&] { tap_step(melodee, n); }); };
    shift_step(4);
    CHECK(has(melodee.status(), "page=tempo"));
    melodee.surface(make_event(0xb0, main_knob_cc, 2));
    press(melodee, Button::MainPush);
    CHECK(has(melodee.status(), "page=kit-play") && has(melodee.status(), "tempo=122.0 click=0"));
    shift_step(5);
    CHECK(has(melodee.status(), "page=metronome") && has(melodee.status(), "click=1"));
    press(melodee, Button::Back);
    shift_step(6);
    melodee.surface(make_event(0xb0, main_knob_cc, 3));
    press(melodee, Button::MainPush);
    CHECK(has(melodee.status(), "page=kit-play") && has(melodee.status(), "swing=53"));
    // Shift + Play is the Move's retrigger: stopped, it does nothing and opens nothing.
    with_shift(melodee, [&] { press(melodee, Button::Play); });
    CHECK(has(melodee.status(), "page=kit-play") && has(melodee.status(), "playing=0"));
    // Mute alone: the selected track; Mute + a track button: that one; Shift + Mute + a track button: solo.
    press(melodee, Button::Mute);
    CHECK(has(melodee.status(), "mute=1 solo=0"));
    press(melodee, Button::Mute);
    CHECK(has(melodee.status(), "mute=0 solo=0"));
    melodee.surface(make_event(0xb0, button_cc(Button::Mute), 127));
    with_shift(melodee, [&] { press(melodee, Button::Row1); });
    melodee.surface(make_event(0xb0, button_cc(Button::Mute), 0));
    CHECK(has(melodee.status(), "track=1 kind=kit") && has(melodee.status(), "mute=0 solo=1"));
    melodee.surface(make_event(0xb0, button_cc(Button::Mute), 127));
    with_shift(melodee, [&] { press(melodee, Button::Row1); });
    melodee.surface(make_event(0xb0, button_cc(Button::Mute), 0));
    CHECK(has(melodee.status(), "solo=0"));
    // A track button held, the Volume knob: that track's volume.
    melodee.surface(make_event(0xb0, button_cc(Button::Row1), 127));
    melodee.surface(make_event(0xb0, master_knob_cc, 127)); // one down
    melodee.surface(make_event(0xb0, button_cc(Button::Row1), 0));
    CHECK(melodee.studio().project().tracks[0].mix.level == 99);

    auto& studio = melodee.studio();
    {   // Track 5 a new kit; track 1 can not be made again.
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.new_track(4, app::TrackKind::Kit) && !studio.new_track(0, app::TrackKind::Kit));
        studio.select(4);
    }
    CHECK(has(melodee.status(), "track=5 kind=kit tracks=2"));
    ring_out(); // the pattern's last hit on the loop plays it to its end
    melodee.audio_event(make_event(0x90, pad_note(0), 110));
    CHECK(loudest(melodee, 10) == 0); // a new kit's pads are empty
    melodee.audio_event(make_event(0x80, pad_note(0), 0));
    // The track buttons: the four of track 5's bank; Row 1 is track 5, Row 2 track 6 (empty).
    press(melodee, Button::Row2);
    CHECK(has(melodee.status(), "track=6 kind=empty") && has(melodee.status(), "page=empty-play"));
    // Its push: the new-track sheet; its push again: a kit's sources (Melodee's Drum sources); LIBRARY: a kit, and Load opens for it.
    press(melodee, Button::MainPush);
    CHECK(has(melodee.status(), "page=new-track"));
    press(melodee, Button::MainPush);
    CHECK(has(melodee.status(), "page=kit-sources") && has(melodee.status(), "kit_source=library"));
    press(melodee, Button::MainPush);
    CHECK(has(melodee.status(), "track=6 kind=kit tracks=3") && has(melodee.status(), "page=load-samples"));
    press(melodee, Button::Back);
    {   // Track 1 copied onto track 9: its pads play the loop there.
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.copy_track(0, 8));
        studio.select(8);
    }
    CHECK(has(melodee.status(), "track=9 kind=kit tracks=4"));
    melodee.audio_event(make_event(0x90, pad_note(0), 110));
    CHECK(loudest(melodee, 20) > to_s32(0.05f));
    melodee.audio_event(make_event(0x80, pad_note(0), 0));
    ring_out();
    {   // Deleted: empty again.
        std::lock_guard<std::mutex> guard(studio.lock);
        studio.delete_track(8);
    }
    loudest(melodee, 2);
    melodee.service(); // the packs handed back, freed
    CHECK(has(melodee.status(), "track=9 kind=empty tracks=3") && has(melodee.status(), "page=empty-play"));
    melodee.audio_event(make_event(0x90, pad_note(0), 110));
    CHECK(loudest(melodee, 10) == 0);
    melodee.audio_event(make_event(0x80, pad_note(0), 0));
    // A track button held: the steps are the sixteen tracks.
    auto hold = [&](bool down) { melodee.surface(make_event(0xb0, button_cc(Button::Row1), down ? 127 : 0)); };
    auto step = [&](unsigned n) { melodee.surface(make_event(0x90, step_note(n), 127)); melodee.surface(make_event(0x80, step_note(n), 0)); };
    hold(true);
    step(0);
    CHECK(has(melodee.status(), "track=1 kind=kit"));
    melodee.surface(make_event(0xb0, button_cc(Button::Copy), 127)); // Copy held: track 1, then 11 (empty): copied at once
    step(0);
    step(10);
    melodee.surface(make_event(0xb0, button_cc(Button::Copy), 0));
    CHECK(has(melodee.status(), "tracks=4"));
    melodee.surface(make_event(0xb0, button_cc(Button::Delete), 127)); // Delete held: track 11, asked first
    step(10);
    melodee.surface(make_event(0xb0, button_cc(Button::Delete), 0));
    hold(false);
    CHECK(has(melodee.status(), "page=delete-track") && has(melodee.status(), "tracks=4"));
    press(melodee, Button::MainPush);
    loudest(melodee, 2);
    CHECK(has(melodee.status(), "page=kit-play") && has(melodee.status(), "tracks=3"));
    hold(true); // an empty step: its new-track sheet
    step(11);
    hold(false);
    CHECK(has(melodee.status(), "track=12 kind=empty") && has(melodee.status(), "page=new-track"));
    press(melodee, Button::Back);
    CHECK(has(melodee.status(), "page=empty-play"));
    CHECK(melodee.save_now().empty());
    // Saved whole: the three kits, the pattern, the tempo.
    auto project = std::make_unique<melodee::Project>();
    auto patterns = std::make_unique<melodee::PatternBank>();
    app::PluginStates states;
    CHECK(app::read_project_file(home + "/Projects/Move.mldp", *project, *patterns, states).empty());
    CHECK(project->used_tracks() == 3 && project->tracks[4].engine == melodee::EngineKind::Sampler && project->tempo == 1220 && project->swing == 53);
    CHECK(project->tracks[0].mix.level == 99);
    CHECK(patterns->at[0][0].events.size() == 4);
}

// The steps (palette indices) and the icons under them (brightnesses) after
// one pass of the app's lights.
struct StepLights { std::array<uint8_t, step_count> steps {}, icons {}; };
StepLights step_lights(App& app) {
    Lights lights;
    app.lights(lights, App::Clock::now());
    StepLights shown;
    lights.update([&](const uint8_t* bytes) {
        if (bytes[0] == 0x9f && bytes[1] >= step_note(0) && bytes[1] < step_note(step_count)) shown.steps[bytes[1] - step_note(0)] = bytes[2];
        if (bytes[0] == 0xbf && bytes[1] >= step_icon_cc(0) && bytes[1] < step_icon_cc(step_count)) shown.icons[bytes[1] - step_icon_cc(0)] = bytes[2];
        return true;
    }, 256);
    return shown;
}

void test_step_icons() {
    // The icons under the steps (docs/MOVE_CONTROLS.md, "Lights"): Shift held
    // lights the icon of every step with a job; the metronome's and the
    // groove's stay lit while on; Setup's flashes while it is open.
    std::filesystem::create_directories("build/test-samples");
    std::filesystem::remove_all("build/test-home-icons");
    app::MelodeeApp melodee("build/test-samples", "build/test-home-icons");
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    auto lit = [](const std::array<uint8_t, step_count>& icons) { unsigned n = 0; for (uint8_t v : icons) n += v != 0; return n; };
    CHECK(lit(step_lights(melodee).icons) == 0);
    melodee.surface(make_event(0xb0, button_cc(Button::Shift), 127));
    auto held = step_lights(melodee);
    CHECK(lit(held.icons) >= 10);
    for (unsigned step = 0; step < step_count; ++step) CHECK((held.icons[step] != 0) == (held.steps[step] != 0)); // under the lit steps
    melodee.surface(make_event(0xb0, button_cc(Button::Shift), 0));
    CHECK(lit(step_lights(melodee).icons) == 0);
    {
        std::lock_guard<std::mutex> guard(melodee.studio().lock);
        melodee.studio().set_click(true, melodee.studio().click_level());
        melodee.studio().set_swing(53);
    }
    auto on = step_lights(melodee).icons;
    CHECK(lit(on) == 2 && on[5] && on[6]); // Step 6's metronome, Step 7's groove
    {
        std::lock_guard<std::mutex> guard(melodee.studio().lock);
        melodee.studio().push("setup");
    }
    bool seen_lit = false, seen_dark = false; // it flashes every 400 ms
    for (int n = 0; n < 60 && !(seen_lit && seen_dark); ++n) {
        (step_lights(melodee).icons[1] ? seen_lit : seen_dark) = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    CHECK(seen_lit && seen_dark);
}

void test_migration() {
    // The first scope's project: the kit alone in Move.mldp, each sample's chop
    // in Move.mldp.chops, the synth track in Move.mldp.synth. The first start
    // folds them into the project and keeps the old files aside.
    std::string folder = "build/test-samples", home = "build/test-home-migrate", loop = folder + "/loop.wav";
    std::string path = home + "/Projects/Move.mldp";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home + "/Projects");
    CHECK(write_test_loop(loop));
    {
        auto project = std::make_unique<melodee::Project>();
        auto patterns = std::make_unique<melodee::PatternBank>();
        melodee::load_sound(project->tracks[0], melodee::EngineKind::Sampler, 0);
        project->tracks[0].pads[0].sample = 0;
        auto& ref = project->samples.edit(0);
        std::snprintf(ref.path, sizeof ref.path, "%s", loop.c_str());
        std::snprintf(ref.name, sizeof ref.name, "loop.wav");
        app::PluginStates none;
        auto bytes = app::project_file_bytes(*project, *patterns, none);
        CHECK(app::write_whole(path, bytes.data(), bytes.size()).empty());
        std::ofstream(path + ".chops") << "melodee-move chops 1\n0 0 0 1 2 2 0 500 500 0\n";
        std::ofstream(path + ".synth") << "melodee-move synth 1\ntrack 1\noctave 1\nmodule fake\npreset 1\nparam level 0.13\n";
    }
    {
        app::MelodeeApp melodee(folder, home, "build/test-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        CHECK(has(melodee.status(), "track=2 kind=synth") && has(melodee.status(), "synth=fake octave=1"));
        CHECK(melodee.synth_value("level") == "0.13" && melodee.synth_value("preset") == "1");
        CHECK(!std::filesystem::exists(path + ".synth") && std::filesystem::exists(path + ".synth.migrated"));
        CHECK(!std::filesystem::exists(path + ".chops") && std::filesystem::exists(path + ".chops.migrated"));
    }
    auto project = std::make_unique<melodee::Project>();
    auto patterns = std::make_unique<melodee::PatternBank>();
    app::PluginStates states;
    CHECK(app::read_project_file(path, *project, *patterns, states).empty());
    CHECK(project->chops[0].used() && project->chops[0].slices.count == 2 && project->chops[0].mode == melodee::ChopMode(1));
    const auto& synth = project->tracks[1];
    app::SynthFile saved;
    CHECK(synth.engine == melodee::EngineKind::Plugin && synth.plugin < melodee::max_plugins && app::read_synth_state(states[synth.plugin], saved));
    CHECK(saved.module == "fake" && saved.preset == 1 && std::string(project->plugins[synth.plugin].path).find("fake") != std::string::npos);
}

void test_sampling() {
    // Melodee's Sample Record on the Move's Sampling button (docs/UI.md
    // "Sampling"): the jack picks mic or line, a pad records onto itself,
    // another pad carries the take on onto it, the pad again ends it, a pad
    // held ends it as it is let go; Audio Recall onto the next empty pad;
    // Resample with the pads playing the kit; the input kept in the settings.
    std::string folder = "build/test-samples", home = "build/test-home-sampling", loop = folder + "/loop.wav";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    CHECK(write_test_loop(loop));
    std::string recorded = home + "/Samples/Recorded";
    {
        app::MelodeeApp melodee(folder, home, "build/test-no-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        CHECK(melodee.load_now(loop, 0).empty());
        Lights lights;
        AudioBlock in {}, out {};
        // The input: a 330 Hz tone, half scale, or silence.
        unsigned phase = 0;
        auto blocks = [&](int count, bool tone) {
            int32_t peak = 0;
            for (int block = 0; block < count; ++block) {
                for (unsigned f = 0; f < audio_frames; ++f, ++phase) {
                    int32_t v = tone ? to_s32(0.5f * float(std::sin(6.2831853 * 330.0 * phase / 44100.0))) : 0;
                    in[2 * f] = in[2 * f + 1] = v;
                }
                out.fill(0);
                melodee.render(in, out);
                peak = std::max(peak, peak_of(out));
                if (block % 8 == 0) melodee.lights(lights, App::Clock::now());
            }
            return peak;
        };
        auto settle = [&] { // the service thread's turn: the take written and onto its pads
            for (int n = 0; n < 200; ++n) {
                melodee.service();
                melodee.lights(lights, App::Clock::now());
                if (has(melodee.status(), "rec=idle")) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        // The jack: no cable (the mic), the speaker.
        melodee.surface(make_event(0xb0, audio_in_sense_cc, 0));
        melodee.surface(make_event(0xb0, audio_out_sense_cc, 0));
        CHECK(has(melodee.status(), "source=MIC in=mic out=speaker"));
        press_button(melodee, Button::Sample);
        CHECK(has(melodee.status(), "page=sampling "));
        melodee.lights(lights, App::Clock::now());
        CHECK(has(melodee.status(), "monitor=0")); // Auto, but the mic through the speaker: off, no howl
        // A06 records at once; A07 carries it on; A07 again ends it.
        tap_pad(melodee, 5);
        blocks(2, true);
        CHECK(has(melodee.status(), "rec=recording"));
        blocks(200, true); // 0.58 s
        tap_pad(melodee, 6);
        blocks(100, true);
        tap_pad(melodee, 6);
        blocks(2, false);
        settle();
        CHECK(has(melodee.status(), "rec=idle") && has(melodee.status(), "last_take=TAKE 1"));
        CHECK(std::filesystem::exists(recorded + "/TAKE 1.wav") && !std::filesystem::exists(recorded + "/TAKE 1.wav.part"));
        CHECK(has(melodee.status(), "pad=A07 samples=2 pads=3"));
        press_button(melodee, Button::Back);
        CHECK(has(melodee.status(), "page=kit-play"));
        // A06 plays the take's first part (the tone), A07 the rest.
        auto play = [&](unsigned pad) {
            melodee.audio_event(make_event(0x90, pad_note(pad), 110));
            int32_t peak = blocks(20, false);
            melodee.audio_event(make_event(0x80, pad_note(pad), 0));
            blocks(400, false);
            return peak;
        };
        CHECK(play(5) > to_s32(0.1f));
        CHECK(play(6) > to_s32(0.1f));
        // Held: A08 records while held, ends as it is let go.
        press_button(melodee, Button::Sample);
        melodee.surface(make_event(0x90, pad_note(7), 100));
        blocks(100, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(450));
        CHECK(has(melodee.status(), "rec=recording"));
        melodee.surface(make_event(0x80, pad_note(7), 0));
        blocks(2, true);
        settle();
        CHECK(has(melodee.status(), "last_take=TAKE 2") && has(melodee.status(), "pads=4"));
        // Tapped: it records on after the tap is let go; Sampling ends it.
        tap_pad(melodee, 8);
        blocks(50, true);
        CHECK(has(melodee.status(), "rec=recording"));
        press_button(melodee, Button::Sample); // a take: ends; Sampling Mode stays
        blocks(2, false);
        settle();
        CHECK(has(melodee.status(), "last_take=TAKE 3") && has(melodee.status(), "page=sampling "));
        press_button(melodee, Button::Sample); // again: leaves
        CHECK(has(melodee.status(), "page=kit-play"));
        // Audio Recall: the last phrase of the ring onto the next empty pad after the last one (A09: A10).
        blocks(300, false);
        blocks(200, true);
        blocks(400, false);
        with_shift(melodee, [&] { press_button(melodee, Button::Sample); });
        settle();
        for (int n = 0; n < 50 && !std::filesystem::exists(recorded + "/RECALL 1.wav"); ++n) melodee.service();
        CHECK(std::filesystem::exists(recorded + "/RECALL 1.wav"));
        CHECK(has(melodee.status(), "pad=A10") && has(melodee.status(), "pads=6"));
        press_button(melodee, Button::Back);
        // A cable goes in: the line in.
        melodee.surface(make_event(0xb0, audio_in_sense_cc, 127));
        CHECK(has(melodee.status(), "source=LINE in=line"));
        // RESAMPLE (knob 1: LINE, LINE L, LINE R, RESAMPLE); A12 is the take's, then A01 plays the kit into it.
        press_button(melodee, Button::Sample);
        melodee.surface(make_event(0xb0, knob_first_cc, 3));
        CHECK(has(melodee.status(), "source=RSMP"));
        tap_pad(melodee, 11);
        melodee.lights(lights, App::Clock::now());
        blocks(4, false);
        CHECK(has(melodee.status(), "rec=recording"));
        melodee.audio_event(make_event(0x90, pad_note(0), 110)); // the pads play the kit while it records
        CHECK(blocks(40, false) > to_s32(0.05f));
        melodee.audio_event(make_event(0x80, pad_note(0), 0));
        press_button(melodee, Button::MainPush); // the push ends it
        blocks(2, false);
        settle();
        CHECK(has(melodee.status(), "last_take=RESAMPLE 1"));
        CHECK(std::filesystem::exists(recorded + "/RESAMPLE 1.wav"));
        // LENGTH SEQ, stopped: the pad starts Play and the take is one pass of the kit's pattern (a bar, 2 s).
        melodee.surface(make_event(0xb0, uint8_t(knob_first_cc + 4), 1));
        tap_pad(melodee, 12);
        blocks(4, false);
        CHECK(has(melodee.status(), "playing=1"));
        blocks(720, false);
        settle();
        CHECK(has(melodee.status(), "last_take=RESAMPLE 2"));
        press_button(melodee, Button::Play);
        melodee.surface(make_event(0xb0, uint8_t(knob_first_cc + 4), 127)); // FREE again
        // START at -24 dB: armed, it waits through silence, the tone starts it.
        melodee.surface(make_event(0xb0, knob_first_cc, 125)); // SOURCE back to the line (RESAMPLE, LINE R, LINE L, LINE)
        CHECK(has(melodee.status(), "source=LINE "));
        melodee.surface(make_event(0xb0, uint8_t(knob_first_cc + 3), 49));
        tap_pad(melodee, 13);
        blocks(100, false);
        CHECK(has(melodee.status(), "rec=armed"));
        blocks(100, true);
        CHECK(has(melodee.status(), "rec=recording"));
        press_button(melodee, Button::Sample);
        blocks(2, false);
        settle();
        CHECK(has(melodee.status(), "last_take=TAKE 4"));
        // Shift + the wheel: the SAMPLING settings; knob 1 Recall's cut on to FIRST.
        with_shift(melodee, [&] { turn_wheel(melodee, 1); });
        melodee.surface(make_event(0xb0, knob_first_cc, 1));
        press_button(melodee, Button::Back);
    }
    // The input as set is kept with the Move's settings.
    std::ifstream settings(home + "/Move.settings");
    std::string text((std::istreambuf_iterator<char>(settings)), std::istreambuf_iterator<char>());
    CHECK(has(text, "in_source 1") && has(text, "in_threshold 72") && has(text, "recall_grab 1"));
    for (const char* kept : {"Recorded", "Edits", "Banks", "Looper", "Expansions", "Keygroups", "Stems"})
        CHECK(std::filesystem::is_directory(home + "/Samples/" + kept));
}

void test_sample_files() {
    // The browser's starred and recent files, its preview, Delete (asked;
    // what the project plays kept); Trim and Save Sample; a kit's sources on
    // the new-track sheet; an MPC expansion installed.
    std::string folder = "build/test-samples-files", home = "build/test-home-files", loop = folder + "/loop.wav";
    std::filesystem::remove_all(folder);
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(folder);
    CHECK(write_test_loop(loop) && write_test_loop(folder + "/spare.wav") && write_test_loop(folder + "/unused.wav"));
    {   // An expansion: a zip with its Expansion.xml, a program and its preview, a sample.
        juce::ZipFile::Builder zip;
        auto add = [&](const char* name, const std::string& text) {
            zip.addEntry(std::make_unique<juce::MemoryInputStream>(text.data(), text.size(), true), 0, name, juce::Time::getCurrentTime());
        };
        add("Expansion.xml", "<?xml version=\"1.0\"?><expansion><title>Test Drums</title></expansion>");
        add("Kit.xpm", "<MPCVObject/>");
        std::ifstream wav(loop, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(wav)), std::istreambuf_iterator<char>());
        add("Samples/Kick.wav", bytes);
        add("[Previews]/Kit.xpm.wav", bytes);
        juce::FileOutputStream out(juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(folder + "/Test.xpn")));
        double progress = 0;
        CHECK(out.openedOk() && zip.writeToStream(out, &progress));
    }
    app::MelodeeApp melodee(folder, home, "build/test-no-modules");
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    CHECK(melodee.restore_now().empty());
    CHECK(melodee.load_now(loop, 0).empty());
    Lights lights;
    AudioBlock in {}, out {};
    auto settle = [&] {
        for (int n = 0; n < 20; ++n) {
            melodee.service();
            out.fill(0);
            melodee.render(in, out);
            melodee.lights(lights, App::Clock::now());
        }
    };
    // Load: the wheel's push on the pad. One place: straight in, its files by name (loop, spare, Test.xpn, unused).
    press_button(melodee, Button::MainPush);
    CHECK(has(melodee.status(), "page=load-samples") && has(melodee.status(), "browse=folder"));
    settle();
    CHECK(has(melodee.status(), "preview=loop.wav")); // it plays as the wheel lands
    with_shift(melodee, [&] { press_button(melodee, Button::MainPush); }); // starred
    CHECK(has(melodee.status(), "starred=1"));
    turn_wheel(melodee, 1);
    settle();
    CHECK(has(melodee.status(), "preview=spare.wav"));
    press_button(melodee, Button::MainPush); // spare.wav onto A01: into RECENT
    settle();
    CHECK(has(melodee.status(), "recent=1"));
    CHECK(std::filesystem::exists(home + "/Starred.txt") && std::filesystem::exists(home + "/Recent.txt"));
    // Delete: spare.wav is A01's now, so kept; loop.wav is in the project still (Melodee's rule), kept; unused.wav: asked, then gone.
    press_button(melodee, Button::Delete);
    CHECK(has(melodee.status(), "page=delete-file"));
    press_button(melodee, Button::MainPush);
    settle();
    CHECK(std::filesystem::exists(folder + "/spare.wav"));
    press_button(melodee, Button::Back); // out, and in again: STARRED and RECENT lead the top
    press_button(melodee, Button::MainPush);
    CHECK(has(melodee.status(), "page=load-samples"));
    turn_wheel(melodee, -10);
    turn_wheel(melodee, 2); // STARRED, RECENT, loop.wav
    press_button(melodee, Button::Delete);
    press_button(melodee, Button::MainPush);
    settle();
    CHECK(std::filesystem::exists(loop));
    turn_wheel(melodee, 10); // unused.wav
    press_button(melodee, Button::Delete);
    press_button(melodee, Button::Back); // no
    settle();
    CHECK(std::filesystem::exists(folder + "/unused.wav"));
    press_button(melodee, Button::Delete);
    press_button(melodee, Button::MainPush); // yes
    settle();
    CHECK(!std::filesystem::exists(folder + "/unused.wav") && has(melodee.status(), "starred=1"));
    // The expansion (the last row): installed into Samples/Expansions/<its title>, then opened there.
    turn_wheel(melodee, 10);
    press_button(melodee, Button::MainPush);
    settle();
    CHECK(std::filesystem::exists(home + "/Samples/Expansions/Test Drums/Samples/Kick.wav"));
    CHECK(has(melodee.status(), "browse=folder"));
    turn_wheel(melodee, -10); // Kit.xpm, first: its own preview plays
    settle();
    CHECK(has(melodee.status(), "preview=Kit.xpm"));
    press_button(melodee, Button::Back);
    CHECK(has(melodee.status(), "page=kit-play"));
    // Trim: Sample Options, TRIM: the region a file of its own, A01 onto it.
    auto options = [&](int row) {
        with_shift(melodee, [&] { press_button(melodee, Button::MainPush); });
        turn_wheel(melodee, row);
        press_button(melodee, Button::MainPush);
    };
    melodee.surface(make_event(0xb0, uint8_t(knob_first_cc + 3), 20)); // knob 4: A01 starts later
    options(2);
    settle();
    CHECK(std::filesystem::exists(home + "/Samples/Edits/SPARE-Trimmed.wav"));
    CHECK(has(melodee.status(), "samples=3"));
    // Save Sample (kit depth's row): the name typed, the sample's own given
    // in capitals, then the strip's OK (the Move's own typing): done.
    options(4);
    CHECK(has(melodee.status(), "typing=1"));
    tap_pad(melodee, 7); // the keyboard's backspace, bottom right
    turn_wheel(melodee, 63); // the strip's end: OK
    press_button(melodee, Button::MainPush);
    settle();
    CHECK(has(melodee.status(), "typing=0"));
    CHECK(std::filesystem::exists(home + "/Samples/SPARE-TRIMME.wav"));
    // A kit's sources: EMPTY makes a kit with nothing on it.
    melodee.surface(make_event(0xb0, button_cc(Button::Row1), 127));
    tap_step(melodee, 2);
    melodee.surface(make_event(0xb0, button_cc(Button::Row1), 0));
    melodee.lights(lights, App::Clock::now());
    CHECK(has(melodee.status(), "track=3 kind=empty") && has(melodee.status(), "page=new-track"));
    press_button(melodee, Button::MainPush);
    turn_wheel(melodee, 1);
    CHECK(has(melodee.status(), "kit_source=empty"));
    press_button(melodee, Button::MainPush);
    CHECK(has(melodee.status(), "track=3 kind=kit") && has(melodee.status(), "page=kit-play") && has(melodee.status(), "pads=0"));
}

void test_chop_depth() {
    // Chop on the Move past the first scope: slices 33-64 on Plus, Delete +
    // a pad, the slice options (Extract, Split, Merge, Remove all cuts), a
    // manual take with the red pad, a step, To Pads KIT.
    std::string folder = "build/test-samples", home = "build/test-home-chop", loop = folder + "/loop.wav";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    CHECK(write_test_loop(loop));
    app::MelodeeApp melodee(folder, home, "build/test-no-modules");
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    CHECK(melodee.restore_now().empty());
    CHECK(melodee.load_now(loop, 0).empty());
    Lights lights;
    AudioBlock in {}, out {};
    auto settle = [&] {
        for (int n = 0; n < 20; ++n) { melodee.service(); out.fill(0); melodee.render(in, out); melodee.lights(lights, App::Clock::now()); }
    };
    auto knob = [&](unsigned n, int detents) { melodee.surface(make_event(0xb0, uint8_t(knob_first_cc + n), uint8_t(detents < 0 ? 128 + detents : detents))); };
    auto options = [&](int row) {
        std::this_thread::sleep_for(std::chrono::milliseconds(320)); // two Shift presses within 0.3 s would lock it (the Move's)
        with_shift(melodee, [&] { press_button(melodee, Button::MainPush); });
        CHECK(has(melodee.status(), "page=slice-options"));
        turn_wheel(melodee, row);
        press_button(melodee, Button::MainPush);
    };
    open_chop(melodee);
    knob(2, 1);
    knob(2, 1);  // type: Auto, Beats, Regions (a step a turn)
    knob(3, 24); // 16 + 24: 40 slices
    CHECK(has(melodee.status(), "slices=40 chop=Regions"));
    press_button(melodee, Button::Up); // Plus: slices 33-40 on the pads
    CHECK(has(melodee.status(), "bank=2"));
    tap_pad(melodee, 2);
    CHECK(has(melodee.status(), "slice=35"));
    press_button(melodee, Button::Down);
    CHECK(has(melodee.status(), "bank=1"));
    melodee.surface(make_event(0xb0, button_cc(Button::Delete), 127)); // Delete + A02: its cut out
    tap_pad(melodee, 1);
    melodee.surface(make_event(0xb0, button_cc(Button::Delete), 0));
    CHECK(has(melodee.status(), "slices=39"));
    tap_step(melodee, 4); // the sample from its fifth sixteenth
    tap_pad(melodee, 3);
    options(0); // Extract: slice 4 onto the next empty pad
    settle();
    CHECK(std::filesystem::exists(home + "/Samples/Edits/LOOP-Slice 4.wav"));
    CHECK(has(melodee.status(), "page=chop") && has(melodee.status(), "slices=39"));
    options(1); // Split
    CHECK(has(melodee.status(), "slices=40"));
    options(2); // Merge
    CHECK(has(melodee.status(), "slices=39"));
    std::this_thread::sleep_for(std::chrono::milliseconds(320));
    with_shift(melodee, [&] { turn_wheel(melodee, 1); }); // the start from hit to hit
    CHECK(has(melodee.status(), "page=chop"));
    options(3); // Remove all cuts: a manual take, from the first pad
    CHECK(has(melodee.status(), "slices=1 chop=Manual") && has(melodee.status(), "manual_next=1"));
    tap_pad(melodee, 0); // the region plays; the red moves to A02
    CHECK(has(melodee.status(), "manual_next=2"));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    tap_pad(melodee, 1); // the red pad cuts
    CHECK(has(melodee.status(), "slices=2") && has(melodee.status(), "manual_next=3"));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    tap_pad(melodee, 2);
    CHECK(has(melodee.status(), "slices=3") && has(melodee.status(), "manual_next=4"));
    tap_pad(melodee, 1); // a lit pad: plays again from its slice, the cut after it gone
    CHECK(has(melodee.status(), "slices=2") && has(melodee.status(), "manual_next=3"));
    knob(0, 1); // another control: the take stops short, the red stays
    CHECK(has(melodee.status(), "manual_next=3"));
    // To Pads KIT: a pad a sound.
    knob(2, -1); // Threshold
    press_button(melodee, Button::MainPush);
    CHECK(has(melodee.status(), "page=to-pads"));
    turn_wheel(melodee, 3);
    press_button(melodee, Button::MainPush);
    CHECK(has(melodee.status(), "page=kit-play"));
}

void test_audio_track() {
    // An audio track (guide/audio-tracks.md): Sampling opens its INPUT, Rec
    // arms it, Play records one pass of its pattern into "Move Audio/Take
    // 001.wav", which the pattern then plays; its CLIP's level; the take back
    // after a restart.
    std::string folder = "build/test-samples", home = "build/test-home-audio";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    AudioBlock in {}, out {};
    unsigned phase = 0;
    Lights lights;
    auto blocks = [&](app::MelodeeApp& melodee, int count, bool tone) {
        int32_t peak = 0;
        for (int block = 0; block < count; ++block) {
            for (unsigned f = 0; f < audio_frames; ++f, ++phase) {
                int32_t v = tone ? to_s32(0.4f * float(std::sin(6.2831853 * 220.0 * phase / 44100.0))) : 0;
                in[2 * f] = in[2 * f + 1] = v;
            }
            out.fill(0);
            melodee.render(in, out);
            peak = std::max(peak, peak_of(out));
            if (block % 16 == 0) { melodee.service(); melodee.lights(lights, App::Clock::now()); }
        }
        return peak;
    };
    {
        app::MelodeeApp melodee(folder, home, "build/test-no-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        {
            std::lock_guard<std::mutex> guard(melodee.studio().lock);
            CHECK(melodee.studio().new_track(2, app::TrackKind::Audio));
            melodee.studio().select(2);
        }
        melodee.surface(make_event(0xb0, audio_in_sense_cc, 127)); // a cable in: the line
        CHECK(has(melodee.status(), "track=3 kind=audio") && has(melodee.status(), "page=audio-play"));
        press_button(melodee, Button::Sample);
        CHECK(has(melodee.status(), "page=audio-input"));
        press_button(melodee, Button::Rec);
        CHECK(has(melodee.status(), "audio_armed=1"));
        blocks(melodee, 20, true);
        press_button(melodee, Button::Play); // one pass of the pattern (a bar, 2 s at 120 BPM) from its top
        blocks(melodee, 200, true);
        CHECK(has(melodee.status(), "audio_arm=-2")); // recording
        blocks(melodee, 700, true);
        for (int n = 0; n < 50; ++n) blocks(melodee, 1, false);
        CHECK(std::filesystem::exists(home + "/Projects/Move Audio/Take 001.wav"));
        press_button(melodee, Button::Back);
        CHECK(has(melodee.status(), "page=audio-play") && has(melodee.status(), "take=1 "));
        CHECK(blocks(melodee, 800, false) > to_s32(0.1f)); // the pattern plays its take, the input silent
        melodee.surface(make_event(0xb0, knob_first_cc, 124)); // knob 1: the level 4 x 0.5 dB down
        CHECK(has(melodee.status(), "take_gain=-20"));
        press_button(melodee, Button::Play);
        CHECK(melodee.save_now().empty());
    }
    {   // Again: the take read back for the pattern, and played.
        app::MelodeeApp melodee(folder, home, "build/test-no-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        CHECK(has(melodee.status(), "kind=audio") && has(melodee.status(), "take=1 ") && has(melodee.status(), "take_gain=-20"));
        blocks(melodee, 20, false);
        press_button(melodee, Button::Play);
        CHECK(blocks(melodee, 800, false) > to_s32(0.08f));
    }
}

void test_sampling_pad_layout() {
    // Sampling and Chop by the pad's number in the pad layout (app/pad_layout.h):
    // in Banks, the bottom row's fifth pad is B01 (the right bank's first), its
    // second row's first A05; Audio Recall's next empty pad is in that order.
    std::string folder = "build/test-samples", home = "build/test-home-layout", loop = folder + "/loop.wav";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    CHECK(write_test_loop(loop));
    {
        app::MelodeeApp melodee(folder, home, "build/test-no-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        app::set_pad_layout(app::PadLayout::Banks); // after Move.settings is read (Rows unless it says Banks), as Setup sets it
        CHECK(melodee.load_now(loop, 0).empty());
        Lights lights;
        AudioBlock in {}, out {};
        unsigned phase = 0;
        auto blocks = [&](int count) {
            for (int block = 0; block < count; ++block) {
                for (unsigned f = 0; f < audio_frames; ++f, ++phase) in[2 * f] = in[2 * f + 1] = to_s32(0.5f * float(std::sin(6.2831853 * 330.0 * phase / 44100.0)));
                out.fill(0);
                melodee.render(in, out);
                if (block % 8 == 0) melodee.lights(lights, App::Clock::now());
            }
        };
        auto settle = [&] {
            for (int n = 0; n < 200 && !has(melodee.status(), "rec=idle"); ++n) { melodee.service(); melodee.lights(lights, App::Clock::now()); }
            for (int n = 0; n < 5; ++n) melodee.service();
        };
        press_button(melodee, Button::Sample);
        tap_pad(melodee, 4); // physical bottom row, fifth: B01
        blocks(150);
        tap_pad(melodee, 8); // physical second row, first: A05, the take carried on onto it
        blocks(150);
        tap_pad(melodee, 8);
        blocks(2);
        settle();
        CHECK(has(melodee.status(), "pad=A05 samples=2 pads=3") && has(melodee.status(), "last_take=TAKE 1"));
        press_button(melodee, Button::Back);
        with_shift(melodee, [&] { press_button(melodee, Button::Sample); }); // Audio Recall: the next empty after A05 is A06
        settle();
        for (int n = 0; n < 50 && !has(melodee.status(), "pad=A06"); ++n) melodee.service();
        CHECK(has(melodee.status(), "pad=A06") && has(melodee.status(), "pads=4"));
        press_button(melodee, Button::Back);
        tap_pad(melodee, 0); // A01 again
        open_chop(melodee);
        melodee.surface(make_event(0xb0, uint8_t(knob_first_cc + 2), 1));
        melodee.surface(make_event(0xb0, uint8_t(knob_first_cc + 2), 1)); // Regions, 16 slices
        tap_pad(melodee, 4); // B01: slice 17 would be; 16 slices: the pad has none
        CHECK(has(melodee.status(), "slices=16") && has(melodee.status(), "slice=1 "));
        tap_pad(melodee, 9); // physical second row, second: A06, slice 6
        CHECK(has(melodee.status(), "slice=6 "));
    }
    app::set_pad_layout(app::PadLayout::Rows);
}

} // namespace

void test_kit_depth() {
    // Kit depth (docs/UI.md "Kits"): 128 pads in eight banks, Plus and Minus
    // moving the 32 by 16; 16 Pitches; copying and clearing pads; the pad
    // pages on Shift + turn; Sample Options; a long sample read from storage
    // as it plays; bank files.
    std::string folder = "build/test-samples", home = "build/test-home-kit", loop = folder + "/loop.wav";
    std::filesystem::create_directories(folder);
    std::filesystem::remove_all(home);
    CHECK(write_test_loop(loop));
    app::MelodeeApp melodee(folder, home, "build/test-no-modules");
    Session session;
    melodee.attach(session);
    melodee.audio_opened(AudioFormat {});
    auto& studio = melodee.studio();
    AudioBlock in {}, out {};
    auto play = [&](unsigned pad, int blocks) {
        melodee.audio_event(make_event(0x90, pad_note(pad), 110));
        int32_t peak = 0;
        for (int block = 0; block < blocks; ++block) { out.fill(0); melodee.render(in, out); peak = std::max(peak, peak_of(out)); }
        melodee.audio_event(make_event(0x80, pad_note(pad), 0));
        // Played out: a third of a second of silence (the loop has quiet between its hits).
        for (int block = 0, silent = 0; block < 8000 && silent < 120; ++block) { out.fill(0); melodee.render(in, out); silent = peak_of(out) ? 0 : silent + 1; }
        return peak;
    };
    auto status = [&] { return melodee.status(); };
    auto hold = [&](Button button, auto&& what) {
        melodee.surface(make_event(0xb0, button_cc(button), 127));
        what();
        melodee.surface(make_event(0xb0, button_cc(button), 0));
    };
    auto used = [&](unsigned pad) { std::lock_guard<std::mutex> guard(studio.lock); return studio.current().pads[pad].used(); };
    CHECK(melodee.load_now(loop, 0).empty());  // A01
    CHECK(melodee.load_now(loop, 40).empty()); // C09
    // Plus and Minus: the 32 pads move 16 at a time through the eight banks.
    CHECK(has(status(), " bank=0 "));
    CHECK(play(0, 20) > to_s32(0.1f));     // A01
    CHECK(play(24, 20) == 0);              // B09: empty
    press_button(melodee, Button::Up);
    CHECK(has(status(), " bank=1 "));
    CHECK(play(24, 20) > to_s32(0.1f));    // C09 is now the pad A09 was
    CHECK(play(0, 20) == 0);               // B01
    for (int n = 0; n < 9; ++n) press_button(melodee, Button::Up);
    CHECK(has(status(), " bank=6 "));      // G-H, the last two
    for (int n = 0; n < 9; ++n) press_button(melodee, Button::Down);
    CHECK(has(status(), " bank=0 "));
    // Shift + Plus or Minus: four pads at a time.
    with_shift(melodee, [&] { press_button(melodee, Button::Up); });
    CHECK(has(status(), " first=A05 "));
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.kit_pad_at(0) == 4 && studio.kit_pad_at(31) == 35);
    }
    press_button(melodee, Button::Up); // a bank on, still four in
    CHECK(has(status(), " bank=1 first=B05 "));
    press_button(melodee, Button::Down);
    with_shift(melodee, [&] { press_button(melodee, Button::Down); });
    CHECK(has(status(), " bank=0 first=A01 "));
    tap_pad(melodee, 0);
    CHECK(has(status(), "pad=A01 "));
    {   // The pad layout (app/pad_layout.h): in Banks the right 4x4 is the second bank.
        app::set_pad_layout(app::PadLayout::Banks);
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.kit_pad_at(4) == 16 && studio.kit_pad_at(8) == 4 && studio.kit_pad_at(31) == 31);
        app::set_pad_layout(app::PadLayout::Rows);
        CHECK(studio.kit_pad_at(4) == 4 && studio.kit_pad_at(8) == 8);
    }
    // Copy held: A01 then A06 (empty) copies; then onto A06 again asks first.
    hold(Button::Copy, [&] { tap_pad(melodee, 0); tap_pad(melodee, 5); });
    CHECK(used(5));
    CHECK(play(5, 20) > to_s32(0.1f));
    hold(Button::Copy, [&] { tap_pad(melodee, 0); tap_pad(melodee, 5); });
    CHECK(has(status(), "page=copy-pad"));
    press_button(melodee, Button::MainPush);
    CHECK(has(status(), "page=kit-play"));
    // Delete held: a pad with no notes is emptied; an empty pad closes the gap.
    hold(Button::Delete, [&] { tap_pad(melodee, 5); });
    CHECK(!used(5));
    CHECK(play(5, 20) == 0);
    CHECK(melodee.load_now(loop, 3).empty()); // A04
    hold(Button::Delete, [&] { tap_pad(melodee, 2); }); // A03, empty: A04 moves down onto it
    CHECK(used(2) && !used(3));
    CHECK(play(2, 20) > to_s32(0.1f));
    // Delete with notes: the notes go first, the sound stays.
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        melodee::Event note;
        note.key = uint8_t(melodee::sampler_first_note + 2);
        studio.patterns().at[studio.track()][studio.current().pattern].events.push_back(note);
    }
    hold(Button::Delete, [&] { tap_pad(melodee, 2); });
    CHECK(used(2));
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.patterns().at[studio.track()][studio.current().pattern].events.empty());
    }
    // 16 Pitches: Shift + Step 8; the left 16 bank A as the MPC lays it, the right 16 A01's pitches.
    tap_pad(melodee, 0);
    with_shift(melodee, [&] { tap_step(melodee, 7); });
    CHECK(has(status(), " pitches=1 "));
    CHECK(play(4, 20) > to_s32(0.05f));    // a pitch of A01
    CHECK(play(28, 20) > to_s32(0.05f));   // the top right one too
    CHECK(play(8, 20) == 0);               // left, second row: A05, empty
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.kit_pad_at(8) == 4 && studio.kit_pad_at(4) < 0);
        int low = studio.pitch_semitones(0), high = studio.pitch_semitones(15);
        CHECK(high > low);
    }
    press_button(melodee, Button::Up);     // the pitches' octave
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.current().octave == melodee::kit_level_octave + 1);
    }
    press_button(melodee, Button::Down);
    with_shift(melodee, [&] { tap_step(melodee, 7); });
    CHECK(has(status(), " pitches=0 "));
    // The pad pages: Shift + turn walks them, past either end back to Play.
    // (One Shift held for the three turns: two presses close together would lock it, as on the Move.)
    with_shift(melodee, [&] {
        turn_wheel(melodee, 1);
        CHECK(has(status(), "page=kit-pages") && has(status(), "kit_page=TRIM"));
        turn_wheel(melodee, 1); // LAYERS
        turn_wheel(melodee, 1); // TUNE
    });
    CHECK(has(status(), "kit_page=TUNE"));
    melodee.surface(make_event(0xb0, knob_first_cc + 2, 1)); // WARP: Seq
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.current().pads[0].warp == melodee::warp_seq);
    }
    melodee.surface(make_event(0xb0, knob_first_cc + 4, 1)); // FOLLOW, the kit's
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.current().params[melodee::sampler::Follow] == 1);
    }
    melodee.surface(make_event(0xb0, knob_first_cc + 2, 127)); // WARP back off
    press_button(melodee, Button::Back);
    CHECK(has(status(), "page=kit-play"));
    with_shift(melodee, [&] {
        turn_wheel(melodee, -1); // the last page
        CHECK(has(status(), "kit_page=KIT FILT"));
        turn_wheel(melodee, 1); // past the end: Play
    });
    CHECK(has(status(), "page=kit-play"));
    press_button(melodee, Button::Back); // between two Shift presses: no lock
    // Sample Options: Reverse, Normalize, Trim.
    with_shift(melodee, [&] { press_button(melodee, Button::MainPush); });
    CHECK(has(status(), "page=sample-options"));
    turn_wheel(melodee, 1);
    press_button(melodee, Button::MainPush); // REVERSE
    CHECK(has(status(), " reverse=1 "));
    press_button(melodee, Button::MainPush); // and back
    turn_wheel(melodee, 2);
    press_button(melodee, Button::MainPush); // NORMALIZE
    CHECK(!has(status(), " gain=0"));
    turn_wheel(melodee, -1);
    press_button(melodee, Button::MainPush); // TRIM SAMPLE: the region as a file, the pad onto it
    melodee.service();
    CHECK(std::filesystem::exists(home + "/Samples/Edits/LOOP-Trimmed.wav"));
    // A long sample (12 s) is read from storage as it plays.
    std::string long_file = folder + "/long.wav";
    {
        melodee::SampleData tone;
        tone.rate = 44100;
        tone.frames = 12 * 44100;
        tone.left.resize(tone.frames);
        for (uint32_t n = 0; n < tone.frames; ++n) tone.left[n] = 0.5f * std::sin(6.2831853f * 220.0f * float(n) / 44100.0f);
        tone.right = tone.left;
        CHECK(melodee::write_wav(long_file, tone).empty());
    }
    CHECK(melodee.load_now(long_file, 6).empty()); // A07
    CHECK(has(status(), " streamed=1 "));
    melodee.service(); // its start held in memory
    CHECK(play(6, 40) > to_s32(0.1f));
    // A bank saved and loaded into another bank.
    {
        std::lock_guard<std::mutex> guard(studio.lock);
        CHECK(studio.save_bank_file(0, "TEST BANK").empty());
        CHECK(studio.load_bank_file(home + "/Samples/Banks/TEST BANK.mldb", 3).empty()); // into D
        CHECK(studio.current().pads[48].used() && studio.current().pads[48 + 6].used());
    }
    melodee.service(); // its files: the same slots, nothing read again
    // Shift held in the samples browser: its options (Save Sample, Save Bank).
    press_button(melodee, Button::MainPush); // the wheel's push on the pad: the samples browser
    CHECK(has(status(), "page=load-samples"));
    melodee.surface(make_event(0xb0, button_cc(Button::Shift), 127));
    {
        Lights lights;
        melodee.lights(lights, std::chrono::steady_clock::now() + std::chrono::milliseconds(500));
    }
    melodee.surface(make_event(0xb0, button_cc(Button::Shift), 0));
    CHECK(has(status(), "page=browser-options"));
    press_button(melodee, Button::Back);
    CHECK(has(status(), "page=load-samples"));
    press_button(melodee, Button::Back);
    press_button(melodee, Button::Up);
    press_button(melodee, Button::Up);
    CHECK(has(status(), " bank=2 "));
    CHECK(play(16, 20) > to_s32(0.1f)); // D01, as A01
    // Chop, with banks C-D in view: the pads are its slices all the same.
    tap_pad(melodee, 16);
    open_chop(melodee);
    CHECK(has(status(), "page=chop "));
    CHECK(play(1, 20) > to_s32(0.05f)); // slice 2, not C02
    press_button(melodee, Button::Back);
    CHECK(play(1, 20) == 0);            // C02 again: empty
}

// A loop's field in the status line ("loop1=PLAY,len=88200,..."): the state, or a number after `key=`.
std::string loop_field(const std::string& status, unsigned loop, const std::string& key = "") {
    std::string tag = " loop" + std::to_string(loop) + "=";
    auto at = status.find(tag);
    if (at == std::string::npos) return {};
    auto end = status.find(' ', at + 1);
    std::string field = status.substr(at + tag.size(), end == std::string::npos ? std::string::npos : end - at - tag.size());
    if (key.empty()) return field.substr(0, field.find(','));
    auto k = field.find("," + key + "=");
    if (k == std::string::npos) return {};
    auto from = k + key.size() + 2;
    return field.substr(from, field.find(',', from) - from);
}

// The audio thread, as the runtime runs it, for what waits on it (a loop held to be saved or committed).
struct AudioThread {
    app::MelodeeApp& app;
    std::atomic<bool> running {true};
    std::thread thread;
    explicit AudioThread(app::MelodeeApp& melodee) : app(melodee), thread([this] {
        AudioBlock in {}, out {};
        while (running) { out.fill(0); app.render(in, out); std::this_thread::sleep_for(std::chrono::microseconds(300)); }
    }) {}
    ~AudioThread() { running = false; thread.join(); }
};

// The Looper's pads in both layouts (app/pad_layout.h): the loops by logical
// number, on the same physical pads, beside the track buttons.
void test_looper_pads() {
    using app::LoopDesk;
    using app::PadLayout;
    for (PadLayout layout : {PadLayout::Rows, PadLayout::Banks})
        for (unsigned physical = 0; physical < 32; ++physical) {
            unsigned row = physical / 8, column = physical % 8;
            auto at = LoopDesk::pad_at(app::logical_pad(uint8_t(physical), layout), layout);
            if (column < 4) CHECK(at.loop == int(3 - row) && at.job == column && at.track < 0);
            else CHECK(at.loop < 0 && at.track == int(row * 4 + column - 4));
        }
    for (unsigned loop = 0; loop < 4; ++loop)
        for (unsigned job = 0; job < 4; ++job)
            for (PadLayout layout : {PadLayout::Rows, PadLayout::Banks}) {
                uint8_t logical = LoopDesk::loop_pad(loop, job, layout);
                CHECK(app::physical_pad(logical, layout) == (3 - loop) * 8 + job);
                auto at = LoopDesk::pad_at(logical, layout);
                CHECK(at.loop == int(loop) && at.job == job);
            }
    // Rows: L1 is pads 25-28, L4 1-4; Banks: L1 13-16, L4 1-4, the track's 16 the right bank, 17-32.
    CHECK(LoopDesk::loop_pad(0, 0, PadLayout::Rows) == 24 && LoopDesk::loop_pad(3, 3, PadLayout::Rows) == 3);
    CHECK(LoopDesk::loop_pad(0, 0, PadLayout::Banks) == 12 && LoopDesk::loop_pad(3, 0, PadLayout::Banks) == 0);
    CHECK(LoopDesk::track_pad(0, PadLayout::Banks) == 16 && LoopDesk::track_pad(15, PadLayout::Banks) == 31);
    CHECK(LoopDesk::track_pad(0, PadLayout::Rows) == 4 && LoopDesk::track_pad(15, PadLayout::Rows) == 31);
}

void test_looper() {
    // The Looper (docs/UI.md, "Looper"): Shift + Step 12 asks, the push turns
    // it on and its memory is made; a loop's row of pads records in the cycle
    // it was read; a take from the input through a 428-frame round trip
    // lands on the beat once the round trip is set (and that far late
    // without it); the loops are kept with the project and come back; Commit
    // to pad (+ note); a clear pad held; off.
    namespace fs = std::filesystem;
    std::string folder = "build/test-samples", home = "build/test-home-looper";
    fs::create_directories(folder);
    fs::remove_all(home);
    std::string loop_file = home + "/Projects/Move Loops/Loop 1.wav";
    auto press = [](app::MelodeeApp& melodee, Button button) { press_button(melodee, button); };
    constexpr unsigned round_trip = 428; // the simulated Move's with a 300-frame cable (build.sh's spike run)
    uint32_t taken = 0;
    {
        app::MelodeeApp melodee(folder, home, "build/test-no-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        auto& studio = melodee.studio();
        // A cable from the output into the input: what went out comes back round_trip frames later.
        std::vector<int32_t> cable(2 * round_trip, 0);
        size_t at = 0;
        AudioBlock in {}, out {};
        auto run = [&](int blocks) {
            for (int block = 0; block < blocks; ++block) {
                for (unsigned f = 0; f < 2 * audio_frames; ++f) in[f] = cable[(at + f) % cable.size()];
                out.fill(0);
                melodee.render(in, out);
                for (unsigned f = 0; f < 2 * audio_frames; ++f) cable[(at + f) % cable.size()] = out[f];
                at = (at + 2 * audio_frames) % cable.size();
            }
        };
        with_shift(melodee, [&] { tap_step(melodee, 11); }); // Shift + Step 12
        CHECK(has(melodee.status(), "page=looper ") && has(melodee.status(), "looper=off") && has(melodee.status(), "looper_page=off"));
        press(melodee, Button::MainPush); // turns it on
        for (int pass = 0; pass < 12; ++pass) { melodee.service(); run(1); }
        CHECK(has(melodee.status(), "looper=on") && has(melodee.status(), "loop_memory=8") && has(melodee.status(), "looper_page=loops"));
        // The pads: L1's record pad (the top row's first) arms it in the audio thread, the MIDI thread picks L1.
        Lights lights;
        melodee.lights(lights, App::Clock::now());
        melodee.audio_event(make_event(0x90, pad_note(24), 100));
        run(1);
        CHECK(loop_field(melodee.status(), 1) == "ARM"); // stopped: waits for Play
        melodee.audio_event(make_event(0x80, pad_note(24), 0));
        melodee.audio_event(make_event(0x90, pad_note(24), 100)); // again: back to empty (an armed take cancelled)
        run(1);
        CHECK(loop_field(melodee.status(), 1) == "EMPTY");
        // In the MPC banks the same pad is the left bank's 13, still L1's record pad.
        app::set_pad_layout(app::PadLayout::Banks);
        melodee.audio_event(make_event(0x80, pad_note(24), 0));
        melodee.audio_event(make_event(0x90, pad_note(24), 100));
        run(1);
        CHECK(loop_field(melodee.status(), 1) == "ARM");
        melodee.audio_event(make_event(0x80, pad_note(24), 0));
        melodee.audio_event(make_event(0x90, pad_note(24), 100));
        run(1);
        CHECK(loop_field(melodee.status(), 1) == "EMPTY");
        app::set_pad_layout(app::PadLayout::Rows);
        // A bar from the input with the metronome on, the round trip set.
        CHECK(studio.loops().remote("length 1 1") == "ok");
        CHECK(studio.loops().remote("latency " + std::to_string(round_trip)) == "ok");
        studio.set_click(true, 100);
        press(melodee, Button::Rec); // Rec: records the selected loop (armed: stopped)
        run(1);
        CHECK(loop_field(melodee.status(), 1) == "ARM");
        press(melodee, Button::Play);
        for (int block = 0; block < 3000 && loop_field(melodee.status(), 1) != "PLAY"; ++block) run(1);
        CHECK(loop_field(melodee.status(), 1) == "PLAY");
        taken = uint32_t(std::stoul("0" + loop_field(melodee.status(), 1, "len")));
        CHECK(taken > 80000 && taken < 96000); // one bar at 120 BPM: 88200 frames
        // Where the bar's first click landed in the loop: on its first frame.
        auto onset = [&] {
            auto& looper = studio.host().looper();
            for (int block = 0; block < 50 && !looper.hold(0); ++block) run(1);
            int found = -1;
            if (const float* left = looper.held_left(0))
                for (uint32_t f = 0; f < looper.held_frames(0) && f < taken; ++f)
                    if (std::fabs(left[f]) > 0.02f) { found = int(f); break; }
            looper.release(0);
            run(1);
            return found < 0 ? found : found > int(taken) / 2 ? found - int(taken) : found;
        };
        int landed = onset();
        std::cout << "looper: a click through a " << round_trip << "-frame round trip lands at frame " << landed << " (latency set)\n";
        CHECK(landed >= -16 && landed <= 16);
        // Without the round trip set, it lands that late (less nothing: the block is the looper's own).
        CHECK(studio.loops().remote("clear 1") == "ok");
        run(4);
        CHECK(studio.loops().remote("latency 0") == "ok");
        CHECK(studio.loops().remote("rec 1") == "ok");
        for (int block = 0; block < 3000 && loop_field(melodee.status(), 1) != "PLAY"; ++block) run(1);
        taken = uint32_t(std::stoul("0" + loop_field(melodee.status(), 1, "len")));
        int late = onset();
        std::cout << "looper: without it, at frame " << late << '\n';
        CHECK(late >= int(round_trip) - 16 && late <= int(round_trip) + 16);
        CHECK(studio.loops().remote("latency " + std::to_string(round_trip)) == "ok");
        // Tempo follow: at 140 BPM the loop plays on stretched, its pitch kept; Rec fits it to the
        // tempo now on the service thread, then overdubs; L1's undo pad takes the pass back, again redoes it.
        studio.set_tempo(1400);
        run(8);
        CHECK(loop_field(melodee.status(), 1) == "PLAY" && loop_field(melodee.status(), 1, "tempo") == "1200");
        press(melodee, Button::Rec);
        for (int pass = 0; pass < 400 && loop_field(melodee.status(), 1, "len") == std::to_string(taken); ++pass) { melodee.service(); run(1); }
        uint32_t fitted = uint32_t(std::stoul("0" + loop_field(melodee.status(), 1, "len")));
        CHECK(fitted > 75400 && fitted < 75800); // 88200 x 120 / 140 = 75600
        CHECK(loop_field(melodee.status(), 1, "tempo") == "1400");
        for (int block = 0; block < 400 && loop_field(melodee.status(), 1) != "DUB"; ++block) run(1);
        CHECK(loop_field(melodee.status(), 1) == "DUB");
        run(200);
        press(melodee, Button::Rec); // the overdub ends: its layer stays (counted from its punch in)
        for (int block = 0; block < 400 && loop_field(melodee.status(), 1) != "PLAY"; ++block) run(1);
        CHECK(loop_field(melodee.status(), 1) == "PLAY" && loop_field(melodee.status(), 1, "layers") == "2");
        melodee.audio_event(make_event(0x90, pad_note(26), 100)); // undo, in the cycle it was read
        melodee.audio_event(make_event(0x80, pad_note(26), 0));
        for (int block = 0; block < 400 && loop_field(melodee.status(), 1, "layers") != "1"; ++block) run(1);
        CHECK(loop_field(melodee.status(), 1, "layers") == "1");
        melodee.audio_event(make_event(0x90, pad_note(26), 100)); // redo
        melodee.audio_event(make_event(0x80, pad_note(26), 0));
        for (int block = 0; block < 400 && loop_field(melodee.status(), 1, "layers") != "2"; ++block) run(1);
        CHECK(loop_field(melodee.status(), 1, "layers") == "2");
        taken = fitted;
        // Kept with the project: Loop 1.wav beside it, its frames the loop's.
        {
            AudioThread audio(melodee);
            CHECK(melodee.save_now().empty());
            for (int pass = 0; pass < 50 && !fs::exists(loop_file); ++pass) { melodee.service(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        }
        CHECK(fs::exists(loop_file));
        CHECK(fs::file_size(loop_file) > taken * 6); // 24-bit stereo
        // Muted from the track buttons' place: Mute + Track 1.
        melodee.surface(make_event(0xb0, button_cc(Button::Mute), 127));
        press(melodee, Button::Row1);
        melodee.surface(make_event(0xb0, button_cc(Button::Mute), 0));
        CHECK(studio.loops().mix(0).mute);
        press(melodee, Button::Mute); // the selected loop, L1: back on
        CHECK(!studio.loops().mix(0).mute);
        CHECK(melodee.save_now().empty());
    }
    {   // A new start: the loop comes back from its file, its length and tempo from the project.
        app::MelodeeApp melodee(folder, home, "build/test-no-modules");
        Session session;
        melodee.attach(session);
        melodee.audio_opened(AudioFormat {});
        CHECK(melodee.restore_now().empty());
        {
            AudioThread audio(melodee);
            for (int pass = 0; pass < 40 && loop_field(melodee.status(), 1, "len") != std::to_string(taken); ++pass) {
                melodee.service();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        CHECK(has(melodee.status(), "looper=on") && has(melodee.status(), "loop_latency=428"));
        CHECK(loop_field(melodee.status(), 1, "len") == std::to_string(taken));
        // Another project swapped in (here the same one again): its loops are read from its folder anew.
        {
            AudioThread audio(melodee);
            {
                std::lock_guard<std::mutex> guard(melodee.studio().lock);
                melodee.studio().loops().reopen();
            }
            for (int pass = 0; pass < 20; ++pass) { melodee.service(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        }
        CHECK(loop_field(melodee.status(), 1, "len") == std::to_string(taken) && has(melodee.status(), "loop_selected=1"));
        // Commit + note: onto the kit's next empty pad (A02 after A01), a note at bar 1, the loop stopped.
        auto& studio = melodee.studio();
        CHECK(studio.loops().commit(0, true));
        {
            AudioThread audio(melodee);
            for (int pass = 0; pass < 100 && !has(melodee.status(), "samples=1 pads=1"); ++pass) {
                melodee.service();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        CHECK(has(melodee.status(), "samples=1 pads=1"));
        CHECK(studio.current().pads[1].used());
        CHECK(fs::exists(home + "/Samples/Looper/L1 Loop 1.wav"));
        const auto& pattern = studio.patterns().at[0][studio.current().pattern];
        CHECK(!pattern.events.empty() && pattern.events.front().key == melodee::sampler_first_note + 1 && pattern.events.front().tick == 0);
        // A clear pad held 0.6 s clears L1 (L1's row, the fourth pad).
        with_shift(melodee, [&] { tap_step(melodee, 11); });
        Lights lights;
        melodee.surface(make_event(0x90, pad_note(27), 100));
        melodee.lights(lights, App::Clock::now() + std::chrono::milliseconds(300));
        CHECK(studio.loops().clearing() == 0);
        melodee.lights(lights, App::Clock::now() + std::chrono::milliseconds(700));
        melodee.surface(make_event(0x80, pad_note(27), 0));
        AudioBlock in {}, out {};
        for (int block = 0; block < 4; ++block) { out.fill(0); melodee.render(in, out); }
        CHECK(loop_field(melodee.status(), 1) == "EMPTY");
        // Off: the loops' folder goes with the next save.
        CHECK(studio.loops().remote("off") == "ok");
        {
            AudioThread audio(melodee);
            CHECK(melodee.save_now().empty());
            for (int pass = 0; pass < 20; ++pass) { melodee.service(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        }
        CHECK(has(melodee.status(), "looper=off"));
        CHECK(!fs::exists(loop_file));
        CHECK(studio.loops().remote("length 9 1").rfind("error", 0) == 0);
        CHECK(studio.loops().remote("source 1 loop 1").rfind("error", 0) == 0);
    }
}

int main() {
    test_controls();
    test_lights();
    test_display();
    test_reblocker();
    test_queues();
    test_spike();
    test_spi_protocol();
    test_latency_meter();
    test_spi_host();
    test_melodee_app();
    test_saving();
    test_main_volume();
    test_schwung_modules();
    test_canvas_page();
    test_synth_track();
    test_tracks();
    test_step_icons();
    test_migration();
    test_kit_depth();
    test_looper_pads();
    test_looper();
    test_sampling();
    test_sample_files();
    test_chop_depth();
    test_audio_track();
    test_sampling_pad_layout();
    std::cout << "move-tests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
