// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "move/controls.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>

// The Move's lights, written to system:midi_playback on MIDI channel 16
// (jack_move drops every other channel without a word).
//
// Pads and steps: note-on, the velocity a palette index (0 is off).
// Row 1-4, Play, Rec and Sample: CC, the value a palette index.
// White buttons: CC, the value a brightness 0-127 (16 dim, 64 medium, 124
//   bright).
// Step icons: CC 16-31, under steps 1-16, a brightness as a white button.
// The palette is the Move's fixed 128 colours; nearest_colour() picks the
// closest documented one for an RGB colour, as Melodee's lights ask for.
// Pads may fade instead (set_pad_fade()): a pad's colour then goes to its
// new one through true RGB, Ableton's LED SysEx
//   F0 00 21 1D 01 01 3B 00 <note> rl rh gl gh bl bh F7
// (each 8-bit channel split into its low seven bits and its top bit), which
// the palette's three rungs a colour cannot do. It goes out as six 3-byte
// pieces (the last {F7, 0, 0}), always whole.
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
// What every palette index shows, the duplicates and darker rungs too
// (black for 0 and 117, which are off): what pads fade between.
Rgb led_rgb(uint8_t index);

// Requested light state, and what was last sent. Set and update from one
// thread (the runtime's MIDI thread, in App::lights()). Nothing is sent for
// a light never set, until clear_all() asks for every light off.
class Lights {
public:
    Lights();
    void pad(unsigned index, uint8_t palette_index);
    void pad(unsigned index, Rgb rgb) { pad(index, nearest_colour(rgb)); }
    void step(unsigned index, uint8_t palette_index);
    // The icon under step `index`, a brightness (0 off).
    void step_icon(unsigned index, uint8_t brightness);
    // A palette index for an RGB button, a brightness for a white one.
    void button(Button button, uint8_t value);
    // Knob `index`'s light (CC 71-78), a palette index (0 off).
    void knob(unsigned index, uint8_t palette_index);
    // Every note and CC light off, the ones never set too.
    void clear_all();

    // Sends what changed, at most `limit` 3-byte messages, continuing next
    // time from where it stopped so a busy light cannot starve the rest.
    // `send(const uint8_t* message)` returns false when it cannot take more.
    // Pads that fade are left to update_fades().
    template <class Send> unsigned update(Send send, unsigned limit = 48) {
        unsigned sent = 0;
        for (unsigned scanned = 0; scanned < 256 && sent < limit; ++scanned) {
            unsigned address = cursor; cursor = (cursor + 1) % 256;
            bool note = address < 128;
            uint8_t number = uint8_t(address % 128);
            if (note && fading() && is_pad_note(number)) continue;
            auto& want = note ? note_want[number] : cc_want[number];
            auto& last = note ? note_sent[number] : cc_sent[number];
            if (want == unset || want == last) continue;
            const uint8_t message[3] {uint8_t((note ? 0x90 : 0xb0) | surface_channel), number, shown(note, number, want)};
            if (!send(message)) break;
            last = want; ++sent;
        }
        return sent;
    }
    // The pads' fades, `now`: each pad going to a new colour gets there
    // over the fade time (a quarter of it on the way up, so a press answers
    // at once), a SysEx at most every frame_interval. `room` is how many
    // pieces the way out can take now; at most `limit` go, so the fades
    // never queue up ahead of a press. Sends nothing unless set_rgb() and
    // set_pad_fade() both allow it.
    template <class Send> unsigned update_fades(Send send, unsigned room, std::chrono::steady_clock::time_point now,
                                                unsigned limit = 4 * sysex_pieces) {
        if (!fading()) return 0;
        unsigned sent = 0;
        limit = std::min(limit, room);
        for (unsigned scanned = 0; scanned < pad_count; ++scanned) {
            unsigned index = fade_cursor; fade_cursor = (fade_cursor + 1) % pad_count;
            uint8_t number = pad_note(index);
            auto& fade = fades[index];
            uint8_t want = note_want[number];
            if (want == unset) continue;
            if (want != note_sent[number]) {
                uint8_t target = shown(true, number, want);
                Rgb to = led_rgb(target);
                if (target && !to.r && !to.g && !to.b) { // 117, black but not off: as it is, at once
                    if (sent + 1 > limit) break;
                    const uint8_t message[3] {uint8_t(0x90 | surface_channel), number, target};
                    if (!send(message)) break;
                    ++sent;
                    note_sent[number] = want;
                    fade.active = false;
                    fade.now = to; fade.known = false; // what that index shows is not known in RGB
                    continue;
                }
                start_fade(fade, to, now);
                note_sent[number] = want;
            }
            if (!fade.active) continue;
            if (!fade.owed && now - fade.last_frame < frame_interval && now < fade.end) continue;
            Rgb at = fade_at(fade, now);
            bool done = now >= fade.end;
            if (!fade.owed && !done && at.r == fade.sent.r && at.g == fade.sent.g && at.b == fade.sent.b) continue;
            if (sent + sysex_pieces > limit) break;
            if (!send_rgb(send, number, at)) break;
            sent += sysex_pieces;
            fade.sent = at; fade.last_frame = now; fade.owed = false;
            if (done) fade.active = false;
        }
        return sent;
    }
    // Whether the way out carries the RGB SysEx (the Move's own device does;
    // JACK's move driver is not known to). Without it pads change at once.
    void set_rgb(bool on);
    // How long a pad takes to fade out, in milliseconds (0: no fade, as before).
    void set_pad_fade(unsigned milliseconds);
    static constexpr unsigned sysex_pieces = 6;
    static constexpr auto frame_interval = std::chrono::milliseconds(12);

    bool pending() const;
    // What a pad or step was last asked to show (a palette index), for an
    // app that changes the lights after its screens set them.
    uint8_t pad_wanted(unsigned index) const;
    uint8_t step_wanted(unsigned index) const;
    // How bright the lights go out, as the Move's Setup: 0 low, 1 mid, 2
    // high, 3 max (as asked). Everything is sent again at the new level. A
    // pad or RGB button keeps its colour: it goes to the palette's darker
    // version of that colour, or stays as it is where there is none. Play,
    // Rec and Sampling stay as asked.
    void set_brightness(uint8_t level);

private:
    static constexpr uint8_t unset = 0xff;
    using Time = std::chrono::steady_clock::time_point;
    struct Fade {
        Rgb from, to, now, sent;
        Time start, end, last_frame;
        bool active = false;
        bool owed = false;  // the fade's first frame is still to go
        bool known = false; // `now` is what the pad shows
    };
    std::array<Fade, pad_count> fades {};
    unsigned fade_cursor = 0;
    bool rgb = false;
    unsigned fade_ms = 0;
    bool fading() const { return rgb && fade_ms > 0; }
    static bool is_pad_note(uint8_t number) { return number >= pad_note(0) && number < pad_note(0) + pad_count; }
    void start_fade(Fade& fade, Rgb to, Time now);
    void resend_pads();
    static Rgb fade_at(Fade& fade, Time now);
    template <class Send> static bool send_rgb(Send& send, uint8_t number, Rgb rgb) {
        const uint8_t pieces[sysex_pieces][3] {
            {0xf0, 0x00, 0x21}, {0x1d, 0x01, 0x01}, {0x3b, 0x00, number},
            {uint8_t(rgb.r & 0x7f), uint8_t(rgb.r >> 7), uint8_t(rgb.g & 0x7f)},
            {uint8_t(rgb.g >> 7), uint8_t(rgb.b & 0x7f), uint8_t(rgb.b >> 7)},
            {0xf7, 0x00, 0x00},
        };
        for (const auto& piece : pieces)
            if (!send(piece)) return false; // cannot happen: the room was asked for first
        return true;
    }
    std::array<uint8_t, 128> note_want {}, note_sent {}, cc_want {}, cc_sent {};
    unsigned cursor = 0;
    uint8_t brightness = 3;
    std::array<uint8_t, 128> dimmed {};  // each palette index at the brightness
    std::array<bool, 128> cc_rgb {};     // a CC whose LED takes a palette index, not a brightness
    std::array<bool, 128> cc_kept {};    // a CC shown as asked at every brightness (Play, Rec, Sampling)
    uint8_t shown(bool note, uint8_t number, uint8_t want) const {
        if (brightness >= 3 || !want || (!note && cc_kept[number])) return want;
        if (note || cc_rgb[number]) return dimmed[want & 127];
        return uint8_t(want * (brightness + 1) / 4); // a white LED's brightness
    }
};

} // namespace move
