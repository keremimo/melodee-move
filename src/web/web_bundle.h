// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstddef>
#include <string>

// The web page's files, carried in the binary (build/gen/web_bundle.bin,
// made by tools/web_bundle.py from src/web/static/ and docs/GUIDE.md): the
// page with the guide in it, the style sheet, the script, Rubik and
// Melodee's wordmark.
namespace web {

struct File {
    const char* data = nullptr;
    size_t size = 0;
    std::string type; // "text/html; charset=utf-8"
    explicit operator bool() const { return data != nullptr; }
};

// The file served at `path` ("/", "/files", "/app.js", "/font/rubik.ttf"); empty when there is none.
File bundled(const std::string& path);

} // namespace web
