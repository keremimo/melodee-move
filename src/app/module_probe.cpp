// SPDX-License-Identifier: AGPL-3.0-only
#include "app/module_probe.h"
#include "app/canvas_page.h"
#include "app/module_host.h"
#include "app/schwung_pages.h"
#include <map>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace app {

namespace {

bool save_screen(const juce::Image& image, const std::string& path) {
    juce::Image large = image.rescaled(image.getWidth() * 4, image.getHeight() * 4, juce::Graphics::lowResamplingQuality);
    juce::File file(path);
    file.deleteFile();
    juce::FileOutputStream out(file);
    return out.openedOk() && juce::PNGImageFormat().writeImageToStream(large, out);
}

// The module's own pages, as Melodee would show them, with no audio thread:
// their reads and sets go to the module directly.
void probe_pages(LoadedModule& module, const std::string& pictures) {
    for (const auto& canvas : module.canvases) {
        CanvasPage::Host host;
        host.get = [&](const std::string& key) { return module.get_now(key); };
        host.set = [&](const std::string& key, const std::string& value) { module.set_now(key, value); };
        host.text_entry = [&](const std::string& title, const std::string&) { std::printf("    page asks for text: %s\n", title.c_str()); };
        host.open_file = [&](const std::string& path) { std::printf("    page hands over %s\n", path.c_str()); };
        std::string error;
        auto page = CanvasPage::load(canvas.script, host, error);
        if (!page) { std::printf("    page %s FAILED: %s\n", canvas.key.c_str(), error.c_str()); continue; }
        auto draw = [&](const char* moment) {
            CanvasPage::Frame frame;
            frame.key = canvas.key;
            for (const auto& one : module.pages)
                if (one.canvas >= 0 && module.canvases[size_t(one.canvas)].key == canvas.key) { frame.keys = one.knobs; break; }
            std::map<std::string, std::string> values;
            for (const auto& key : frame.keys)
                if (!key.empty()) values[key] = module.get_now(key);
            for (const auto& key : canvas.extra_keys) values[key] = module.get_now(key);
            juce::Image screen(juce::Image::ARGB, CanvasPage::width, CanvasPage::height, true);
            {
                juce::Graphics g(screen);
                page->draw(g, values, frame);
            }
            auto path = pictures + "/" + module.info().id + "-" + canvas.key + "-" + moment + ".png";
            if (save_screen(screen, path)) std::printf("    page %s: %s\n", canvas.key.c_str(), path.c_str());
        };
        draw("first");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        draw("later");
        page->midi(0xb0, 14, 1); // entered: the first gesture proves it
        page->midi(0xb0, 14, 1);
        draw("entered");
    }
}

} // namespace

int probe_modules(const std::string& root, const std::string& pictures) {
    using Clock = std::chrono::steady_clock;
    auto found = scan_modules(root);
    std::printf("%zu sound generators in %s\n", found.size(), root.c_str());
    int failed = 0;
    for (const auto& info : found) {
        std::printf("%-14s api %d  ", info.id.c_str(), info.api);
        std::fflush(stdout);
        if (info.api != 2) { std::printf("skipped: older API\n"); continue; }
        auto started = Clock::now();
        std::string error;
        auto module = LoadedModule::load(info, error);
        double load_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        if (!module) { std::printf("FAILED: %s\n", error.c_str()); ++failed; continue; }
        unsigned knobs = 0;
        for (const auto& page : module->pages) knobs += unsigned(page.knobs.size());
        std::printf("loaded in %.0f ms; %zu params, %zu pages, %u knobs", load_ms, module->params.size(), module->pages.size(), knobs);
        if (!module->preset_param.empty()) std::printf(", %d presets (%s)", module->preset_count, module->preset_name.c_str());
        std::printf("\n");
        if (!pictures.empty()) probe_pages(*module, pictures);
        for (size_t n = 0; n < module->pages.size(); ++n) {
            std::printf("    %-12s", module->pages[n].name.c_str());
            for (const auto& key : module->pages[n].knobs) {
                const ModuleParam* param = module->param(key);
                std::printf(" %s=%s", key.c_str(), param ? param->shown().c_str() : "?");
            }
            std::printf("\n");
        }
        // A second for a module that finishes loading on a thread of its own,
        // then a chord of three notes for a second and let go, as the audio
        // thread plays it, timed.
        std::this_thread::sleep_for(std::chrono::seconds(1));
        SynthPlayer player;
        player.set_module(module.get());
        move::AudioBlock input {}, output {};
        double slowest = 0, total = 0;
        int32_t peak = 0, tail = 0;
        const int blocks = 600;
        for (int block = 0; block < blocks; ++block) {
            auto begun = Clock::now();
            if (block == 10) for (int note : {48, 55, 64}) player.note_on(note, 100);
            if (block == 360) for (int note : {48, 55, 64}) player.note_off(note);
            output.fill(0);
            player.render(input, output, 1.0f);
            double us = std::chrono::duration<double, std::micro>(Clock::now() - begun).count();
            if (block > 10) slowest = std::max(slowest, us);
            total += us;
            for (int32_t sample : output) {
                int32_t level = sample == INT32_MIN ? INT32_MAX : std::abs(sample);
                peak = std::max(peak, level);
                if (block >= blocks - 20) tail = std::max(tail, level);
            }
        }
        std::printf("    played: peak %.3f, after release %.3f; render %.0f us mean, %.0f us slowest of 2902\n",
                    double(peak) / 2147483648.0, double(tail) / 2147483648.0, total / blocks, slowest);
        player.set_module(nullptr);
        player.render(input, output, 1.0f);
        if (peak == 0) ++failed;
    }
    return failed ? 1 : 0;
}

int probe_schwung_pages(const std::string& root, const std::string& id, const std::string& pictures) {
    using Clock = std::chrono::steady_clock;
    auto found = scan_modules(root);
    auto info = std::find_if(found.begin(), found.end(), [&](const ModuleInfo& one) { return one.id == id; });
    if (info == found.end()) { std::printf("no module %s in %s\n", id.c_str(), root.c_str()); return 1; }
    std::string error;
    auto module = LoadedModule::load(*info, error);
    if (!module) { std::printf("%s FAILED: %s\n", id.c_str(), error.c_str()); return 1; }
    CanvasPage::Host host;
    host.get = [&](const std::string& key) {
        auto answer = module->get_now(key);
        return answer.empty() && (key == "ui_hierarchy" || key == "chain_params") ? module->declared(key) : answer;
    };
    host.set = [&](const std::string& key, const std::string& value) { module->set_now(key, value); };
    host.text_entry = [&](const std::string& title, const std::string& initial) { std::printf("    keyboard: %s [%s]\n", title.c_str(), initial.c_str()); };
    host.open_file = [](const std::string&) {};
    auto started = Clock::now();
    auto pages = SchwungPages::load(host, info->dir, "[]", [](const std::string& action) { std::printf("    action: %s\n", action.c_str()); }, error);
    double load_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    if (!pages) { std::printf("Schwung's pages FAILED: %s\n", error.c_str()); return 1; }
    std::printf("%s: Schwung's page code loaded in %.0f ms\n", id.c_str(), load_ms);

    int shot = 0, draws = 0;
    double slowest_draw = 0, total_draw = 0;
    auto frame = [&](int count, const char* name) { // `count` screens, 23 ms apart, as Melodee's screen thread draws; the last kept
        for (int n = 0; n < count; ++n) {
            juce::Image screen(juce::Image::ARGB, CanvasPage::width, CanvasPage::height, true);
            auto t0 = Clock::now();
            {
                juce::Graphics g(screen);
                pages->draw(g);
            }
            double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            slowest_draw = std::max(slowest_draw, ms);
            total_draw += ms;
            ++draws;
            if (n == count - 1 && name && *name) {
                char file[512];
                std::snprintf(file, sizeof file, "%s/%s-%02d-%s.png", pictures.c_str(), id.c_str(), shot++, name);
                save_screen(screen, file);
                std::printf("  %-12s %s, page %d of %d%s\n", name, pages->page_name().c_str(), pages->page_index() + 1, pages->page_count(),
                            pages->wants_wheel() ? ", has the wheel" : "");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(23));
        }
    };
    auto cc = [&](int number, int value) { pages->midi(0xb0, uint8_t(number), uint8_t(value)); };
    auto note = [&](int number, bool on) { pages->midi(on ? 0x90 : 0x80, uint8_t(number), on ? 127 : 0); };

    frame(60, "first"); // the contract read, values in over the first ticks
    for (int n = 0; n < int(pages->page_count()); ++n) { cc(14, 1); frame(25, "jog"); } // every page
    note(0, true); for (int n = 0; n < 6; ++n) cc(71, 1); frame(10, "knob1"); note(0, false); frame(40, "released");
    cc(3, 127); cc(3, 0); frame(25, "push");  // its sections
    cc(14, 1); frame(10, "menu-turn");
    cc(51, 127); cc(51, 0); frame(25, "back");
    std::printf("draws: %d, %.1f ms mean, %.1f ms slowest (Schwung's tick + render + Melodee's ctx)\n", draws, total_draw / draws, slowest_draw);
    return 0;
}

} // namespace app
