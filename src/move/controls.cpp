// SPDX-License-Identifier: AGPL-3.0-only
#include "move/controls.h"

namespace move {

Input decode(const uint8_t* bytes, size_t size) {
    Input input;
    if (size < 3) return input;
    unsigned kind = bytes[0] & 0xf0;
    uint8_t number = bytes[1] & 0x7f, value = bytes[2] & 0x7f;
    if (kind == 0x80 || kind == 0x90) {
        bool on = kind == 0x90 && value != 0;
        if (number >= pad_first_note && number < pad_first_note + pad_count) {
            input.kind = Input::Kind::Pad; input.index = uint8_t(number - pad_first_note);
            input.pressed = on; input.value = on ? value : 0;
        } else if (number >= step_first_note && number < step_first_note + step_count) {
            input.kind = Input::Kind::Step; input.index = uint8_t(number - step_first_note);
            input.pressed = on; input.value = on ? value : 0;
        } else if (number < touch_first_note + touch_count) {
            input.kind = Input::Kind::Touch; input.index = number;
            input.pressed = on && value >= 64;
        }
    } else if (kind == 0xa0) {
        if (number >= pad_first_note && number < pad_first_note + pad_count) {
            input.kind = Input::Kind::PadPressure; input.index = uint8_t(number - pad_first_note);
            input.value = value;
        }
    } else if (kind == 0xb0) {
        if (number >= knob_first_cc && number < knob_first_cc + knob_count) {
            input.kind = Input::Kind::Knob; input.index = uint8_t(number - knob_first_cc);
            input.delta = relative_steps(value);
        } else if (number == master_knob_cc) {
            input.kind = Input::Kind::MasterKnob; input.delta = relative_steps(value);
        } else if (number == main_knob_cc) {
            input.kind = Input::Kind::MainKnob; input.delta = relative_steps(value);
        } else if (number == audio_in_sense_cc || number == audio_out_sense_cc) {
            input.kind = number == audio_in_sense_cc ? Input::Kind::AudioInSense : Input::Kind::AudioOutSense;
            input.value = value;
        } else {
            for (size_t n = 0; n < buttons.size(); ++n)
                if (buttons[n].cc == number) {
                    input.kind = Input::Kind::Button; input.index = uint8_t(n);
                    input.pressed = value != 0; input.value = value;
                    break;
                }
        }
    }
    return input;
}

const char* kind_name(Input::Kind kind) {
    switch (kind) {
    case Input::Kind::None: return "none";
    case Input::Kind::Pad: return "pad";
    case Input::Kind::PadPressure: return "pressure";
    case Input::Kind::Step: return "step";
    case Input::Kind::Button: return "button";
    case Input::Kind::Knob: return "knob";
    case Input::Kind::MasterKnob: return "master";
    case Input::Kind::MainKnob: return "jog";
    case Input::Kind::Touch: return "touch";
    case Input::Kind::AudioInSense: return "in-sense";
    case Input::Kind::AudioOutSense: return "out-sense";
    }
    return "?";
}

} // namespace move
