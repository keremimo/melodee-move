// SPDX-License-Identifier: AGPL-3.0-only
#include "move/display.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace move {
namespace {

float luminance(juce::Colour colour) {
    float alpha = colour.getFloatAlpha(); // transparent is black, as on the screen
    return alpha * (0.2126f * colour.getRed() + 0.7152f * colour.getGreen() + 0.0722f * colour.getBlue());
}

} // namespace

Frame pack(const juce::Image& image) {
    Frame frame {};
    juce::Image::BitmapData data(image, juce::Image::BitmapData::readOnly);
    int width = std::min(image.getWidth(), screen_width), height = std::min(image.getHeight(), screen_height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (luminance(data.getPixelColour(x, y)) >= 128.0f) set_pixel(frame, x, y, true);
    return frame;
}

DisplayMessage display_message(const Frame& frame) {
    DisplayMessage message {};
    std::copy(display_magic.begin(), display_magic.end(), message.begin());
    std::copy(frame.begin(), frame.end(), message.begin() + display_magic.size());
    return message;
}

juce::Image crush(const juce::Image& source, Fit fit, Tone tone) {
    juce::Image result(juce::Image::RGB, screen_width, screen_height, true);
    int source_width = source.getWidth(), source_height = source.getHeight();
    if (source_width <= 0 || source_height <= 0) return result;

    // The target area on the Move's screen.
    int area_width = screen_width, area_height = screen_height;
    if (fit == Fit::Letterbox) {
        double scale = std::min(double(screen_width) / source_width, double(screen_height) / source_height);
        area_width = std::max(1, int(std::lround(source_width * scale)));
        area_height = std::max(1, int(std::lround(source_height * scale)));
    }
    int left = (screen_width - area_width) / 2, top = (screen_height - area_height) / 2;

    // A summed-area table of the source's luminance, so each target pixel is
    // the mean of the source pixels under it.
    std::vector<double> sums(size_t(source_width + 1) * size_t(source_height + 1), 0.0);
    auto at = [&](int x, int y) -> double& { return sums[size_t(y) * size_t(source_width + 1) + size_t(x)]; };
    {
        juce::Image::BitmapData data(source, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < source_height; ++y) {
            double row = 0;
            for (int x = 0; x < source_width; ++x) {
                row += luminance(data.getPixelColour(x, y));
                at(x + 1, y + 1) = at(x + 1, y) + row;
            }
        }
    }
    std::vector<float> grey(size_t(area_width) * size_t(area_height));
    for (int y = 0; y < area_height; ++y) {
        int y0 = y * source_height / area_height, y1 = std::max(y0 + 1, (y + 1) * source_height / area_height);
        for (int x = 0; x < area_width; ++x) {
            int x0 = x * source_width / area_width, x1 = std::max(x0 + 1, (x + 1) * source_width / area_width);
            double total = at(x1, y1) - at(x0, y1) - at(x1, y0) + at(x0, y0);
            grey[size_t(y) * size_t(area_width) + size_t(x)] = float(total / double((x1 - x0) * (y1 - y0)));
        }
    }

    // To black and white, spreading the error Floyd-Steinberg's way when dithering.
    juce::Image::BitmapData out(result, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < area_height; ++y)
        for (int x = 0; x < area_width; ++x) {
            float& value = grey[size_t(y) * size_t(area_width) + size_t(x)];
            bool lit = value >= 128.0f;
            out.setPixelColour(left + x, top + y, lit ? juce::Colours::white : juce::Colours::black);
            if (tone == Tone::Threshold) continue;
            float error = value - (lit ? 255.0f : 0.0f);
            auto spread = [&](int dx, int dy, float weight) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < area_width && ny < area_height) grey[size_t(ny) * size_t(area_width) + size_t(nx)] += error * weight;
            };
            spread(1, 0, 7.0f / 16); spread(-1, 1, 3.0f / 16); spread(0, 1, 5.0f / 16); spread(1, 1, 1.0f / 16);
        }
    return result;
}

juce::Image frame_image(const Frame& frame, int scale) {
    scale = std::max(1, scale);
    juce::Image image(juce::Image::RGB, screen_width * scale, screen_height * scale, false);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff050608));
    g.setColour(juce::Colour(0xffd8f0ff));
    for (int y = 0; y < screen_height; ++y)
        for (int x = 0; x < screen_width; ++x)
            if (pixel(frame, x, y)) g.fillRect(x * scale, y * scale, scale, scale);
    return image;
}

} // namespace move
