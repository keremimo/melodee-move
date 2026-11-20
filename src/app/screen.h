// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/studio.h"
#include "move/app.h"
#include <juce_graphics/juce_graphics.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// One screen of Melodee on the Move (docs/UI.md): what it draws, what its
// controls do, its lights. Each screen is a file of its own in
// src/app/screens/, registered there with a RegisterScreen, so a new screen
// is a new file and touches no other: the registry makes one of each at
// start; a screen that opens from a button on a track's Play screen says
// so in its registration (a Trigger), and a track kind's Play screen says
// which kind it plays (a Home).
//
// Every call is under the studio's lock, on the MIDI thread (surface,
// lights) or the screen thread (draw).
namespace app {

struct Knob { std::string name, value; float fraction = 0; };

class Screen {
public:
    explicit Screen(Studio& studio) : studio(studio) {}
    virtual ~Screen() = default;
    const std::string& id() const { return own_id; }

    virtual void enter() {} // shown, over the Play screen or as it
    virtual void leave() {} // gone (closed, or the track changed)
    // A control the app has not taken (the track buttons, Play, the
    // master knob, Shift are the app's; a held track button takes the
    // steps). False: not this screen's, so a Trigger may open another.
    virtual bool surface(const move::Input& input) = 0;
    virtual void lights(move::Lights& lights) = 0;
    virtual void draw(juce::Graphics& g) = 0;
    // Knob n (0-7) as a touch shows it large; an empty name: not in use.
    virtual Knob knob(unsigned) const { return {}; }
    // What the touched knob's rail says under its number.
    virtual std::string knob_owner() const { return {}; }
    // Fields for the status line (" key=value ..."), whether shown or not.
    virtual std::string status() const { return {}; }

protected:
    Studio& studio;

private:
    friend struct RegisterScreen;
    friend class Studio;
    std::string own_id;
};

// When a screen opens from a button: the button (with Shift or not), on
// tracks of `kinds` (a bit a TrackKind; 0: any), wherever the screen on top
// does not use that button itself. It opens over the track's Play screen.
struct Trigger {
    move::Button button = move::Button::Count;
    bool shift = false;
    uint32_t kinds = 0;
};
constexpr uint32_t kind_bit(TrackKind kind) { return 1u << unsigned(kind); }

using ScreenFactory = std::function<std::unique_ptr<Screen>(Studio&)>;
struct ScreenEntry {
    std::string id;
    ScreenFactory make;
    std::vector<Trigger> triggers;
    uint32_t home_of = 0; // the Play screen of these track kinds
};
const std::vector<ScreenEntry>& registered_screens();

// In a screen's own file:
//   static RegisterScreen chop("chop", make<ChopScreen>, {{move::Button::Loop, false, kind_bit(TrackKind::Kit)}});
struct RegisterScreen {
    RegisterScreen(std::string id, ScreenFactory make, std::vector<Trigger> triggers = {}, uint32_t home_of = 0);
};
template <class T> std::unique_ptr<Screen> make(Studio& studio) { return std::make_unique<T>(studio); }

// The Play screen for a track kind.
std::string home_screen(TrackKind kind);

} // namespace app
