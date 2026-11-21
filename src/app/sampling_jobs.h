// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/sampling.h"
#include "app/studio.h"
#include "melodee/engine/recall_ring.h"
#include <filesystem>
#include <string>
#include <vector>

// Sampling's work in hand, shared by sampling.cpp and sampling_files.cpp.
namespace app {

// A file Melodee wrote, and its folders up to Melodee's own, given to the
// Move's user (a run over SSH is root's), as the project file is.
inline void hand_back(const std::string& path, const std::string& home) {
    namespace fs = std::filesystem;
    constexpr const char* move_user_data = "/data/UserData";
    give_to_owner_of(path, move_user_data);
    const fs::path top = fs::path(home);
    for (fs::path folder = fs::path(path).parent_path(); !folder.empty() && folder != folder.parent_path(); folder = folder.parent_path()) {
        give_to_owner_of(folder.string(), move_user_data);
        if (folder == top) break;
    }
}

// A file being made: a take drained from the recorder as it comes, or Audio
// Recall's copy of the ring. The MIDI thread fills in what it is; the
// service thread does the rest.
struct Sampling::Job {
    enum class Kind { Take, Recall } kind = Kind::Take;
    std::string path;  // the file it becomes
    std::string label; // "TAKE 3"
    unsigned track = 0;
    Clock::time_point armed_at {};
    // A take, the service thread's while it records.
    melodee::WavStream file;
    bool open = false;
    uint32_t drained = 0; // device frames written
    std::vector<float> left, right; // the take as it came, while it fits a pad (60 s)
    bool too_long = false;
    // Audio Recall's.
    int pad = -1;
    melodee::RecallOptions options;
};

// A sample's region as a file of its own: Trim, Save, Extract.
struct Sampling::Edit {
    enum class Kind { Trim, Save, Extract } kind = Kind::Trim;
    unsigned track = 0, pad = 0, slice = 0;
    uint32_t start = 0, end = 0; // 44.1 kHz frames; end 0: the sample's end
    std::string name, path;
    const melodee::SampleData* source = nullptr; // the slot's, kept for the run
    melodee::PadSound sound;
    int target = -1; // Extract: the pad it goes onto
};

// An audio track's take as it is written: "<project> Audio/Take NNN.wav.part" until whole.
struct Sampling::AudioJob {
    unsigned track = 0, take = 0;
    std::string path; // the file it becomes
    uint16_t tempo = 1200;
    melodee::WavStream file;
    bool open = false;
    std::vector<float> left, right; // the take as it came (a pattern's pass at most)
};

} // namespace app
