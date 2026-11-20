// SPDX-License-Identifier: AGPL-3.0-only
// Melodee's MIDI and sync on the Move (src/app/midi.*, docs/MIDI.md): the
// USB-MIDI packets of the device page's cable 2, the out path (what is
// played by hand at once, the clocks, panic, a paced SysEx, a module's
// MIDI), MIDI in (notes, a MIDI track's echo and its loop guard, Remote,
// program change, Thru, MIDI clock and MTC in), the settings and the
// project's MVMO section. A test binary of its own, beside move-tests.
#include "app/melodee_app.h"
#include "app/midi.h"
#include "app/studio.h"
#include "melodee/engine/midi_engine.h"
#include "move/host.h"
#include "move/spi_device.h"
#include "move/spi_protocol.h"
#include "move/tools.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace move;
namespace mt = melodee::midi_track;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)

uint32_t packet(std::initializer_list<uint8_t> bytes) {
    std::vector<uint8_t> b(bytes);
    return spi::usb_midi_out_packet(spi::cable_external, b.data(), b.size());
}
uint32_t make(uint8_t cin, uint8_t a, uint8_t b = 0, uint8_t c = 0) { return uint32_t(0x20 | cin) | uint32_t(a) << 8 | uint32_t(b) << 16 | uint32_t(c) << 24; }

void test_packets() {
    CHECK(packet({0x92, 60, 100}) == make(0x9, 0x92, 60, 100));
    CHECK(packet({0xb0, 74, 64}) == make(0xb, 0xb0, 74, 64));
    CHECK(packet({0xc1, 12}) == make(0xc, 0xc1, 12));
    CHECK(packet({0xf8}) == make(0xf, 0xf8));
    CHECK(packet({0xfa}) == make(0xf, 0xfa));
    CHECK(packet({0xf1, 0x35}) == make(0x2, 0xf1, 0x35));
    CHECK(packet({0xf2, 4, 1}) == make(0x3, 0xf2, 4, 1));
    // SysEx in pieces: three going on, then the end with one, two or three.
    CHECK(packet({0xf0, 0x7f, 0x7f}) == make(0x4, 0xf0, 0x7f, 0x7f));
    CHECK(packet({0x01, 0x01, 0x20}) == make(0x4, 0x01, 0x01, 0x20));
    CHECK(packet({0xf7}) == make(0x5, 0xf7));
    CHECK(packet({0x10, 0xf7}) == make(0x6, 0x10, 0xf7));
    CHECK(packet({0x10, 0x11, 0xf7}) == make(0x7, 0x10, 0x11, 0xf7));
    CHECK(packet({0xf0, 0x43, 0xf7}) == make(0x7, 0xf0, 0x43, 0xf7));
    CHECK(packet({0x10, 0x11}) == 0); // a piece cut short is none
    // In: cable 2 keeps system common and SysEx pieces; cable 0 and the heartbeat do not.
    uint8_t page[spi::page_size] {};
    auto put = [&](unsigned slot, uint32_t word) { spi::DisplayServer::store32(page + spi::in_midi + 8 * slot, word); };
    put(0, make(0x2, 0xf1, 0x21));
    put(1, make(0x4, 0xf0, 0x7f, 0x7f));
    put(2, make(0x7, 0x01, 0x02, 0xf7));
    put(3, uint32_t(0x04) | 0xf0u << 8);        // a SysEx piece on cable 0: left out
    put(4, uint32_t(0x22));                      // the heartbeat on cable 2: no status
    put(5, make(0x9, 0x90, 36, 90));
    std::array<spi::MidiIn, spi::midi_in_slots> events;
    unsigned skipped = 0, count = spi::read_midi_in(page, events, skipped);
    CHECK(count == 4 && skipped == 2);
    CHECK(events[0].cable == 2 && events[0].size == 2 && events[0].bytes[0] == 0xf1 && events[0].bytes[1] == 0x21);
    CHECK(events[1].size == 3 && events[1].bytes[0] == 0xf0);
    CHECK(events[2].size == 3 && events[2].bytes[0] == 0x01 && events[2].bytes[2] == 0xf7);
    CHECK(events[3].size == 3 && events[3].bytes[0] == 0x90);
}

// The app against the simulated XMOS: MIDI out goes on cable 2 before the
// lights, MIDI in on cable 2 reaches the app.
class EchoApp : public App {
public:
    void render(const AudioBlock&, AudioBlock&) override {}
    void draw(juce::Graphics&) override {}
    void midi(const ExternalMidi& m) override { std::lock_guard<std::mutex> hold(lock); heard.push_back(m); }
    unsigned midi_out(ExternalMidi* out, unsigned room) override {
        unsigned n = 0;
        while (n < room && sent < 40) { out[n++] = {{0x90, uint8_t(sent), 100}, 3, 0}; ++sent; }
        return n;
    }
    std::mutex lock;
    std::vector<ExternalMidi> heard;
    unsigned sent = 0;
};

void test_spi_cable() {
    auto owned = std::make_unique<FakeSpiDevice>(false, 0, "3:x0x92,60,100;4:x0xf8,0,0;5:0x90,68,100");
    FakeSpiDevice* device = owned.get();
    EchoApp app;
    Session session;
    app.attach(session);
    auto host = make_spi_host(app, session, std::move(owned));
    host->start();
    for (int n = 0; n < 4; ++n) host->leds.push({0x9f, uint8_t(71 + n), colour::white});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    host->finish(std::chrono::milliseconds(300));
    const auto& seen = device->seen();
    CHECK(seen.external_messages == 40 && session.midi_out.load() == 40);
    CHECK(seen.external.size() == 40 && seen.external[0] == make(0x9, 0x90, 0, 100));
    CHECK(seen.external_at[spi::external_out_slots] > seen.external_at[0]); // 16 an exchange at most
    CHECK(seen.led_messages >= 4); // the lights still get through
    // In: what came on cable 2 reaches midi() on the MIDI thread (the runtime's job; here the host's queue).
    MidiIn message;
    unsigned external = 0, surface = 0;
    while (host->midi_in.pop(message)) (message.external ? external : surface) += 1;
    CHECK(external == 2 && surface == 1);
}

// ---------------------------------------------------------------- the app

struct Rig {
    std::string home;
    std::unique_ptr<app::MelodeeApp> melodee;
    Session session;
    AudioBlock in {}, out {};
    explicit Rig(const std::string& name, bool fresh = true) : home("build/test-midi/" + name) {
        if (fresh) std::filesystem::remove_all(home);
        std::filesystem::create_directories(home);
        melodee = std::make_unique<app::MelodeeApp>("build/test-samples", home, "build/test-no-modules");
        melodee->attach(session);
        melodee->audio_opened(AudioFormat {});
    }
    app::Studio& studio() { return melodee->studio(); }
    // A block rendered and its exchange's MIDI out taken, as the SPI host does.
    std::vector<ExternalMidi> block() {
        out.fill(0);
        melodee->render(in, out);
        std::vector<ExternalMidi> sent(spi::external_out_slots);
        sent.resize(melodee->midi_out(sent.data(), spi::external_out_slots));
        return sent;
    }
    std::vector<ExternalMidi> blocks(int count) {
        std::vector<ExternalMidi> all;
        for (int n = 0; n < count; ++n) { auto sent = block(); all.insert(all.end(), sent.begin(), sent.end()); }
        return all;
    }
    void pump() { Lights lights; melodee->lights(lights, std::chrono::steady_clock::now()); }
    void in_message(std::initializer_list<uint8_t> bytes, uint64_t ns = 0) {
        ExternalMidi m;
        for (uint8_t b : bytes) m.bytes[m.size++] = b;
        m.timestamp_ns = ns;
        melodee->midi(m);
    }
    void midi_track(unsigned t) {
        std::lock_guard<std::mutex> hold(studio().lock);
        studio().new_track(t, app::TrackKind::Midi);
        studio().select(t);
    }
};

unsigned count_of(const std::vector<ExternalMidi>& sent, uint8_t status, int a = -1) {
    unsigned n = 0;
    for (const auto& m : sent) n += m.bytes[0] == status && (a < 0 || m.bytes[1] == a);
    return n;
}

void test_out() {
    Rig rig("out");
    rig.midi_track(2); // channel 3
    rig.pump();
    rig.blocks(2);
    // A pad on the MIDI track: its note goes out with the very next exchange, on the track's channel.
    rig.melodee->audio_event(make_event(0x90, pad_note(0), 100));
    auto sent = rig.block();
    CHECK(count_of(sent, 0x92) == 1);
    rig.melodee->audio_event(make_event(0x80, pad_note(0), 0));
    CHECK(count_of(rig.block(), 0x82) == 1);
    // MIDI clock out: Start, then 24 pulses a quarter (120 BPM: 172 blocks a quarter).
    {
        std::lock_guard<std::mutex> hold(rig.studio().lock);
        rig.studio().midi.config.sync_out = melodee::MidiSync::Clock;
        rig.studio().set_tempo(1200);
    }
    rig.pump();
    rig.blocks(2);
    { std::lock_guard<std::mutex> hold(rig.studio().lock); rig.studio().play(); }
    auto clocks = rig.blocks(350);
    CHECK(count_of(clocks, 0xfa) == 1);
    unsigned pulses = count_of(clocks, 0xf8);
    CHECK(pulses >= 46 && pulses <= 50);
    { std::lock_guard<std::mutex> hold(rig.studio().lock); rig.studio().stop(); }
    CHECK(count_of(rig.blocks(4), 0xfc) == 1);
    // MTC out: a full frame (a SysEx in pieces) at Play, then quarter frames, four a frame at 25 fps.
    {
        std::lock_guard<std::mutex> hold(rig.studio().lock);
        rig.studio().midi.config.sync_out = melodee::MidiSync::Mtc;
    }
    rig.pump();
    rig.blocks(2);
    { std::lock_guard<std::mutex> hold(rig.studio().lock); rig.studio().play(); }
    auto mtc = rig.blocks(345); // a second
    CHECK(count_of(mtc, 0xf0) == 1);
    unsigned quarters = count_of(mtc, 0xf1);
    CHECK(quarters >= 95 && quarters <= 101);
    { std::lock_guard<std::mutex> hold(rig.studio().lock); rig.studio().stop(); }
    rig.blocks(4);
    // Panic: All Sound Off, All Notes Off and bend centre, sixteen channels, 16 an exchange.
    { std::lock_guard<std::mutex> hold(rig.studio().lock); rig.studio().midi.panic(rig.studio()); }
    auto panic = rig.blocks(4);
    unsigned cc120 = 0, cc123 = 0, bends = 0;
    for (const auto& m : panic) {
        cc120 += (m.bytes[0] & 0xf0) == 0xb0 && m.bytes[1] == 120;
        cc123 += (m.bytes[0] & 0xf0) == 0xb0 && m.bytes[1] == 123;
        bends += (m.bytes[0] & 0xf0) == 0xe0 && m.bytes[1] == 0 && m.bytes[2] == 64;
    }
    CHECK(cc120 == 16 && cc123 == 16 && bends == 16);
    // A Schwung module's MIDI out, from another thread: out with the next exchange.
    const uint8_t from_module[8] {0x09, 0x95, 64, 90, 0x0b, 0xb5, 1, 33};
    std::thread([&] { CHECK(rig.studio().midi.module_send(from_module, 8) == 2); }).join();
    auto module_sent = rig.block();
    CHECK(count_of(module_sent, 0x95, 64) == 1 && count_of(module_sent, 0xb5, 1) == 1);
}

void test_in() {
    Rig rig("in");
    rig.midi_track(2); // channel 3, selected
    rig.pump();
    rig.blocks(2);
    // A note on channel 1 plays the selected MIDI track, which echoes it on its own channel.
    rig.in_message({0x90, 60, 90});
    auto sent = rig.blocks(2);
    CHECK(count_of(sent, 0x92, 60) == 1);
    rig.in_message({0x80, 60, 0});
    CHECK(count_of(rig.blocks(2), 0x82, 60) == 1);
    // A CC is echoed as it came (no slot sends CC 20), re-channelled.
    rig.in_message({0xb0, 20, 77});
    CHECK(count_of(rig.blocks(2), 0xb2, 20) == 1);
    // The loop guard: a note on the track's own channel does not go out again.
    rig.in_message({0x92, 62, 90});
    CHECK(count_of(rig.blocks(2), 0x92, 62) == 0);
    rig.in_message({0x82, 62, 0});
    rig.blocks(2);
    // Remote: CC 102 on the Remote channel selects a track and goes no further.
    {
        std::lock_guard<std::mutex> hold(rig.studio().lock);
        rig.studio().midi.config.remote_channel = 16;
    }
    rig.in_message({0xbf, 102, 0});
    { std::lock_guard<std::mutex> hold(rig.studio().lock); CHECK(rig.studio().track() == 0); }
    rig.in_message({0xbf, 103, 5}); // pattern 6 on the selected track
    { std::lock_guard<std::mutex> hold(rig.studio().lock); CHECK(rig.studio().project().tracks[0].pattern == 5); }
    // Program change launches that pattern on every track (once turned on).
    rig.in_message({0xc0, 3});
    { std::lock_guard<std::mutex> hold(rig.studio().lock); CHECK(rig.studio().project().tracks[2].pattern == 0); rig.studio().midi.config.program_change = true; }
    rig.in_message({0xc0, 3});
    { std::lock_guard<std::mutex> hold(rig.studio().lock); CHECK(rig.studio().project().tracks[2].pattern == 3 && rig.studio().project().tracks[0].pattern == 3); }
    // Thru (the kit selected: nothing echoed): what comes in goes out as it came.
    { std::lock_guard<std::mutex> hold(rig.studio().lock); rig.studio().midi.config.thru = true; }
    rig.in_message({0xb4, 7, 100});
    CHECK(count_of(rig.blocks(1), 0xb4, 7) == 1);
    // A track's own channel (its MIDI IN) plays it whichever is selected.
    {
        std::lock_guard<std::mutex> hold(rig.studio().lock);
        rig.studio().midi.config.thru = false;
        rig.studio().project().tracks[2].midi_channel = 9;
    }
    rig.in_message({0x98, 48, 70});
    CHECK(count_of(rig.blocks(2), 0x92, 48) == 1);
    rig.in_message({0x88, 48, 0});
    CHECK(count_of(rig.blocks(2), 0x82, 48) == 1);
}

uint64_t now_ns() { return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }

void test_sync_in() {
    Rig rig("sync");
    {
        std::lock_guard<std::mutex> hold(rig.studio().lock);
        rig.studio().midi.config.sync_in = melodee::MidiSync::Clock;
    }
    rig.pump();
    rig.blocks(2);
    // Start, then pulses 20.833 ms apart: 120 BPM; the transport follows.
    uint64_t at = now_ns();
    rig.in_message({0xfa}, at);
    for (int pulse = 0; pulse < 48; ++pulse) {
        rig.in_message({0xf8}, at + uint64_t(pulse) * 20833333u);
        rig.blocks(1);
    }
    {
        std::lock_guard<std::mutex> hold(rig.studio().lock);
        CHECK(rig.studio().midi.following() && rig.studio().playing());
        unsigned tempo = rig.studio().midi.tempo_in();
        CHECK(tempo >= 1190 && tempo <= 1210);
    }
    rig.in_message({0xfc}, at + 49 * 20833333u);
    rig.blocks(2);
    { std::lock_guard<std::mutex> hold(rig.studio().lock); CHECK(!rig.studio().midi.following() && !rig.studio().playing()); }

    // MTC in at 25 fps: quarter frames 10 ms apart from 0:00:01:00; the song starts once two codes agree.
    {
        std::lock_guard<std::mutex> hold(rig.studio().lock);
        rig.studio().midi.config.sync_in = melodee::MidiSync::Mtc;
    }
    rig.pump();
    uint64_t start = now_ns();
    unsigned frame = 25; // one second in
    for (int piece = 0; piece < 64; ++piece) {
        unsigned code = frame + unsigned(piece / 8) * 2, f = code % 25, s = code / 25 % 60;
        uint8_t nibble = 0;
        switch (piece % 8) {
        case 0: nibble = uint8_t(f & 15); break;
        case 1: nibble = uint8_t(f >> 4); break;
        case 2: nibble = uint8_t(s & 15); break;
        case 3: nibble = uint8_t(s >> 4); break;
        case 7: nibble = uint8_t(1 << 1); break; // 25 fps, hour 0
        default: nibble = 0; break;
        }
        rig.in_message({0xf1, uint8_t((piece % 8) << 4 | nibble)}, start + uint64_t(piece) * 10000000u);
        rig.blocks(1);
    }
    { std::lock_guard<std::mutex> hold(rig.studio().lock); CHECK(rig.studio().midi.following_mtc() && rig.studio().playing()); }
}

void test_sysex() {
    Rig rig("sysex");
    rig.midi_track(2);
    std::filesystem::create_directories(rig.home + "/SysEx");
    std::vector<uint8_t> file {0xf0, 0x41, 0x10, 0x16, 0x12, 0x00, 0x00, 0x00, 0x7f, 0xf7, 0x00, 0xf0, 0x43, 0x00, 0xf7};
    { std::ofstream out(rig.home + "/SysEx/JUNO.SYX", std::ios::binary); out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size())); }
    {
        std::lock_guard<std::mutex> hold(rig.studio().lock);
        auto files = rig.studio().midi.sysex_files(rig.studio());
        CHECK(files.size() == 1 && files[0] == "JUNO.SYX");
        std::strcpy(rig.studio().project().tracks[2].midi.sysex, "JUNO.SYX");
        rig.studio().midi.send_sysex(rig.studio(), 2);
    }
    rig.melodee->service();
    // A pad's note played meanwhile waits behind the SysEx: nothing lands inside it.
    rig.melodee->audio_event(make_event(0x90, pad_note(0), 100));
    std::vector<uint8_t> got;
    bool note_inside = false, open = false;
    for (int n = 0; n < 10; ++n)
        for (const auto& m : rig.block()) {
            bool piece = m.bytes[0] == 0xf0 || m.bytes[0] == 0xf7 || m.bytes[0] < 0x80; // a lone F7 ends one
            if (!piece && open) note_inside = true;
            if (piece) {
                for (unsigned k = 0; k < m.size; ++k) { got.push_back(m.bytes[k]); open = m.bytes[k] != 0xf7; }
            }
        }
    std::vector<uint8_t> wanted(file.begin(), file.begin() + 10);
    wanted.insert(wanted.end(), file.begin() + 11, file.end()); // the stray byte between them is left out
    CHECK(got == wanted && !note_inside);
    rig.melodee->service(); // the buffer it sent, freed
    CHECK(rig.melodee->status().find("midi_sysex=1/") != std::string::npos);
}

void test_saved() {
    {
        Rig rig("saved");
        rig.midi_track(2);
        {
            std::lock_guard<std::mutex> hold(rig.studio().lock);
            auto& midi = rig.studio().midi;
            midi.config.in_channel = 5;
            midi.config.sync_out = melodee::MidiSync::Mtc;
            midi.config.remote_channel = 16;
            midi.track_out[0] = 7;
            CHECK(midi.out_channel(rig.studio(), 0) == 7);
            CHECK(midi.out_channel(rig.studio(), 1) == 1);  // AUTO: MIDI Config's out channel
            CHECK(midi.out_channel(rig.studio(), 2) == 3);  // a MIDI track: its DEVICE channel
            rig.studio().changed();
        }
        CHECK(rig.melodee->save_now().empty());
    }
    Rig again("saved", false);
    std::lock_guard<std::mutex> hold(again.studio().lock);
    const auto& midi = again.studio().midi;
    CHECK(midi.config.in_channel == 5 && midi.config.sync_out == melodee::MidiSync::Mtc && midi.config.remote_channel == 16);
    CHECK(midi.track_out[0] == 7 && midi.track_out[1] == 0);
    CHECK(midi.config.port == melodee::MidiPort::Usb && !midi.config.cv_out);
}

} // namespace

int main() {
    std::filesystem::create_directories("build/test-midi");
    std::filesystem::create_directories("build/test-samples");
    std::filesystem::create_directories("build/test-no-modules");
    test_packets();
    test_spi_cable();
    test_out();
    test_in();
    test_sync_in();
    test_sysex();
    test_saved();
    std::cout << "midi-tests: " << checks - failures << '/' << checks << " checks passed\n";
    return failures ? 1 : 0;
}
