// SPDX-License-Identifier: AGPL-3.0-only
#include "app/previews.h"
#include "app/melodee_app.h"
#include "move/display.h"
#include "move/runtime.h"
#include "move/tools.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <thread>

namespace app {
namespace {

bool save(MelodeeApp& melodee, const juce::File& folder, const char* name) {
    juce::Image image(juce::Image::ARGB, move::screen_width, move::screen_height, true);
    {
        juce::Graphics g(image);
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        melodee.draw(g);
    }
    auto file = folder.getChildFile(juce::String(name) + ".png");
    file.deleteFile();
    juce::FileOutputStream output(file);
    bool ok = output.openedOk() && juce::PNGImageFormat().writeImageToStream(move::frame_image(move::pack(image), 4), output);
    if (ok) std::cout << file.getFullPathName() << '\n';
    return ok;
}

} // namespace

int write_previews(const std::string& directory) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto samples = folder.getChildFile("samples");
    samples.createDirectory();
    auto loop = samples.getChildFile("Break 120.wav").getFullPathName().toStdString();
    if (!move::write_test_loop(loop)) return 1;

    MelodeeApp melodee(samples.getFullPathName().toStdString());
    move::Session session;
    melodee.attach(session);
    melodee.audio_opened(move::AudioFormat {});
    auto press = [&](move::Button button) {
        auto event = move::make_event(0xbf, move::button_cc(button), 127);
        move::track_input(session, event.input);
        melodee.surface(event);
    };
    auto send = [&](uint8_t status, uint8_t number, uint8_t value) {
        auto event = move::make_event(status, number, value);
        move::track_input(session, event.input);
        melodee.surface(event);
    };
    bool ok = save(melodee, folder, "empty");
    if (!melodee.load_now(loop, 0).empty()) return 1;
    ok = save(melodee, folder, "play") && ok;
    send(0x9f, 0, 127); // touch knob 1
    send(0xbf, move::knob_first_cc, 2);
    ok = save(melodee, folder, "touch") && ok;
    send(0x9f, 0, 0);
    press(move::Button::Menu);
    ok = save(melodee, folder, "load") && ok;
    press(move::Button::Menu);
    // Clear the touch's moment before the next screens.
    melodee.surface(move::make_event(0x9f, 0, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    press(move::Button::Loop);
    send(0xbf, move::main_knob_cc, 6);
    ok = save(melodee, folder, "chop") && ok;
    press(move::Button::MainPush);
    ok = save(melodee, folder, "to-pads") && ok;
    return ok ? 0 : 1;
}

} // namespace app
