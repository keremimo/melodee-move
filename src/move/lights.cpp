// SPDX-License-Identifier: AGPL-3.0-only
#include "move/lights.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace move {
namespace {

// The documented palette entries (Cycling '74's rnbo.move.control, as listed
// in Schwung's constants.mjs; THIRD_PARTY.md). Duplicates are left out so a
// colour always maps to the first index that shows it.
struct PaletteEntry { uint8_t index; uint32_t rgb; };
constexpr PaletteEntry palette[] {
    {0, 0x000000}, {124, 0x1a1a1a}, {118, 0x595959}, {120, 0xffffff},
    {1, 0xff2424}, {2, 0xf23a0c}, {3, 0xff9900}, {4, 0xa68956}, {5, 0xedf95a}, {6, 0xc19d08},
    {7, 0xffff00}, {8, 0x56bf13}, {9, 0x2c8403}, {10, 0x246b24}, {11, 0x19ff30}, {12, 0x159573},
    {13, 0x176b50}, {14, 0x00ffff}, {15, 0x0074fc}, {16, 0x274fcc}, {17, 0x00448c}, {18, 0x644ad9},
    {19, 0x4d3fa0}, {20, 0x8700ff}, {21, 0xe657e3}, {22, 0x660099}, {23, 0xff0099}, {24, 0xa14c5f},
    {25, 0xff4dc4}, {26, 0xeb8be1}, {27, 0xa63421}, {28, 0x995628}, {29, 0x876700}, {30, 0x90821f},
    {31, 0x4a8700}, {32, 0x007f12}, {33, 0x1853b2}, {34, 0x624bad}, {35, 0x733a67},
    {43, 0xaeff99}, {44, 0x7cdd9f}, {45, 0x89b47d}, {46, 0x80f3ff}, {47, 0x7acefc}, {48, 0x68a1d3},
    {49, 0x858fc2}, {50, 0xbbaaf2},
    {65, 0x4d0b0b}, {66, 0x1a0404}, {67, 0x4d1204}, {73, 0x4d491f}, {74, 0x1a180a}, {75, 0x403302},
    {76, 0x1a1501}, {77, 0x4d4d00}, {78, 0x1a1a00}, {79, 0x1c4007}, {80, 0x0b1a03}, {81, 0x113301},
    {83, 0x113311}, {85, 0x0a4d0a}, {87, 0x073327}, {89, 0x104d39}, {90, 0x030d0a}, {93, 0x00234d},
    {95, 0x0c1940}, {97, 0x00254d}, {99, 0x231a4d}, {100, 0x0c091a}, {101, 0x251e4d}, {102, 0x0c0a1a},
    {104, 0x0d001a}, {105, 0x4d1d4c}, {106, 0x1a0a19}, {107, 0x33004d}, {108, 0x11001a}, {109, 0x4d002e},
    {110, 0x1a000f}, {111, 0x4d242d}, {112, 0x1a0c0f}, {113, 0x4d173b}, {114, 0x1a0814}, {115, 0x4d2d49},
    {116, 0x1a0f18}, {125, 0x0000ff}, {126, 0x00ff00}, {127, 0xff0000},
};

// Every palette index's colour, so every index a light can ask for fades:
// the documented ones above; the darker rungs they leave out (colour k's dim
// at 63 + 2k, dark at 64 + 2k) as colour k scaled to the listed rungs' own
// peaks, #4D and #1A; the greys' ramp (122 #CCCCCC, 123 #404040); the few
// pastels left from Schwung's shared/constants.mjs (THIRD_PARTY.md).
constexpr uint32_t led_colours[128] {
    0x000000, 0xff2424, 0xf23a0c, 0xff9900, 0xa68956, 0xedf95a, 0xc19d08, 0xffff00,
    0x56bf13, 0x2c8403, 0x246b24, 0x19ff30, 0x159573, 0x176b50, 0x00ffff, 0x0074fc,
    0x274fcc, 0x00448c, 0x644ad9, 0x4d3fa0, 0x8700ff, 0xe657e3, 0x660099, 0xff0099,
    0xa14c5f, 0xff4dc4, 0xeb8be1, 0xa63421, 0x995628, 0x876700, 0x90821f, 0x4a8700,
    0x007f12, 0x1853b2, 0x624bad, 0x733a67, 0xf8bcaf, 0xff9b76, 0xffbf5f, 0xd9af71,
    0xfff480, 0xbfba69, 0xbccc88, 0xaeff99, 0x7cdd9f, 0x89b47d, 0x80f3ff, 0x7acefc,
    0x68a1d3, 0x858fc2, 0xbbaaf2, 0xcdbbe4, 0xef8bb0, 0x859d8c, 0x6b756e, 0x84909b,
    0x6a7075, 0x88859d, 0x6c6a75, 0x9d859c, 0x746a74, 0x9c9d85, 0x74756a, 0x9d8484,
    0x756a6a, 0x4d0b0b, 0x1a0404, 0x4d1204, 0x1a0601, 0x4d2e00, 0x1a1000, 0x4d4028,
    0x1a150d, 0x4d491f, 0x1a180a, 0x403302, 0x1a1501, 0x4d4d00, 0x1a1a00, 0x1c4007,
    0x0b1a03, 0x113301, 0x091a01, 0x113311, 0x091a09, 0x0a4d0a, 0x031a05, 0x073327,
    0x041a14, 0x104d39, 0x030d0a, 0x004d4d, 0x001a1a, 0x00234d, 0x000c1a, 0x0c1940,
    0x050a1a, 0x00254d, 0x000d1a, 0x231a4d, 0x0c091a, 0x251e4d, 0x0c0a1a, 0x29004d,
    0x0d001a, 0x4d1d4c, 0x1a0a19, 0x33004d, 0x11001a, 0x4d002e, 0x1a000f, 0x4d242d,
    0x1a0c0f, 0x4d173b, 0x1a0814, 0x4d2d49, 0x1a0f18, 0x000000, 0x595959, 0x1a1a1a,
    0xffffff, 0x595959, 0xcccccc, 0x404040, 0x1a1a1a, 0x0000ff, 0x00ff00, 0xff0000,
};

Rgb unpack(uint32_t rgb) { return {uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)}; }

// A light made dimmer goes down its own colour's rungs and no other: the
// palette's darker rows are colours 1-26 again, colour k's dim version at
// 63 + 2k and its dark one at 64 + 2k, about a third and a tenth as bright
// (Schwung's shared/constants.mjs lists every pair; ours lists most). Other
// palette colours bear no darker version of themselves and stay as they
// are; neighbouring entries of a like hue are other colours on the Move.
struct Rung { uint8_t index; float level; };
constexpr float dim_level = 0.35f, dark_level = 0.12f;
// The greys, as Schwung's white ramp: white, 122 (#CCCCCC), light grey,
// 123 (#404040), dark grey.
constexpr Rung greys[] {{120, 1.0f}, {122, 0.8f}, {118, 0.35f}, {123, 0.25f}, {124, 0.08f}};

// The index that shows `index` at `scale` of its brightness: of its own
// rungs from it down, the one nearest that (by ratio).
uint8_t dimmed_colour(uint8_t index, float scale) {
    Rung rungs[std::size(greys)] {};
    size_t count = 0;
    if (index >= 1 && index <= 26) {
        rungs[count++] = {index, 1.0f};
        rungs[count++] = {uint8_t(63 + 2 * index), dim_level};
        rungs[count++] = {uint8_t(64 + 2 * index), dark_level};
    } else if (index >= 65 && index <= 116 && index % 2) {
        rungs[count++] = {index, dim_level};
        rungs[count++] = {uint8_t(index + 1), dark_level};
    } else {
        for (const auto& grey : greys)
            if (count || grey.index == index) rungs[count++] = grey;
    }
    if (count < 2) return index; // a dark rung, or no darker version
    float target = rungs[0].level * scale;
    uint8_t best = index;
    float best_distance = std::numeric_limits<float>::max();
    for (size_t n = 0; n < count; ++n) {
        float distance = std::fabs(std::log(rungs[n].level / target));
        if (distance < best_distance) { best_distance = distance; best = rungs[n].index; }
    }
    return best;
}

} // namespace

uint8_t nearest_colour(Rgb rgb) {
    if (!rgb.r && !rgb.g && !rgb.b) return colour::off;
    uint8_t best = 0;
    long best_distance = std::numeric_limits<long>::max();
    for (const auto& entry : palette) {
        if (entry.index == 0) continue; // a colour asked for is never off
        Rgb candidate = unpack(entry.rgb);
        long dr = long(rgb.r) - candidate.r, dg = long(rgb.g) - candidate.g, db = long(rgb.b) - candidate.b;
        // Weighted for the eye: green differences show most, blue least.
        long distance = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (distance < best_distance) { best_distance = distance; best = entry.index; }
    }
    return best;
}

Rgb led_rgb(uint8_t index) { return unpack(led_colours[index & 127]); }

Rgb palette_rgb(uint8_t index) {
    for (const auto& entry : palette)
        if (entry.index == index) return unpack(entry.rgb);
    return {};
}

Lights::Lights() {
    note_want.fill(unset); note_sent.fill(unset);
    cc_want.fill(unset); cc_sent.fill(unset);
    for (const auto& info : buttons)
        if (info.led == LedKind::Rgb) cc_rgb[info.cc & 127] = true;
    // Play, Rec and Sampling keep their colours at every brightness, as on
    // the Move: green, red and dim white have no darker versions of
    // themselves, and a transport light in another colour reads as another state.
    for (Button kept : {Button::Play, Button::Rec, Button::Sample}) cc_kept[button_cc(kept)] = true;
    for (unsigned n = 0; n < 128; ++n) dimmed[n] = uint8_t(n);
}

uint8_t Lights::pad_wanted(unsigned index) const {
    if (index >= pad_count) return 0;
    uint8_t want = note_want[pad_note(index)];
    return want == unset ? 0 : want;
}

uint8_t Lights::step_wanted(unsigned index) const {
    if (index >= step_count) return 0;
    uint8_t want = note_want[step_note(index)];
    return want == unset ? 0 : want;
}

void Lights::set_brightness(uint8_t level) {
    level = std::min<uint8_t>(level, 3);
    if (level == brightness) return;
    brightness = level;
    // A quarter, a half and three quarters as the eye sees them: the LEDs'
    // light to the power 2.2.
    static const float scale[4] {0.047f, 0.218f, 0.531f, 1.0f};
    for (unsigned n = 0; n < 128; ++n) dimmed[n] = dimmed_colour(uint8_t(n), scale[level]);
    note_sent.fill(unset); // everything again, at the new level
    cc_sent.fill(unset);
}

void Lights::pad(unsigned index, uint8_t palette_index) {
    if (index < pad_count) note_want[pad_note(index)] = palette_index & 0x7f;
}

void Lights::step(unsigned index, uint8_t palette_index) {
    if (index < step_count) note_want[step_note(index)] = palette_index & 0x7f;
}

void Lights::step_icon(unsigned index, uint8_t brightness) {
    if (index < step_count) cc_want[step_icon_cc(index)] = brightness & 0x7f;
}

void Lights::button(Button button, uint8_t value) {
    if (button >= Button::Count || buttons[size_t(button)].led == LedKind::None) return;
    cc_want[button_cc(button)] = value & 0x7f;
}

void Lights::clear_all() {
    set_pad_fade(0); // off at once, by note, as every other light
    note_want.fill(0); cc_want.fill(0);
    note_sent.fill(unset); cc_sent.fill(unset);
}

void Lights::set_rgb(bool on) {
    if (on == rgb) return;
    bool was = fading();
    rgb = on;
    if (was != fading()) resend_pads();
}

void Lights::set_pad_fade(unsigned milliseconds) {
    if (milliseconds == fade_ms) return;
    bool was = fading();
    fade_ms = milliseconds;
    if (was != fading()) resend_pads();
}

// Fading turned on or off: every pad again, the new way, so none stays
// halfway through a fade the other path knows nothing of.
void Lights::resend_pads() {
    for (unsigned at = 0; at < pad_count; ++at) note_sent[pad_note(at)] = unset;
    fades.fill(Fade {});
}

// From what the pad shows now (black when unknown: it has just been lit by
// a note, or never) to `to`, faster on the way up.
void Lights::start_fade(Fade& fade, Rgb to, Time now) {
    Rgb from = fade.active ? fade_at(fade, now) : fade.known ? fade.now : Rgb {};
    auto sum = [](Rgb c) { return unsigned(c.r) + c.g + c.b; };
    unsigned ms = sum(to) > sum(from) ? std::max(fade_ms / 4, 15u) : fade_ms;
    fade.from = from; fade.to = to; fade.now = from;
    fade.start = now; fade.end = now + std::chrono::milliseconds(ms);
    fade.owed = true;
    fade.active = true; fade.known = true;
}

// Where a fade is at `now`: eased (smoothstep), each channel interpolated in
// light (squared) so a fade down does not drop to dark too soon.
Rgb Lights::fade_at(Fade& fade, Time now) {
    if (now >= fade.end) return fade.now = fade.to;
    double t = std::chrono::duration<double>(now - fade.start).count() / std::chrono::duration<double>(fade.end - fade.start).count();
    t = std::clamp(t, 0.0, 1.0);
    t = t * t * (3 - 2 * t);
    auto mix = [t](uint8_t a, uint8_t b) {
        double la = double(a) * a, lb = double(b) * b;
        return uint8_t(std::lround(std::sqrt(la + (lb - la) * t)));
    };
    return fade.now = Rgb {mix(fade.from.r, fade.to.r), mix(fade.from.g, fade.to.g), mix(fade.from.b, fade.to.b)};
}

bool Lights::pending() const {
    for (unsigned n = 0; n < 128; ++n)
        if ((note_want[n] != unset && note_want[n] != note_sent[n]) || (cc_want[n] != unset && cc_want[n] != cc_sent[n]))
            return true;
    return false;
}

} // namespace move
