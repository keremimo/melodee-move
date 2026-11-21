// SPDX-License-Identifier: AGPL-3.0-only
// The Live Set export (live_export.h). Live reads a set's XML leniently: an
// element left out takes its default, so only what Melodee sets is written
// (an empty set is a few hundred bytes), in the order Live writes its own.
// What Live does not forgive, found against Live 12.4: an element it does not
// know where it stands ("Unknown class"), a track whose clip slots are not one
// a scene (return tracks have none), a sample part not marked active (the
// Simpler comes up empty), a path relative to the project folder without
// Live's own "Ableton Project Info" beside it (so paths here are relative to
// the set, RelativePathType 1).
#include "app/live_export.h"
#include "melodee/engine/engine_info.h"
#include "melodee/engine/midi_engine.h"
#include "melodee/engine/sfz.h"
#include "melodee/model/scales.h"
#include "melodee/shell/sample_library.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace app {
namespace fs = std::filesystem;
using melodee::EngineKind;
using melodee::Event;
using melodee::PadSound;
using melodee::Pattern;
using melodee::Project;
using melodee::Track;

int live_time_signature(unsigned beats, unsigned unit) {
    int log = 0;
    while ((2u << log) <= std::max(unit, 1u)) ++log;
    return int(std::clamp(beats, 1u, 99u)) - 1 + 99 * log;
}

namespace {

constexpr double floor_gain = 0.0003162277571, top_gain = 1.99526238; // Live's faders and sends: -70 dB .. +6 dB
constexpr double before_all = -63072000;                              // an automation's value before its first point
constexpr int played_root = 60;                                        // an instrument pad's key with no pitch
constexpr int tempo_target = 100;                                      // the tempo's automation target (a pointee id)

std::string num(double v) {
    if (!std::isfinite(v)) v = 0;
    char text[32];
    std::snprintf(text, sizeof text, "%.10g", v);
    return text;
}

std::string escaped(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c < 0x20 ? ' ' : char(c);
        }
    }
    return out;
}

std::string attr(const char* name, const std::string& v) { return std::string(" ") + name + "=\"" + escaped(v) + "\""; }
std::string attr(const char* name, double v) { return attr(name, num(v)); }

// Live's XML as it is written: each value an element of its own, with a Value.
struct Xml {
    std::string text;
    Xml& open(const char* tag, const std::string& attributes = {}) { text += '<'; text += tag; text += attributes; text += '>'; return *this; }
    Xml& close(const char* tag) { text += "</"; text += tag; text += '>'; return *this; }
    Xml& empty(const char* tag) { text += '<'; text += tag; text += "/>"; return *this; }
    Xml& value(const char* tag, const std::string& v) { text += '<'; text += tag; text += attr("Value", v); text += "/>"; return *this; }
    Xml& number(const char* tag, double v) { return value(tag, num(v)); }
    Xml& flag(const char* tag, bool v) { return value(tag, v ? "true" : "false"); }
    Xml& manual(const char* tag, double v) { open(tag); number("Manual", v); return close(tag); } // a parameter
    Xml& manual_flag(const char* tag, bool v) { open(tag); flag("Manual", v); return close(tag); }
    Xml& range(const char* tag, int low, int high) {
        open(tag).number("Min", low).number("Max", high).number("CrossfadeMin", low).number("CrossfadeMax", high);
        return close(tag);
    }
    Xml& add(const std::string& xml) { text += xml; return *this; }
};

double gain_of_level(int level) { return std::clamp<double>(melodee::volume_gain(level), floor_gain, top_gain); }
double gain_of_db(double db) { return std::clamp(std::pow(10.0, db / 20), floor_gain, top_gain); }
double send_gain(unsigned percent) { return std::clamp(percent / 100.0, floor_gain, 1.0); }
double beats_of(double ticks) { return ticks / melodee::ticks_per_quarter; }

// A tick as the sequencer plays it under the project's swing (EngineHost tick_length()): each pair of sixteenths
// is split swing : 100 - swing.
double swung(double tick, unsigned swing) {
    if (swing == 50) return tick;
    constexpr double half = melodee::sixteenth_ticks, pair = 2.0 * melodee::sixteenth_ticks;
    const double base = std::floor(tick / pair) * pair, at = tick - base, s = std::clamp(swing, 50u, 75u) / 50.0;
    return base + (at < half ? at * s : half * s + (at - half) * (2.0 - s));
}

std::string lower(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::string file_safe(const std::string& name) {
    std::string out;
    for (char c : name) out += std::string("/\\:\"*?<>|").find(c) != std::string::npos || static_cast<unsigned char>(c) < 0x20 ? '-' : c;
    return out.empty() ? std::string("Sample") : out;
}

// ---------------------------------------------------------------- the files the set plays

struct Audio {
    double rate = 0;
    int64_t frames = 0;
};

// Every file the set plays, copied once into Samples/Imported under a name of its own.
class Files {
public:
    explicit Files(const fs::path& project) {
        std::error_code error;
        root = fs::absolute(project, error); // Live's Path is whole; it falls back on RelativePath where it is not
        if (error) root = project;
        formats.registerBasicFormats();
    }
    // The copy's path in the set ("Samples/Imported/<name>") for the file at `source` (named after `stem` when given);
    // empty when it is not there, or not a sound Live reads.
    std::string add(const std::string& source, LiveExport& report, const std::string& stem = {}) {
        if (auto found = names.find(source); found != names.end()) return found->second;
        std::error_code error;
        if (source.empty() || !fs::is_regular_file(source, error)) { ++report.missing; return names[source] = {}; }
        Audio audio;
        if (std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(juce::String::fromUTF8(source.c_str())))); reader) {
            audio.rate = reader->sampleRate;
            audio.frames = reader->lengthInSamples;
        }
        if (audio.frames <= 0 || audio.rate <= 0) { ++report.skipped; return names[source] = {}; }
        const std::string extension = fs::path(source).extension().string();
        const std::string base = file_safe(stem.empty() ? fs::path(source).stem().string() : stem);
        std::string file = base + extension;
        for (unsigned n = 2; taken.count(lower(file)); ++n) file = base + " " + std::to_string(n) + extension;
        const fs::path folder = root / "Samples" / "Imported";
        fs::create_directories(folder, error);
        fs::copy_file(source, folder / file, fs::copy_options::overwrite_existing, error);
        if (error) { ++report.skipped; return names[source] = {}; }
        taken.insert(lower(file));
        ++report.samples;
        std::string relative = "Samples/Imported/" + file;
        infos[relative] = audio;
        return names[source] = relative;
    }
    const Audio& info(const std::string& relative) { return infos[relative]; }
    std::string absolute(const std::string& relative) const { return (root / relative).string(); }

private:
    fs::path root;
    juce::AudioFormatManager formats;
    std::map<std::string, std::string> names; // by source
    std::map<std::string, Audio> infos;        // by the copy's path
    std::set<std::string> taken;              // names in lower case
};

void sample_ref(Xml& x, Files& files, const std::string& relative) {
    const Audio& audio = files.info(relative);
    x.open("SampleRef").open("FileRef");
    x.number("RelativePathType", 1).value("RelativePath", relative).value("Path", files.absolute(relative)).number("Type", 2);
    x.close("FileRef").number("DefaultDuration", double(audio.frames)).number("DefaultSampleRate", audio.rate).close("SampleRef");
}

// A Melodee frame (44.1 kHz) as a frame of a file at `rate`.
int64_t file_frame(uint32_t at, double rate) { return std::llround(at * rate / 44100.0); }

// ---------------------------------------------------------------- Simpler and Sampler

// A sample part: a region of a file, the keys and velocities that play it, how it sits over the device.
struct Part {
    std::string file;
    int64_t start = 0, end = 0;   // frames of the file
    int low_key = 0, high_key = 127, low_velocity = 1, high_velocity = 127, root = 60;
    int detune = 0;               // cents, -50..50
    double gain = 1, pan = 0;     // linear; -1..1
    bool loop = false;            // a zone's own loop (Sampler), loop_start..loop_end
    int64_t loop_start = 0, loop_end = 0;
    double warp_seconds = 0, warp_beats = 0; // warped to the song: so many seconds of the file are so many beats
};

// What the device plays its parts with.
struct Voice {
    double volume_db = 0, pan = 0, velocity = 1;
    int transpose = 0, fine = 0;
    bool one_shot = false, reverse = false, mono = false, loop = false;
    double loop_length = 1; // Simpler's loop: the share of the region looped, back from its end
    double attack_ms = 0.1, decay_ms = 600, sustain = 1, release_ms = 50, fade_in_ms = 0, fade_out_ms = 0.1;
};

void write_part(Xml& x, Files& files, const Part& p, unsigned id) {
    x.open("MultiSamplePart", attr("Id", id) + attr("InitUpdateAreSlicesFromOnsetsEditableAfterRead", "false") +
                                  attr("HasImportedSlicePoints", "false") + attr("NeedsAnalysisData", "false"));
    x.value("Name", fs::path(p.file).stem().string()).flag("Selection", id == 0).flag("IsActive", true).flag("Solo", false);
    x.range("KeyRange", p.low_key, p.high_key).range("VelocityRange", p.low_velocity, p.high_velocity).range("SelectorRange", 0, 127);
    x.number("RootKey", std::clamp(p.root, 0, 127)).number("Detune", std::clamp(p.detune, -50, 50)).number("TuneScale", 100);
    x.number("Panorama", std::clamp(p.pan, -1.0, 1.0)).number("Volume", std::clamp(p.gain, 0.0, 3.981)).flag("Link", false);
    x.number("SampleStart", double(p.start)).number("SampleEnd", double(p.end));
    x.open("SustainLoop").number("Start", double(p.loop ? p.loop_start : p.start)).number("End", double(p.loop ? p.loop_end : p.end));
    x.number("Mode", p.loop ? 1 : 0).number("Crossfade", 0).number("Detune", 0).close("SustainLoop");
    x.open("ReleaseLoop").number("Start", double(p.start)).number("End", double(p.end)).number("Mode", 3).number("Crossfade", 0).number("Detune", 0);
    x.close("ReleaseLoop");
    sample_ref(x, files, p.file);
    x.open("SampleWarpProperties").open("WarpMarkers");
    const bool warped = p.warp_seconds > 0 && p.warp_beats > 0;
    if (warped) {
        x.empty("WarpMarker Id=\"0\" SecTime=\"0\" BeatTime=\"0\"");
        x.add("<WarpMarker" + attr("Id", 1) + attr("SecTime", p.warp_seconds) + attr("BeatTime", p.warp_beats) + "/>");
    }
    x.close("WarpMarkers").number("WarpMode", 0).flag("IsWarped", warped).close("SampleWarpProperties");
    x.close("MultiSamplePart");
}

// A Simpler ("OriginalSimpler") or a Sampler ("MultiSampler") of `parts`; empty when there are none.
std::string sampler_device(const char* tag, const std::vector<Part>& parts, const Voice& v, Files& files) {
    if (parts.empty()) return {};
    Xml x;
    x.open(tag, attr("Id", 0)).open("Player").open("MultiSampleMap").open("SampleParts");
    for (unsigned n = 0; n < parts.size(); ++n) write_part(x, files, parts[n], n);
    x.close("SampleParts").close("MultiSampleMap");
    x.open("LoopModulators").manual("SampleStart", 0).manual("SampleLength", 1).manual_flag("LoopOn", v.loop);
    x.manual("LoopLength", std::clamp(v.loop_length, 0.0, 1.0)).close("LoopModulators");
    x.manual_flag("Reverse", v.reverse).close("Player");
    x.open("Pitch").manual("TransposeKey", std::clamp(v.transpose, -48, 48)).manual("TransposeFine", std::clamp(v.fine, -50, 50)).close("Pitch");
    x.open("VolumeAndPan").manual("Volume", std::clamp(v.volume_db, -36.0, 36.0)).manual("VolumeVelScale", std::clamp(v.velocity, 0.0, 1.0));
    x.manual("Panorama", std::clamp(v.pan, -1.0, 1.0));
    x.open("Envelope").manual("AttackTime", std::clamp(v.attack_ms, 0.1, 20000.0)).manual("DecayTime", std::clamp(v.decay_ms, 1.0, 60000.0));
    x.manual("SustainLevel", std::clamp(v.sustain, floor_gain, 1.0)).manual("ReleaseTime", std::clamp(v.release_ms, 1.0, 60000.0)).close("Envelope");
    x.open("OneShotEnvelope").manual("FadeInTime", std::clamp(v.fade_in_ms, 0.0, 2000.0)).manual("FadeOutTime", std::clamp(v.fade_out_ms, 0.1, 2000.0));
    x.close("OneShotEnvelope").close("VolumeAndPan");
    x.open("Globals");
    if (v.mono) x.number("NumVoices", 0);
    x.number("PlaybackMode", v.one_shot ? 1 : 0).close("Globals");
    x.close(tag);
    return x.text;
}

// A pad's envelope, tune, level and play as the device's (the sampler's own times, engine/sampler.cpp).
Voice voice_of(const PadSound& pad, int transpose) {
    Voice v;
    v.volume_db = melodee::volume_db(pad.level) + pad.gain / 100.0;
    if (!std::isfinite(v.volume_db)) v.volume_db = -36;
    v.pan = pad.pan / 50.0;
    v.transpose = transpose;
    v.fine = pad.fine;
    v.velocity = pad.velocity_sense / 100.0;
    v.one_shot = pad.play == melodee::PadPlay::OneShot;
    v.reverse = pad.reverse;
    v.mono = pad.mono;
    const double decay_s = melodee::time_seconds(pad.decay) + 0.005;
    v.attack_ms = melodee::time_seconds(pad.attack) * 300.0;
    v.decay_ms = pad.decay >= 100 ? 60000 : decay_s * 1000;
    v.sustain = pad.decay >= 100 ? 1.0 : pad.sustain / 100.0;
    v.release_ms = (melodee::time_seconds(pad.release) * 0.5 + 0.003) * 1000;
    v.fade_in_ms = v.attack_ms;
    // A one shot that decays from its end fades over its decay before the end, as Simpler's One-Shot fades out.
    v.fade_out_ms = v.one_shot && !pad.decay_from_start && pad.decay < 100 ? decay_s * 1000 : 0.1;
    return v;
}

// ---------------------------------------------------------------- the tracks the set gets

enum class Lane : uint8_t { Kit, Pad, Keys, Notes, Prints, Takes };

struct Planned {
    unsigned index = 0;   // Melodee's track
    Lane lane = Lane::Notes;
    int pad = -1;         // Lane::Pad: the kit pad it plays
    bool shifted = false; // an instrument's notes carry its pad's pitch
    std::string name, device;
    bool audio = false;   // an audio track (prints, takes)
    bool muted = false;
    int program = -1, bank_msb = -1, bank_lsb = -1; // a MIDI track's, sent by each clip
    double volume = 1, pan = 0;
};

struct Note {
    double time = 0, length = 0, chance = 1;
    int key = 60, velocity = 100;
};

std::string track_name(const Track& track, const char* kind, unsigned index) {
    if (track.name[0]) return track.name;
    return std::string(kind) + " " + std::to_string(index + 1);
}

// A track's colour in Live's palette, nearest the Move's for its place (ui/track_style.h): azure, orange, violet,
// green, yellow, magenta, cyan, red, royal blue, pink, mint, purple, lime, rose, sky, tan.
int colour_of(unsigned index) {
    static const int colours[] {8, 1, 10, 5, 17, 11, 7, 14, 9, 12, 6, 24, 4, 0, 22, 2};
    return colours[index % 16];
}

// The notes a planned track plays of `written` (a Melodee pattern of its track), in beats from the pattern's top.
std::vector<Note> notes_of(const Project& project, const Planned& planned, const Pattern& written, const std::set<int>& moved, double& length_beats) {
    const Track& track = project.tracks[planned.index];
    const Pattern played = melodee::played_pattern(written, track.speed);
    const uint32_t length = std::max<uint32_t>(played.length, 1);
    length_beats = beats_of(length);
    std::vector<Note> out;
    for (const Event& e : played.events) {
        if (e.tick >= length) continue;
        int key = e.key;
        const int pad = int(e.key) - int(melodee::sampler_first_note);
        const bool kit_key = pad >= 0 && pad < int(melodee::sampler_pads);
        switch (planned.lane) {
        case Lane::Kit:
            if (!kit_key || moved.count(pad)) continue;
            key = e.key % 128;
            break;
        case Lane::Pad:
            if (pad != planned.pad) continue;
            key = played_root + e.pitch + (planned.shifted ? track.pads[size_t(pad)].pitch : 0);
            break;
        case Lane::Keys:
            key = e.key + (planned.shifted ? track.pads[0].pitch : 0);
            break;
        default: break;
        }
        // A kit lane of its own length plays again through the pattern.
        const uint32_t lane = std::max<uint32_t>(played.lane_length(e.key), 1);
        const unsigned hits = e.hits();
        for (uint32_t round = e.tick; round < length; round += lane) {
            const double each = double(std::max<uint16_t>(e.length, 1)) / hits;
            for (unsigned n = 0; n < hits; ++n) {
                const double from = round + n * each, to = from + each;
                if (from >= length) break;
                Note note;
                note.time = beats_of(swung(from, project.swing));
                note.length = std::max(beats_of(swung(to, project.swing)) - note.time, 1.0 / 128);
                note.key = key + (planned.lane == Lane::Pad ? e.hit_pitch(n) : 0);
                note.velocity = std::clamp(e.hit_velocity(e.velocity, n), 1, 127);
                note.chance = std::clamp<int>(e.chance, 0, 100) / 100.0;
                if (note.key < 0 || note.key > 127) continue;
                out.push_back(note);
            }
            if (lane >= length) break;
        }
    }
    return out;
}

void write_time_signature(Xml& x, const Project& project) {
    const auto ts = project.time_signature();
    x.open("TimeSignature").open("TimeSignatures").open("RemoteableTimeSignature", attr("Id", 0));
    x.number("Numerator", ts.beats).number("Denominator", ts.unit).number("Time", 0);
    x.close("RemoteableTimeSignature").close("TimeSignatures").close("TimeSignature");
}

// The clip's loop: `start` to `end` in the clip's beats, played over `from` .. `to` (Session View: 0 .. its loop).
void write_loop(Xml& x, double from, double to, double start, double end) {
    x.number("CurrentStart", from).number("CurrentEnd", to);
    x.open("Loop").number("LoopStart", start).number("LoopEnd", end).number("StartRelative", 0).flag("LoopOn", true);
    x.number("OutMarker", end).number("HiddenLoopStart", start).number("HiddenLoopEnd", end).close("Loop");
}

std::string midi_clip(const Project& project, const Planned& planned, const std::vector<Note>& notes, double length, const std::string& name,
                      unsigned id, int colour, double from, double to, bool disabled, int program) {
    Xml x;
    x.open("MidiClip", attr("Id", id) + attr("Time", from));
    write_loop(x, from, to, 0, length);
    x.value("Name", name).number("Color", colour);
    write_time_signature(x, project);
    x.flag("Disabled", disabled);
    std::map<int, std::vector<const Note*>> keys;
    for (const auto& n : notes) keys[n.key].push_back(&n);
    x.open("Notes").open("KeyTracks");
    unsigned key_id = 0, note_id = 1;
    for (const auto& [key, list] : keys) {
        x.open("KeyTrack", attr("Id", key_id++)).open("Notes");
        for (const Note* n : list)
            x.add("<MidiNoteEvent" + attr("Time", n->time) + attr("Duration", n->length) + attr("Velocity", n->velocity) + attr("VelocityDeviation", 0) +
                  attr("OffVelocity", 64) + attr("Probability", n->chance) + attr("IsEnabled", "true") + attr("NoteId", note_id++) + "/>");
        x.close("Notes").number("MidiKey", key).close("KeyTrack");
    }
    x.close("KeyTracks").open("NoteIdGenerator").number("NextId", note_id).close("NoteIdGenerator").close("Notes");
    const bool sends_program = planned.lane == Lane::Notes && program >= 0;
    x.number("BankSelectCoarse", sends_program ? planned.bank_msb : -1).number("BankSelectFine", sends_program ? planned.bank_lsb : -1);
    x.number("ProgramChange", sends_program ? program : -1);
    x.close("MidiClip");
    return x.text;
}

// An audio clip of `relative`, warped so `seconds` of it are `beats`, looping `loop_start` .. `loop_start + length`.
struct AudioPlace {
    std::string file;
    double seconds = 0, beats = 0, loop_start = 0, length = 0, gain = 1;
    int semi = 0, fine = 0;
    bool repitch = false;
};
std::string audio_clip(const Project& project, Files& files, const AudioPlace& a, const std::string& name, unsigned id, int colour, double from,
                       double to, bool disabled) {
    Xml x;
    x.open("AudioClip", attr("Id", id) + attr("Time", from));
    const bool session = to <= 0;
    if (session) { from = a.loop_start; to = a.loop_start + a.length; }
    x.number("CurrentStart", from).number("CurrentEnd", to);
    x.open("Loop").number("LoopStart", a.loop_start).number("LoopEnd", a.loop_start + a.length).number("StartRelative", 0).flag("LoopOn", true);
    x.number("OutMarker", a.loop_start + a.length).number("HiddenLoopStart", a.loop_start).number("HiddenLoopEnd", a.loop_start + a.length).close("Loop");
    x.value("Name", name).number("Color", colour);
    write_time_signature(x, project);
    x.flag("Disabled", disabled).flag("IsWarped", true);
    sample_ref(x, files, a.file);
    x.number("WarpMode", a.repitch ? 3 : 0);
    x.number("PitchCoarse", std::clamp(a.semi, -48, 48)).number("PitchFine", std::clamp(a.fine, -50, 50)).number("SampleVolume", a.gain);
    x.open("WarpMarkers").empty("WarpMarker Id=\"0\" SecTime=\"0\" BeatTime=\"0\"");
    x.add("<WarpMarker" + attr("Id", 1) + attr("SecTime", a.seconds) + attr("BeatTime", a.beats) + "/>").close("WarpMarkers");
    x.close("AudioClip");
    return x.text;
}

void write_mixer(Xml& x, const Planned& p, const melodee::Mix& mix, const std::vector<unsigned>& buses) {
    x.open("Mixer").open("Sends");
    for (unsigned n = 0; n < buses.size(); ++n)
        x.open("TrackSendHolder", attr("Id", n)).manual("Send", send_gain(mix.sends[buses[n]])).flag("EnabledByUser", true).close("TrackSendHolder");
    x.close("Sends").manual_flag("Speaker", !p.muted).flag("SoloSink", mix.solo).manual("Pan", p.pan).manual("Volume", p.volume).close("Mixer");
}

void write_slots(Xml& x, unsigned scenes, const std::map<unsigned, std::string>& clips) {
    x.open("ClipSlotList");
    for (unsigned s = 0; s < scenes; ++s) {
        x.open("ClipSlot", attr("Id", s)).open("ClipSlot").open("Value");
        if (auto found = clips.find(s); found != clips.end()) x.add(found->second);
        x.close("Value").close("ClipSlot").flag("HasStop", true).close("ClipSlot");
    }
    x.close("ClipSlotList");
}

// The set as gzip, through a part file renamed into place.
std::string write_set(const std::string& xml, const fs::path& out) {
    fs::path part = out;
    part += ".part";
    {
        juce::FileOutputStream file(juce::File(juce::String::fromUTF8(part.string().c_str())));
        if (!file.openedOk()) return "Cannot write " + out.string();
        file.setPosition(0);
        file.truncate();
        juce::GZIPCompressorOutputStream packed(file, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        if (!packed.write(xml.data(), xml.size())) return "Cannot write " + out.string();
        packed.flush();
    }
    std::error_code error;
    fs::rename(part, out, error);
    if (error) return "Cannot write " + out.string();
    return {};
}

} // namespace

std::string export_live_set(const Project& project, const melodee::PatternBank& patterns, const std::string& folder, const std::string& name,
                            LiveExport& report, const melodee::PrintTable* prints, const std::string& prints_dir, const std::string& audio_dir) {
    report = LiveExport {};
    const fs::path where = fs::path(folder) / (name + " Project");
    std::error_code error;
    fs::create_directories(where, error);
    if (error) return "Cannot make " + where.string();
    Files files(where);
    const double project_bpm = project.tempo / 10.0;

    // ---- what each Melodee track becomes
    std::vector<Planned> planned;
    std::map<unsigned, std::set<int>> moved; // a kit's pads gone to tracks of their own
    for (unsigned t = 0; t < melodee::track_count; ++t) {
        const Track& track = project.tracks[t];
        if (!track.used()) continue;
        Planned base;
        base.index = t;
        base.muted = track.mix.mute;
        base.volume = gain_of_level(track.mix.level);
        base.pan = track.mix.pan / 50.0;
        switch (track.engine) {
        case EngineKind::Audio: {
            Planned p = base;
            p.lane = Lane::Takes;
            p.audio = true;
            p.name = track_name(track, "Audio", t);
            planned.push_back(p);
            break;
        }
        case EngineKind::Midi: {
            using namespace melodee::midi_track;
            Planned p = base;
            p.lane = Lane::Notes;
            p.name = track_name(track, "MIDI", t);
            const int msb = track.params[BankMsb], lsb = track.params[BankLsb], program = track.params[Program]; // 0 Off, else n + 1
            p.bank_msb = msb ? msb - 1 : -1;
            p.bank_lsb = lsb ? lsb - 1 : -1;
            p.program = program ? program - 1 : -1;
            planned.push_back(p);
            break;
        }
        case EngineKind::Plugin: { // a Schwung module: its notes, and its prints beside them
            Planned p = base;
            p.lane = Lane::Notes;
            p.name = track_name(track, "Synth", t);
            planned.push_back(p);
            bool printed = false;
            if (prints && !prints_dir.empty())
                for (const auto& r : prints->records) printed = printed || (r.track == t && !r.file.empty());
            if (printed) {
                Planned audio = base;
                audio.lane = Lane::Prints;
                audio.audio = true;
                audio.name = p.name + " Print";
                // A print is made at print_mix()'s level and pan: the track plays it at its own over those.
                const melodee::Mix at = melodee::print_mix(track);
                audio.volume = gain_of_db(double(melodee::volume_db(track.mix.level)) - melodee::volume_db(at.level));
                if (!std::isfinite(melodee::volume_db(track.mix.level))) audio.volume = floor_gain;
                audio.pan = std::clamp(track.mix.pan - at.pan, -50, 50) / 50.0;
                planned.push_back(audio);
            }
            break;
        }
        case EngineKind::Keygroup: case EngineKind::Sampler: {
            const bool keyed = track.engine == EngineKind::Keygroup;
            // Which pads the kit plays at other pitches: those are tracks of their own.
            std::array<bool, melodee::sampler_pads> pitched {};
            if (keyed) pitched[0] = true;
            else
                for (const auto& pattern : patterns.at[t])
                    for (const auto& e : pattern.events) {
                        const int pad = int(e.key) - int(melodee::sampler_first_note);
                        if (pad >= 0 && pad < int(melodee::sampler_pads) && (e.pitch || e.pitch_ramp)) pitched[size_t(pad)] = true;
                    }
            std::vector<std::string> branches; // the Drum Rack's
            for (unsigned n = 0; n < (keyed ? 1u : melodee::sampler_pads); ++n) {
                const PadSound& pad = track.pads[n];
                if (!pad.used()) continue;
                const auto& ref = project.samples[pad.sample % melodee::sample_slots];
                std::vector<Part> parts;
                bool instrument = ref.instrument();
                if (!instrument) {
                    // Its layers, by velocity (cycle and random play one: the first).
                    const bool by_velocity = pad.layer_play == melodee::LayerPlay::Velocity;
                    for (unsigned l = 0; l < melodee::pad_layers; ++l) {
                        const auto layer = pad.layer(l);
                        if (!layer.used()) continue;
                        if (l && !by_velocity) { ++report.skipped; continue; }
                        const auto& layer_ref = project.samples[layer.sample % melodee::sample_slots];
                        if (layer_ref.instrument()) { ++report.skipped; continue; }
                        Part part;
                        part.file = files.add(layer_ref.path, report);
                        if (part.file.empty()) continue;
                        const Audio& audio = files.info(part.file);
                        part.start = std::min<int64_t>(file_frame(layer.start, audio.rate), audio.frames);
                        part.end = layer.end ? std::min<int64_t>(file_frame(layer.end, audio.rate), audio.frames) : audio.frames;
                        if (part.end <= part.start) part.end = audio.frames;
                        part.low_velocity = by_velocity ? std::max<int>(layer.low, 1) : 1;
                        part.high_velocity = by_velocity ? layer.high : 127;
                        if (l) { // a layer's tune, level and pan over the pad's
                            part.root = 60 - layer.pitch;
                            part.detune = layer.fine;
                            part.gain = std::pow(10.0, layer.gain / 2000.0);
                            part.pan = layer.pan / 50.0;
                        }
                        if (keyed) part.root += melodee::keyed_root(project, track) - 60;
                        if (pad.warp == melodee::warp_seq) { // follows the song's tempo: its region at the tempo it was played at
                            const double seconds = double(part.end - part.start) / audio.rate;
                            double bpm = project_bpm;
                            if (pad.own_tempo) bpm = pad.own_tempo / 10.0;
                            else if (pad.beats && seconds > 0) bpm = pad.beats * 60.0 / seconds;
                            part.warp_seconds = double(part.end) / audio.rate;
                            part.warp_beats = part.warp_seconds * bpm / 60.0;
                        } else if (pad.warped()) {
                            ++report.skipped; // stretched by a percentage: plays unwarped
                        }
                        parts.push_back(part);
                    }
                } else {
                    std::string why;
                    const auto read = melodee::read_instrument(ref.path, &why);
                    for (const auto& region : read.regions) {
                        const auto& zone = region.zone;
                        if (zone.on_release || zone.sequence > 1) continue;
                        std::string source = fs::path(region.sample).is_absolute() ? region.sample : melodee::instrument_zone_file(ref.path, region.sample);
                        Part part;
                        part.file = files.add(source, report);
                        if (part.file.empty()) continue;
                        const Audio& audio = files.info(part.file);
                        part.start = std::clamp<int64_t>(region.offset, 0, audio.frames);
                        part.end = region.end >= 0 ? std::min<int64_t>(region.end, audio.frames) : audio.frames;
                        part.low_key = zone.low_key; part.high_key = zone.high_key;
                        part.low_velocity = std::max<int>(zone.low_velocity, 1); part.high_velocity = zone.high_velocity;
                        const int semis = int(std::lround(zone.tune / 100.0));
                        part.root = zone.root - semis;
                        part.detune = zone.tune - semis * 100;
                        part.gain = std::pow(10.0, zone.gain / 20.0);
                        part.pan = zone.pan;
                        part.loop = zone.loop != melodee::ZoneLoop::Off && zone.loop_end > zone.loop_start;
                        part.loop_start = zone.loop_start;
                        part.loop_end = std::min<int64_t>(zone.loop_end, audio.frames);
                        parts.push_back(part);
                    }
                    if (read.regions.empty()) ++report.skipped;
                }
                if (parts.empty()) continue;
                // One zone is a sample: a Simpler (every edition of Live has one) over every key.
                const bool zones = instrument && parts.size() > 1;
                if (instrument && !zones) { parts[0].low_key = 0; parts[0].high_key = 127; parts[0].low_velocity = 1; parts[0].high_velocity = 127; }
                // A WAV's tune is the device's; an instrument's notes carry it.
                Voice voice = voice_of(pad, instrument ? 0 : pad.pitch);
                if (instrument) voice.one_shot = false;
                const std::string device = sampler_device(zones ? "MultiSampler" : "OriginalSimpler", parts, voice, files);
                if (keyed || pitched[n]) {
                    Planned p = base;
                    p.lane = keyed ? Lane::Keys : Lane::Pad;
                    p.pad = int(n);
                    p.shifted = instrument;
                    char label[4];
                    melodee::pad_label(n, label);
                    p.name = keyed ? track_name(track, "Keys", t) : track_name(track, "Kit", t) + " " + label;
                    p.device = device;
                    p.muted = track.mix.mute || (!keyed && track.pad_muted(n));
                    planned.push_back(p);
                    if (!keyed) moved[t].insert(int(n));
                    continue;
                }
                // A pad of the Drum Rack, on its note; an instrument's at its key with no pitch, moved by the pad's.
                const int note = int(melodee::sampler_first_note + n) % 128;
                Xml b;
                b.open("DrumBranch", attr("Id", branches.size()));
                const std::string label = ref.name[0] ? std::string(ref.name) : fs::path(ref.path).stem().string();
                b.open("Name").value("EffectiveName", label).value("UserName", label).close("Name");
                b.open("DeviceChain").open("MidiToAudioDeviceChain", attr("Id", 0)).open("Devices").add(device).close("Devices");
                b.close("MidiToAudioDeviceChain").close("DeviceChain");
                b.range("BranchSelectorRange", 0, 0).flag("IsSoloed", false);
                b.open("MixerDevice").manual_flag("On", true).manual_flag("Speaker", !track.pad_muted(n)).manual("Volume", 1).manual("Panorama", 0);
                b.close("MixerDevice");
                b.open("BranchInfo").number("ReceivingNote", 128 - note);
                b.number("SendingNote", instrument ? std::clamp(played_root + pad.pitch, 0, 127) : 60);
                b.number("ChokeGroup", std::min<int>(pad.group, 16)).close("BranchInfo");
                b.close("DrumBranch");
                branches.push_back(b.text);
            }
            if (!keyed && !branches.empty()) {
                Planned p = base;
                p.lane = Lane::Kit;
                p.name = track_name(track, "Kit", t);
                Xml d;
                d.open("DrumGroupDevice", attr("Id", 0)).open("Branches");
                for (const auto& branch : branches) d.add(branch);
                d.close("Branches").close("DrumGroupDevice");
                p.device = d.text;
                // Before its pads' own tracks, as the kit comes first.
                auto first_pad = std::find_if(planned.begin(), planned.end(), [&](const Planned& q) { return q.index == t; });
                planned.insert(first_pad, p);
            }
            break;
        }
        default: break;
        }
    }
    if (planned.empty()) return "Nothing to export";

    // ---- the buses something sends to: return tracks, in their order
    std::vector<unsigned> buses;
    for (unsigned b = 0; b < melodee::bus_count; ++b) {
        bool fed = false;
        for (const auto& p : planned) fed = fed || project.tracks[p.index].mix.sends[b] > 0;
        if (fed) buses.push_back(b);
    }

    // ---- scenes: one a pattern number, up to the last any track has
    auto has_pattern = [&](const Planned& p, unsigned pattern) {
        const Pattern& written = patterns.at[p.index][pattern];
        if (p.lane == Lane::Takes) return written.audio.used();
        if (p.lane == Lane::Prints) {
            const auto* record = prints ? prints->find(p.index, pattern) : nullptr;
            return record && !record->file.empty();
        }
        return !written.events.empty() && written.events.front().tick < written.length;
    };
    unsigned scenes = 1;
    for (const auto& p : planned)
        for (unsigned n = 0; n < melodee::pattern_count; ++n)
            if (has_pattern(p, n)) scenes = std::max(scenes, n + 1);
    report.scenes = scenes;

    const auto ts = project.time_signature();
    const double bar_beats = ts.beats * 4.0 / ts.unit;
    auto pattern_beats = [&](const Planned& p, unsigned pattern) {
        const Track& track = project.tracks[p.index];
        return beats_of(std::max<uint64_t>(melodee::played_ticks(patterns.at[p.index][pattern % melodee::pattern_count].length, track.speed), 1));
    };
    // What an audio track plays of a pattern.
    auto audio_of = [&](const Planned& p, unsigned pattern, AudioPlace& place) {
        const Pattern& written = patterns.at[p.index][pattern % melodee::pattern_count];
        place.length = pattern_beats(p, pattern);
        if (p.lane == Lane::Takes) {
            const auto& take = written.audio;
            if (!take.used() || audio_dir.empty()) return false;
            if (take.reverse) { ++report.skipped; return false; }
            place.file = files.add((fs::path(audio_dir) / melodee::audio_take_name(take.take)).string(), report);
            if (place.file.empty()) return false;
            const double bpm = std::max<uint16_t>(take.tempo, 1) / 10.0;
            place.seconds = std::max<uint32_t>(take.take_frames, 1) / 44100.0; // the take counts 44.1 kHz frames
            place.beats = place.seconds * bpm / 60.0;
            place.loop_start = take.offset / 44100.0 * bpm / 60.0;
            place.gain = std::pow(10.0, take.gain / 200.0);
            place.semi = take.semi;
            place.fine = take.fine;
            place.repitch = take.mode == melodee::WarpMode::Repitch;
            return true;
        }
        const auto* record = prints ? prints->find(p.index, pattern) : nullptr;
        if (!record || record->file.empty()) return false;
        char stem[64];
        std::snprintf(stem, sizeof stem, "%s %s", p.name.c_str(), melodee::print_pattern_name(pattern).c_str());
        place.file = files.add((fs::path(prints_dir) / record->file).string(), report, stem);
        if (place.file.empty()) return false;
        place.seconds = std::max<uint32_t>(record->pass, 1) / 44100.0; // one pass of the pattern, as the project counts
        place.beats = place.length;
        place.loop_start = 0;
        return true;
    };

    // ---- the tracks
    Xml x;
    x.open("Tracks");
    unsigned id = 10;
    for (const auto& p : planned) {
        const Track& track = project.tracks[p.index];
        const int colour = colour_of(p.index);
        const char* kind = p.audio ? "AudioTrack" : "MidiTrack";
        std::map<unsigned, std::string> session;
        std::string arranged;
        unsigned clip_id = 0;
        for (unsigned n = 0; n < scenes; ++n) {
            if (!has_pattern(p, n)) continue;
            const std::string clip_name = melodee::print_pattern_name(n);
            if (p.audio) {
                AudioPlace place;
                if (!audio_of(p, n, place)) continue;
                session[n] = audio_clip(project, files, place, clip_name, clip_id++, colour, 0, 0, false);
            } else {
                double length = 0;
                const auto notes = notes_of(project, p, patterns.at[p.index][n], moved[p.index], length);
                if (notes.empty() && (p.lane == Lane::Kit || p.lane == Lane::Pad)) continue; // the pattern plays nothing here
                const int program = patterns.at[p.index][n].program ? patterns.at[p.index][n].program - 1 : p.program;
                session[n] = midi_clip(project, p, notes, length, clip_name, clip_id++, colour, 0, length, false, program);
                report.notes += unsigned(notes.size());
            }
            ++report.clips;
        }
        // The song: each of its clips on this track's lane, its pattern looping through it.
        const auto& song = project.song;
        for (unsigned c = 0; c < song.count; ++c) {
            const auto& clip = song.clips[c];
            if (clip.track != p.index) continue;
            const double from = clip.bar * bar_beats, to = clip.end() * bar_beats;
            const std::string clip_name = melodee::print_pattern_name(clip.pattern);
            if (p.audio) {
                AudioPlace place;
                if (!audio_of(p, clip.pattern, place)) continue;
                arranged += audio_clip(project, files, place, clip_name, clip_id++, colour, from, to, clip.muted);
            } else {
                const Pattern& written = patterns.at[p.index][clip.pattern % melodee::pattern_count];
                double length = 0;
                const auto notes = notes_of(project, p, written, moved[p.index], length);
                if (notes.empty() && p.lane != Lane::Notes) continue;
                const int program = written.program ? written.program - 1 : p.program;
                arranged += midi_clip(project, p, notes, length, clip_name, clip_id++, colour, from, to, clip.muted, program);
            }
            ++report.arranged;
        }
        x.open(kind, attr("Id", id++));
        x.open("Name").value("EffectiveName", p.name).value("UserName", p.name).close("Name").number("Color", colour);
        x.open("DeviceChain");
        write_mixer(x, p, track.mix, buses);
        x.open("MainSequencer");
        write_slots(x, scenes, session);
        const char* timeline = p.audio ? "Sample" : "ClipTimeable";
        x.open(timeline).open("ArrangerAutomation").open("Events").add(arranged).close("Events").close("ArrangerAutomation").close(timeline);
        x.close("MainSequencer");
        x.open("FreezeSequencer");
        write_slots(x, scenes, {});
        x.close("FreezeSequencer");
        x.open("DeviceChain").open("Devices").add(p.device).close("Devices").close("DeviceChain");
        x.close("DeviceChain").close(kind);
        ++report.tracks;
    }
    for (unsigned b : buses) { // a return track a bus, its level (its effects are not carried)
        const std::string bus_name = std::string("Bus ") + char('A' + b);
        x.open("ReturnTrack", attr("Id", id++));
        x.open("Name").value("EffectiveName", bus_name).value("UserName", bus_name).close("Name").number("Color", 0);
        x.open("DeviceChain").open("Mixer").open("Sends");
        for (unsigned n = 0; n < buses.size(); ++n)
            x.open("TrackSendHolder", attr("Id", n)).manual("Send", floor_gain).flag("EnabledByUser", true).close("TrackSendHolder");
        x.close("Sends").manual_flag("Speaker", true).manual("Pan", 0).manual("Volume", send_gain(project.buses[b].level)).close("Mixer");
        x.open("FreezeSequencer").empty("ClipSlotList").close("FreezeSequencer");
        x.close("DeviceChain").close("ReturnTrack");
    }
    x.close("Tracks");

    // ---- the main track: tempo, time signature, the song's tempo lane
    const auto& lane = project.song.tempo;
    x.open("MainTrack");
    if (!lane.empty()) {
        x.open("AutomationEnvelopes").open("Envelopes").open("AutomationEnvelope", attr("Id", 0));
        x.open("EnvelopeTarget").number("PointeeId", tempo_target).close("EnvelopeTarget").open("Automation").open("Events");
        unsigned event = 0;
        double last = project_bpm;
        auto point = [&](double time, double bpm) {
            x.add("<FloatEvent" + attr("Id", event++) + attr("Time", time) + attr("Value", bpm) + "/>");
        };
        point(before_all, project_bpm);
        for (unsigned n = 0; n < lane.count; ++n) {
            const auto& tp = lane.points[n];
            const double at = tp.bar * bar_beats, bpm = tp.tempo / 10.0;
            if (!tp.ramp) point(at, last); // a step: the tempo before, to its bar
            point(at, bpm);
            last = bpm;
        }
        x.close("Events").close("Automation").close("AutomationEnvelope").close("Envelopes").close("AutomationEnvelopes");
    }
    x.open("DeviceChain").open("Mixer");
    x.open("Tempo").number("Manual", project_bpm).open("AutomationTarget", attr("Id", tempo_target)).number("LockEnvelope", 0).close("AutomationTarget").close("Tempo");
    x.manual("TimeSignature", live_time_signature(ts.beats, ts.unit));
    x.close("Mixer").close("DeviceChain").close("MainTrack");

    x.open("SendsPre");
    for (unsigned n = 0; n < buses.size(); ++n) x.add("<SendPreBool" + attr("Id", n) + attr("Value", "false") + "/>");
    x.close("SendsPre");

    // ---- scenes: each pattern number, named and timed as the project's
    x.open("Scenes");
    for (unsigned n = 0; n < scenes; ++n) {
        const auto& scene = project.scenes[n];
        const unsigned tempo = scene.tempo ? scene.tempo : project.seq_tempo ? project.pattern_tempo[n] : 0;
        x.open("Scene", attr("Id", n)).value("Name", scene.name[0] ? std::string(scene.name) : melodee::print_pattern_name(n));
        x.number("Tempo", tempo ? tempo / 10.0 : project_bpm).flag("IsTempoEnabled", tempo != 0).close("Scene");
    }
    x.close("Scenes");

    // ---- the song's loop brace and markers
    const auto& song = project.song;
    x.open("Transport").flag("LoopOn", song.looping()).number("LoopStart", song.loop_start * bar_beats);
    x.number("LoopLength", (song.looping() ? song.loop_end - song.loop_start : 4) * bar_beats).close("Transport");
    x.open("Locators").open("Locators");
    for (unsigned m = 0; m < song.marker_count; ++m) {
        const auto& marker = song.markers[m];
        x.open("Locator", attr("Id", m)).number("Time", marker.bar * bar_beats).value("Name", marker.name).flag("IsSongStart", false).close("Locator");
    }
    x.close("Locators").close("Locators");

    const std::string document = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
        "<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12117\" SchemaChangeCount=\"10\" Creator=\"Melodee\" Revision=\"\">\n<LiveSet>" +
        "<NextPointeeId Value=\"100000\"/>" + x.text + "</LiveSet>\n</Ableton>\n";
    return write_set(document, where / (name + ".als"));
}

} // namespace app
