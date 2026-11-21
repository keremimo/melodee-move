// SPDX-License-Identifier: AGPL-3.0-only
#include "app/previews.h"
#include "app/melodee_app.h"
#include "move/display.h"
#include "move/runtime.h"
#include "move/tools.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include "app/module_host.h"
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

int write_previews(const std::string& directory, const std::string& modules) {
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    auto samples = folder.getChildFile("samples");
    samples.createDirectory();
    auto loop = samples.getChildFile("Break 120.wav").getFullPathName().toStdString();
    if (!move::write_test_loop(loop)) return 1;

    auto home = folder.getChildFile("home");
    home.deleteRecursively(); // a fresh project each time
    MelodeeApp melodee(samples.getFullPathName().toStdString(), home.getFullPathName().toStdString(),
                       modules.empty() ? folder.getChildFile("no-modules").getFullPathName().toStdString() : modules);
    move::Session session;
    melodee.attach(session);
    melodee.audio_opened(move::AudioFormat {});
    auto send = [&](uint8_t status, uint8_t number, uint8_t value) {
        auto event = move::make_event(status, number, value);
        move::track_input(session, event.input);
        melodee.surface(event);
    };
    // A button pressed and let go, as a hand does (docs/MOVE_CONTROLS.md).
    auto press = [&](move::Button button) {
        send(0xbf, move::button_cc(button), 127);
        send(0xbf, move::button_cc(button), 0);
    };
    auto shifted = [&](move::Button button) {
        send(0xbf, move::button_cc(move::Button::Shift), 127);
        press(button);
        send(0xbf, move::button_cc(move::Button::Shift), 0);
    };
    bool ok = save(melodee, folder, "empty");
    if (!melodee.load_now(loop, 0).empty()) return 1;
    ok = save(melodee, folder, "play") && ok;
    send(0x9f, 0, 127); // touch knob 1
    send(0xbf, move::knob_first_cc, 2);
    ok = save(melodee, folder, "touch") && ok;
    send(0x9f, 0, 0);
    press(move::Button::MainPush); // the Device View's pad: Load
    ok = save(melodee, folder, "load") && ok;
    press(move::Button::Back);
    // Clear the touch's moment before the next screens.
    melodee.surface(move::make_event(0x9f, 0, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    shifted(move::Button::MainPush); // Sample Options
    ok = save(melodee, folder, "sample-options") && ok;
    press(move::Button::MainPush);   // Chop
    for (int n = 0; n < 6; ++n) press(move::Button::Right);
    ok = save(melodee, folder, "chop") && ok;
    press(move::Button::MainPush);
    ok = save(melodee, folder, "to-pads") && ok;

    // The tracks: a track button held (the sixteen; with the Volume knob,
    // the track's volume), the Move's tempo, metronome and groove (Shift +
    // Steps 5-7), an empty track and its new-track sheet.
    press(move::Button::Back);
    press(move::Button::Back);
    send(0xbf, move::button_cc(move::Button::Row1), 127);
    std::this_thread::sleep_for(std::chrono::milliseconds(450)); // held: the sixteen
    ok = save(melodee, folder, "tracks") && ok;
    send(0xbf, move::master_knob_cc, 127);
    ok = save(melodee, folder, "track-volume") && ok;
    send(0xbf, move::button_cc(move::Button::Row1), 0);
    auto shift_step = [&](unsigned step, const char* name) {
        send(0xbf, move::button_cc(move::Button::Shift), 127);
        send(0x9f, move::step_note(step), 127);
        send(0x8f, move::step_note(step), 0);
        send(0xbf, move::button_cc(move::Button::Shift), 0);
        bool saved = save(melodee, folder, name);
        press(move::Button::Back);
        return saved;
    };
    ok = shift_step(4, "tempo") && ok;
    ok = shift_step(5, "metronome") && ok;
    ok = shift_step(6, "groove") && ok;
    press(move::Button::Row4);
    ok = save(melodee, folder, "track-empty") && ok;
    press(move::Button::MainPush);
    ok = save(melodee, folder, "new-track") && ok;
    press(move::Button::Back);
    press(move::Button::Row1);
    if (modules.empty()) return ok ? 0 : 1;

    // The synth track: empty, its modules, the first one loaded, a knob.
    press(move::Button::Back);
    press(move::Button::Back);
    press(move::Button::Row2);
    ok = save(melodee, folder, "synth-empty") && ok;
    press(move::Button::MainPush); // no module yet: the push lists them
    ok = save(melodee, folder, "synth-modules") && ok;
    press(move::Button::Back);
    std::string first;
    for (const auto& info : scan_modules(modules))
        if (info.api == 2) { first = info.id; break; }
    if (first.empty() || !melodee.load_module_now(first).empty()) { std::cerr << "no module to preview in " << modules << '\n'; return 1; }
    std::this_thread::sleep_for(std::chrono::milliseconds(1600)); // the module's name, said, is gone
    ok = save(melodee, folder, "synth") && ok;
    send(0x9f, 1, 127); // touch knob 2
    ok = save(melodee, folder, "synth-touch") && ok;
    send(0x9f, 1, 0);
    // Its menu (Shift + the wheel's push), a value being edited, a list to choose from.
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    shifted(move::Button::MainPush);
    send(0xbf, move::main_knob_cc, 2);
    ok = save(melodee, folder, "synth-menu") && ok;
    press(move::Button::MainPush);
    ok = save(melodee, folder, "synth-edit") && ok;
    press(move::Button::MainPush);
    send(0xbf, move::main_knob_cc, 2);
    press(move::Button::MainPush);
    move::AudioBlock in {}, out {};
    melodee.render(in, out); // a list is read on the audio thread
    melodee.service();
    ok = save(melodee, folder, "synth-items") && ok;
    return ok ? 0 : 1;
}

} // namespace app
