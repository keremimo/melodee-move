// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// The Move's control surface as jack_move delivers it on system:midi_capture.
// The numbers are the Move's own (Schwung's shared/constants.mjs, after
// Cycling '74's rnbo.move.control; THIRD_PARTY.md), not guessed from
// captures. The driver sends on MIDI channel 16 and takes LED writes only on
// channel 16; decode() reads any channel so a remapped port still works.
//
// Pads   notes 68-99, bottom left to top right in rows of eight; velocity,
//        then polyphonic pressure on the same note.
// Steps  notes 16-31, step 1 to 16.
// Icons  CC 16-31: the white lights under steps 1-16 that show their
//        printed Shift icons; lights only, nothing comes in on them.
// Touch  notes 0-9: knobs 1-8, the master knob, the main knob. 127 is
//        touched; 0-63 is let go.
// Knobs  CC 71-78, the master knob CC 79 and the main knob (jogwheel) CC 14
//        are relative: 1-63 clockwise, 64-127 counter-clockwise.
// Buttons are CCs: 127 pressed, 0 released.
// Sense  CC 114 the audio input (0 microphone, 127 line in) and CC 115 the
//        output (0 speaker, 127 line out).
namespace move {

constexpr uint8_t surface_channel = 15; // MIDI channel 16, as a status nibble
constexpr uint8_t pad_first_note = 68, step_first_note = 16, touch_first_note = 0;
constexpr unsigned pad_count = 32, pad_columns = 8, pad_rows = 4, step_count = 16, touch_count = 10;
constexpr uint8_t step_icon_first_cc = 16;
constexpr uint8_t knob_first_cc = 71, master_knob_cc = 79, main_knob_cc = 14;
constexpr unsigned knob_count = 8;
constexpr uint8_t audio_in_sense_cc = 114, audio_out_sense_cc = 115;
constexpr unsigned touch_master = 8, touch_main = 9;

enum class LedKind : uint8_t { None, White, Rgb };

enum class Button : uint8_t {
    MainPush, Row1, Row2, Row3, Row4, Shift, Menu, Back, Capture, Down, Up, Undo,
    Loop, Copy, Left, Right, Play, Rec, Mute, Sample, Delete, Count
};

struct ButtonInfo { uint8_t cc; const char* name; LedKind led; };
// Row 1 is the top track button, beside the top row of pads.
constexpr std::array<ButtonInfo, size_t(Button::Count)> buttons {{
    {3, "Main push", LedKind::None}, {43, "Row 1", LedKind::Rgb}, {42, "Row 2", LedKind::Rgb},
    {41, "Row 3", LedKind::Rgb}, {40, "Row 4", LedKind::Rgb}, {49, "Shift", LedKind::White},
    {50, "Menu", LedKind::White}, {51, "Back", LedKind::White}, {52, "Capture", LedKind::White},
    {54, "Down", LedKind::White}, {55, "Up", LedKind::White}, {56, "Undo", LedKind::White},
    {58, "Loop", LedKind::White}, {60, "Copy", LedKind::White}, {62, "Left", LedKind::White},
    {63, "Right", LedKind::White}, {85, "Play", LedKind::Rgb}, {86, "Rec", LedKind::Rgb},
    {88, "Mute", LedKind::White}, {118, "Sample", LedKind::Rgb}, {119, "Delete", LedKind::White},
}};

struct Input {
    enum class Kind : uint8_t {
        None, Pad, PadPressure, Step, Button, Knob, MasterKnob, MainKnob, Touch, AudioInSense, AudioOutSense
    };
    Kind kind = Kind::None;
    uint8_t index = 0;    // Pad 0-31, Step 0-15, Button, Knob 0-7 or Touch 0-9
    bool pressed = false; // Pads, steps, buttons and touches
    uint8_t value = 0;    // Velocity, pressure, or the sense value
    int delta = 0;        // Knob detents, positive clockwise
};

// 0 is the bottom row, 0 the left column.
constexpr unsigned pad_row(unsigned pad) { return pad / pad_columns; }
constexpr unsigned pad_column(unsigned pad) { return pad % pad_columns; }
constexpr uint8_t pad_note(unsigned pad) { return uint8_t(pad_first_note + pad); }
constexpr uint8_t step_note(unsigned step) { return uint8_t(step_first_note + step); }
constexpr uint8_t step_icon_cc(unsigned step) { return uint8_t(step_icon_first_cc + step); }
constexpr uint8_t button_cc(Button button) { return buttons[size_t(button)].cc; }
constexpr int relative_steps(uint8_t value) { return value == 0 ? 0 : value < 64 ? int(value) : int(value) - 128; }

// One message from system:midi_capture; Kind::None for anything else.
Input decode(const uint8_t* bytes, size_t size);
const char* kind_name(Input::Kind kind);

} // namespace move
