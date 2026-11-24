// SPDX-License-Identifier: AGPL-3.0-only
#include "move/spi_device.h"
#include "move/power.h"
#include "move/spi_protocol.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace move {
namespace {

class MoveDevice : public SpiDevice {
public:
    MoveDevice() {
        fd = open(spi::device_path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            int error = errno;
            if (error == EBUSY)
                throw std::runtime_error("Ableton's app still holds /dev/ablspi0.0; start melodee-move from Schwung's Tools "
                                         "menu or tools/run-stock.sh, which stop it first");
            if (error == ENOENT)
                throw std::runtime_error("no /dev/ablspi0.0: not a Move on Ableton's firmware (on Armbian, use --backend jack)");
            throw std::runtime_error(std::string("cannot open /dev/ablspi0.0: ") + std::strerror(error));
        }
        void* mapped = mmap(nullptr, spi::page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapped == MAP_FAILED) {
            int error = errno;
            close(fd);
            throw std::runtime_error(std::string("cannot map /dev/ablspi0.0: ") + std::strerror(error));
        }
        memory = static_cast<uint8_t*>(mapped);
        std::memset(memory, 0, spi::in_midi); // a clean TX half: no MIDI, no screen answer, silence
        if (ioctl(fd, spi::ioctl_set_speed, spi::speed_hz) != 0)
            throw std::runtime_error(std::string("cannot set the SPI speed: ") + std::strerror(errno));
    }
    ~MoveDevice() override {
        if (memory) munmap(memory, spi::page_size);
        if (fd >= 0) close(fd);
    }
    uint8_t* page() override { return memory; }
    bool transfer() override {
        int result;
        do result = ioctl(fd, spi::ioctl_wait_and_send_with_size, spi::transfer_size);
        while (result != 0 && errno == EINTR);
        return result == 0;
    }
    uint64_t transfer_ns() const override {
        uint64_t value;
        std::memcpy(&value, memory + spi::transfer_time, sizeof value);
        return value;
    }

private:
    int fd = -1;
    uint8_t* memory = nullptr;
};

} // namespace

std::unique_ptr<SpiDevice> open_move_device() { return std::make_unique<MoveDevice>(); }

struct FakeSpiDevice::State {
    bool paced;
    unsigned loopback;
    struct Scripted { uint64_t transfer; uint8_t status, data1, data2; bool external, power; };
    std::vector<Scripted> script;
    size_t next_scripted = 0;
    alignas(64) uint8_t memory[spi::page_size] {};
    std::vector<int16_t> line;        // the cable: stereo frames, a ring
    size_t written = 0;               // frames written into the line so far
    uint32_t requested = 0;           // the chunk asked for in the last RX
    uint8_t assembling[frame_bytes] {};
    unsigned chunks_in_frame = 0;
    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();
    Seen seen;
};

FakeSpiDevice::FakeSpiDevice(bool paced, unsigned loopback_frames, std::string script) : state(std::make_unique<State>()) {
    state->paced = paced;
    state->loopback = loopback_frames;
    state->line.assign(2 * 16384, 0);
    state->seen.external.reserve(4096); // kept on the device's thread: no allocation there
    state->seen.sysex.reserve(256);
    state->seen.external_at.reserve(4096);
    // "transfer:status,data1,data2;..." with numbers in decimal or 0x hex.
    for (size_t at = 0; at < script.size();) {
        size_t end = script.find(';', at);
        if (end == std::string::npos) end = script.size();
        std::string item = script.substr(at, end - at);
        at = end + 1;
        if (item.empty()) continue;
        char* cursor = item.data();
        State::Scripted entry {};
        entry.transfer = std::strtoull(cursor, &cursor, 0); if (*cursor) ++cursor;
        if (*cursor == 'x') { entry.external = true; ++cursor; }
        if (*cursor == 'p') { // a power report: the state's flags, the charge
            entry.power = true; ++cursor;
            entry.status = power::report_status;
            entry.data1 = uint8_t(std::strtoul(cursor, &cursor, 0)); if (*cursor) ++cursor;
            entry.data2 = uint8_t(std::strtoul(cursor, &cursor, 0));
            state->script.push_back(entry);
            continue;
        }
        entry.status = uint8_t(std::strtoul(cursor, &cursor, 0)); if (*cursor) ++cursor;
        entry.data1 = uint8_t(std::strtoul(cursor, &cursor, 0)); if (*cursor) ++cursor;
        entry.data2 = uint8_t(std::strtoul(cursor, &cursor, 0));
        state->script.push_back(entry);
    }
    std::sort(state->script.begin(), state->script.end(), [](const auto& a, const auto& b) { return a.transfer < b.transfer; });
}

FakeSpiDevice::~FakeSpiDevice() = default;
uint8_t* FakeSpiDevice::page() { return state->memory; }
const FakeSpiDevice::Seen& FakeSpiDevice::seen() const { return state->seen; }

bool FakeSpiDevice::transfer() {
    auto& s = *state;
    uint8_t* page = s.memory;
    if (s.paced) {
        s.next += std::chrono::nanoseconds(1000000000ull * audio_frames / spi::audio_rate); // 2.9 ms
        std::this_thread::sleep_until(s.next);
    }
    // The XMOS takes the TX half: lights, a screen chunk, audio.
    for (unsigned slot = 0; slot < spi::midi_out_slots; ++slot) {
        uint32_t packet = spi::DisplayServer::load32(page + spi::out_midi + slot * 4);
        if (!packet) continue;
        if ((packet >> 4 & 0x0f) == spi::cable_external) { // MIDI out, not a light
            ++s.seen.external_messages;
            if (s.seen.external.size() < 4096) { s.seen.external.push_back(packet); s.seen.external_at.push_back(s.seen.transfers); }
            continue;
        }
        if (uint8_t code = packet & 0x0f; code >= 0x4 && code <= 0x7) { // a SysEx piece
            if (s.seen.sysex.size() < 256) s.seen.sysex.push_back(packet);
        }
        ++s.seen.led_messages;
        s.seen.last_led_packet = packet;
    }
    uint32_t answered = spi::DisplayServer::load32(page + spi::out_display_index);
    if (s.requested && answered == s.requested) {
        size_t offset = (answered - 1) * spi::display_chunk;
        size_t length = answered == spi::display_chunks ? frame_bytes - offset : spi::display_chunk;
        std::memcpy(s.assembling + offset, page + spi::out_display_data, length);
        ++s.seen.chunks_answered;
        if (answered == spi::display_chunks) {
            std::memcpy(s.seen.frame, s.assembling, frame_bytes);
            ++s.seen.frames_completed;
        }
    } else if (answered) ++s.seen.chunks_wrong;
    size_t ring = s.line.size() / 2;
    for (unsigned n = 0; n < audio_frames; ++n) {
        for (unsigned channel = 0; channel < 2; ++channel) {
            const uint8_t* at = page + spi::out_audio + 4 * n + 2 * channel;
            int16_t sample = int16_t(uint16_t(at[0]) | uint16_t(at[1]) << 8);
            s.line[2 * ((s.written + n) % ring) + channel] = sample;
            s.seen.output_peak = std::max<int32_t>(s.seen.output_peak, std::abs(int32_t(sample)));
        }
    }
    s.written += audio_frames;
    ++s.seen.transfers;

    // ...and fills the RX half: the script's control messages, the next
    // screen chunk it wants, and what came back through the cable.
    unsigned events = 0;
    while (s.next_scripted < s.script.size() && s.script[s.next_scripted].transfer <= s.seen.transfers && events < spi::midi_in_slots) {
        const auto& entry = s.script[s.next_scripted++];
        uint8_t* event = page + spi::in_midi + 8 * events++;
        if (entry.external) { // as the XMOS puts external MIDI in: cable 2, its code index
            const uint8_t bytes[3] {entry.status, entry.data1, entry.data2};
            spi::DisplayServer::store32(event, spi::usb_midi_out_packet(spi::cable_external, bytes, 3));
        } else if (entry.power) { // as the XMOS reports power: cable 14, a control change
            event[0] = uint8_t(spi::cable_power << 4 | 0xb);
            event[1] = entry.status; event[2] = entry.data1; event[3] = entry.data2;
        } else {
            event[0] = uint8_t(spi::cable_controls << 4 | entry.status >> 4);
            event[1] = entry.status; event[2] = entry.data1; event[3] = entry.data2;
        }
        spi::DisplayServer::store32(event + 4, uint32_t(s.seen.transfers));
    }
    if (events < spi::midi_in_slots && events) std::memset(page + spi::in_midi + 8 * events, 0, 8);
    s.requested = s.requested % spi::display_chunks + 1;
    spi::DisplayServer::store32(page + spi::in_display_index, s.requested);
    for (unsigned n = 0; n < audio_frames; ++n) {
        size_t frame = s.written - audio_frames + n;
        for (unsigned channel = 0; channel < 2; ++channel) {
            int16_t sample = frame >= s.loopback ? s.line[2 * ((frame - s.loopback) % ring) + channel] : 0;
            uint8_t* at = page + spi::in_audio + 4 * n + 2 * channel;
            at[0] = uint8_t(uint16_t(sample)); at[1] = uint8_t(uint16_t(sample) >> 8);
        }
    }
    return true;
}

} // namespace move
