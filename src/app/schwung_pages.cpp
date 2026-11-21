// SPDX-License-Identifier: AGPL-3.0-only
#include "app/schwung_pages.h"
#include <juce_core/juce_core.h>
#include <cstdlib>
#include <filesystem>
#include <memory>

// The seam, in the binary as the web page's files are (make runs at the
// repository root).
asm(R"(
    .section .rodata
    .global melodee_schwung_pages_start
melodee_schwung_pages_start:
    .incbin "src/app/schwung_pages/host.js"
    .global melodee_schwung_pages_end
melodee_schwung_pages_end:
    .byte 0
    .previous
)");

extern "C" const char melodee_schwung_pages_start[], melodee_schwung_pages_end[];

namespace app {

std::string SchwungPages::shared_folder() {
    const char* given = std::getenv("MELODEE_SCHWUNG_SHARED");
    std::string folder = given && *given ? given : "/data/UserData/schwung/shared";
    std::error_code error;
    return std::filesystem::exists(std::filesystem::path(folder) / "param_pages" / "page_controller.mjs", error) ? folder : std::string();
}

std::unique_ptr<SchwungPages> SchwungPages::load(CanvasPage::Host host, const std::string& module_dir, const std::string& trailing,
                                                 std::function<void(const std::string&)> action, std::string& error) {
    if (shared_folder().empty()) { error = "no Schwung shared code"; return nullptr; }
    std::unique_ptr<SchwungPages> pages(new SchwungPages());
    auto* self = pages.get();
    auto get = std::move(host.get);
    auto set = std::move(host.set);
    host.get = [get, module_dir, trailing](const std::string& key) {
        if (key == "melodee:module_dir") return module_dir;
        if (key == "melodee:trailing") return trailing;
        return get(key);
    };
    host.set = [set, self, action](const std::string& key, const std::string& value) {
        if (key == "melodee:described") self->take(value);
        else if (key == "melodee:action") { if (action) action(value); }
        else set(key, value);
    };
    std::string source(melodee_schwung_pages_start, size_t(melodee_schwung_pages_end - melodee_schwung_pages_start));
    pages->page = CanvasPage::load_source(source, "melodee-schwung-pages.js", std::move(host), error);
    if (!pages->page) return nullptr;
    return pages;
}

void SchwungPages::draw(juce::Graphics& g) {
    CanvasPage::Frame frame;
    frame.key = "schwung-pages";
    page->draw(g, {}, frame);
}

void SchwungPages::midi(uint8_t status, uint8_t number, uint8_t value) { page->midi(status, number, value); }

void SchwungPages::text_entered(const std::string& text, bool cancelled) { page->text_entered(text, cancelled); }

void SchwungPages::take(const std::string& described) {
    auto parsed = juce::JSON::parse(juce::String(described));
    if (!parsed.isObject()) return;
    wheel = bool(parsed["wheel"]);
    exit = bool(parsed["exit"]);
    index = int(parsed["index"]);
    count = int(parsed["count"]);
    name = parsed["page"].toString().toStdString();
}

} // namespace app
