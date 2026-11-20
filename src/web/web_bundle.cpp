// SPDX-License-Identifier: AGPL-3.0-only
// The bundle's layout (tools/web_bundle.py): "MVWB", then for each file its
// path, its type and its bytes, each as a 32-bit little-endian length and
// that many bytes. The assembler reads it from the build folder (make runs
// at the repository root), as Melodee's page on the MPC is carried.
#include "web/web_bundle.h"
#include <cstdint>
#include <cstring>
#include <map>

asm(R"(
    .section .rodata
    .balign 16
    .global melodee_move_web_start
melodee_move_web_start:
    .incbin "build/gen/web_bundle.bin"
    .global melodee_move_web_end
melodee_move_web_end:
    .previous
)");

extern "C" const unsigned char melodee_move_web_start[], melodee_move_web_end[];

namespace web {
namespace {
const std::map<std::string, File>& files() {
    static const std::map<std::string, File> all = [] {
        std::map<std::string, File> out;
        const unsigned char* at = melodee_move_web_start;
        const unsigned char* end = melodee_move_web_end;
        auto field = [&](const char*& data, uint32_t& size) {
            if (end - at < 4) return false;
            size = uint32_t(at[0]) | uint32_t(at[1]) << 8 | uint32_t(at[2]) << 16 | uint32_t(at[3]) << 24;
            at += 4;
            if (uint32_t(end - at) < size) return false;
            data = reinterpret_cast<const char*>(at);
            at += size;
            return true;
        };
        if (end - at < 4 || std::memcmp(at, "MVWB", 4) != 0) return out;
        at += 4;
        for (;;) {
            const char *path, *type, *data;
            uint32_t path_size, type_size, data_size;
            if (!field(path, path_size) || !field(type, type_size) || !field(data, data_size)) break;
            out[std::string(path, path_size)] = File {data, data_size, std::string(type, type_size)};
        }
        return out;
    }();
    return all;
}
} // namespace

File bundled(const std::string& path) {
    auto found = files().find(path);
    return found == files().end() ? File {} : found->second;
}

} // namespace web
