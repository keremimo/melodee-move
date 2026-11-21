// SPDX-License-Identifier: AGPL-3.0-only
#include "move/runtime.h"
#include "move/remote.h"
#include <cstring>
#include "move/display.h"
#include "move/host.h"
#include "move/spi_device.h"
#include "move/spi_protocol.h"
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

namespace move {
namespace {

volatile std::sig_atomic_t stop_signal = 0;
void on_signal(int) { stop_signal = 1; }

constexpr auto lights_interval = std::chrono::milliseconds(5);
constexpr auto frame_interval = std::chrono::microseconds(16667);
constexpr auto frame_refresh = std::chrono::seconds(1); // resend an unchanged frame, in case one was lost
constexpr auto status_interval = std::chrono::seconds(5);

// One line of counters. `process` is the audio thread's mean and longest
// cycle in microseconds since the last line, against the period's length;
// `pad` the time from reading a pad to the end of the render it sounds in.
std::string status_line(Session& session, const App& app) {
    static uint64_t last_calls = 0, last_ns = 0;
    uint64_t calls = session.process_calls.load(), total = session.process_ns.load();
    double mean_us = calls > last_calls ? double(total - last_ns) / double(calls - last_calls) / 1000.0 : 0.0;
    double peak_us = double(session.process_ns_peak.exchange(0)) / 1000.0;
    double period_us = session.rate ? 1e6 * session.period / session.rate : 0.0;
    last_calls = calls; last_ns = total;
    std::ostringstream line;
    line.setf(std::ios::fixed); line.precision(2);
    line << "backend=" << session.backend.load() << " realtime=" << (session.realtime ? "yes" : "no")
         << " rate=" << session.rate.load() << " period=" << session.period.load() << " latency=" << session.latency_frames.load()
         << " blocks=" << session.blocks.load() << " late=" << session.xruns.load() << " underruns=" << session.underruns.load()
         << std::setprecision(0) << " process_us=" << mean_us << '/' << peak_us << " of " << period_us
         << " pad_us=" << double(session.pad_to_render_ns.load()) / 1000.0 << '/' << double(session.pad_to_render_ns_peak.load()) / 1000.0
         << " transfer_us=" << double(session.device_transfer_ns.load()) / 1000.0 << std::setprecision(2)
         << " pads=" << session.pad_events.load() << " midi_in=" << session.midi_in.load() << " midi_dropped=" << session.midi_dropped.load()
         << " leds=" << session.leds_sent.load() << " frames=" << session.frames_sent.load() << " frames_failed=" << session.frames_failed.load()
         << " in_peak=" << session.input_peak.load() << " out_peak=" << session.output_peak.load();
    if (session.backend.load() == std::string("jack")) line << " cpu=" << session.cpu_load.load() << '%';
    if (auto extra = app.status(); !extra.empty()) line << ' ' << extra;
    return line.str();
}

std::unique_ptr<Host> make_host(App& app, Session& session, const RunOptions& options) {
    Backend backend = options.backend;
    if (backend == Backend::Auto) backend = access(spi::device_path, F_OK) == 0 ? Backend::Spi : Backend::Jack;
    switch (backend) {
    case Backend::Spi: return make_spi_host(app, session, open_move_device());
    case Backend::SpiSimulated:
        return make_spi_host(app, session, std::make_unique<FakeSpiDevice>(true, options.simulated_loopback_frames, options.simulated_script));
    case Backend::Jack:
    case Backend::Auto: break;
    }
    return make_jack_host(app, session, options.client_name);
}

} // namespace

void track_input(Session& session, const Input& input) {
    if (input.kind == Input::Kind::Pad) {
        uint32_t bit = 1u << input.index;
        if (input.pressed) {
            session.held_pads.fetch_or(bit);
            session.last_pad = input.index;
            session.velocity = input.value;
        } else session.held_pads.fetch_and(~bit);
    } else if (input.kind == Input::Kind::Button && input.index == uint8_t(Button::Shift)) {
        session.shift = input.pressed;
    }
}

int run_app(App& app, const RunOptions& options) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_signal);  // the SSH session it was started from went away
    std::signal(SIGPIPE, SIG_IGN);   // a closed terminal or socket is an error to ignore, not a reason to die
    if (mlockall(MCL_CURRENT | MCL_FUTURE)) std::cerr << "melodee-move: mlockall failed; pages may fault in the audio thread\n";

    Session session;
    app.attach(session);
    auto host = make_host(app, session, options);
    host->start();
    std::cerr << "melodee-move: running on " << host->name() << " at " << session.rate << " Hz; Shift + Back quits\n";

    std::atomic<bool> running {true}, quit {false};

    std::thread midi_thread([&] {
        Lights lights;
        lights.clear_all();
        auto next_lights = App::Clock::now();
        pollfd wake {host->wake_descriptor(), POLLIN, 0};
        auto send = [&](const uint8_t* bytes) { return host->leds.push({bytes[0], bytes[1], bytes[2]}); };
        while (running) {
            auto now = App::Clock::now();
            int timeout = int(std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(next_lights - now).count()));
            if (poll(&wake, 1, timeout) > 0) {
                uint64_t count;
                [[maybe_unused]] auto got = read(host->wake_descriptor(), &count, sizeof count);
            }
            MidiIn message;
            while (host->midi_in.pop(message)) {
                if (message.external) {
                    ExternalMidi external;
                    external.bytes = message.bytes; external.size = message.size; external.timestamp_ns = message.timestamp_ns;
                    app.midi(external);
                    continue;
                }
                SurfaceEvent event;
                event.input = decode(message.bytes.data(), message.size);
                event.bytes = message.bytes;
                event.timestamp_ns = message.timestamp_ns;
                const auto& input = event.input;
                if (input.kind == Input::Kind::Button && input.index == uint8_t(Button::Back) && input.pressed && session.shift) {
                    quit = true;
                    continue;
                }
                track_input(session, input);
                app.surface(event);
            }
            now = App::Clock::now();
            if (now >= next_lights) {
                app.lights(lights, now);
                lights.update(send);
                next_lights = now + lights_interval;
            }
        }
        // Every light off on the way out.
        lights.clear_all();
        auto deadline = App::Clock::now() + std::chrono::milliseconds(500);
        while (lights.pending() && App::Clock::now() < deadline && host->alive()) {
            lights.update(send, 256);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    std::thread screen_thread([&] {
        juce::Image image(juce::Image::ARGB, screen_width, screen_height, true);
        Frame last {};
        bool first = true;
        auto last_sent = App::Clock::now();
        auto next = App::Clock::now();
        while (running) {
            {
                juce::Graphics g(image);
                g.fillAll(juce::Colours::black);
                g.setColour(juce::Colours::white);
                app.draw(g);
            }
            Frame frame = pack(image);
            auto now = App::Clock::now();
            if (first || frame != last || now - last_sent >= frame_refresh) {
                host->display.back() = display_message(frame);
                host->display.publish();
                last = frame; last_sent = now; first = false;
            }
            next += frame_interval;
            if (next < now) next = now;
            std::this_thread::sleep_until(next);
        }
        host->display.back() = display_message(Frame {}); // a blank screen on the way out
        host->display.publish();
    });

    // The control channel: status, presses played into the input, the app's commands.
    ControlChannel channel;
    if (!options.control_path.empty()) {
        std::string failed;
        if (!channel.open(options.control_path, failed)) std::cerr << "melodee-move: no control channel at " << options.control_path << ": " << failed << '\n';
    }
    auto answer = [&](const std::string& line) -> std::string {
        if (line == "status") return status_line(session, app);
        for (const char* verb : {"melodee-press ", "press "}) {
            if (line.compare(0, std::strlen(verb), verb)) continue;
            std::vector<Injected> messages;
            std::string failed;
            if (!press_messages(line.substr(std::strlen(verb)), messages, failed)) return failed;
            for (const auto& message : messages) {
                if (!host->injected.push(message.bytes)) return "busy";
                std::this_thread::sleep_for(std::chrono::milliseconds(message.wait_ms));
            }
            return "ok";
        }
        auto reply = app.command(line);
        return reply.empty() ? "unknown: " + line : reply;
    };

    auto started = App::Clock::now();
    auto next_status = started + status_interval;
    const char* reason = "stopped";
    while (true) {
        if (stop_signal) { reason = "signal"; break; }
        if (quit) { reason = "Shift + Back"; break; }
        if (!host->alive()) { reason = "the host went away"; break; }
        if (app.finished()) { reason = "the app finished"; break; }
        auto now = App::Clock::now();
        if (options.seconds > 0 && now - started >= std::chrono::duration<double>(options.seconds)) { reason = "time"; break; }
        app.service();
        channel.poll(answer);
        if (now >= next_status) { std::cerr << status_line(session, app) << '\n'; next_status = now + status_interval; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    app.stopping(); // the audio thread still runs: the app's last writes that need it
    running = false;
    midi_thread.join();
    screen_thread.join();
    bool alive = host->alive();
    host->finish(std::chrono::milliseconds(500));
    std::cerr << "melodee-move: " << reason << "; " << status_line(session, app) << '\n';
    return alive ? 0 : 1;
}

} // namespace move
