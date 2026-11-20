// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "melodee/engine/sfz.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace melodee { class EngineHost; struct SampleData; }

// Melodee's multisampled instruments on a keygroup track (docs/UI.md,
// "Instruments"): an SFZ, an MPC keygroup program (.xpm, MPC 3's .xty), a
// Logic EXS24 instrument, a SoundFont preset ("<file>.sf2#<preset>") or a
// DecentSampler preset, read by Melodee's own readers (engine/sfz.h,
// keygroup.h, exs.h, sf2.h, dspreset.h; shell/mpc_program.h, sf2_file.h,
// dspreset_file.h), compiled unchanged. As on the MPC the zones reach the
// audio thread at once, each silent until its file has landed: a PCM WAV is
// read in place from storage as it plays (engine/stream_pager.h), any other
// (FLAC, AIFF, Ogg, a SoundFont's sounds) is decoded and held in memory.
// Reading and landing allocate and touch files: never on the audio thread.
namespace app {

struct Sample;

// What a keygroup's instrument is, for its screens and its landing.
struct Instrument {
    std::string format;               // "SFZ", "MPC", "EXS", "SF2", "DECENT"
    std::string folder, sf2;          // where its files are named from; an .sf2's own file
    melodee::SfzRead read;            // its regions, each zone's part set
    std::vector<std::string> parts;   // its files as it names them (an .sf2: its sounds)
    std::vector<std::vector<uint16_t>> zones_of; // each part's zones
    std::vector<std::vector<float>> held;        // a part decoded: left then right (one side: left alone)
    std::atomic<unsigned> landed {0}, failed {0};
    std::atomic<bool> cancelled {false};
    std::string failure;              // the first file's trouble, for the screen
    bool done() const { return landed.load() + failed.load() >= parts.size(); }
};

// The kinds of file, by name: an instrument a keygroup loads (an .sf2's
// preset among them), a SoundFont (a folder of its presets), and the
// format's short name for the screens ("" for a sample).
bool instrument_file(const std::string& path);
bool soundfont_file(const std::string& path);
std::string instrument_format(const std::string& path);
std::string preset_path(const std::string& sf2, unsigned index); // "<file>.sf2#<index>"
// Where files an instrument plays that are not PCM WAVs are laid out to be
// read in place (Melodee's Cache folder); set once, as the Studio starts.
void set_instrument_cache(const std::string& folder);

struct SoundfontPreset { unsigned index = 0; int bank = 0, program = 0; std::string name; };
std::vector<SoundfontPreset> soundfont_presets(const std::string& path, std::string& error);

// The instrument read and its zones built (no file landed yet), as a
// Sample whose data is a Multisample. Null and `error` set where it cannot.
std::unique_ptr<Sample> read_instrument_sample(const std::string& path, std::string& error);
// An instrument (by its name) or a sample (studio.h read_sample()).
std::unique_ptr<Sample> read_any_sample(const std::string& path, std::string& error);

// Each file of an instrument landed, on a thread of its own, once the host
// has the sample: its part read in place or decoded, its zones' start and
// loop held in memory, then let play.
class Landings {
public:
    explicit Landings(melodee::EngineHost& host) : host(host) {}
    ~Landings(); // each one called off and waited for
    Landings(const Landings&) = delete;
    Landings& operator=(const Landings&) = delete;
    void start(Sample& sample);
    void reap(); // the threads that have finished, joined
private:
    struct Running { std::thread thread; std::shared_ptr<std::atomic<bool>> finished; Instrument* instrument = nullptr; };
    melodee::EngineHost& host;
    std::vector<Running> running;
};

// One file of `instrument` landed into `data`'s part `index` (the landing
// thread's; tests call it directly). Empty on success.
std::string land_part(Instrument& instrument, melodee::SampleData& data, unsigned index, melodee::EngineHost* host);

} // namespace app
