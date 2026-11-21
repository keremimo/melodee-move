// SPDX-License-Identifier: AGPL-3.0-only
// What Melodee's MPC 3 export (melodee-sample shell/mpc3_export.cpp, compiled
// unchanged) asks of the shell it runs in about instrument files: on the MPC
// the sample library reads SFZ, keygroup programs, EXS and DecentSampler
// presets (shell/sample_library_instruments.cpp, which brings the whole
// library with it). On the Move a kit pad plays a sample file, so these are
// the Move shell's: an SFZ read beside its samples, and anything else left
// out of the export (counted in its report as a pad not written).
#include "melodee/engine/sfz.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace melodee {
namespace fs = std::filesystem;

SfzRead read_instrument(const std::string& path, std::string* error) {
    auto extension = fs::path(path).extension().string();
    for (auto& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (extension != ".sfz") {
        if (error) *error = "not on the Move";
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot read it";
        return {};
    }
    std::stringstream text;
    text << in.rdbuf();
    return read_sfz(text.str());
}

std::string instrument_zone_file(const std::string& instrument, const std::string& sample) {
    std::string relative = sample;
    for (auto& c : relative) if (c == '\\') c = '/';
    auto found = fs::path(instrument).parent_path() / relative;
    std::error_code e;
    return fs::exists(found, e) ? found.string() : std::string();
}

} // namespace melodee
