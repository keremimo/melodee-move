// SPDX-License-Identifier: AGPL-3.0-only
#include "app/schwung_pages.h"
#include <juce_core/juce_core.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <utility>

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

namespace {

std::string text(const juce::var& value) { return value.isVoid() || value.isUndefined() ? std::string() : value.toString().toStdString(); }

} // namespace

std::string SchwungPages::shared_folder() {
    std::string folder = SchwungDisplay::root() + "/shared";
    std::error_code error;
    return std::filesystem::exists(std::filesystem::path(folder) / "param_pages" / "page_controller.mjs", error) ? folder : std::string();
}

std::unique_ptr<SchwungPages> SchwungPages::load(CanvasPage::Host host, Hooks hooks, std::string& error) {
    if (shared_folder().empty()) { error = "no Schwung shared code"; return nullptr; }
    std::unique_ptr<SchwungPages> pages(new SchwungPages());
    auto* self = pages.get();
    self->hooks = std::move(hooks);
    self->states = self->hooks.page_states;
    self->pads.fill(-1);
    auto read = host.read ? std::move(host.read) : [get = host.get](const std::string& key) { return std::optional<std::string>(get(key)); };
    auto set = std::move(host.set);
    host.read = [read, self](const std::string& key) -> std::optional<std::string> { // Schwung's three answers
        if (key.rfind("melodee:", 0) != 0) return read(key);
        if (key == "melodee:entry") return std::exchange(self->entry, std::string());
        if (key == "melodee:shift") return std::string(self->hooks.shift && self->hooks.shift() ? "1" : "0");
        if (key == "melodee:reader") return std::string(self->hooks.reader && self->hooks.reader() ? "1" : "0");
        if (key == "melodee:page_states") return self->states;
        if (key == "melodee:trailing") return self->hooks.trailing ? self->hooks.trailing() : std::string("[]");
        return std::nullopt;
    };
    host.set = [set, self](const std::string& key, const std::string& value) {
        if (key.rfind("melodee:", 0) != 0) { set(key, value); return; }
        if (key == "melodee:status") self->take_status(value);
        else if (key == "melodee:exit") self->exited = true;
        else if (key == "melodee:knob") {
            int knob = -1, colour = 0;
            std::istringstream(value) >> knob >> colour;
            if (knob < 0) self->rings.fill(-1);
            else if (knob < 8) self->rings[size_t(knob)] = colour;
        } else if (key == "melodee:pad_block") {
            bool on = value == "1";
            if (on == self->blocked) return;
            self->blocked = on;
            if (!on) self->pads.fill(-1);
            if (self->hooks.pad_block) self->hooks.pad_block(on);
        } else if (key == "melodee:pad") {
            int note = -1, colour = 0;
            std::istringstream(value) >> note >> colour;
            if (note >= 68 && note < 68 + 32) self->pads[size_t(note - 68)] = colour;
        } else if (key == "melodee:page_states") self->states = value;
        else if (key == "melodee:say") { if (self->hooks.say) self->hooks.say(value); }
        else if (key == "melodee:action") { if (self->hooks.action) self->hooks.action(value); }
    };
    std::string source(melodee_schwung_pages_start, size_t(melodee_schwung_pages_end - melodee_schwung_pages_start));
    pages->page = CanvasPage::load_source(source, "melodee-schwung-pages.js", std::move(host), error);
    if (!pages->page) return nullptr;
    return pages;
}

void SchwungPages::enter(const std::string& page_name, bool open) {
    auto* what = new juce::DynamicObject();
    what->setProperty("slot", hooks.slot);
    what->setProperty("component", juce::String(hooks.component));
    what->setProperty("module", juce::String(hooks.module_id));
    what->setProperty("name", juce::String(hooks.module_name));
    what->setProperty("abbrev", juce::String(hooks.module_abbrev));
    what->setProperty("dir", juce::String(hooks.module_dir));
    if (!page_name.empty()) {
        what->setProperty("page", juce::String(page_name));
        what->setProperty("enter", open);
    }
    entry = juce::JSON::toString(juce::var(what), true).toStdString();
    exited = false;
}

void SchwungPages::frame(juce::Graphics& g) {
    CanvasPage::Frame frame;
    frame.key = "schwung";
    frame.clear = false; // Schwung's screen code clears for itself; its host never does
    page->draw(g, {}, frame);
}

void SchwungPages::midi(uint8_t status, uint8_t number, uint8_t value) { page->midi(status, number, value); }

void SchwungPages::release_pads() {
    if (!blocked) return;
    blocked = false;
    pads.fill(-1);
    if (hooks.pad_block) hooks.pad_block(false);
}

void SchwungPages::take_status(const std::string& json) {
    auto parsed = juce::JSON::parse(juce::String(json));
    if (!parsed.isObject()) return;
    Status next;
    next.view = text(parsed["view"]);
    next.page = text(parsed["page"]);
    next.kind = text(parsed["kind"]);
    next.canvas = bool(parsed["canvas"]);
    next.picker = bool(parsed["picker"]);
    next.typing = bool(parsed["typing"]);
    next.warning = bool(parsed["warning"]);
    next.text = text(parsed["text"]);
    next.title = text(parsed["title"]);
    next.index = parsed["index"].isVoid() ? -1 : int(parsed["index"]);
    if (auto* pairs = parsed["footer"].getArray())
        for (const auto& pair : *pairs)
            if (auto* two = pair.getArray(); two && two->size() == 2) next.footer.emplace_back(text((*two)[0]), text((*two)[1]));
    if (auto* rows = parsed["rows"].getArray())
        for (const auto& row : *rows)
            if (auto* two = row.getArray(); two && two->size() == 2) next.rows.emplace_back(text((*two)[0]), text((*two)[1]));
    shown = std::move(next);
}

} // namespace app
