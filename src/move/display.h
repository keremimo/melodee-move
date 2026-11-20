// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <cstddef>
#include <cstdint>

// The Move's screen: 128 x 64, one bit a pixel, in the SSD1306 page layout.
// A frame reaches it as one JACK MIDI event on system:display: the 8 bytes
// "MOVEDISP", then 1024 bytes, byte (y / 8) * 128 + x, bit y % 8.
namespace move {

constexpr int screen_width = 128, screen_height = 64;
constexpr size_t frame_bytes = screen_width * screen_height / 8;
constexpr std::array<uint8_t, 8> display_magic {{'M', 'O', 'V', 'E', 'D', 'I', 'S', 'P'}};
constexpr size_t display_message_bytes = display_magic.size() + frame_bytes;

using Frame = std::array<uint8_t, frame_bytes>;
using DisplayMessage = std::array<uint8_t, display_message_bytes>;

constexpr size_t frame_byte(int x, int y) { return size_t((y >> 3) * screen_width + x); }
constexpr uint8_t frame_bit(int y) { return uint8_t(1u << (y & 7)); }
inline void set_pixel(Frame& frame, int x, int y, bool on) {
    if (x < 0 || x >= screen_width || y < 0 || y >= screen_height) return;
    if (on) frame[frame_byte(x, y)] |= frame_bit(y);
    else frame[frame_byte(x, y)] &= uint8_t(~frame_bit(y));
}
inline bool pixel(const Frame& frame, int x, int y) {
    return x >= 0 && x < screen_width && y >= 0 && y < screen_height && (frame[frame_byte(x, y)] & frame_bit(y));
}

// A 128 x 64 image to a frame: a pixel is lit where it is at least half
// bright. Apps draw white on black.
Frame pack(const juce::Image& image);
DisplayMessage display_message(const Frame& frame);

// Another screen's picture squeezed onto the Move's: averaged down, then to
// black and white. Letterbox keeps the shape (a 320 x 240 picture becomes
// 85 x 64, centred); Stretch fills the screen. Threshold suits interface
// screens (a dark background stays black); Dither (Floyd-Steinberg) keeps
// greys in photos and waveforms but sprinkles a dark background with dots.
enum class Fit { Letterbox, Stretch };
enum class Tone { Threshold, Dither };
juce::Image crush(const juce::Image& source, Fit fit, Tone tone = Tone::Threshold);

// A frame as an image for a PNG preview, each pixel `scale` square, lit
// pixels pale blue on near-black as the Move's screen shows them.
juce::Image frame_image(const Frame& frame, int scale);

} // namespace move
