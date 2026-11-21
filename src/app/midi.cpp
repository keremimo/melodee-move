// SPDX-License-Identifier: AGPL-3.0-only
// Melodee's MIDI and sync on the Move's one MIDI port (midi.h, docs/MIDI.md),
// after melodee-sample's shell/midi_io.cpp, which does the same for the
// MPC's jacks and USB.
#include "app/midi.h"
#include "app/studio.h"
#include "melodee/engine/engine_host.h"
#include "melodee/engine/midi_engine.h"
#include "melodee/model/pattern.h"
#include "melodee/model/sampler.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace app {

namespace fs = std::filesystem;
using melodee::Command;
using Type = Command::Type;
using melodee::EngineKind;
using melodee::MidiOutEvent;
using melodee::track_count;
namespace mt = melodee::midi_track;

namespace {

constexpr int64_t lately_ns = 150000000;         // the rail's IN and OUT stay lit this long
constexpr size_t sysex_most = 64 * 1024;          // a .syx read at most this big (as Melodee)
constexpr unsigned sysex_pieces_a_block = 4;      // 12 bytes an exchange: 4.1 KB/s, a little past DIN's 3.1
constexpr uint64_t block_frames = move::audio_frames;

int64_t now_ns() {
    return int64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
int64_t ns_of(Midi::Clock::time_point t) {
    return int64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
}
move::ExternalMidi message(uint8_t a, int b, int c) {
    move::ExternalMidi m;
    m.bytes[0] = a;
    m.size = 1;
    if (b >= 0) { m.bytes[1] = uint8_t(b & 0x7f); m.size = 2; }
    if (c >= 0) { m.bytes[2] = uint8_t(c & 0x7f); m.size = 3; }
    return m;
}
// Ableton's note names: 60 is C3.
std::string note_text(unsigned note) {
    static const char* names[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[note % 12] + std::to_string(int(note / 12) - 2);
}

} // namespace

Midi::Midi() {
    for (auto& keys : played) keys.fill(-1);
    for (auto& sent : last_sent) sent.store(0, std::memory_order_relaxed);
    for (auto& progress : sysex_progress) progress.store(0, std::memory_order_relaxed);
    config.port = melodee::MidiPort::Usb; // the Move's one port
}

Midi::~Midi() {
    SysexJob job;
    while (sysex_jobs.pop(job)) delete job.bytes;
    while (sysex_done.pop(job)) delete job.bytes;
    delete sending.bytes;
}

unsigned Midi::out_channel(const Studio& studio, unsigned t) const {
    const auto& track = studio.project().tracks[t % track_count];
    if (track.engine == EngineKind::Midi) return unsigned(std::clamp<int>(track.params[mt::Channel], 1, 16));
    if (track_out[t % track_count]) return track_out[t % track_count];
    return melodee::out_channel_of(config, track);
}

bool Midi::in_lately(Clock::time_point now) const { return ns_of(now) - in_at.load(std::memory_order_relaxed) < lately_ns; }
bool Midi::out_lately(Clock::time_point now) const { return ns_of(now) - out_at.load(std::memory_order_relaxed) < lately_ns; }

std::string Midi::last_out(unsigned track) const {
    uint32_t bytes = last_sent[track % track_count].load(std::memory_order_relaxed);
    if (!bytes) return {};
    unsigned status = bytes >> 16 & 0xff, a = bytes >> 8 & 0xff, b = bytes & 0xff;
    switch (status & 0xf0) {
    case 0x90: return note_text(a) + " " + std::to_string(b);
    case 0x80: return note_text(a) + " OFF";
    case 0xb0: return "CC" + std::to_string(a) + " " + std::to_string(b);
    case 0xc0: return "PROG " + std::to_string(a + 1);
    case 0xe0: return "BEND " + std::to_string(int(a | b << 7) - 8192);
    case 0xa0: case 0xd0: return "PRESS";
    default: return {};
    }
}

// ---------------------------------------------------------------- settings

void Midi::write_settings(std::ostream& out) const {
    std::ostringstream keys;
    melodee::write_midi_config(keys, config);
    // Melodee writes key=value; Move.settings has key value.
    std::string text = keys.str();
    std::replace(text.begin(), text.end(), '=', ' ');
    out << text;
}

bool Midi::read_setting(const std::string& key, int value) {
    if (!melodee::read_midi_config(config, key, value)) return false;
    config.port = melodee::MidiPort::Usb; // one port: whatever an MPC's file said
    config.cv_out = false;                 // no sync jack
    return true;
}

std::string Midi::section_text() const {
    std::ostringstream out;
    out << "melodee-move midi 1\n";
    for (unsigned t = 0; t < track_count; ++t)
        if (track_out[t]) out << "out " << t << ' ' << int(track_out[t]) << '\n';
    return out.str();
}

void Midi::read_section(const std::string& text) {
    track_out.fill(0);
    std::istringstream in(text);
    std::string word;
    unsigned track = 0, channel = 0;
    while (in >> word)
        if (word == "out" && in >> track >> channel && track < track_count) track_out[track] = uint8_t(std::min(channel, 16u));
}

namespace {
constexpr char section_tag[4] {'M', 'V', 'M', 'O'};
constexpr char project_magic[8] {'M', 'E', 'L', 'O', 'D', 'E', 'E', '1'};
} // namespace

void Midi::append_section(std::vector<uint8_t>& bytes) const {
    auto text = section_text();
    bytes.insert(bytes.end(), section_tag, section_tag + 4);
    auto size = uint32_t(text.size());
    for (int k = 0; k < 4; ++k) bytes.push_back(uint8_t(size >> (8 * k)));
    bytes.insert(bytes.end(), text.begin(), text.end());
}

void Midi::read_section_of(const std::string& project_path) {
    track_out.fill(0);
    std::ifstream in(project_path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 8 || !std::equal(project_magic, project_magic + 8, bytes.begin())) return;
    for (size_t at = 8; at + 8 <= bytes.size();) {
        uint32_t size = 0;
        for (int k = 0; k < 4; ++k) size |= uint32_t(bytes[at + 4 + size_t(k)]) << (8 * k);
        if (at + 8 + size > bytes.size()) break;
        if (std::equal(section_tag, section_tag + 4, bytes.begin() + long(at)))
            read_section(std::string(bytes.begin() + long(at + 8), bytes.begin() + long(at + 8 + size)));
        at += 8 + size;
    }
}

// ---------------------------------------------------------------- in (MIDI thread)

void Midi::receive(Studio& studio, const move::ExternalMidi& m) {
    if (!m.size) return;
    const uint8_t status = m.bytes[0];
    if (status == 0xf0 || status < 0x80 || sysex_open) { sysex_in(studio, m); return; }
    in_at.store(now_ns(), std::memory_order_relaxed);
    ins.fetch_add(1, std::memory_order_relaxed);
    const uint64_t ns = m.timestamp_ns;
    const bool channel_message = status < 0xf0;
    const unsigned channel = (status & 0x0f) + 1u, type = status & 0xf0u;
    const uint8_t a = m.size > 1 ? m.bytes[1] : 0, b = m.size > 2 ? m.bytes[2] : 0;
    auto& project = studio.project();

    std::array<uint8_t, track_count> own {};
    uint16_t heard = 0; // the Move has one port: every track with a channel of its own hears it
    for (unsigned t = 0; t < track_count; ++t) {
        own[t] = project.tracks[t].midi_channel;
        if (own[t]) heard = uint16_t(heard | 1u << t);
    }
    // Remote CCs: heard whatever the channel says, never passed on.
    if (channel_message && type == 0xb0) {
        auto job = melodee::remote_cc(config, channel, a, b);
        using K = melodee::RemoteJob::Kind;
        if (job.kind != K::None) {
            if (job.kind == K::Track) { studio.select(job.index); studio.say("TRACK " + std::to_string(job.index + 1)); }
            else if (job.kind == K::Pattern) launch(studio, studio.track(), job.index);
            else if (job.kind == K::Scene) {
                for (unsigned t = 0; t < track_count; ++t) if (project.tracks[t].used()) launch(studio, t, job.index);
                studio.say("SEQUENCE " + std::to_string(job.index + 1));
            }
            return;
        }
    }
    const uint16_t tracks = channel_message ? melodee::note_tracks(config, own, heard, studio.track(), channel, true) : 0;
    // A MIDI track that plays what came in sends it out again on its own
    // channel, unless that is the channel it came on (a keyboard playing its
    // own synth: the instrument wants Local Off). Thru then skips it.
    uint16_t echo = 0;
    if (channel_message && type != 0xc0)
        for (unsigned t = 0; t < track_count; ++t) {
            const auto& track = project.tracks[t];
            if (!((tracks >> t) & 1) || track.engine != EngineKind::Midi) continue;
            if (int(channel) == track.params[mt::Channel]) echo_guarded.fetch_add(1, std::memory_order_relaxed);
            else echo = uint16_t(echo | 1u << t);
        }
    if (config.thru && !echo && thru.push(m)) thru_sent.fetch_add(1, std::memory_order_relaxed);

    // MIDI clock in.
    const bool clock_in = config.sync_in == melodee::MidiSync::Clock;
    if (status == 0xf8) {
        if (!clock_in) return;
        pulses_in.fetch_add(1, std::memory_order_relaxed);
        last_pulse_ns = ns;
        follower.pulse(ns);
        unsigned tempo = follower.tempo_tenths();
        if (tempo && tempo != tempo_sent) { studio.send({Type::ExtSync, 0, 0, 2, int16_t(tempo)}); tempo_sent = tempo; tempo_seen.store(tempo); }
        if (ext_running.load(std::memory_order_relaxed)) {
            // The sequencer goes through a block after the pulse came, at that frame: steady, not block by block.
            const auto& clock = studio.host().audio_clock();
            Command pulse {Type::ExtSync, 0, 0, 1};
            pulse.position = clock.known() ? uint32_t(clock.frame_at(int64_t(ns)) + block_frames) : 0;
            studio.send(pulse);
        }
        return;
    }
    if (status == 0xf2) { // Song Position: in sixteenths
        if (!clock_in || m.size < 3) return;
        song_position = uint32_t(a | b << 7) * melodee::sixteenth_ticks;
        return;
    }
    if (status == 0xfa || status == 0xfb) { // Start, Continue
        if (!clock_in) return;
        if (studio.playing()) studio.send({Type::Stop});
        ext_running.store(true, std::memory_order_relaxed);
        Command play {Type::Play};
        if (status == 0xfb) play.position = uint32_t(song_position); // the ticks, as the transport counts them
        studio.send(play);
        studio.send({Type::ExtSync, 0, 0, 3}); // the ticks wait for the clock's pulses
        return;
    }
    if (status == 0xfc) { // Stop
        if (!clock_in) return;
        ext_running.store(false, std::memory_order_relaxed);
        studio.send({Type::ExtSync, 0, 0, 4});
        if (studio.playing()) studio.send({Type::Stop});
        return;
    }
    if (status == 0xf1) { // MTC quarter frame
        mtc_in.fetch_add(1, std::memory_order_relaxed);
        if (config.sync_in == melodee::MidiSync::Mtc && m.size >= 2) follow_mtc(studio, mtc.quarter(a, ns));
        return;
    }
    if (!channel_message) return;

    if (type == 0xc0) { // program change: that pattern on every track
        if (!config.program_change || (config.in_channel && config.in_channel != channel)) return;
        if (a >= melodee::pattern_count) return;
        for (unsigned t = 0; t < track_count; ++t) if (project.tracks[t].used()) launch(studio, t, a);
        studio.say("PROGRAM CHANGE P" + std::to_string(a + 1));
        return;
    }
    if (type == 0x80 || (type == 0x90 && b == 0)) { // a note off ends the note wherever it is held
        for (unsigned t = 0; t < track_count; ++t)
            if (held[t][a]) note_in(studio, t, a, 0, false, ns, (echo >> t) & 1);
        return;
    }
    if (type == 0x90) {
        if (!config.pad_in) return;
        for (unsigned t = 0; t < track_count; ++t) {
            if (!((tracks >> t) & 1)) continue;
            const auto& track = project.tracks[t];
            int key = melodee::note_key(track, a, melodee::levels_key(track, 60, false) >= 0);
            if (key >= 0) note_in(studio, t, key, b, true, ns, (echo >> t) & 1);
        }
        return;
    }
    // What the engine does not play goes out on each MIDI track's channel.
    if (echo && (type == 0xe0 || (type == 0xb0 && a != 1) || type == 0xa0 || type == 0xd0)) echo_raw(studio, m.bytes.data(), m.size, echo, ns);
    if (type == 0xe0) { // the pitch wheel, whole, on the plug-in tracks and kits it plays
        int value = int(a | b << 7) - 8192;
        for (unsigned t = 0; t < track_count; ++t) {
            auto& track = project.tracks[t];
            if (!((tracks >> t) & 1) || (track.engine != EngineKind::Plugin && !track.sampled())) continue;
            track.bend = int8_t(std::clamp(value / 128, -64, 63));
            studio.send({Type::Bend, uint8_t(t), 0, 0, int16_t(value)});
        }
        return;
    }
    if (type == 0xa0 || type == 0xd0) {
        if (!config.pad_in) return;
        for (unsigned t = 0; t < track_count; ++t) {
            if (!((tracks >> t) & 1) || ((echo >> t) & 1)) continue;
            if (type == 0xa0) {
                int key = melodee::note_key(project.tracks[t], a);
                if (key >= 0) studio.send({Type::Pressure, uint8_t(t), 0, uint16_t(key), int16_t(b)});
            } else {
                for (int k = 0; k < 128; ++k)
                    if (held[t][size_t(k)]) studio.send({Type::Pressure, uint8_t(t), 0, uint16_t(played[t][size_t(k)]), int16_t(a)});
            }
        }
        return;
    }
    if (type == 0xb0) {
        for (unsigned t = 0; t < track_count; ++t) {
            if (!((tracks >> t) & 1)) continue;
            auto& track = project.tracks[t];
            if (a == 1) { // the mod wheel
                track.wheel = b;
                studio.send({Type::Wheel, uint8_t(t), 0, 0, int16_t(b * 1000 / 127)});
            } else if (a == 120 || a == 123) { // All Sound Off, All Notes Off
                for (int k = 0; k < 128; ++k) if (held[t][size_t(k)]) note_in(studio, t, k, 0, false, ns, (echo >> t) & 1);
            } else if (track.engine == EngineKind::Plugin || track.sampled()) { // the sustain pedal and the rest, as they came
                studio.send({Type::Control, uint8_t(t), 0, uint16_t(a), int16_t(b)});
            }
        }
    }
}

// A key MIDI in plays on a track: to Note Repeat or the arp when on, else
// straight to the engine, at the frame it came in at. A MIDI track echoing
// it sends it out again; any other track keeps it to itself (pattern 1).
void Midi::note_in(Studio& studio, unsigned t, int key, int velocity, bool down, uint64_t ns, bool echo) {
    t %= track_count;
    if (key < 0 || key > 127) return;
    if (down) held[t][size_t(key)] = true;
    else if (held[t][size_t(key)]) held[t][size_t(key)] = false;
    else return;
    if (down) played[t][size_t(key)] = int16_t(key);
    int plays = played[t][size_t(key)] >= 0 ? played[t][size_t(key)] : key;
    const auto& track = studio.project().tracks[t];
    Command c {down ? Type::NoteOn : Type::NoteOff, uint8_t(t), uint8_t(echo ? 0 : 1), uint16_t(plays), int16_t(down ? std::max(velocity, 1) : 0)};
    if (track.repeat) { c.type = Type::RepeatKey; c.pattern = 0; c.value = int16_t(down ? velocity : 0); }
    else if (track.arp) { c.type = Type::ArpKey; c.pattern = 0; c.value = int16_t(down ? velocity : 0); if (!down && track.arp_latch) return; }
    const auto& clock = studio.host().audio_clock();
    if (ns && clock.known() && (c.type == Type::NoteOn || c.type == Type::NoteOff)) { // to the frame it came at, a block on
        c.timed = true;
        c.at = uint32_t(clock.frame_at(int64_t(ns)) + block_frames);
    }
    studio.send(c);
    if (echo) echoed.fetch_add(1, std::memory_order_relaxed);
}

void Midi::release_all(Studio& studio) {
    for (unsigned t = 0; t < track_count; ++t)
        for (int k = 0; k < 128; ++k)
            if (held[t][size_t(k)]) note_in(studio, t, k, 0, false, 0, studio.project().tracks[t].engine == EngineKind::Midi);
}

void Midi::launch(Studio& studio, unsigned t, unsigned pattern) {
    auto& track = studio.project().tracks[t % track_count];
    track.pattern = uint8_t(pattern % melodee::pattern_count);
    studio.send({Type::Pattern, uint8_t(t % track_count), 0, 0, int16_t(track.pattern)});
    studio.changed();
}

// A CC, bend or pressure for MIDI tracks: a CC a slot sends turns that slot,
// the bend turns BEND, the rest goes out as it came.
void Midi::echo_raw(Studio& studio, const uint8_t* bytes, unsigned size, uint16_t tracks, uint64_t ns) {
    if (size < 2) return;
    unsigned type = bytes[0] & 0xf0, a = bytes[1], b = size > 2 ? bytes[2] : 0;
    for (unsigned t = 0; t < track_count; ++t) {
        if (!((tracks >> t) & 1)) continue;
        auto& track = studio.project().tracks[t];
        if (type == 0xb0) {
            int slot = -1;
            for (int k = 0; k < mt::slots && slot < 0; ++k) {
                int address = track.params[size_t(mt::slot_address(k))];
                if (mt::slot_kind(address) == mt::SlotKind::Cc && mt::slot_number(address) == int(a)) slot = k;
            }
            if (slot >= 0) {
                int id = mt::slot_value(slot);
                track.params[size_t(id)] = int16_t(b);
                studio.send({Type::Param, uint8_t(t), 0, uint16_t(id), int16_t(b)});
                echoed.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
        }
        if (type == 0xe0) {
            int value = int((a | b << 7) >> 7) - 64;
            track.params[mt::Bend] = int16_t(value);
            studio.send({Type::Param, uint8_t(t), 0, uint16_t(mt::Bend), int16_t(value)});
            echoed.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        Command e {Type::MidiEcho, uint8_t(t), 0, uint16_t(type), int16_t(b)};
        e.position = a;
        const auto& clock = studio.host().audio_clock();
        if (ns && clock.known()) { e.timed = true; e.at = uint32_t(clock.frame_at(int64_t(ns)) + block_frames); }
        studio.send(e);
        echoed.fetch_add(1, std::memory_order_relaxed);
    }
}

// A SysEx coming in, in pieces: an MTC full frame (where the master stands)
// is read; any other is counted and let go.
void Midi::sysex_in(Studio& studio, const move::ExternalMidi& m) {
    for (unsigned n = 0; n < m.size; ++n) {
        uint8_t byte = m.bytes[n];
        if (byte == 0xf0) { sysex_open = true; sysex_size = 0; }
        if (!sysex_open) continue;
        if (sysex_size < sysex_buffer.size()) sysex_buffer[sysex_size] = byte;
        ++sysex_size;
        if (byte != 0xf7) continue;
        sysex_open = false;
        in_at.store(now_ns(), std::memory_order_relaxed);
        ins.fetch_add(1, std::memory_order_relaxed);
        const auto& s = sysex_buffer;
        if (sysex_size == 10 && s[1] == 0x7f && s[3] == 0x01 && s[4] == 0x01) { // F0 7F dev 01 01 hr mn sc fr F7
            mtc_in.fetch_add(1, std::memory_order_relaxed);
            if (config.sync_in == melodee::MidiSync::Mtc)
                follow_mtc(studio, mtc.full(s[5] & 0x1f, s[6], s[7], s[8], melodee::MtcRate((s[5] >> 5) & 3), m.timestamp_ns));
        } else {
            sysex_dropped_in.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void Midi::send_mtc(Studio& studio, uint16_t what, const melodee::MtcReader::Event& e) {
    Command c {Type::MtcIn, 0, 0, what};
    c.position = uint32_t(studio.host().audio_clock().heard_frame(int64_t(e.ns)));
    c.micros = uint32_t(std::clamp(e.seconds, 0.0, 4000.0) * 1e6);
    studio.send(c);
}

void Midi::follow_mtc(Studio& studio, const melodee::MtcReader::Event& e) {
    using K = melodee::MtcReader::Event::Kind;
    switch (e.kind) {
    case K::None: case K::Cue: break; // a cue while stopped: the playhead stays (no Song locate on the Move yet)
    case K::Start:
    case K::Jump:
        if (mtc_held) break;
        if (!studio.playing() && !mtc_playing) studio.send({Type::Play});
        if (!mtc_playing) mtc_taken = false;
        mtc_playing = true;
        send_mtc(studio, 1, e);
        break;
    case K::Time:
        if (mtc_playing) send_mtc(studio, 2, e);
        break;
    case K::Stop:
        if (mtc_playing && studio.playing()) studio.send({Type::Stop});
        mtc_playing = mtc_taken = mtc_held = false;
        break;
    }
    mtc_leads.store(mtc_playing && !mtc_held, std::memory_order_relaxed);
}

void Midi::panic(Studio& studio) {
    release_all(studio);
    studio.send({Type::AllNotesOff, 255}); // the engines, and the notes the engine host owes an off
    panics_asked.fetch_add(1, std::memory_order_release);
}

void Midi::configure(Studio& studio, bool force) {
    bool changed = force || !configured || config != config_sent;
    if (changed) {
        studio.send({Type::MidiSync, 0, 0, 0, int16_t(config.sync_out == melodee::MidiSync::Clock)});
        studio.send({Type::MidiSync, 0, 0, 1, int16_t(config.sync_out == melodee::MidiSync::Mtc)});
        studio.send({Type::MidiSync, 0, 0, 2, 0}); // no sync jack
        bool follow = config.sync_in == melodee::MidiSync::Clock;
        if (!configured || follow != (config_sent.sync_in == melodee::MidiSync::Clock)) {
            studio.send({Type::ExtSync, 0, 0, 0, int16_t(follow)});
            if (!follow) { ext_running.store(false); tempo_sent = 0; tempo_seen.store(0); follower.reset(); }
        }
        if (config.sync_in != melodee::MidiSync::Mtc && configured && config_sent.sync_in == melodee::MidiSync::Mtc) {
            mtc.reset();
            if (mtc_playing) studio.send({Type::MtcIn, 0, 0, 0});
            mtc_playing = mtc_taken = mtc_held = false;
            mtc_leads.store(false);
        }
        config_sent = config;
    }
    const auto& project = studio.project();
    for (unsigned t = 0; t < track_count; ++t) {
        auto channel = uint8_t(out_channel(studio, t));
        auto mode = uint8_t(config.pad_out);
        if (!(configured && !force && channel == channel_sent[t] && mode == mode_sent[t])) {
            studio.send({Type::MidiTrack, uint8_t(t), 0, channel, int16_t(mode)});
            channel_sent[t] = channel; mode_sent[t] = mode;
        }
        // A MIDI track's AUDIO: the Move's input (L, R, both) as its sound.
        const auto& track = project.tracks[t];
        auto audio = uint8_t(track.engine == EngineKind::Midi ? std::clamp<int>(track.params[mt::AudioIn], 0, mt::AudioCount - 1) : 0);
        if (!configured || force || audio != audio_sent[t]) {
            studio.send({Type::MidiAudio, uint8_t(t), 0, 0, int16_t(audio)});
            audio_sent[t] = audio;
        }
    }
    configured = true;
}

void Midi::pump(Studio& studio, Clock::time_point now) {
    configure(studio);
    if (config.sync_in == melodee::MidiSync::Mtc) {
        follow_mtc(studio, mtc.idle(uint64_t(ns_of(now))));
        if (mtc_playing && studio.host().mtc_following()) mtc_taken = true;
        else if (mtc_playing && mtc_taken && !studio.host().mtc_following()) { // stopped here while the master runs on
            mtc_playing = mtc_taken = false;
            mtc_held = mtc.running() || mtc.settling();
            mtc_leads.store(false, std::memory_order_relaxed);
        }
    }
    // A clock that stopped coming: Start or Continue again finds it.
    if (ext_running.load(std::memory_order_relaxed) && last_pulse_ns && ns_of(now) - int64_t(last_pulse_ns) > 2000000000) {
        follower.reset();
        last_pulse_ns = 0;
    }
}

// ---------------------------------------------------------------- SysEx out

std::string Midi::sysex_folder(const Studio& studio) const { return (fs::path(studio.home_folder()) / "SysEx").string(); }

std::vector<std::string> Midi::sysex_files(const Studio& studio) const {
    std::vector<std::string> files;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(sysex_folder(studio), error)) {
        auto extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (extension == ".syx" && entry.is_regular_file(error)) files.push_back(entry.path().filename().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

void Midi::send_sysex(Studio& studio, unsigned track) {
    const auto& t = studio.project().tracks[track % track_count];
    if (t.engine != EngineKind::Midi || !t.midi.sysex[0]) { studio.say("NO SYSEX FILE"); return; }
    sysex_asked.push_back(track % track_count);
    sysex_progress[track % track_count].store(1, std::memory_order_relaxed);
}

int Midi::sysex_percent(unsigned track) const { return sysex_progress[track % track_count].load(std::memory_order_relaxed) - 1; }

std::string Midi::command(Studio& studio, const std::string& arguments) {
    std::istringstream in(arguments);
    std::string first;
    in >> first;
    if (first == "panic") { panic(studio); studio.say("MIDI PANIC"); return "panic"; }
    // Hex bytes: a message, or a SysEx in pieces of up to three as the port brings them.
    std::vector<uint8_t> message;
    for (std::string word = first; !word.empty() && message.size() < 512; word.clear(), in >> word) {
        char* end = nullptr;
        unsigned long byte = std::strtoul(word.c_str(), &end, 16);
        if (*end || byte > 0xff) return "usage: melodee-midi HEX... | panic";
        message.push_back(uint8_t(byte));
    }
    if (message.empty() || !(message[0] & 0x80)) return "usage: melodee-midi HEX... | panic";
    bool sysex = message[0] == 0xf0;
    for (size_t at = 0; at < message.size();) {
        move::ExternalMidi m;
        m.timestamp_ns = uint64_t(now_ns());
        size_t take = std::min<size_t>(3, message.size() - at);
        for (size_t k = 0; k < take; ++k) m.bytes[m.size++] = message[at + k];
        at += sysex ? take : message.size(); // a message: its first three bytes
        receive(studio, m);
    }
    return "in=" + std::to_string(ins.load());
}

void Midi::service(Studio& studio) {
    SysexJob done;
    while (sysex_done.pop(done)) { delete done.bytes; sysex_sent.fetch_add(1, std::memory_order_relaxed); }
    std::vector<std::pair<unsigned, std::string>> asked;
    {
        std::lock_guard<std::mutex> hold(studio.lock);
        for (unsigned t : sysex_asked) asked.push_back({t, (fs::path(sysex_folder(studio)) / studio.project().tracks[t].midi.sysex).string()});
        sysex_asked.clear();
    }
    for (const auto& [track, path] : asked) {
        std::ifstream in(path, std::ios::binary);
        auto* bytes = new std::vector<uint8_t>();
        if (in) {
            in.seekg(0, std::ios::end);
            auto size = std::min<size_t>(size_t(std::max<std::streamoff>(in.tellg(), 0)), sysex_most);
            in.seekg(0);
            bytes->resize(size);
            in.read(reinterpret_cast<char*>(bytes->data()), std::streamsize(size));
        }
        if (bytes->empty() || !sysex_jobs.push({bytes, uint8_t(track)})) {
            delete bytes;
            sysex_progress[track].store(0, std::memory_order_relaxed);
            std::lock_guard<std::mutex> hold(studio.lock);
            studio.say("SYSEX: CANNOT READ");
        }
    }
}

// ---------------------------------------------------------------- out (audio thread)

void Midi::put(uint8_t a, int b, int c) {
    if (!wire.push(message(a, b, c))) dropped.fetch_add(1, std::memory_order_relaxed);
}

void Midi::put_realtime(uint8_t status) {
    if (!realtime.push(message(status, -1, -1))) dropped.fetch_add(1, std::memory_order_relaxed);
}

void Midi::put_sysex(const uint8_t* bytes, size_t size) {
    for (size_t at = 0; at < size;) {
        move::ExternalMidi piece;
        while (at < size && piece.size < 3) {
            piece.bytes[piece.size++] = bytes[at];
            if (bytes[at++] == 0xf7) break;
        }
        if (!wire.push(piece)) { dropped.fetch_add(1, std::memory_order_relaxed); return; }
    }
}

void Midi::do_panic() {
    pending.keep_if([](const MidiOutEvent& e) { return e.kind != MidiOutEvent::Kind::NoteOn; });
    for (uint8_t channel = 0; channel < 16; ++channel) {
        put(uint8_t(0xb0 | channel), 120, 0);
        put(uint8_t(0xb0 | channel), 123, 0);
        put(uint8_t(0xe0 | channel), 0, 64);
    }
    panics.fetch_add(1, std::memory_order_relaxed);
}

void Midi::deliver(const MidiOutEvent& e) {
    using K = MidiOutEvent::Kind;
    uint8_t ch = uint8_t((std::clamp<int>(e.channel, 1, 16) - 1) & 0x0f);
    auto note = [&](uint8_t status, int a, int b) {
        put(status, a, b);
        if (e.track < track_count) last_sent[e.track].store(uint32_t(status) << 16 | uint32_t(a & 0x7f) << 8 | uint32_t(b & 0x7f), std::memory_order_relaxed);
    };
    switch (e.kind) {
    case K::NoteOn: note(uint8_t(0x90 | ch), e.a, e.b); break;
    case K::NoteOff: note(uint8_t(0x80 | ch), e.a, 0); break;
    case K::Control: note(uint8_t(0xb0 | ch), e.a, e.b); break;
    case K::Program: note(uint8_t(0xc0 | ch), e.a, -1); break;
    case K::Cc14:
        note(uint8_t(0xb0 | ch), e.a, int(e.value >> 7) & 127);
        note(uint8_t(0xb0 | ch), e.a + 32, int(e.value) & 127);
        break;
    case K::Nrpn: {
        unsigned number = e.value >> 14, data = e.value & 0x3fff;
        note(uint8_t(0xb0 | ch), 99, int(number >> 7) & 127);
        note(uint8_t(0xb0 | ch), 98, int(number) & 127);
        note(uint8_t(0xb0 | ch), 6, int(data >> 7) & 127);
        note(uint8_t(0xb0 | ch), 38, int(data) & 127);
        break;
    }
    case K::PitchBend: note(uint8_t(0xe0 | ch), int(e.value) & 127, int(e.value >> 7) & 127); break;
    case K::ChannelPressure: note(uint8_t(0xd0 | ch), e.b, -1); break;
    case K::PolyPressure: note(uint8_t(0xa0 | ch), e.a, e.b); break;
    case K::Clock: put_realtime(0xf8); break;
    case K::Start: put_realtime(0xfa); break;
    case K::Stop: put_realtime(0xfc); break;
    case K::Continue:
        put(0xf2, int(e.value) & 127, int(e.value >> 7) & 127);
        put_realtime(0xfb);
        break;
    case K::MtcQuarter: put(0xf1, (e.a & 7) << 4 | (e.b & 15)); break;
    case K::MtcFull: {
        unsigned f = e.value % 25, s = e.value / 25 % 60, m = e.value / 1500 % 60, h = e.value / 90000 % 24;
        const uint8_t full[10] {0xf0, 0x7f, 0x7f, 0x01, 0x01, uint8_t(h | 1 << 5), uint8_t(m), uint8_t(s), uint8_t(f), 0xf7}; // 25 fps
        put_sysex(full, sizeof full);
        break;
    }
    case K::CvHigh: case K::CvLow: break; // no sync jack on the Move
    }
}

void Midi::after_render(melodee::EngineHost& host) {
    // Every render is one block; the engine counts its frames the same way
    // (a block parked or rendered silent counts too).
    frames_end = host.blocks_rendered() * block_frames;
    unsigned asked = panics_asked.load(std::memory_order_acquire);
    if (asked != panics_done) { panics_done = asked; do_panic(); }
    MidiOutEvent e;
    while (host.take_midi(e)) {
        // Played by hand: now. The rest when its frame is heard: the block it falls in.
        int64_t due = e.live ? 0 : int64_t(e.frame);
        if (e.live && (e.kind == MidiOutEvent::Kind::NoteOn || e.kind == MidiOutEvent::Kind::NoteOff))
            superseded.fetch_add(pending.supersede(e, due), std::memory_order_relaxed);
        if (pending.full()) { deliver(e); continue; }
        pending.add(e, due);
    }
    unsigned sent = 0;
    while (sent < pending.size() && pending[sent].due < int64_t(frames_end)) deliver(pending[sent++].event);
    pending.drop_front(sent);
    move::ExternalMidi m;
    while (thru.pop(m)) if (!wire.push(m)) dropped.fetch_add(1, std::memory_order_relaxed);
    while (module_out.pop(m)) if (!wire.push(m)) dropped.fetch_add(1, std::memory_order_relaxed);
    if (!sending.bytes && sysex_jobs.pop(sending)) { sending_at = 0; sending_open = false; }
}

unsigned Midi::take(move::ExternalMidi* out, unsigned room) {
    unsigned n = 0;
    while (n < room && realtime.count) { out[n++] = realtime.front(); realtime.pop(); }
    if (sending.bytes) { // a SysEx going out: paced pieces, the rest waits behind it
        const auto& bytes = *sending.bytes;
        for (unsigned pieces = 0; pieces < sysex_pieces_a_block && n < room && sending_at < bytes.size();) {
            // From F0 to F7, in pieces of three; whatever lies between messages is left out.
            if (!sending_open) {
                if (bytes[sending_at++] != 0xf0) continue;
                sending_open = true;
                --sending_at;
            }
            move::ExternalMidi piece;
            while (sending_at < bytes.size() && piece.size < 3) {
                piece.bytes[piece.size++] = bytes[sending_at];
                if (bytes[sending_at++] == 0xf7) { sending_open = false; break; }
            }
            if (piece.size == 3 || !sending_open) { out[n++] = piece; ++pieces; } // a last piece cut short by the file's end is left out
            sysex_bytes.fetch_add(piece.size, std::memory_order_relaxed);
        }
        sysex_progress[sending.track % track_count].store(int(1 + std::max<size_t>(1, sending_at * 100 / std::max<size_t>(bytes.size(), 1))),
                                                         std::memory_order_relaxed);
        if (sending_at >= bytes.size()) {
            sysex_progress[sending.track % track_count].store(0, std::memory_order_relaxed);
            if (sysex_done.push(sending)) { sending = {}; sending_open = false; } // else handed back next time
        }
    } else {
        while (n < room && wire.count) { out[n++] = wire.front(); wire.pop(); }
    }
    if (n) { out_at.store(now_ns(), std::memory_order_relaxed); outs.fetch_add(n, std::memory_order_relaxed); }
    return n;
}

// ---------------------------------------------------------------- a module's MIDI out

namespace { thread_local bool audio_thread_mark = false; }

void Midi::audio_thread() { audio_thread_mark = true; }

int Midi::module_send(const uint8_t* packets, int length) {
    int sent = 0;
    for (int at = 0; at + 4 <= length; at += 4) {
        const uint8_t* p = packets + at;
        unsigned code = p[0] & 0x0f;
        static constexpr uint8_t sizes[16] {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
        unsigned size = sizes[code];
        if (!size) continue;
        move::ExternalMidi m;
        m.size = uint8_t(size);
        for (unsigned k = 0; k < size; ++k) m.bytes[k] = p[1 + k];
        bool ok;
        if (audio_thread_mark) ok = wire.push(m);
        else { std::lock_guard<std::mutex> hold(module_lock); ok = module_out.push(m); }
        if (ok) { ++sent; module_msgs.fetch_add(1, std::memory_order_relaxed); }
        else dropped.fetch_add(1, std::memory_order_relaxed);
    }
    return sent;
}

std::string Midi::status() const {
    return " melodee_midi_in=" + std::to_string(ins.load()) + " melodee_midi_out=" + std::to_string(outs.load()) + " melodee_midi_dropped=" + std::to_string(dropped.load()) +
           " melodee_midi_clock_in=" + std::to_string(pulses_in.load()) + " melodee_midi_ext=" + std::to_string(int(ext_running.load())) +
           " melodee_midi_ext_tempo=" + std::to_string(tempo_seen.load()) + " melodee_midi_mtc_in=" + std::to_string(mtc_in.load()) +
           " melodee_midi_mtc_leads=" + std::to_string(int(mtc_leads.load())) + " melodee_midi_echo=" + std::to_string(echoed.load()) + "/" +
           std::to_string(echo_guarded.load()) + " melodee_midi_thru=" + std::to_string(thru_sent.load()) + " melodee_midi_sysex=" +
           std::to_string(sysex_sent.load()) + "/" + std::to_string(sysex_bytes.load()) + " melodee_midi_sysex_in_dropped=" +
           std::to_string(sysex_dropped_in.load()) + " melodee_midi_panics=" + std::to_string(panics.load()) + " melodee_midi_module=" +
           std::to_string(module_msgs.load()) + " melodee_midi_superseded=" + std::to_string(superseded.load());
}

} // namespace app
