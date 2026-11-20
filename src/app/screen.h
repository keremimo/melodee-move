// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/bindings.h"
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
    // Where the controls are, as docs/MOVE_CONTROLS.md's map names its
    // screens: a track's Play screen is Play, a list the wheel walks List,
    // a browser Browser. It may change with what the screen shows (a
    // module's menu is a List, its own page a Canvas).
    virtual bindings::Screen screen_kind() const { return bindings::Screen::Play; }
    // A job of the bindings table (src/app/bindings.h), the control's use
    // and where it came from. The app takes the jobs that are the same
    // everywhere (the track buttons, Play, the volume knob, typing); the
    // rest come here, then to the track's Play screen under this one.
    // False: not this screen's, so a Trigger may open another.
    virtual bool job(bindings::Job job, const bindings::Use& use) = 0;
    // Every input as it comes, before its jobs: for what a screen shows of
    // controls held (pads down), never to act on.
    virtual void input(const move::Input&) {}
    virtual void lights(move::Lights& lights) = 0;
    virtual void draw(juce::Graphics& g) = 0;
    // Knob n (0-7) as a touch shows it large; an empty name: not in use.
    virtual Knob knob(unsigned) const { return {}; }
    // What the touched knob's rail says under its number.
    virtual std::string knob_owner() const { return {}; }
    // The detail line under knob n shown large; empty: none.
    virtual std::string knob_detail(unsigned) const { return {}; }
    // Fields for the status line (" key=value ..."), whether shown or not.
    virtual std::string status() const { return {}; }

protected:
    Studio& studio;

private:
    friend struct RegisterScreen;
    friend class Studio;
    std::string own_id;
};

// When a screen opens from a job of the bindings table (Shift + Step 5:
// Job::Tempo), on tracks of `kinds` (a bit a TrackKind; 0: any), wherever
// the screens do not take that job themselves. It opens over the track's
// Play screen.
struct Trigger {
    bindings::Job job = bindings::Job::Nothing;
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
//   static RegisterScreen tempo("tempo", make<Tempo>, {{bindings::Job::Tempo}});
struct RegisterScreen {
    RegisterScreen(std::string id, ScreenFactory make, std::vector<Trigger> triggers = {}, uint32_t home_of = 0);
};
template <class T> std::unique_ptr<Screen> make(Studio& studio) { return std::make_unique<T>(studio); }

// The Play screen for a track kind.
std::string home_screen(TrackKind kind);

} // namespace app
