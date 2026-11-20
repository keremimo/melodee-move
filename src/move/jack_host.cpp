// SPDX-License-Identifier: AGPL-3.0-only
// The JACK host: a client of `jackd -d move` (poundhard-armbian). libjack
// is loaded when this host starts, not linked, so the same binary runs on
// Ableton's firmware, which has no JACK at all.
#include "move/host.h"
#include <jack/jack.h>
#include <jack/midiport.h>
#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace move {
namespace {

#define JACK_FUNCTIONS(X) \
    X(jack_client_open) X(jack_client_close) X(jack_activate) X(jack_deactivate) X(jack_port_register) \
    X(jack_port_by_name) X(jack_port_name) X(jack_connect) X(jack_port_get_all_connections) X(jack_free) \
    X(jack_get_sample_rate) X(jack_set_process_callback) X(jack_set_xrun_callback) X(jack_on_shutdown) \
    X(jack_port_get_buffer) X(jack_midi_get_event_count) X(jack_midi_event_get) X(jack_midi_clear_buffer) \
    X(jack_midi_event_write) X(jack_last_frame_time) X(jack_frames_to_time) X(jack_cpu_load)

struct Jack {
#define DECLARE(function) decltype(&::function) function = nullptr;
    JACK_FUNCTIONS(DECLARE)
#undef DECLARE
    void* library = nullptr;
    Jack() {
        library = dlopen("libjack.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!library) throw std::runtime_error("libjack.so.0 not found; the JACK host needs it");
#define LOAD(function) \
        function = reinterpret_cast<decltype(function)>(dlsym(library, #function)); \
        if (!function) throw std::runtime_error("libjack.so.0 lacks " #function);
        JACK_FUNCTIONS(LOAD)
#undef LOAD
    }
};

constexpr unsigned led_messages_per_period = 48; // as PoundHard's launcher: leaves the port room

class JackHost : public Host {
public:
    JackHost(App& app, Session& session, const std::string& name) : Host(app, session) {
        jack_status_t status {};
        client = jack.jack_client_open(name.c_str(), JackNoStartServer, &status);
        if (!client) throw std::runtime_error("cannot reach the JACK server (is jackd-move.service running?)");
        audio_in[0] = register_port("in_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
        audio_in[1] = register_port("in_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
        audio_out[0] = register_port("out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
        audio_out[1] = register_port("out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
        surface_in = register_port("surface", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);
        external_in = register_port("midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);
        leds_out = register_port("leds", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);
        display_out = register_port("display", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);
        session.rate = jack.jack_get_sample_rate(client);
        session.backend = "jack";
        jack.jack_set_process_callback(client, [](jack_nframes_t frames, void* self) { return static_cast<JackHost*>(self)->process(frames); }, this);
        jack.jack_set_xrun_callback(client, [](void* self) { ++static_cast<JackHost*>(self)->session.xruns; return 0; }, this);
        jack.jack_on_shutdown(client, [](void* self) { static_cast<JackHost*>(self)->server_gone(); }, this);
    }
    ~JackHost() override {
        if (client) { jack.jack_deactivate(client); jack.jack_client_close(client); }
    }

    const char* name() const override { return "jack"; }

    void start() override {
        if (jack.jack_activate(client)) throw std::runtime_error("cannot activate the JACK client");
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

    bool alive() const override { return !gone.load(); }

    void finish(std::chrono::milliseconds limit) override {
        auto deadline = std::chrono::steady_clock::now() + limit;
        while (!leds.empty() && alive() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // the blank frame
    }

private:
    jack_port_t* register_port(const char* port, const char* type, unsigned long flags) {
        auto* registered = jack.jack_port_register(client, port, type, flags, 0);
        if (!registered) throw std::runtime_error(std::string("cannot register JACK port ") + port);
        return registered;
    }
    bool exists(const char* port) const { return jack.jack_port_by_name(client, port) != nullptr; }
    void connect(const char* source, jack_port_t* destination, bool required) {
        if (!exists(source)) { if (required) std::cerr << "melodee-move: no " << source << " on this server\n"; return; }
        int result = jack.jack_connect(client, source, jack.jack_port_name(destination));
        if (result && result != EEXIST) std::cerr << "melodee-move: cannot connect " << source << '\n';
    }
    void connect(jack_port_t* source, const char* destination, bool required) {
        if (!exists(destination)) { if (required) std::cerr << "melodee-move: no " << destination << " on this server\n"; return; }
        int result = jack.jack_connect(client, jack.jack_port_name(source), destination);
        if (result && result != EEXIST) std::cerr << "melodee-move: cannot connect to " << destination << '\n';
    }
    // jack_move takes every frame sent to system:display; two writers make
    // the screen flicker between them. PoundHard's launcher writes it every
    // period, so it has to be stopped first (tools/run-spike.sh does).
    void warn_about_other_screen_writers() const {
        auto* port = jack.jack_port_by_name(client, "system:display");
        if (!port) return;
        const char** connections = jack.jack_port_get_all_connections(client, port);
        if (!connections) return;
        std::string ours = jack.jack_port_name(display_out);
        for (const char** connection = connections; *connection; ++connection)
            if (ours != *connection)
                std::cerr << "melodee-move: " << *connection << " also draws the screen; stop it (systemctl stop move-launcher-menu)\n";
        jack.jack_free(connections);
    }
    void server_gone() { gone = true; wake(); }

    void read_midi(jack_port_t* port, jack_nframes_t frames, bool external, bool& arrived) {
        void* buffer = jack.jack_port_get_buffer(port, frames);
        uint32_t count = jack.jack_midi_get_event_count(buffer);
        jack_nframes_t period_start = jack.jack_last_frame_time(client);
        for (uint32_t n = 0; n < count; ++n) {
            jack_midi_event_t event;
            if (jack.jack_midi_event_get(&event, buffer, n)) continue;
            uint64_t at = uint64_t(jack.jack_frames_to_time(client, period_start + event.time)) * 1000;
            incoming(event.buffer, event.size, external, at, arrived);
        }
    }

    int process(jack_nframes_t frames) {
        uint64_t began = monotonic_ns();
        bool arrived = false;
        uint64_t pads_before = session.pad_events.load(std::memory_order_relaxed);
        read_midi(surface_in, frames, false, arrived);
        read_midi(external_in, frames, true, arrived);
        take_injected(began, arrived); // the control channel's presses, as the device's own
        if (arrived) wake();
        bool pad_this_cycle = session.pad_events.load(std::memory_order_relaxed) != pads_before;

        auto* in_left = static_cast<const float*>(jack.jack_port_get_buffer(audio_in[0], frames));
        auto* in_right = static_cast<const float*>(jack.jack_port_get_buffer(audio_in[1], frames));
        auto* out_left = static_cast<float*>(jack.jack_port_get_buffer(audio_out[0], frames));
        auto* out_right = static_cast<float*>(jack.jack_port_get_buffer(audio_out[1], frames));
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
        if (pad_this_cycle) {
            uint64_t spent = monotonic_ns() - began;
            session.pad_to_render_ns.store(spent, std::memory_order_relaxed);
            if (spent > session.pad_to_render_ns_peak.load(std::memory_order_relaxed)) session.pad_to_render_ns_peak.store(spent, std::memory_order_relaxed);
        }
        float in_peak = 0, out_peak = 0;
        for (jack_nframes_t n = 0; n < frames; ++n) {
            in_peak = std::max({in_peak, std::fabs(in_left[n]), std::fabs(in_right[n])});
            out_peak = std::max({out_peak, std::fabs(out_left[n]), std::fabs(out_right[n])});
        }
        session.input_peak.store(in_peak, std::memory_order_relaxed);
        session.output_peak.store(out_peak, std::memory_order_relaxed);
        session.underruns.store(reblocker->underruns(), std::memory_order_relaxed);

        void* led_buffer = jack.jack_port_get_buffer(leds_out, frames);
        jack.jack_midi_clear_buffer(led_buffer);
        LedMessage led;
        unsigned sent = 0;
        while (sent < led_messages_per_period && leds.pop(led)) {
            if (jack.jack_midi_event_write(led_buffer, 0, led.data(), led.size())) break;
            ++sent;
        }
        session.leds_sent.fetch_add(sent, std::memory_order_relaxed);

        void* display_buffer = jack.jack_port_get_buffer(display_out, frames);
        jack.jack_midi_clear_buffer(display_buffer);
        if (display.take()) display_pending = true;
        if (display_pending) {
            if (jack.jack_midi_event_write(display_buffer, 0, display.front().data(), display.front().size()) == 0) {
                display_pending = false;
                ++session.frames_sent;
            } else ++session.frames_failed;
        }
        session.cpu_load.store(jack.jack_cpu_load(client), std::memory_order_relaxed);
        uint64_t spent = monotonic_ns() - began;
        session.process_calls.fetch_add(1, std::memory_order_relaxed);
        session.process_ns.fetch_add(spent, std::memory_order_relaxed);
        if (spent > session.process_ns_peak.load(std::memory_order_relaxed)) session.process_ns_peak.store(spent, std::memory_order_relaxed);
        return 0;
    }

    Jack jack;
    jack_client_t* client = nullptr;
    std::array<jack_port_t*, 2> audio_in {}, audio_out {};
    jack_port_t *surface_in = nullptr, *external_in = nullptr, *leds_out = nullptr, *display_out = nullptr;
    std::unique_ptr<Reblocker> reblocker = std::make_unique<Reblocker>();
    jack_nframes_t period = 0;
    bool display_pending = false;
    std::atomic<bool> gone {false};
};

} // namespace

std::unique_ptr<Host> make_jack_host(App& app, Session& session, const std::string& client_name) {
    return std::make_unique<JackHost>(app, session, client_name);
}

} // namespace move
