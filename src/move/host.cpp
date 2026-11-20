// SPDX-License-Identifier: AGPL-3.0-only
#include "move/host.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <algorithm>
#include <ctime>
#include <stdexcept>

namespace move {

Host::Host(App& app, Session& session) : app(app), session(session) {
    wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd < 0) throw std::runtime_error("eventfd failed");
}

Host::~Host() {
    if (wake_fd >= 0) close(wake_fd);
}

void Host::wake() {
    uint64_t one = 1;
    [[maybe_unused]] auto written = write(wake_fd, &one, sizeof one);
}

uint64_t Host::monotonic_ns() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return uint64_t(now.tv_sec) * 1000000000u + uint64_t(now.tv_nsec);
}

void Host::incoming(const uint8_t* bytes, size_t size, bool external, uint64_t timestamp_ns, bool& arrived) {
    if (size == 0 || size > 3) { ++session.midi_dropped; return; }
    if (!external) {
        SurfaceEvent event;
        std::copy(bytes, bytes + size, event.bytes.begin());
        event.input = decode(event.bytes.data(), size);
        event.timestamp_ns = timestamp_ns;
        if (event.input.kind == Input::Kind::Pad || event.input.kind == Input::Kind::PadPressure) {
            app.audio_event(event);
            session.pad_events.fetch_add(1, std::memory_order_relaxed);
        }
    }
    MidiIn message;
    std::copy(bytes, bytes + size, message.bytes.begin());
    message.size = uint8_t(size);
    message.external = external;
    message.timestamp_ns = timestamp_ns;
    if (midi_in.push(message)) { ++session.midi_in; arrived = true; }
    else ++session.midi_dropped;
}

} // namespace move
