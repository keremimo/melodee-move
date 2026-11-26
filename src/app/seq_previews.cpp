// SPDX-License-Identifier: AGPL-3.0-only
// The sequencer's real screens as PNGs (docs/UI.md, "Sequencer"), reached
// as the hands reach them, to set beside its mockups (docs/ui/sequencer.png).
#include "app/melodee_app.h"
#include "app/previews.h"
#include "app/sequencer.h"
#include "move/display.h"
#include "move/runtime.h"
#include "move/tools.h"
#include <chrono>
#include <iostream>
#include <thread>

namespace app {
namespace {

bool save(MelodeeApp& melodee, const juce::File& folder, const char* name) {
    move::Lights lights;
    melodee.lights(lights, std::chrono::steady_clock::now());
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
    return output.openedOk() && juce::PNGImageFormat().writeImageToStream(move::frame_image(move::pack(image), 4), output);
}

} // namespace

bool write_sequencer_previews(const juce::File& folder) {
    folder.createDirectory();
    auto samples = folder.getChildFile("samples");
    samples.createDirectory();
    auto loop = samples.getChildFile("Kick 909.wav").getFullPathName().toStdString();
    if (!move::write_test_loop(loop)) return false;
    auto home = folder.getChildFile("home");
    home.deleteRecursively();
    MelodeeApp melodee(samples.getFullPathName().toStdString(), home.getFullPathName().toStdString(), folder.getChildFile("no-modules").getFullPathName().toStdString());
    move::Session session;
    melodee.attach(session);
    melodee.audio_opened(move::AudioFormat {});
    if (!melodee.load_now(loop, 0).empty()) return false;
    auto send = [&](uint8_t status, uint8_t number, uint8_t value) {
        auto event = move::make_event(status, number, value);
        move::track_input(session, event.input);
        if (event.input.kind == move::Input::Kind::Pad) melodee.audio_event(event);
        melodee.surface(event);
    };
    auto press = [&](move::Button b) { send(0xbf, move::button_cc(b), 127); send(0xbf, move::button_cc(b), 0); };
    auto step = [&](unsigned s, bool down) { send(down ? 0x9f : 0x8f, move::step_note(s), down ? 127 : 0); };
    auto shifted = [&](auto&& what) { send(0xbf, move::button_cc(move::Button::Shift), 127); what(); send(0xbf, move::button_cc(move::Button::Shift), 0); };
    auto rest = [] { std::this_thread::sleep_for(std::chrono::milliseconds(700)); };
    bool ok = true;
    auto& seq = melodee.studio().sequencer();
    // A pattern of four bars: the kick on 1, 4, 7, 11 of each.
    for (unsigned s : {0u, 3u, 6u, 10u}) { step(s, true); step(s, false); }
    {
        std::lock_guard<std::mutex> guard(melodee.studio().lock);
        auto& p = seq.pattern();
        const uint32_t bar = 4 * melodee::ticks_per_quarter;
        seq::set_length(p, 4 * bar, melodee::max_bars * bar);
        for (unsigned b = 1; b < 3; ++b) seq::copy_span(p, seq::bar_span(0, bar), seq::bar_span(b, bar), -1);
        p.events[1].chance = 75;
        p.events[1].condition = 1;
        p.events[1].ratchet = 3;
        p.events[1].flags = melodee::step_accent;
    }
    ok = save(melodee, folder, "play") && ok;
    step(3, true);
    ok = save(melodee, folder, "step-held") && ok;
    send(0xbf, move::knob_first_cc + 7, 120); // knob 8 down: a lock
    ok = save(melodee, folder, "step-lock") && ok;
    press(move::Button::MainPush);
    ok = save(melodee, folder, "step-trig") && ok;
    step(3, false);
    step(5, true);
    ok = save(melodee, folder, "step-empty") && ok;
    step(5, false);
    press(move::Button::Loop);
    step(1, true); step(1, false);
    ok = save(melodee, folder, "loop") && ok;
    press(move::Button::Loop);
    send(0xbf, move::button_cc(move::Button::Loop), 127);
    send(0xbf, move::main_knob_cc, 1);
    send(0xbf, move::button_cc(move::Button::Loop), 0);
    ok = save(melodee, folder, "loop-length") && ok;
    rest();
    press(move::Button::Menu);
    ok = save(melodee, folder, "session") && ok;
    send(0x9f, move::pad_note(24), 100);
    send(0x9f, move::pad_note(26), 100);
    send(0x8f, move::pad_note(26), 0);
    send(0x9f, move::pad_note(27), 100);
    send(0x8f, move::pad_note(27), 0);
    ok = save(melodee, folder, "session-chain") && ok;
    send(0x8f, move::pad_note(24), 0);
    press(move::Button::MainPush);
    ok = save(melodee, folder, "scene") && ok;
    press(move::Button::Back);
    press(move::Button::Menu);
    shifted([&] { step(2, true); step(2, false); });
    ok = save(melodee, folder, "workflow") && ok;
    press(move::Button::Back);
    shifted([&] { step(10, true); step(10, false); });
    ok = save(melodee, folder, "repeat") && ok;
    shifted([&] { step(10, true); step(10, false); });
    shifted([&] { step(4, true); step(4, false); });
    ok = save(melodee, folder, "tempo") && ok;
    press(move::Button::Back);
    rest();
    shifted([&] { press(move::Button::Loop); });
    const char* pages[] {"pattern", "entry", "roll", "motion", "mod", "macros", "feel", "follow", "lane"};
    for (const char* name : pages) {
        ok = save(melodee, folder, name) && ok;
        if (std::string(name) == "pattern") { press(move::Button::MainPush); ok = save(melodee, folder, "pattern-jobs") && ok; press(move::Button::Back); }
        shifted([&] { send(0xbf, move::main_knob_cc, 1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(1600)); // the page's name said, gone
    }
    press(move::Button::Back);
    // Rec from stopped: the count-in.
    press(move::Button::Rec);
    move::AudioBlock in {}, out {};
    for (int block = 0; block < 300; ++block) melodee.render(in, out);
    ok = save(melodee, folder, "count-in") && ok;
    press(move::Button::Play);
    for (int block = 0; block < 4; ++block) melodee.render(in, out);
    std::cout << folder.getFullPathName() << '\n';
    return ok;
}

} // namespace app
