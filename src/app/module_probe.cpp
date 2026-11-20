// SPDX-License-Identifier: AGPL-3.0-only
#include "app/module_probe.h"
#include "app/canvas_page.h"
#include "app/module_host.h"
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
            std::map<std::string, std::string> values;
            for (const auto& key : canvas.extra_keys) values[key] = module.get_now(key);
            juce::Image screen(juce::Image::ARGB, CanvasPage::width, CanvasPage::height, true);
            {
                juce::Graphics g(screen);
                page->draw(g, values);
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
        for (size_t n = 0; n < module->pages.size() && n < 3; ++n) {
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

} // namespace app
