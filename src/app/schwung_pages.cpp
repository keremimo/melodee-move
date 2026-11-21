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

namespace {

std::string text(const juce::var& value) { return value.isVoid() || value.isUndefined() ? std::string() : value.toString().toStdString(); }

ModuleView::List list_of(const juce::var& from, bool rows_are_objects) {
    ModuleView::List list;
    if (!from.isObject()) return list;
    list.shown = true;
    list.entered = bool(from["entered"]);
    list.cursor = int(from["cursor"]);
    list.current = from.hasProperty("current") ? int(from["current"]) : -1;
    if (from.hasProperty("index")) list.cursor = int(from["index"]);
    list.title = text(from["title"]);
    if (auto* rows = from["rows"].getArray())
        for (const auto& row : *rows)
            list.rows.push_back(rows_are_objects ? ModuleView::Row {text(row["label"]), text(row["value"])} : ModuleView::Row {text(row), {}});
    return list;
}

} // namespace

std::string SchwungPages::shared_folder() {
    const char* given = std::getenv("MELODEE_SCHWUNG_SHARED");
    std::string folder = given && *given ? given : "/data/UserData/schwung/shared";
    std::error_code error;
    return std::filesystem::exists(std::filesystem::path(folder) / "param_pages" / "page_controller.mjs", error) ? folder : std::string();
}

std::unique_ptr<SchwungPages> SchwungPages::load(CanvasPage::Host host, Hooks hooks, std::string& error) {
    if (shared_folder().empty()) { error = "no Schwung shared code"; return nullptr; }
    std::unique_ptr<SchwungPages> pages(new SchwungPages());
    auto* self = pages.get();
    self->states = hooks.page_states;
    auto read = host.read ? std::move(host.read) : [get = host.get](const std::string& key) { return std::optional<std::string>(get(key)); };
    auto set = std::move(host.set);
    host.read = [read, self, hooks](const std::string& key) -> std::optional<std::string> { // Schwung's three answers
        if (key == "melodee:module_dir") return hooks.module_dir;
        if (key == "melodee:trailing") return hooks.trailing ? hooks.trailing() : std::string("[]");
        if (key == "melodee:page_states") return self->states;
        return read(key);
    };
    host.set = [set, self, hooks](const std::string& key, const std::string& value) {
        if (key == "melodee:view") self->take(value);
        else if (key == "melodee:page_states") self->states = value;
        else if (key == "melodee:action") { if (hooks.action) hooks.action(value); }
        else if (key == "melodee:say") { if (hooks.say) hooks.say(value); }
        else set(key, value);
    };
    std::string source(melodee_schwung_pages_start, size_t(melodee_schwung_pages_end - melodee_schwung_pages_start));
    pages->page = CanvasPage::load_source(source, "melodee-schwung-pages.js", std::move(host), error);
    if (!pages->page) return nullptr;
    return pages;
}

void SchwungPages::frame(juce::Graphics& g, juce::Rectangle<int> band) {
    CanvasPage::Frame frame;
    frame.key = "schwung-pages";
    frame.x = band.getX();
    frame.y = band.getY();
    frame.width = band.getWidth();
    frame.height = band.getHeight();
    page->draw(g, {}, frame);
}

void SchwungPages::midi(uint8_t status, uint8_t number, uint8_t value) { page->midi(status, number, value); }

void SchwungPages::text_entered(const std::string& text, bool cancelled) { page->text_entered(text, cancelled); }

void SchwungPages::take(const std::string& view) {
    auto parsed = juce::JSON::parse(juce::String(view));
    if (!parsed.isObject()) return;
    ModuleView next;
    next.ready = true;
    next.unsupported = bool(parsed["unsupported"]);
    next.unresolved = bool(parsed["unresolved"]);
    if (next.unresolved && model.ready) { model.unresolved = true; return; } // hold the last frame, as Schwung's host does
    next.kind = text(parsed["kind"]);
    next.page = text(parsed["page"]);
    next.index = int(parsed["index"]);
    next.count = int(parsed["count"]);
    if (const auto& h = parsed["header"]; h.isObject()) next.header = {text(h["left"]), text(h["right"]), bool(h["inverted"])};
    if (auto* cells = parsed["cells"].getArray())
        for (const auto& c : *cells) {
            ModuleView::Cell cell;
            if (c.isObject()) {
                cell.empty = false;
                cell.key = text(c["key"]);
                cell.name = text(c["name"]);
                cell.short_name = text(c["short"]);
                cell.value = text(c["value"]);
                cell.header_value = text(c["headerValue"]);
                cell.widget = text(c["widget"]);
                cell.normalized = c["normalized"].isDouble() || c["normalized"].isInt() ? double(c["normalized"]) : -1;
                cell.enum_index = c.hasProperty("enumIndex") ? int(c["enumIndex"]) : -1;
                cell.options = int(c["options"]);
                cell.read_only = bool(c["readOnly"]);
                cell.turnable = bool(c["turnable"]);
                cell.divable = bool(c["divable"]);
                cell.touched = bool(c["touched"]);
            }
            next.cells.push_back(std::move(cell));
        }
    next.menu = list_of(parsed["menu"], true);
    next.items = list_of(parsed["items"], false);
    next.picker = list_of(parsed["picker"], true);
    next.peek = list_of(parsed["peek"], false);
    next.enum_picker = list_of(parsed["enumPicker"], false);
    if (const auto& p = parsed["preset"]; p.isObject())
        next.preset = {true, bool(p["entered"]), text(p["name"]), int(p["index"]), int(p["count"])};
    if (const auto& c = parsed["canvas"]; c.isObject())
        next.canvas = {true, bool(c["enterable"]), bool(c["entered"]), bool(c["opened"]), text(c["key"])};
    if (auto* pairs = parsed["footer"].getArray())
        for (const auto& pair : *pairs)
            if (auto* two = pair.getArray(); two && two->size() == 2) next.footer.emplace_back(text((*two)[0]), text((*two)[1]));
    next.notice = text(parsed["notice"]);
    if (const auto& h = parsed["hint"]; h.isObject()) {
        next.hint_title = text(h["title"]);
        if (auto* lines = h["lines"].getArray())
            for (const auto& line : *lines) next.hint.push_back(text(line));
    }
    next.wheel = bool(parsed["wheel"]);
    next.exit = bool(parsed["exit"]);
    next.live_press = bool(parsed["livePress"]);
    model = std::move(next);
}

} // namespace app
