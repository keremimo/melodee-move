// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/engine/midi_out.h"
#include "melodee/model/limits.h"
#include "melodee/model/midi_config.h"
#include "melodee/shell/midi_route.h"
#include "melodee/shell/mtc_in.h"
#include "move/app.h"
#include "move/queues.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace melodee { class EngineHost; struct Track; }

// Melodee's MIDI and sync on the Move (docs/MIDI.md). The Move has one MIDI
// port for Melodee: USB-MIDI cable 2 of its device page, where a device on
// USB-A and a computer on USB-C's "Ableton Move Standalone Port" both
// arrive, in and out. What melodee-sample's shell does in shell/midi_io.cpp
// for the MPC's jacks and USB, this does for that one port; Melodee's engine
// host decides what goes out and when (engine/midi_out.h), its route and MTC
// reader (shell/midi_route.h, shell/mtc_in.h) are compiled unchanged.
//
// Out, on the audio thread: after each render() the engine host's events go
// into a queue by the frame they belong to, and those heard in the block just
// rendered go out with the exchange that carries its audio (the device takes
// MIDI out once an exchange: a block, 2.9 ms, is the finest timing there is).
// What is played by hand (a pad, a key, MIDI in, a knob) goes at once. The
// clocks go first; a SysEx goes in paced pieces while everything else but
// the clocks waits behind it; Panic, Thru and a Schwung module's MIDI out
// join from other threads through lock-free rings.
//
// In, on the MIDI thread, under the studio's lock: notes on MIDI Config's in
// channel play the selected track, a track's own channel (its MIDI IN in the
// track options) plays it whichever is selected; the Remote CCs select a
// track or launch a pattern or a sequence; program change launches a pattern
// on every track; MIDI clock and MTC lead the transport while Sync In says
// so; Thru sends it on; a MIDI track echoes what plays it.
namespace app {

class Studio;

class Midi {
public:
    using Clock = std::chrono::steady_clock;
    Midi();
    ~Midi();
    Midi(const Midi&) = delete;
    Midi& operator=(const Midi&) = delete;

    // ---------------------------------------------------------- under the studio's lock
    melodee::MidiConfig config;
    // MIDI Config's SCALE KEYS: on an In-Key track a keyboard's white keys
    // play the scale, its black keys nothing (app/scale_keys.h). The Move's
    // own key in Move.settings, as melodee-sample keeps it in its settings.
    bool scale_keys = false;
    // Each track's MIDI OUT (the Move's track options): 0 AUTO (its MIDI IN
    // channel, else MIDI Config's out channel), else 1-16. Kept in the
    // project's MVMO section; the MPC keeps one channel a track.
    std::array<uint8_t, melodee::track_count> track_out {};
    // The channel track `t`'s notes go out on: a MIDI track its DEVICE
    // channel, any other its MIDI OUT, else as AUTO says.
    unsigned out_channel(const Studio& studio, unsigned t) const;
    // MIDI thread: one message from the port (a SysEx in pieces).
    void receive(Studio& studio, const move::ExternalMidi& message);
    // MIDI thread, every lights() pass: MIDI Config and the tracks' channels
    // to the engine when they change, MTC's stop, a MIDI track's input.
    void pump(Studio& studio, Clock::time_point now);
    // Every note here ends; All Sound Off, All Notes Off and bend centre on
    // all sixteen channels.
    void panic(Studio& studio);
    // A MIDI track's SysEx file, sent now (by hand) or as the project loads.
    void send_sysex(Studio& studio, unsigned track);
    std::string sysex_folder(const Studio& studio) const;
    std::vector<std::string> sysex_files(const Studio& studio) const; // the folder's .syx, sorted
    int sysex_percent(unsigned track) const; // how far its SysEx has gone out; -1 none going
    // MIDI clock or MTC in leads the transport; the tempo heard (tenths).
    bool following() const { return ext_running.load(std::memory_order_relaxed) || mtc_leads.load(std::memory_order_relaxed); }
    bool following_mtc() const { return mtc_leads.load(std::memory_order_relaxed); }
    unsigned tempo_in() const { return tempo_seen.load(std::memory_order_relaxed); }
    // MIDI went in or out a moment ago (the rail's IN and OUT).
    bool in_lately(Clock::time_point now) const;
    bool out_lately(Clock::time_point now) const;
    // The last message a MIDI track sent, as the detail line shows it
    // ("C3 100", "CC74 64"); empty when none yet.
    std::string last_out(unsigned track) const;

    // Move.settings: MIDI Config's keys (model/midi_config.h's names).
    void write_settings(std::ostream& out) const;
    bool read_setting(const std::string& key, int value);
    // The project's MVMO section (a section of the Move's own after
    // Melodee's, which Melodee's reader skips): each track's MIDI OUT.
    std::string section_text() const;
    void read_section(const std::string& text);
    void append_section(std::vector<uint8_t>& project_bytes) const;
    void read_section_of(const std::string& project_path); // none there: every track AUTO

    // ---------------------------------------------------------- service thread
    // The SysEx files asked for, read (bounded) and handed to the audio
    // thread; the ones it has sent, freed.
    void service(Studio& studio);
    // `melodee-midi` (MELODEE.md › Remote, through the runtime's control
    // socket, App::command()): "panic", or a message in hex ("90 3c 64"),
    // taken as if it came in on the port. Under the studio's lock. The reply.
    std::string command(Studio& studio, const std::string& arguments);

    // ---------------------------------------------------------- audio thread
    // Marks the audio thread (audio_opened()): a module's MIDI out from it
    // goes straight onto the wire, from any other through a ring.
    static void audio_thread();
    // After the engine host's render: its events into the queue, the ones
    // heard in this block (or played by hand) onto the wire.
    void after_render(melodee::EngineHost& host);
    // What goes out with the next exchange.
    unsigned take(move::ExternalMidi* out, unsigned room);

    // ---------------------------------------------------------- any thread
    // A Schwung module's MIDI out (host_api's midi_send_external): USB-MIDI
    // packets, four bytes each.
    int module_send(const uint8_t* packets, int length);
    std::string status() const;

private:
    // MIDI thread.
    void configure(Studio& studio, bool force = false);
    void note_in(Studio& studio, unsigned track, int key, int velocity, bool down, uint64_t ns, bool echo);
    void release_all(Studio& studio);
    void follow_mtc(Studio& studio, const melodee::MtcReader::Event& event);
    void send_mtc(Studio& studio, uint16_t what, const melodee::MtcReader::Event& event);
    void echo_raw(Studio& studio, const uint8_t* bytes, unsigned size, uint16_t tracks, uint64_t ns);
    void sysex_in(Studio& studio, const move::ExternalMidi& message);
    std::array<std::array<bool, 128>, melodee::track_count> held {}; // keys MIDI in holds down, a track each
    std::array<std::array<int16_t, 128>, melodee::track_count> played {}; // what each held key played (16 Levels)
    melodee::ClockFollower follower;
    melodee::MtcReader mtc;
    bool mtc_playing = false, mtc_taken = false, mtc_held = false;
    unsigned tempo_sent = 0;
    uint32_t song_position = 0;
    bool configured = false;
    melodee::MidiConfig config_sent;
    std::array<uint8_t, melodee::track_count> channel_sent {}, mode_sent {}, audio_sent {};
    uint64_t last_pulse_ns = 0;
    std::array<uint8_t, 32> sysex_buffer {}; // a SysEx coming in (an MTC full frame), up to its F7
    unsigned sysex_size = 0;
    bool sysex_open = false;
    std::vector<unsigned> sysex_asked; // tracks whose SysEx the service thread is to read

    // Audio thread.
    void deliver(const melodee::MidiOutEvent& event);
    void put(uint8_t a, int b = -1, int c = -1);      // a message onto the wire
    void put_realtime(uint8_t status);
    void put_sysex(const uint8_t* bytes, size_t size); // a whole SysEx, in pieces, onto the wire
    void do_panic();
    melodee::MidiOutQueue pending;
    struct Wire {
        static constexpr unsigned capacity = 1024;
        std::array<move::ExternalMidi, capacity> items {};
        unsigned head = 0, count = 0;
        bool push(const move::ExternalMidi& m) { if (count == capacity) return false; items[(head + count++) % capacity] = m; return true; }
        const move::ExternalMidi& front() const { return items[head]; }
        void pop() { head = (head + 1) % capacity; --count; }
    };
    Wire wire, realtime;
    struct SysexJob { std::vector<uint8_t>* bytes = nullptr; uint8_t track = 0; };
    move::SpscRing<SysexJob, 16> sysex_jobs, sysex_done; // service -> audio, audio -> service
    SysexJob sending;
    size_t sending_at = 0;
    bool sending_open = false; // between its F0 and F7
    unsigned panics_done = 0;
    uint64_t frames_end = 0;

    // Between threads.
    move::SpscRing<move::ExternalMidi, 256> thru;          // MIDI thread -> audio thread
    move::SpscRing<move::ExternalMidi, 256> module_out;    // a module's, from a thread not the audio one
    std::mutex module_lock;                                // that ring's producers
    std::atomic<unsigned> panics_asked {0};
    std::atomic<bool> ext_running {false}, mtc_leads {false};
    std::atomic<unsigned> tempo_seen {0};
    std::atomic<int64_t> in_at {0}, out_at {0}; // steady_clock ns of the last message in and out
    std::array<std::atomic<uint32_t>, melodee::track_count> last_sent {}; // a MIDI track's last message: its bytes
    std::array<std::atomic<int>, melodee::track_count> sysex_progress {}; // percent + 1; 0 none going
    std::atomic<unsigned> ins {0}, outs {0}, pulses_in {0}, mtc_in {0}, echoed {0}, echo_guarded {0}, thru_sent {0};
    std::atomic<unsigned> dropped {0}, sysex_dropped_in {0}, sysex_sent {0}, sysex_bytes {0}, panics {0}, module_msgs {0};
    std::atomic<unsigned> superseded {0}, late_blocks {0};
};

} // namespace app
