// SPDX-License-Identifier: AGPL-3.0-only
#include "web/zip_stream.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

namespace web {
namespace fs = std::filesystem;
namespace {

constexpr uint64_t zip_limit = 0xffffffffull; // no Zip64
constexpr size_t entry_limit = 0xffff;
constexpr uint16_t flags = 0x0800;   // names in UTF-8
constexpr uint16_t version = 20;     // 2.0: stored files in folders

const std::array<uint32_t, 256>& crc_table() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t {};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

void le16(std::string& out, uint32_t v) { out += char(v & 0xff); out += char((v >> 8) & 0xff); }
void le32(std::string& out, uint32_t v) { le16(out, v & 0xffff); le16(out, v >> 16); }

// The header before a file's data (local) or in the directory at the end (central).
std::string header(const ZipPlan::Entry& e, bool central, uint32_t offset) {
    std::string out;
    le32(out, central ? 0x02014b50u : 0x04034b50u);
    if (central) le16(out, 0x0300 | version); // made on Unix
    le16(out, version);
    le16(out, flags);
    le16(out, 0); // stored
    le16(out, e.time);
    le16(out, e.date);
    le32(out, e.crc);
    le32(out, uint32_t(e.size));
    le32(out, uint32_t(e.size));
    le16(out, uint32_t(e.name.size()));
    le16(out, 0); // no extra field
    if (central) {
        le16(out, 0);              // no comment
        le16(out, 0);              // disk 0
        le16(out, 0);              // internal attributes
        le32(out, 0100644u << 16); // a plain file, rw-r--r--
        le32(out, offset);
    }
    return out + e.name;
}

} // namespace

uint32_t crc32_of(const void* data, size_t size, uint32_t crc) {
    const auto& table = crc_table();
    const auto* p = static_cast<const unsigned char*>(data);
    crc = ~crc;
    for (size_t n = 0; n < size; ++n) crc = table[(crc ^ p[n]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

std::string plan_zip(const std::string& folder, const std::string& top, ZipPlan& plan) {
    plan = ZipPlan {};
    std::error_code error;
    if (!fs::is_directory(folder, error)) return "Not a folder";
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(folder, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        const auto name = it->path().filename().string();
        if (!name.empty() && name[0] == '.') { // hidden: left out, and its insides
            if (it->is_directory(error) && !it->is_symlink(error)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink(error) || !it->is_regular_file(error)) continue;
        files.push_back(it->path());
    }
    if (error) return "Cannot read the folder";
    std::sort(files.begin(), files.end());
    if (files.size() > entry_limit) return "Too many files for a zip";
    std::vector<char> buffer(1 << 20);
    uint64_t at = 0, directory = 0;
    for (const auto& file : files) {
        ZipPlan::Entry e;
        e.path = file.string();
        e.name = top + "/" + fs::relative(file, folder, error).generic_string();
        struct stat info {};
        if (::stat(e.path.c_str(), &info) != 0) return "Cannot read " + e.name;
        e.size = uint64_t(info.st_size);
        std::tm when {};
        const time_t changed = info.st_mtime;
        if (localtime_r(&changed, &when) && when.tm_year >= 80) {
            e.time = uint16_t(when.tm_hour << 11 | when.tm_min << 5 | when.tm_sec / 2);
            e.date = uint16_t((when.tm_year - 80) << 9 | (when.tm_mon + 1) << 5 | when.tm_mday);
        } else {
            e.date = 1 << 5 | 1; // 1 January 1980
        }
        std::ifstream in(e.path, std::ios::binary);
        if (!in) return "Cannot read " + e.name;
        uint64_t read = 0;
        while (in) {
            in.read(buffer.data(), std::streamsize(buffer.size()));
            const auto got = size_t(in.gcount());
            e.crc = crc32_of(buffer.data(), got, e.crc);
            read += got;
        }
        if (read != e.size) return "Cannot read " + e.name;
        at += 30 + e.name.size() + e.size;
        directory += 46 + e.name.size();
        if (e.size > zip_limit || at > zip_limit) return "Too big for a zip (4 GB)";
        plan.entries.push_back(std::move(e));
    }
    plan.bytes = at + directory + 22;
    if (plan.bytes > zip_limit) return "Too big for a zip (4 GB)";
    return {};
}

bool send_zip(const ZipPlan& plan, const std::function<bool(const std::string&)>& send) {
    std::vector<char> buffer(1 << 20);
    std::vector<uint32_t> offsets;
    uint64_t at = 0;
    for (const auto& e : plan.entries) {
        offsets.push_back(uint32_t(at));
        std::string head = header(e, false, 0);
        if (!send(head)) return false;
        at += head.size();
        std::ifstream in(e.path, std::ios::binary);
        uint64_t left = e.size;
        while (left) {
            in.read(buffer.data(), std::streamsize(std::min<uint64_t>(left, buffer.size())));
            const auto got = size_t(in.gcount());
            if (!got || !send(std::string(buffer.data(), got))) return false; // cut short: the browser sees a broken zip
            left -= got;
            at += got;
        }
    }
    std::string directory;
    for (size_t n = 0; n < plan.entries.size(); ++n) directory += header(plan.entries[n], true, offsets[n]);
    std::string end;
    le32(end, 0x06054b50u);
    le16(end, 0);
    le16(end, 0);
    le16(end, uint32_t(plan.entries.size()));
    le16(end, uint32_t(plan.entries.size()));
    le32(end, uint32_t(directory.size()));
    le32(end, uint32_t(at));
    le16(end, 0);
    return send(directory + end);
}

} // namespace web
