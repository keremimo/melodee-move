// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// A folder as a zip, written as it goes out (the web page's download of a
// folder: an Ableton Live Set's project, a stems folder). Every file is
// stored as it is (sounds do not shrink), under "<top>/<its path>"; each is
// read once for its CRC first, so the headers before the data are whole and
// any unzip reads it, and the length is known before a byte is sent. Hidden
// files (a dot first) and links are left out. No Zip64: a folder past 4 GB or
// 65535 files is refused.
namespace web {

struct ZipPlan {
    struct Entry {
        std::string path, name; // on the Move; in the zip
        uint64_t size = 0;
        uint32_t crc = 0;
        uint16_t time = 0, date = 0; // MS-DOS's, of its last change
    };
    std::vector<Entry> entries;
    uint64_t bytes = 0; // the whole zip
};

// The plan for `folder`, reading each file for its CRC. Empty error on success.
std::string plan_zip(const std::string& folder, const std::string& top, ZipPlan& plan);
// Sends the zip in parts; false when the browser went (or a file could not be read: the zip is cut short).
bool send_zip(const ZipPlan& plan, const std::function<bool(const std::string&)>& send);

uint32_t crc32_of(const void* data, size_t size, uint32_t crc = 0);

} // namespace web
