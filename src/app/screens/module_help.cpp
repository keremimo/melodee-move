// SPDX-License-Identifier: AGPL-3.0-only
// A module's help (Module › Module Help on its pages): its help.json, as
// Schwung shows it (a tree of titles, each ending in lines of text), in
// Melodee's look. The wheel walks the titles or scrolls the text, the push
// opens a title, Back goes up one, then back to the pages. The text, written
// for Schwung's 128-pixel lines, is wrapped again to Melodee's narrower ones.
#include "app/screens/common.h"
#include <juce_core/juce_core.h>
#include <fstream>
#include <iterator>
#include <sstream>

namespace app::screens {
namespace {

struct HelpNode {
    std::string title;
    std::vector<std::string> lines; // a leaf's, wrapped
    std::vector<HelpNode> children;
};

// Paragraphs (lines up to a blank one) joined, then wrapped to `width`.
std::vector<std::string> wrap(const std::vector<std::string>& lines, size_t width) {
    std::vector<std::string> out;
    std::string paragraph;
    auto flush = [&] {
        std::istringstream words(paragraph);
        std::string word, line;
        while (words >> word) {
            if (!line.empty() && line.size() + 1 + word.size() > width) { out.push_back(line); line.clear(); }
            line += (line.empty() ? "" : " ") + word;
        }
        if (!line.empty()) out.push_back(line);
        paragraph.clear();
    };
    for (const auto& line : lines) {
        if (line.empty()) { flush(); out.emplace_back(); continue; }
        paragraph += (paragraph.empty() ? "" : " ") + line;
    }
    flush();
    return out;
}

HelpNode node_of(const juce::var& from, size_t width) {
    HelpNode node;
    node.title = from["title"].toString().toStdString();
    if (auto* lines = from["lines"].getArray()) {
        std::vector<std::string> raw;
        for (const auto& line : *lines) raw.push_back(line.toString().toStdString());
        node.lines = wrap(raw, width);
    }
    if (auto* children = from["children"].getArray())
        for (const auto& child : *children) node.children.push_back(node_of(child, width));
    return node;
}

class ModuleHelp final : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        root = {};
        path.clear();
        const LoadedModule* module = target_module();
        if (!module) return;
        std::ifstream in(module->info().dir + "/help.json", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto parsed = juce::JSON::parse(juce::String(text));
        if (parsed.isObject()) root = node_of(parsed, size_t((ui::content_width - 4) / move::glyph_advance));
        path.push_back({0, 0});
    }

    bindings::Screen screen_kind() const override { return bindings::Screen::List; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        if (path.empty()) { if (job == Job::Back) studio.close(); return job == Job::Back; }
        const HelpNode& at = current();
        auto& place = path.back();
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: {
            int count = at.children.empty() ? std::max(0, int(at.lines.size()) - visible_lines()) + 1 : int(at.children.size());
            place.cursor = std::clamp(place.cursor + (use.delta > 0 ? 1 : -1), 0, std::max(0, count - 1));
            return true;
        }
        case Job::ListPush:
            if (!at.children.empty() && place.cursor < int(at.children.size())) path.push_back({place.cursor, 0});
            return true;
        case Job::Back:
            path.pop_back();
            if (path.empty()) studio.close();
            return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        for (unsigned at = 0; at < pad_count; ++at) lights.pad(at, move::colour::off);
        lights.button(move::Button::Back, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        const LoadedModule* module = target_module();
        ui::rail(g, module ? upper(module->info().abbrev).substr(0, 4) : "SYN", std::to_string(studio.pages_target.track + 1), "HELP");
        if (path.empty() || (root.children.empty() && root.lines.empty())) {
            move::draw_text_centred(g, ui::content_x, 24, ui::content_width, "NO HELP");
            ui::detail(g, "BACK OUT");
            return;
        }
        const HelpNode& at = current();
        g.setColour(juce::Colours::white);
        g.fillRect(ui::content_x, 0, ui::content_width, ui::header_height);
        g.setColour(juce::Colours::black);
        move::draw_text(g, ui::content_x + 2, 1, upper(path.size() == 1 ? "HELP: " + at.title : at.title).substr(0, 16));
        g.setColour(juce::Colours::white);
        const auto area = juce::Rectangle<int>(ui::content_x, ui::header_height + 2, ui::content_width, ui::subject_height - ui::header_height - 2);
        if (!at.children.empty()) {
            std::vector<std::string> rows;
            for (const auto& child : at.children) rows.push_back(upper(child.title).substr(0, 14) + (child.children.empty() ? "" : " >"));
            ui::list(g, area, rows, path.back().cursor, 1);
            ui::detail(g, "JOG SEL CLK OPEN");
            return;
        }
        int first = path.back().cursor, y = area.getY();
        for (int n = first; n < int(at.lines.size()) && n < first + visible_lines(); ++n, y += move::glyph_height + 2)
            move::draw_text(g, area.getX() + 2, y, upper(at.lines[size_t(n)]));
        ui::detail(g, "JOG SCRL BACK OUT");
    }

private:
    struct Place { int child = 0, cursor = 0; };
    HelpNode root;
    std::vector<Place> path; // the root's place first, then each title opened

    const HelpNode& current() const {
        const HelpNode* node = &root;
        for (size_t n = 1; n < path.size(); ++n) node = &node->children[size_t(path[n].child)];
        return *node;
    }
    static int visible_lines() { return (ui::subject_height - ui::header_height - 2) / (move::glyph_height + 2); }
    const LoadedModule* target_module() const {
        const auto& target = studio.pages_target;
        if (target.fx_entry != melodee::no_plugin) {
            FxModule* fx = const_cast<Studio&>(studio).fx_module(target.fx_entry);
            return fx ? fx->module.get() : nullptr;
        }
        ModuleTrack* track = const_cast<Studio&>(studio).module_track(target.track);
        return track ? track->module.get() : nullptr;
    }
};

RegisterScreen registered("module-help", make<ModuleHelp>);

} // namespace
} // namespace app::screens
