// SPDX-License-Identifier: AGPL-3.0-only
#include "move/runtime.h"
#include "move/display.h"
#include "move/queues.h"
#include <jack/jack.h>
#include <jack/midiport.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace move {
namespace {

volatile std::sig_atomic_t stop_signal = 0;
void on_signal(int) { stop_signal = 1; }

constexpr unsigned led_messages_per_period = 48; // as PoundHard's launcher: leaves the port room
constexpr auto lights_interval = std::chrono::milliseconds(5);
constexpr auto frame_interval = std::chrono::microseconds(16667);
constexpr auto frame_refresh = std::chrono::seconds(1); // resend an unchanged frame, in case one was lost
constexpr auto status_interval = std::chrono::seconds(5);

struct MidiIn {
    std::array<uint8_t, 3> bytes {};
    uint8_t size = 0;
    bool external = false;
    uint64_t timestamp_ns = 0;
};

class JackHost {
public:
    JackHost(App& app, Session& session, const std::string& name) : app(app), session(session) {
        wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wake_fd < 0) throw std::runtime_error("eventfd failed");
        jack_status_t status {};
        client = jack_client_open(name.c_str(), JackNoStartServer, &status);
        if (!client) throw std::runtime_error("cannot reach the JACK server (is jackd-move.service running?)");
        audio_in[0] = register_port("in_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
        audio_in[1] = register_port("in_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
        audio_out[0] = register_port("out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
        audio_out[1] = register_port("out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
        surface_in = register_port("surface", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);
        external_in = register_port("midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);
        leds_out = register_port("leds", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);
        display_out = register_port("display", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);
        session.rate = jack_get_sample_rate(client);
        jack_set_process_callback(client, [](jack_nframes_t frames, void* self) { return static_cast<JackHost*>(self)->process(frames); }, this);
        jack_set_xrun_callback(client, [](void* self) { ++static_cast<JackHost*>(self)->session.xruns; return 0; }, this);
        jack_on_shutdown(client, [](void* self) { static_cast<JackHost*>(self)->server_gone(); }, this);
    }
    ~JackHost() {
        if (client) { jack_deactivate(client); jack_client_close(client); }
        if (wake_fd >= 0) close(wake_fd);
    }
    JackHost(const JackHost&) = delete;
    JackHost& operator=(const JackHost&) = delete;

    void start() {
        if (jack_activate(client)) throw std::runtime_error("cannot activate the JACK client");
        connect("system:capture_1", audio_in[0], false);
        connect("system:capture_2", audio_in[1], false);
        connect(audio_out[0], "system:playback_1", true);
        connect(audio_out[1], "system:playback_2", true);
        connect("system:midi_capture", surface_in, true);
        connect("system:midi_capture_ext", external_in, false);
        connect(leds_out, "system:midi_playback", true);
        connect(display_out, "system:display", true);
        warn_about_other_screen_writers();
    }

    bool alive() const { return !gone.load(); }
    int wake_descriptor() const { return wake_fd; }
    SpscRing<MidiIn, 1024> midi_in;
    SpscRing<std::array<uint8_t, 3>, 1024> leds;
    TripleBuffer<DisplayMessage> display;

private:
    jack_port_t* register_port(const char* port, const char* type, unsigned long flags) {
        auto* registered = jack_port_register(client, port, type, flags, 0);
        if (!registered) throw std::runtime_error(std::string("cannot register JACK port ") + port);
        return registered;
    }
    bool exists(const char* port) const { return jack_port_by_name(client, port) != nullptr; }
    void connect(const char* source, jack_port_t* destination, bool required) {
        if (!exists(source)) { if (required) std::cerr << "melodee-move: no " << source << " on this server\n"; return; }
        int result = jack_connect(client, source, jack_port_name(destination));
        if (result && result != EEXIST) std::cerr << "melodee-move: cannot connect " << source << '\n';
    }
    void connect(jack_port_t* source, const char* destination, bool required) {
        if (!exists(destination)) { if (required) std::cerr << "melodee-move: no " << destination << " on this server\n"; return; }
        int result = jack_connect(client, jack_port_name(source), destination);
        if (result && result != EEXIST) std::cerr << "melodee-move: cannot connect to " << destination << '\n';
    }
    // jack_move takes every frame sent to system:display; two writers make
    // the screen flicker between them. PoundHard's launcher writes it every
    // period, so it has to be stopped first (tools/run-spike.sh does).
    void warn_about_other_screen_writers() const {
        auto* port = jack_port_by_name(client, "system:display");
        if (!port) return;
        const char** connections = jack_port_get_all_connections(client, port);
        if (!connections) return;
        std::string ours = jack_port_name(display_out);
        for (const char** connection = connections; *connection; ++connection)
            if (ours != *connection)
                std::cerr << "melodee-move: " << *connection << " also draws the screen; stop it (systemctl stop move-launcher-menu)\n";
        jack_free(connections);
    }
    void server_gone() {
        gone = true;
        uint64_t one = 1;
        [[maybe_unused]] auto written = write(wake_fd, &one, sizeof one);
    }

    void read_midi(jack_port_t* port, jack_nframes_t frames, bool external, bool& arrived) {
        void* buffer = jack_port_get_buffer(port, frames);
        uint32_t count = jack_midi_get_event_count(buffer);
        jack_nframes_t period_start = jack_last_frame_time(client);
        for (uint32_t n = 0; n < count; ++n) {
            jack_midi_event_t event;
            if (jack_midi_event_get(&event, buffer, n)) continue;
            if (event.size == 0 || event.size > 3) { ++session.midi_dropped; continue; }
            MidiIn message;
            std::copy(event.buffer, event.buffer + event.size, message.bytes.begin());
            message.size = uint8_t(event.size);
            message.external = external;
            message.timestamp_ns = uint64_t(jack_frames_to_time(client, period_start + event.time)) * 1000;
            if (midi_in.push(message)) { ++session.midi_in; arrived = true; }
            else ++session.midi_dropped;
        }
    }

    static uint64_t monotonic_ns() {
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        return uint64_t(now.tv_sec) * 1000000000u + uint64_t(now.tv_nsec);
    }

    int process(jack_nframes_t frames) {
        uint64_t began = monotonic_ns();
        bool arrived = false;
        read_midi(surface_in, frames, false, arrived);
        read_midi(external_in, frames, true, arrived);
        if (arrived) {
            uint64_t one = 1;
            [[maybe_unused]] auto written = write(wake_fd, &one, sizeof one);
        }

        auto* in_left = static_cast<const float*>(jack_port_get_buffer(audio_in[0], frames));
        auto* in_right = static_cast<const float*>(jack_port_get_buffer(audio_in[1], frames));
        auto* out_left = static_cast<float*>(jack_port_get_buffer(audio_out[0], frames));
        auto* out_right = static_cast<float*>(jack_port_get_buffer(audio_out[1], frames));
        if (frames != period) {
            period = frames;
            reblocker->reset(frames);
            session.period = frames;
            session.latency_frames = reblocker->latency_frames();
            app.audio_opened(AudioFormat {session.rate.load(), frames, reblocker->latency_frames()});
        }
        reblocker->process(in_left, in_right, out_left, out_right, frames, [this](const AudioBlock& input, AudioBlock& output) {
            app.render(input, output);
            session.blocks.fetch_add(1, std::memory_order_relaxed);
        });
        float in_peak = 0, out_peak = 0;
        for (jack_nframes_t n = 0; n < frames; ++n) {
            in_peak = std::max({in_peak, std::fabs(in_left[n]), std::fabs(in_right[n])});
            out_peak = std::max({out_peak, std::fabs(out_left[n]), std::fabs(out_right[n])});
        }
        session.input_peak.store(in_peak, std::memory_order_relaxed);
        session.output_peak.store(out_peak, std::memory_order_relaxed);
        session.underruns.store(reblocker->underruns(), std::memory_order_relaxed);

        void* led_buffer = jack_port_get_buffer(leds_out, frames);
        jack_midi_clear_buffer(led_buffer);
        std::array<uint8_t, 3> led;
        unsigned sent = 0;
        while (sent < led_messages_per_period && leds.pop(led)) {
            if (jack_midi_event_write(led_buffer, 0, led.data(), led.size())) break;
            ++sent;
        }
        session.leds_sent.fetch_add(sent, std::memory_order_relaxed);

        void* display_buffer = jack_port_get_buffer(display_out, frames);
        jack_midi_clear_buffer(display_buffer);
        if (display.take()) display_pending = true;
        if (display_pending) {
            if (jack_midi_event_write(display_buffer, 0, display.front().data(), display.front().size()) == 0) {
                display_pending = false;
                ++session.frames_sent;
            } else ++session.frames_failed;
        }
        session.cpu_load.store(jack_cpu_load(client), std::memory_order_relaxed);
        uint64_t spent = monotonic_ns() - began;
        session.process_calls.fetch_add(1, std::memory_order_relaxed);
        session.process_ns.fetch_add(spent, std::memory_order_relaxed);
        if (spent > session.process_ns_peak.load(std::memory_order_relaxed)) session.process_ns_peak.store(spent, std::memory_order_relaxed);
        return 0;
    }

    App& app;
    Session& session;
    jack_client_t* client = nullptr;
    std::array<jack_port_t*, 2> audio_in {}, audio_out {};
    jack_port_t *surface_in = nullptr, *external_in = nullptr, *leds_out = nullptr, *display_out = nullptr;
    std::unique_ptr<Reblocker> reblocker = std::make_unique<Reblocker>();
    jack_nframes_t period = 0;
    bool display_pending = false;
    std::atomic<bool> gone {false};
    int wake_fd = -1;
};

// One line of counters. `process` is our callback's mean and longest time in
// microseconds since the last line, against the period's length.
std::string status_line(Session& session, const App& app) {
    static uint64_t last_calls = 0, last_ns = 0;
    uint64_t calls = session.process_calls.load(), total = session.process_ns.load();
    double mean_us = calls > last_calls ? double(total - last_ns) / double(calls - last_calls) / 1000.0 : 0.0;
    double peak_us = double(session.process_ns_peak.exchange(0)) / 1000.0;
    double period_us = session.rate ? 1e6 * session.period / session.rate : 0.0;
    last_calls = calls; last_ns = total;
    std::ostringstream line;
    line.setf(std::ios::fixed); line.precision(2);
    line << "rate=" << session.rate << " period=" << session.period << " latency=" << session.latency_frames
         << " blocks=" << session.blocks << " xruns=" << session.xruns << " underruns=" << session.underruns
         << " cpu=" << session.cpu_load << "% process_us=" << std::setprecision(0) << mean_us << '/' << peak_us
         << " of " << period_us << std::setprecision(2)
         << " midi_in=" << session.midi_in << " midi_dropped=" << session.midi_dropped
         << " leds=" << session.leds_sent << " frames=" << session.frames_sent << " frames_failed=" << session.frames_failed
         << " in_peak=" << session.input_peak << " out_peak=" << session.output_peak;
    if (auto extra = app.status(); !extra.empty()) line << ' ' << extra;
    return line.str();
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
    JackHost host(app, session, options.client_name);
    host.start();
    std::cerr << "melodee-move: running at " << session.rate << " Hz; Shift + Back quits\n";

    std::atomic<bool> running {true}, quit {false};

    std::thread midi_thread([&] {
        Lights lights;
        lights.clear_all();
        auto next_lights = App::Clock::now();
        pollfd wake {host.wake_descriptor(), POLLIN, 0};
        while (running) {
            auto now = App::Clock::now();
            int timeout = int(std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(next_lights - now).count()));
            if (poll(&wake, 1, timeout) > 0) {
                uint64_t count;
                [[maybe_unused]] auto got = read(host.wake_descriptor(), &count, sizeof count);
            }
            MidiIn message;
            while (host.midi_in.pop(message)) {
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
                lights.update([&](const uint8_t* bytes) { return host.leds.push({bytes[0], bytes[1], bytes[2]}); });
                next_lights = now + lights_interval;
            }
        }
        // Every light off on the way out.
        lights.clear_all();
        auto deadline = App::Clock::now() + std::chrono::milliseconds(500);
        while (lights.pending() && App::Clock::now() < deadline && host.alive()) {
            lights.update([&](const uint8_t* bytes) { return host.leds.push({bytes[0], bytes[1], bytes[2]}); }, 256);
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
                host.display.back() = display_message(frame);
                host.display.publish();
                last = frame; last_sent = now; first = false;
            }
            next += frame_interval;
            if (next < now) next = now;
            std::this_thread::sleep_until(next);
        }
        host.display.back() = display_message(Frame {}); // a blank screen on the way out
        host.display.publish();
    });

    auto started = App::Clock::now();
    auto next_status = started + status_interval;
    const char* reason = "stopped";
    while (true) {
        if (stop_signal) { reason = "signal"; break; }
        if (quit) { reason = "Shift + Back"; break; }
        if (!host.alive()) { reason = "the JACK server went away"; break; }
        if (app.finished()) { reason = "the app finished"; break; }
        auto now = App::Clock::now();
        if (options.seconds > 0 && now - started >= std::chrono::duration<double>(options.seconds)) { reason = "time"; break; }
        app.service();
        if (now >= next_status) { std::cerr << status_line(session, app) << '\n'; next_status = now + status_interval; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    running = false;
    uint64_t one = 1;
    [[maybe_unused]] auto written = write(host.wake_descriptor(), &one, sizeof one);
    midi_thread.join();
    screen_thread.join();
    // Let the process callback carry the last lights and the blank frame out.
    auto deadline = App::Clock::now() + std::chrono::milliseconds(300);
    while (!host.leds.empty() && host.alive() && App::Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cerr << "melodee-move: " << reason << "; " << status_line(session, app) << '\n';
    return host.alive() ? 0 : 1;
}

} // namespace move
