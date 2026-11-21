// SPDX-License-Identifier: AGPL-3.0-only
// The device host: owns /dev/ablspi0.0 on Ableton's firmware, with
// Ableton's app stopped (by Schwung's standalone launcher or
// tools/run-stock.sh). One thread runs the whole cycle, in the order that
// gives a pad the shortest way to the speaker:
//
//   lights out -> exchange (blocks ~2.9 ms) -> screen chunk asked for ->
//   MIDI in, pads straight to App::audio_event() -> render() -> audio out
//
// so a pad read in one exchange sounds in the audio sent with the next.
#include "move/host.h"
#include "move/spi_device.h"
#include "move/spi_protocol.h"
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace move {
namespace {

constexpr unsigned fade_in_blocks = 32;          // ~90 ms, so the start makes no click
constexpr uint64_t block_ns = 2902494;           // 128 frames at 44.1 kHz
constexpr uint64_t late_after_ns = block_ns + 400000;
constexpr unsigned failures_before_giving_up = 100;
constexpr unsigned blank_chunks_on_exit = 2 * spi::display_chunks;

class SpiHost : public Host {
public:
    SpiHost(App& app, Session& session, std::unique_ptr<SpiDevice> device) : Host(app, session), device(std::move(device)) {}
    ~SpiHost() override {
        running = false;
        if (thread.joinable()) thread.join();
    }

    const char* name() const override { return "spi"; }

    void start() override {
        session.rate = spi::audio_rate;
        session.period = audio_frames;
        session.latency_frames = 0;
        session.backend = "spi";
        thread = std::thread([this] { run(); });
    }

    bool alive() const override { return !gone.load(); }

    void finish(std::chrono::milliseconds limit) override {
        finishing = true;
        auto deadline = std::chrono::steady_clock::now() + limit;
        while (!done && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        running = false;
        if (thread.joinable()) thread.join();
    }

private:
    // Move's own audio priority, on the core its SPI interrupts are pinned
    // to. Refused for the `ableton` user Schwung runs tools as: the session
    // says so, and the status line shows it.
    void become_realtime() {
        cpu_set_t cores;
        CPU_ZERO(&cores);
        CPU_SET(3, &cores);
        if (sysconf(_SC_NPROCESSORS_ONLN) > 3) pthread_setaffinity_np(pthread_self(), sizeof cores, &cores);
        sched_param priority {};
        priority.sched_priority = 70;
        session.realtime = pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority) == 0;
        pthread_setname_np(pthread_self(), "melodee-spi");
        if (!session.realtime)
            std::cerr << "melodee-move: the audio thread is not realtime (SCHED_FIFO refused); expect an occasional late block\n";
    }

    // A MIDI system reset on the controls' cable ends the surface's
    // power-on light show (what Schwung's boot-select and EvilGenius send).
    void reset_surface(uint8_t* page) {
        const uint32_t reset = 0xff00u | 0x0fu;
        spi::write_midi_out(page, &reset, 1);
        device->transfer();
        spi::write_midi_out(page, nullptr, 0);
    }

    void run() {
        become_realtime();
        uint8_t* page = device->page();
        reset_surface(page);
        app.audio_opened(AudioFormat {spi::audio_rate, audio_frames, 0});

        std::array<spi::MidiIn, spi::midi_in_slots> events;
        std::array<uint32_t, spi::midi_out_slots> packets {};
        AudioBlock input {}, output {};
        spi::Dither dither;
        spi::DisplayServer screen;
        Frame newest {};
        unsigned failures = 0, faded = 0, blank_chunks = 0;
        uint64_t last_transfer = 0;

        std::array<ExternalMidi, spi::external_out_slots> outgoing {};
        while (running) {
            // MIDI out first (cable 2: USB-A and USB-C), with the audio of
            // the render it was decided in; then lights, up to 20 a cycle in
            // all, moved to the controls' channel 1.
            unsigned count = 0;
            unsigned taken = app.midi_out(outgoing.data(), spi::external_out_slots);
            for (unsigned n = 0; n < taken && n < outgoing.size(); ++n)
                if (uint32_t packet = spi::usb_midi_out_packet(spi::cable_external, outgoing[n].bytes.data(), outgoing[n].size)) packets[count++] = packet;
            const unsigned midi_count = count;
            session.midi_out.fetch_add(midi_count, std::memory_order_relaxed);
            LedMessage led;
            while (count < spi::midi_out_slots && leds.pop(led)) {
                const uint8_t bytes[3] {uint8_t(led[0] & 0xf0), led[1], led[2]};
                if (uint32_t packet = spi::usb_midi_packet(spi::cable_controls, bytes, 3)) packets[count++] = packet;
            }
            spi::write_midi_out(page, packets.data(), count);
            session.leds_sent.fetch_add(count - midi_count, std::memory_order_relaxed);

            if (!device->transfer()) {
                if (++failures > failures_before_giving_up) break;
                continue;
            }
            failures = 0;
            uint64_t began = monotonic_ns();
            if (last_transfer && began - last_transfer > late_after_ns) session.xruns.fetch_add(1, std::memory_order_relaxed);
            last_transfer = began;
            session.device_transfer_ns.store(device->transfer_ns(), std::memory_order_relaxed);

            // The screen chunk the XMOS asked for, from one latched picture.
            if (display.take()) std::copy(display.front().begin() + display_magic.size(), display.front().end(), newest.begin());
            unsigned served = screen.serve(page, [&] { return newest; });
            if (served == spi::display_chunks) session.frames_sent.fetch_add(1, std::memory_order_relaxed);
            if (finishing && served) ++blank_chunks;

            // MIDI in: pads go to the app on this thread, everything to the MIDI thread.
            unsigned skipped = 0;
            unsigned received = spi::read_midi_in(page, events, skipped);
            bool arrived = false;
            uint64_t pads_before = session.pad_events.load(std::memory_order_relaxed);
            for (unsigned n = 0; n < received; ++n) {
                const auto& event = events[n];
                if (event.cable == spi::cable_controls || event.cable == spi::cable_external)
                    incoming(event.bytes.data(), event.size, event.cable == spi::cable_external, began, arrived);
            }
            if (received || skipped) spi::clear_midi_in(page);
            take_injected(began, arrived); // the control channel's presses, as the device's own
            if (arrived) wake();
            bool pad_this_cycle = session.pad_events.load(std::memory_order_relaxed) != pads_before;

            // Render, and send it with the next exchange.
            spi::read_audio_in(page, input);
            output.fill(0);
            app.render(input, output);
            session.blocks.fetch_add(1, std::memory_order_relaxed);
            if (faded < fade_in_blocks) {
                for (auto& sample : output) sample = int32_t(int64_t(sample) * faded / fade_in_blocks);
                ++faded;
            }
            spi::write_audio_out(page, output, dither);

            uint64_t ended = monotonic_ns();
            if (pad_this_cycle) {
                session.pad_to_render_ns.store(ended - began, std::memory_order_relaxed);
                if (ended - began > session.pad_to_render_ns_peak.load(std::memory_order_relaxed))
                    session.pad_to_render_ns_peak.store(ended - began, std::memory_order_relaxed);
            }
            int32_t in_peak = 0, out_peak = 0;
            for (unsigned n = 0; n < input.size(); ++n) {
                in_peak = std::max(in_peak, input[n] == INT32_MIN ? INT32_MAX : std::abs(input[n]));
                out_peak = std::max(out_peak, output[n] == INT32_MIN ? INT32_MAX : std::abs(output[n]));
            }
            session.input_peak.store(float(in_peak) / 2147483648.0f, std::memory_order_relaxed);
            session.output_peak.store(float(out_peak) / 2147483648.0f, std::memory_order_relaxed);
            session.process_calls.fetch_add(1, std::memory_order_relaxed);
            session.process_ns.fetch_add(ended - began, std::memory_order_relaxed);
            if (ended - began > session.process_ns_peak.load(std::memory_order_relaxed))
                session.process_ns_peak.store(ended - began, std::memory_order_relaxed);

            // On the way out: the lights are off and the screen blank once
            // the queue is empty and two full screens of blank went out.
            if (finishing && leds.empty() && blank_chunks >= blank_chunks_on_exit) break;
        }
        if (failures > failures_before_giving_up) {
            std::cerr << "melodee-move: the Move's device stopped answering\n";
            gone = true;
            wake();
        } else {
            // Silence and no lights in the last exchange.
            std::fill(page, page + spi::in_midi, uint8_t(0));
            device->transfer();
        }
        done = true;
    }

    std::unique_ptr<SpiDevice> device;
    std::thread thread;
    std::atomic<bool> running {true}, finishing {false}, done {false}, gone {false};
};

} // namespace

std::unique_ptr<Host> make_spi_host(App& app, Session& session, std::unique_ptr<SpiDevice> device) {
    return std::make_unique<SpiHost>(app, session, std::move(device));
}

} // namespace move
