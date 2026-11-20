// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/controls.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

// The Move's controls as Ableton made them, and Melodee's jobs on the ones
// the Move leaves free: docs/MOVE_CONTROLS.md as one table. A row is a
// control, used one way (a press, a tap, a hold, a double press, a turn),
// with some modifiers held, on a screen and a kind of track; it names the
// job that use does there. `build/controls-tests` reads the doc's map and
// checks it against this table, row for row (tests/controls_tests.cpp).
//
// The table never sits between a pad and its sound: pads go to
// App::audio_event() in the cycle they were read. The MIDI thread asks
// pad_sounds() whenever what is held or shown changes and tells the audio
// thread through one atomic whether a pad press should sound.
//
// Wiring (MIDI thread, under the studio's lock): Gestures::feed() each
// input with where the app is; resolve() each use it returns; do the job.
// Gestures::tick() every lights() call, for holds that came due.
namespace app::bindings {

// What is on the screen. Screens over Play keep Play's controls for what
// they do not take (`under`); Typing takes everything but the rows marked
// Everywhere.
enum class Screen : uint8_t {
    Any,        // a row for every screen but Typing
    Everywhere, // a row for every screen, Typing too
    Play,       // Note Mode: the selected track's Device View
    Loop,       // Loop Mode
    Session,    // the Patterns grid
    Overview,   // the projects
    Browser,    // a list the wheel's push opened: samples, modules, presets, track types
    List,       // a menu or sheet: Setup, Sample Options, a module's menu, track options
    Chop,
    ToPads,
    Sampling,
    Typing,
    Canvas,     // a module's own page, entered
    Tracks,     // the sixteen tracks, latched (Shift + Note/Session)
    Count
};

// The selected track's kind, as far as the controls care.
enum class Track : uint8_t {
    Any,
    Kit,     // Melodee's sampler kit
    Pitches, // a kit with 16 Pitches on (Shift + Step 8): its rows fall back to Kit's
    Synth,   // a note track: a Schwung synth, a keygroup, MIDI, a plug-in
    Audio,
    Count
};

enum class Control : uint8_t {
    Pad, PadPressure, Step, TrackButton, Knob, KnobTouch, Volume, VolumeTouch,
    WheelTurn, WheelPush, WheelTouch,
    Shift, NoteSession, Back, Capture, Sampling, Loop, Mute, Undo, Copy, Delete,
    Plus, Minus, Left, Right, Play, Rec,
    Count
};

// How a control is used. Every press is a Press; a button let go within
// hold_time with nothing else used meanwhile is also a Tap; held past
// hold_time unused, a Hold; pressed again within double_time, a Double.
// A row reacts to one of them, so a control does either its Press job or
// its Tap job in one place, never both (checked by the tests).
enum class Gesture : uint8_t { Press, Tap, Hold, Double, Turn, Count };

// What is held down while the control is used.
namespace held {
constexpr uint16_t none = 0, shift = 1 << 0, track = 1 << 1, step = 1 << 2, pad = 1 << 3,
                   copy = 1 << 4, del = 1 << 5, mute = 1 << 6, loop = 1 << 7;
}

#define MELODEE_JOBS(X) \
    X(Nothing) \
    X(PlayPad) X(PlayNote) X(PlayPitch) X(Aftertouch) X(SelectPadQuietly) X(PlaySlice) X(TypeKey) X(RecordPad) \
    X(LaunchPattern) X(PasteBounced) X(SelectProject) X(ProjectOptions) X(PadLevel) X(ProjectVolume) \
    X(CopyPad) X(CopyPattern) X(CopyProject) X(ClearPad) X(DeletePattern) X(DeleteProject) X(RemoveCut) X(MutePad) \
    X(NoteOnStep) X(NoteOnBar) X(QuantizePad) \
    X(ToggleStep) X(StepVelocity) X(StepLength) X(StepLengthTo) X(TransposeStep) X(TransposeStepOctave) \
    X(NudgeStep) X(NudgeStepFine) X(NudgeStepWhole) X(StepMotion) X(SelectBar) X(LoopRange) X(OneBarLoop) \
    X(BarLength) X(BarVelocity) X(TransposeBar) X(NudgeBar) X(CopyStep) X(CopyBar) X(ClearBar) X(PlayFromPart) \
    X(TypeDigit) X(PickTrack) \
    X(Projects) X(Setup) X(Workflow) X(Mixer) X(Tempo) X(Metronome) X(Groove) X(SixteenPitches) X(KeysAndScales) \
    X(FullVelocity) X(Repeat) X(Looper) X(Song) X(NewPattern) X(DoubleLoop) X(Quantize) \
    X(SelectTrack) X(PeekTrack) X(TrackLevel) X(TrackOptions) X(MuteTrack) X(SoloTrack) X(CopyTrack) X(ClearTrack) \
    X(TurnKnob) X(ShowKnob) X(TurnKnobFine) X(SecondParameter) X(DeleteMotion) X(ToggleMotion) X(TurnMainEffect) \
    X(MainVolume) X(ShowMeter) \
    X(Focus) X(OpenBrowser) X(SampleOptions) X(ModuleMenu) X(Page) X(ShowFocus) X(BrowseTurn) X(BrowsePush) \
    X(BrowserOptions) X(ListTurn) X(ListTurnFine) X(ListPush) X(ChopAmount) X(ToPads) X(ToPadsChoose) X(ToPadsPlace) \
    X(InputTurn) X(InputPush) X(ShowSource) X(CanvasTurn) X(CanvasPush) X(TypeDone) X(LoopLength) X(LoopLengthFine) \
    X(PlayStop) X(Restart) X(PreviewProject) X(Record) X(RecordAudio) \
    X(Recall) X(ClearRecall) X(SamplingMode) X(AudioRecall) \
    X(LoopMode) X(MuteSelected) X(Undo) X(Redo) X(UndoList) X(DuplicatePattern) X(DeleteCurrentPattern) X(TypeBackspace) \
    X(ToggleSession) X(PeekSession) X(TracksOverview) X(Back) X(BackToPlay) X(LeaveChop) X(CanvasBack) X(TypeCancel) \
    X(Quit) X(ShiftSteps) X(ShiftLock) X(Uppercase) \
    X(Octave) X(PadBank) X(PitchesOctave) X(ScaleStep) X(TrackBank) X(SlicePage) X(Bar) X(Bar16) X(PatternPage) X(SliceStep)

enum class Job : uint8_t {
#define MELODEE_JOB_ENUM(name) name,
    MELODEE_JOBS(MELODEE_JOB_ENUM)
#undef MELODEE_JOB_ENUM
    Count
};
const char* job_name(Job job);

// Where a row stands, as docs/MOVE_CONTROLS.md's Status column says.
enum class Status : uint8_t { Done, Next, Core, Wave2, Free };
const char* status_name(Status status); // "done", "next", "core", "wave 2", "free"

struct Binding {
    const char* doc;        // the doc's Control cell, word for word
    Screen screen;
    Track track;
    uint16_t held;          // exactly these held (held::...)
    Control control;
    Gesture gesture;
    Job job;
    Status status;
    const char* area = "";  // a Wave2 row's area: "sequencer", "sampling", ...
    uint32_t only = 0;      // pads or steps: a bit an index; 0 for any
    int8_t step_held = -1;  // with held::step: which step is held (0-15); -1 any
};
const std::vector<Binding>& table();

struct Where {
    Screen screen = Screen::Play;
    Track track = Track::Kit;
};
struct Use {
    Control control = Control::Pad;
    int index = 0;          // the pad, step, knob or track button
    Gesture gesture = Gesture::Press;
    uint16_t held = held::none;
    int held_step = -1;     // the first step held, if any
    int delta = 0;          // a turn's detents
};

// The screen whose controls `screen` keeps for what it does not take;
// itself where none.
Screen under(Screen screen);
// The row that answers `use` at `where`, or null: the screen's own rows,
// then the screens under it, then Any (not in Typing), then Everywhere;
// within each, the track's own rows before Any track's (Pitches before Kit).
const Binding* find(const Where& where, const Use& use);
Job resolve(const Where& where, const Use& use);
// Whether a pad pressed now plays its sound (the audio thread's flag):
// false only where a row gives the pad another job.
bool pad_sounds(const Where& where, uint16_t held, unsigned pad);
bool plays(Job job);

// Control and index for a decoded input; Control::Count for none (the
// audio sense CCs). Pressure, knobs and the wheel's turn are Turns.
struct Mapped { Control control = Control::Count; int index = 0; };
Mapped map(const move::Input& input);

// Presses, taps, holds and double presses from the surface's edges, and
// what is held. Shift is held while down or locked (a double press locks it,
// any press unlocks it); a locked Shift is not held for Play, as on the
// Move (Shift + Play restarts only with Shift down).
class Gestures {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto hold_time = std::chrono::milliseconds(400);
    static constexpr auto double_time = std::chrono::milliseconds(300);

    // The uses one input makes (none, one or two).
    std::vector<Use> feed(const move::Input& input, Clock::time_point now);
    // Holds that came due by `now`.
    std::vector<Use> tick(Clock::time_point now);

    uint16_t held(Control for_control = Control::Count) const;
    int held_step() const;
    bool shift_locked() const { return locked; }
    void reset();

private:
    struct Down {
        Control control = Control::Count;
        int index = 0;
        Clock::time_point since {};
        uint16_t held = 0;  // what was held when it went down
        int held_step = -1;
        bool used = false;  // something else was used while it was down
        bool held_long = false;
    };
    std::vector<Down> downs;
    struct Last { Control control = Control::Count; int index = 0; Clock::time_point at {}; };
    Last last_press;
    bool locked = false, shift_down = false;
    void mark_used();
};

} // namespace app::bindings
