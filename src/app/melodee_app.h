// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/bindings.h"
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
//                  (screens/tracks.cpp); held with the Volume knob, that
//                  track's volume, as on the Move
//   Mute           with a track button: mute it; Shift + Mute + track
//                  button: solo it; alone: mute the selected track (Move)
//   Play           play / stop (Move)
//   Shift + Step 5 / 6 / 7  tempo, metronome, groove (Move)
//   Volume knob    the main volume; Shift + Back goes back to Move (the runtime's)
//   A touched knob shows large, over any screen
//   Typing         a module's page asked for text: the pads and steps type
//
// and hands the rest to the screen on top. Pads sound from audio_event(),
// in the cycle they were read (Studio::audio_event()).
namespace app {

// Where the running Melodee listens for commands (move/remote.h): its
// folder's melodee-move.sock (`home` empty: /data/UserData/Melodee).
std::string control_path(const std::string& home);

class MelodeeApp : public move::App {
public:
    explicit MelodeeApp(std::string library = {}, std::string home = {}, std::string modules = {});
    ~MelodeeApp() override; // saves what is not saved yet

    void render(const move::AudioBlock& input, move::AudioBlock& output) override; // audio thread
    void audio_opened(const move::AudioFormat& format) override;                    // audio thread
    void audio_event(const move::SurfaceEvent& event) override;                     // audio thread
    void surface(const move::SurfaceEvent& event) override;                         // MIDI thread
    void midi(const move::ExternalMidi& message) override;                          // MIDI thread
    unsigned midi_out(move::ExternalMidi* out, unsigned room) override;             // audio thread
    void lights(move::Lights& lights, Clock::time_point now) override;              // MIDI thread
    void draw(juce::Graphics& g) override;                                          // screen thread
    void service() override;                                                        // service thread
    void stopping() override;                                                       // service thread, the run ending
    std::string status() const override;
    bool finished() const override; // Setup's Back to Move
    std::string command(const std::string& line) override; // melodee-render; the rest is the runtime's

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
    // The controls through the bindings table (src/app/bindings.h,
    // docs/MOVE_CONTROLS.md): each use of a control is a job, done by the
    // app (global), the screen on top, the track's Play screen under it, or
    // a screen that opens from it (trigger). Under `lock`.
    void act(const bindings::Use& use);
    bool global(bindings::Job job, const bindings::Use& use);
    bool trigger(bindings::Job job); // a screen that opens from this job, opened
    bindings::Where where() const;
    void settle();                   // Shift and the pads' flag, after any input or hold
    bool holding() const { return rows_down.any(); } // a track button is down
    unsigned held_track() const;     // the first track button down, as a track
    void draw_volume(juce::Graphics& g);
    void settings_lights(move::Lights& lights, Clock::time_point now); // Setup's pad lights and the light show
    Screen* tracks() const;            // the sixteen tracks, while a track button is held

    // Undo steps (Studio::close_step()): a press closes one; a turn keeps
    // its step open while the same control turns, until another control or
    // two seconds still.
    void close_gesture();
    std::string step_name(bindings::Job job, const bindings::Use& use) const;
    bool gesture_open = false;
    bindings::Control gesture_control = bindings::Control::Count;
    int gesture_index = 0;
    std::string gesture_name;
    Clock::time_point gesture_at {};

    // Setup's About, from the runtime's session; the usage log.
    Clock::time_point machine_at {};
    uint64_t machine_calls = 0, machine_ns = 0;
    Clock::time_point started = Clock::now(), usage_at {};
    long long started_unix = 0;
    std::string usage;
    size_t usage_written = 0;

    std::unique_ptr<Studio> own;
    bindings::Gestures gestures;
    std::bitset<4> rows_down;           // the track buttons held
    Clock::time_point held_at {};       // since when
    unsigned volume_track = 0;          // whose volume is shown
    int volume_loop = -1;               // on the Looper: the loop's (L1-L4), not a track's
    Clock::time_point volume_until {};  // a track's volume shown, for a moment
    bindings::Job last_job = bindings::Job::Nothing; // for the status line
    int touched = -1; // a touched knob, for a moment
    Clock::time_point touched_until {};
    // The main meter (the Move's VU meter): while the Volume knob is touched,
    // and a moment after it is turned or let go.
    Clock::time_point meter_until {};
    void draw_meter(juce::Graphics& g);
};

} // namespace app
