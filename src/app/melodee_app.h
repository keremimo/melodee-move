// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/studio.h"
#include "move/app.h"
#include <bitset>
#include <chrono>
#include <memory>
#include <string>

// Melodee on the Move (docs/UI.md): sixteen tracks on Melodee's own engine
// host, in the Rail design. The app is thin: the studio (studio.h) holds
// the project, the engine and what the screens share; each screen is a file
// of its own (screen.h, src/app/screens/). The app keeps what is the same
// on every screen:
//
//   Track buttons  the four tracks of the selected track's bank; one held
//                  shows the sixteen and makes the steps pick among them
//                  (screens/tracks.cpp)
//   Play           play / stop; Shift + Play the tempo
//   Master knob    the volume; Shift + Back goes back to Move (the runtime's)
//   A touched knob shows large, over any screen
//   Typing         a module's page asked for text: the pads and steps type
//
// and hands the rest to the screen on top. Pads sound from audio_event(),
// in the cycle they were read (Studio::audio_event()).
namespace app {

class MelodeeApp : public move::App {
public:
    explicit MelodeeApp(std::string library = {}, std::string home = {}, std::string modules = {});
    ~MelodeeApp() override; // saves what is not saved yet

    void render(const move::AudioBlock& input, move::AudioBlock& output) override; // audio thread
    void audio_opened(const move::AudioFormat& format) override;                    // audio thread
    void audio_event(const move::SurfaceEvent& event) override;                     // audio thread
    void surface(const move::SurfaceEvent& event) override;                         // MIDI thread
    void lights(move::Lights& lights, Clock::time_point now) override;              // MIDI thread
    void draw(juce::Graphics& g) override;                                          // screen thread
    void service() override;                                                        // service thread
    std::string status() const override;

    // For tests and tools.
    Studio& studio() { return *own; }
    std::string load_now(const std::string& path, unsigned pad) { return own->load_now(path, pad); }
    std::string save_now() { return own->save_now(); }
    std::string restore_now() { return own->restore(); }
    std::string load_module_now(const std::string& id) { return own->load_module_now(own->track(), id); }
    unsigned slice_count() const;                          // Chop's slices, as the status line says
    std::string synth_value(const std::string& key) const; // as the screen has it
    bool canvas_pixel(int x, int y) const;                 // the module's own page as last drawn, for tests
    std::string ask_module(const std::string& key);       // the module's answer now, for tests
    const std::string& project_path() const { return own->project_path(); }

private:
    void type(char c);
    void draw_typing(juce::Graphics& g);
    void draw_touched(juce::Graphics& g);
    bool trigger(move::Button button); // a screen that opens from this button, opened
    bool holding() const { return rows_down.any(); } // a track button is down
    Screen* tracks() const;            // the sixteen tracks, while a track button is held

    std::unique_ptr<Studio> own;
    std::bitset<4> rows_down;           // the track buttons held
    Clock::time_point held_at {};       // since when
    int touched = -1; // a touched knob, for a moment
    Clock::time_point touched_until {};
};

} // namespace app
