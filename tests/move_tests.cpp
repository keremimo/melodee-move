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
#include "app/melodee_app.h"
#include <chrono>
#include <filesystem>
#include <cmath>
#include <cstring>
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
    melodee.surface(make_event(0xb0, button_cc(Button::Loop), 127)); // Chop
    unsigned slices = melodee.slice_count();
    CHECK(slices >= 4 && slices <= 64);
    CHECK(melodee.status().find("page=chop") != std::string::npos);
    CHECK(play(1, 20) > to_s32(0.05f));     // pad 2 plays slice 2
    CHECK(quiet());
    CHECK(play(slices, 20) == 0);           // past the last slice: nothing
    melodee.surface(make_event(0xb0, button_cc(Button::MainPush), 127)); // To Pads
    melodee.surface(make_event(0xb0, button_cc(Button::MainPush), 127)); // Poly, onto the empty pads
    CHECK(melodee.status().find("page=play") != std::string::npos);
    CHECK(melodee.status().find("pad=A02") != std::string::npos); // the first empty pad after A01
    CHECK(play(1, 20) > to_s32(0.05f));     // A02 now holds slice 1
    CHECK(quiet());
    // Knob 1 tunes the last pad; touching it shows it; a turn of Regions' amount re-chops.
    melodee.surface(make_event(0xb0, knob_first_cc, 3));
    CHECK(melodee.status().find("page=play") != std::string::npos);
    melodee.surface(make_event(0xb0, button_cc(Button::Back), 127));
    CHECK(melodee.load_now(folder + "/missing.wav", 3) != "");
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
    auto press = [](app::MelodeeApp& melodee, Button button) { melodee.surface(make_event(0xb0, button_cc(button), 127)); };
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
        first.surface(make_event(0x90, pad_note(0), 100)); // A01 again
        press(first, Button::Loop);
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
    second.surface(make_event(0x90, pad_note(0), 100));
    press(second, Button::Loop); // A01's chop as it was left: Beats, the same slices
    CHECK(second.slice_count() == chopped && has(second.status(), "chop=Beats"));
    press(second, Button::Back);
    CHECK(second.save_now().empty());
    app::KitFile kit;
    CHECK(app::load_kit(project, kit).empty());
    CHECK(kit.samples.size() == 2); // the missing file is kept, by path
    CHECK(app::load_kit(home + "/nothing.mldp", kit) == "missing");
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

} // namespace

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
    std::cout << "move-tests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
