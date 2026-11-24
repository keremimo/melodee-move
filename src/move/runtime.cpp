// SPDX-License-Identifier: AGPL-3.0-only
#include "move/runtime.h"
#include "move/memory.h"
#include "move/remote.h"
#include <cstring>
#include "move/display.h"
#include "move/font.h"
#include "move/host.h"
#include "move/power.h"
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
    Power power = session.power.load();
    line << " power=" << (power == Power::Asking ? "asking" : power == Power::ShuttingDown ? "shutting" : "on") << " battery=" << session.battery.load();
    if (session.backend.load() == std::string("jack")) line << " cpu=" << session.cpu_load.load() << '%';
    if (auto extra = app.status(); !extra.empty()) line << ' ' << extra;
    return line.str();
}

// The power button's question for an app that draws none (App::draw_power()):
// a sheet over its screen, Ableton's words.
void draw_power_sheet(juce::Graphics& g, Power power) {
    const juce::Rectangle<int> box {6, 13, screen_width - 12, 38};
    g.setColour(juce::Colours::black);
    g.fillRect(box);
    g.setColour(juce::Colours::white);
    g.drawRect(box);
    if (power == Power::Asking) {
        draw_text_centred(g, box.getX(), box.getY() + 5, box.getWidth(), "PRESS WHEEL TO");
        draw_text_centred(g, box.getX(), box.getY() + 14, box.getWidth(), "SHUT DOWN");
        draw_text_centred(g, box.getX(), box.getY() + 26, box.getWidth(), "BACK: CANCEL");
    } else {
        draw_text_centred(g, box.getX(), box.getY() + 15, box.getWidth(), "SHUTTING DOWN");
    }
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
    const std::string locked = lock_memory();
    if (locked != "onfault" && locked != "all") std::cerr << "melodee-move: " << locked << "; pages may fault in the audio thread\n";

    Session session;
    app.attach(session);
    auto host = make_host(app, session, options);
    host->start();
    std::cerr << "melodee-move: running on " << host->name() << " at " << session.rate << " Hz, memory locked " << locked << "; Shift + Back quits\n";

    std::atomic<bool> running {true}, quit {false};
    // The power button (move/power.h): yes said on the MIDI thread; the
    // XMOS's yes wanted once the app has saved, and sent.
    std::atomic<bool> power_yes {false}, yes_wanted {false}, yes_sent {false};

    std::thread midi_thread([&] {
        Lights lights;
        lights.clear_all();
        lights.set_rgb(host->rgb_leds());
        PowerQuestion question;
        bool no_owed = false;
        auto next_lights = App::Clock::now();
        pollfd wake {host->wake_descriptor(), POLLIN, 0};
        auto send = [&](const uint8_t* bytes) { return host->leds.push({bytes[0], bytes[1], bytes[2]}); };
        // An answer to the XMOS, whole, in the lights' queue (this thread is
        // its one producer): Ableton's SysEx in three pieces, on the
        // controls' cable as Ableton's app sends it.
        auto answer = [&](const power::Answer& sysex) {
            if (!host->power_button() || host->leds.room() < 3) return false;
            for (size_t at = 0; at < sysex.size(); at += 3) send(sysex.data() + at);
            return true;
        };
        while (running) {
            auto now = App::Clock::now();
            int timeout = int(std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(next_lights - now).count()));
            if (poll(&wake, 1, timeout) > 0) {
                uint64_t count;
                [[maybe_unused]] auto got = read(host->wake_descriptor(), &count, sizeof count);
            }
            std::array<uint8_t, 2> report;
            while (host->power.pop(report)) {
                if (question.report(report[0], report[1], now)) {
                    session.power = question.state();
                    std::cerr << "melodee-move: the power button " << (question.state() == Power::Asking ? "asks to shut down" : "no longer asks") << '\n';
                }
                session.battery = question.charge();
            }
            if (no_owed) no_owed = !answer(power::answer_no); // the queue was full
            if (yes_wanted && !yes_sent && answer(power::answer_yes)) yes_sent = true;
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
                PowerQuestion::Answer said;
                if (question.take(input, said, now)) {
                    if (said == PowerQuestion::Answer::No) {
                        no_owed = !answer(power::answer_no);
                        std::cerr << "melodee-move: not shutting down (Back)\n";
                    } else if (said == PowerQuestion::Answer::Yes) {
                        power_yes = true;
                    }
                    session.power = question.state();
                    continue;
                }
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
                if (question.state() == Power::Asking) lights.button(Button::Back, white_bright); // its no
                lights.update(send);
                lights.update_fades(send, unsigned(host->leds.room()), now);
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
                Power power = session.power.load();
                if (power == Power::On || !app.draw_power(g)) {
                    app.draw(g);
                    if (power != Power::On) draw_power_sheet(g, power);
                }
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
        if (power_yes) { reason = "the power button"; break; }
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
    if (power_yes) {
        // Saved: now the XMOS's yes, then the Move's shutdown, as Ableton's
        // app does. Halt stops us (/etc/init.d/move, then sendsigs) before
        // xmos-power-off; till then the screen says so. If the shutdown is
        // refused, Ableton's app comes back and asks again.
        yes_wanted = true;
        for (int waited = 0; waited < 100 && (!yes_sent || !host->leds.empty()); ++waited)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::string why;
        if (power::shut_down(why)) {
            std::cerr << "melodee-move: shutting the Move down\n";
            auto asked = App::Clock::now();
            while (!stop_signal && host->alive() && App::Clock::now() - asked < std::chrono::seconds(30))
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } else {
            std::cerr << "melodee-move: the Move did not shut down: " << why << '\n';
        }
    }
    running = false;
    midi_thread.join();
    screen_thread.join();
    bool alive = host->alive();
    host->finish(std::chrono::milliseconds(500));
    std::cerr << "melodee-move: " << reason << "; " << status_line(session, app) << '\n';
    return alive ? 0 : 1;
}

} // namespace move
