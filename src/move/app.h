// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/audio.h"
#include "move/controls.h"
#include "move/lights.h"
#include "move/power.h"
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

// What an app on the Move provides, and what the runtime tells it. The hooks
// and their threads follow sample-base's App (melodee-sample/base/docs/APPS.md)
// so Melodee can move across: render() gets the same 128-frame S32 blocks,
// surface() the controls, lights() the LEDs and draw() the screen. What
// differs is the hardware: 32 pads, 16 steps, 8 knobs and a 128 x 64
// one-bit screen in place of the MPC Sample's 16 pads and 320 x 240 colour.
namespace move {

// One message from the surface, decoded, with its raw bytes and when it
// came in (CLOCK_MONOTONIC nanoseconds, from JACK's frame time; 0 for one
// made up by a test).
struct SurfaceEvent {
    Input input;
    std::array<uint8_t, 3> bytes {};
    uint64_t timestamp_ns = 0;
};

// A message from or to the external MIDI port (system:midi_capture_ext;
// on the Move's own device, USB-MIDI cable 2: a device on USB-A and a
// computer on USB-C, docs/MIDI.md): a channel or system message of one to
// three bytes, or a piece of a SysEx, three bytes at most, the first piece
// beginning with F0 and the last ending with F7.
struct ExternalMidi {
    std::array<uint8_t, 3> bytes {};
    uint8_t size = 0;
    uint64_t timestamp_ns = 0;
};

// What the runtime tracks, readable from every hook.
struct Session {
    std::atomic<uint32_t> held_pads {0};  // bit n: pad n held
    std::atomic<int> last_pad {-1}, velocity {0};
    std::atomic<bool> shift {false};
    std::atomic<unsigned> rate {44100}, period {128}, latency_frames {0};
    std::atomic<uint64_t> blocks {0}, xruns {0}, underruns {0};
    std::atomic<uint64_t> midi_in {0}, midi_dropped {0}, leds_sent {0}, frames_sent {0}, frames_failed {0};
    std::atomic<uint64_t> midi_out {0}; // messages sent on the external MIDI port
    std::atomic<float> input_peak {0}, output_peak {0}; // The last period's, 0..1
    std::atomic<float> cpu_load {0};                    // JACK's DSP load, percent
    std::atomic<uint64_t> process_calls {0}, process_ns {0}; // time in the audio thread's cycle, ours alone
    std::atomic<uint64_t> process_ns_peak {0};               // the longest since the last status line
    // Pads handed to App::audio_event(), and for a cycle that carried one,
    // the time from reading it to the end of the render() it sounds in.
    std::atomic<uint64_t> pad_events {0}, pad_to_render_ns {0}, pad_to_render_ns_peak {0};
    std::atomic<uint64_t> device_transfer_ns {0}; // the Move's device: how long the driver's last transfer took
    std::atomic<bool> realtime {false};           // the audio thread got SCHED_FIFO
    std::atomic<const char*> backend {""};        // "jack" or "spi"
    // The Move's power button (move/power.h): "shut down?" asked, or
    // shutting down; and the battery's charge, percent (-1: not reported).
    std::atomic<Power> power {Power::On};
    std::atomic<int> battery {-1};
};

class App {
public:
    using Clock = std::chrono::steady_clock;
    virtual ~App() = default;

    // Audio (JACK's process thread), every 128 frames: write `output` from
    // `input`, interleaved stereo S32. Never allocate, lock, wait or touch
    // files here.
    virtual void render(const AudioBlock& input, AudioBlock& output) = 0;
    // Audio, before the first render() and after JACK changes its period.
    virtual void audio_opened(const AudioFormat&) {}

    // Audio thread, before the render() it should sound in: a pad hit,
    // release or pressure change, read in this very cycle. This is the path
    // for sound, with no thread in between; surface() still gets the same
    // event afterwards on the MIDI thread, for everything else. Keeps
    // render()'s rules.
    virtual void audio_event(const SurfaceEvent&) {}
    // MIDI thread: a message from the Move's controls. Shift + Back never
    // arrives: the runtime takes it to quit. Nor do the wheel and Back
    // while the power button's question is open (draw_power()).
    virtual void surface(const SurfaceEvent&) {}
    // MIDI thread: a message from an external MIDI port.
    virtual void midi(const ExternalMidi&) {}
    // Audio thread, before each exchange with the device (each JACK period):
    // what goes out on the external MIDI port with the audio of the last
    // render(), up to `room` messages in order. Returns how many it wrote;
    // the rest waits for the next. Keeps render()'s rules.
    virtual unsigned midi_out(ExternalMidi*, unsigned /*room*/) { return 0; }
    // MIDI thread, every 5 ms: ask for lights. Only changes are sent.
    virtual void lights(Lights&, Clock::time_point) {}

    // Screen thread, at most 60 times a second: draw the whole 128 x 64
    // screen, white on black. Only changed frames go to the screen.
    virtual void draw(juce::Graphics&) = 0;
    // Screen thread, in place of draw() while the power button's question
    // is open or the Move shuts down (session's `power`; the runtime takes
    // the wheel's push as yes and Back as no, as Ableton's app does): draw
    // that whole screen. False: the app has none, and the runtime draws its
    // own over draw()'s.
    virtual bool draw_power(juce::Graphics&) { return false; }

    // Service thread, every 10 ms: slow work (files, loading).
    virtual void service() {}
    // Service thread, once, as the run ends (Shift + Back, a signal, its
    // time) while the audio thread still runs: last writes that need it.
    virtual void stopping() {}
    // Any thread: true once the app wants to quit.
    virtual bool finished() const { return false; }
    // Any thread: extra `key=value` fields for the status line.
    virtual std::string status() const { return {}; }
    // Service thread: a line from the control channel (move/remote.h) that
    // is not the runtime's own; the reply, or empty for "unknown".
    virtual std::string command(const std::string&) { return {}; }

    // Before any hook runs. Virtual so an app that wraps another (the web
    // page's layer, src/web/web_layer.h) attaches it too.
    virtual void attach(const Session& session) { attached = &session; }

protected:
    const Session& session() const { return *attached; }

private:
    const Session* attached = nullptr;
};

} // namespace move
