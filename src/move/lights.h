// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/controls.h"
#include <array>
#include <cstdint>

// The Move's lights, written to system:midi_playback on MIDI channel 16
// (jack_move drops every other channel without a word).
//
// Pads and steps: note-on, the velocity a palette index (0 is off).
// Row 1-4, Play, Rec and Sample: CC, the value a palette index.
// White buttons: CC, the value a brightness 0-127 (16 dim, 64 medium, 124
//   bright).
// The palette is the Move's fixed 128 colours; nearest_colour() picks the
// closest documented one for an RGB colour, as Melodee's lights ask for.
namespace move {

struct Rgb { uint8_t r = 0, g = 0, b = 0; };

namespace colour {
constexpr uint8_t off = 0, red = 127, green = 126, blue = 125, white = 120, light_grey = 118, dark_grey = 124;
constexpr uint8_t bright_red = 1, orange = 3, yellow = 7, bright_green = 8, cyan = 14, azure = 15, royal_blue = 16;
constexpr uint8_t violet = 19, hot_magenta = 21, deep_blue = 93, dark_blue = 95, dark_teal = 87;
constexpr uint8_t brown_yellow = 75, deep_magenta = 109, very_dark_green = 80;
} // namespace colour

constexpr uint8_t white_dim = 16, white_medium = 64, white_bright = 124;

// The closest palette index to `rgb`; black gives 0 (off).
uint8_t nearest_colour(Rgb rgb);
// The documented colour of a palette index, black for an undocumented one.
Rgb palette_rgb(uint8_t index);

// Requested light state, and what was last sent. Set and update from one
// thread (the runtime's MIDI thread, in App::lights()). Nothing is sent for
// a light never set, until clear_all() asks for every light off.
class Lights {
public:
    Lights();
    void pad(unsigned index, uint8_t palette_index);
    void pad(unsigned index, Rgb rgb) { pad(index, nearest_colour(rgb)); }
    void step(unsigned index, uint8_t palette_index);
    // A palette index for an RGB button, a brightness for a white one.
    void button(Button button, uint8_t value);
    // Every note and CC light off, the ones never set too.
    void clear_all();

    // Sends what changed, at most `limit` 3-byte messages, continuing next
    // time from where it stopped so a busy light cannot starve the rest.
    // `send(const uint8_t* message)` returns false when it cannot take more.
    template <class Send> unsigned update(Send send, unsigned limit = 48) {
        unsigned sent = 0;
        for (unsigned scanned = 0; scanned < 256 && sent < limit; ++scanned) {
            unsigned address = cursor; cursor = (cursor + 1) % 256;
            bool note = address < 128;
            uint8_t number = uint8_t(address % 128);
            auto& want = note ? note_want[number] : cc_want[number];
            auto& last = note ? note_sent[number] : cc_sent[number];
            if (want == unset || want == last) continue;
            const uint8_t message[3] {uint8_t((note ? 0x90 : 0xb0) | surface_channel), number, shown(note, number, want)};
            if (!send(message)) break;
            last = want; ++sent;
        }
        return sent;
    }
    bool pending() const;
    // What a pad or step was last asked to show (a palette index), for an
    // app that changes the lights after its screens set them.
    uint8_t pad_wanted(unsigned index) const;
    uint8_t step_wanted(unsigned index) const;
    // How bright the lights go out, as the Move's Setup: 0 low, 1 mid, 2
    // high, 3 max (as asked). Everything is sent again at the new level.
    void set_brightness(uint8_t level);

private:
    static constexpr uint8_t unset = 0xff;
    std::array<uint8_t, 128> note_want {}, note_sent {}, cc_want {}, cc_sent {};
    unsigned cursor = 0;
    uint8_t brightness = 3;
    std::array<uint8_t, 128> dimmed {};  // each palette index at the brightness
    std::array<bool, 128> cc_rgb {};     // a CC whose LED takes a palette index, not a brightness
    uint8_t shown(bool note, uint8_t number, uint8_t want) const {
        if (brightness >= 3 || !want) return want;
        if (note || cc_rgb[number]) return dimmed[want & 127];
        return uint8_t(want * (brightness + 1) / 4); // a white LED's brightness
    }
};

} // namespace move
