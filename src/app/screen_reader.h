// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <chrono>
#include <string>

// The screen reader (Schwung's, through move::Speech): what it says of
// Melodee. Each service tick the app tells it what the screen shows; it says
// what changed once things have been still for a moment (Schwung's
// debounce, 300 ms: a knob turning says its last value):
//
//   at the start  "Melodee." the view, the track, and how to quit
//   a new view    the view and the track ("Tempo. Track 2, synth.")
//   a new track   the track
//   a knob        its name and value, touched or turned ("Tune, +2 ST.")
//   a message     what the screen's message line says ("Saved.")
//
// Text only: no thread, no lock, no clock of its own.
namespace app {

class ScreenReader {
public:
    using Clock = std::chrono::steady_clock;
    struct View {
        std::string view;    // "Tempo", "Play"
        std::string track;   // "Track 2, synth"
        std::string knob;    // "Tune, +2 ST"; empty: no knob in hand
        std::string message; // the message line; empty: none
        bool operator==(const View& other) const {
            return view == other.view && track == other.track && knob == other.knob && message == other.message;
        }
    };
    static constexpr auto debounce = std::chrono::milliseconds(300);
    static constexpr const char* quit_hint = "Shift and Back goes back to Move.";

    // What to say now, or nothing.
    std::string next(const View& now, Clock::time_point at);

    // A screen's id (src/app/screens/) as it is said; a track's Play screen
    // is "Play", an id without a name of its own is its words.
    static std::string view_name(const std::string& screen_id, bool play);
    // Words as eSpeak says them best: capitals of four letters or more in
    // lower case (TUNE, LEVEL), shorter ones kept to be spelled (DB, ST,
    // FX), spaces collapsed.
    static std::string speakable(const std::string& text);

private:
    bool started = false;
    View said, seen;
    Clock::time_point changed {};
};

} // namespace app
