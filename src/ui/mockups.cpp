// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/mockups.h"
#include "move/display.h"
#include "move/font.h"
#include "ui/widgets.h"
#include <cmath>
#include <functional>
#include <iostream>

namespace ui {
namespace mockup {
namespace {

const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// The example the screens show: pad A05 of track 1's kit holds a two-bar
// break at 94 BPM; track 2 has OB-Xd, a Schwung synth.
struct Hit { int at; float level, decay; };
std::vector<float> peaks_of(const std::vector<Hit>& hits, int columns, float floor) {
    std::vector<float> peaks(size_t(columns), floor);
    for (int column = 0; column < columns; ++column)
        for (const auto& hit : hits)
            if (column >= hit.at) peaks[size_t(column)] += hit.level * std::exp(-(column - hit.at) / hit.decay);
    for (auto& peak : peaks) peak = std::min(peak, 1.0f);
    return peaks;
}
std::vector<float> break_peaks() {
    return peaks_of({{0, .95f, 10}, {64, .95f, 10}, {32, .75f, 7}, {96, .75f, 7}, {56, .45f, 3}, {88, .45f, 3}, {120, .5f, 4},
                     {8, .35f, 2}, {16, .35f, 2}, {24, .35f, 2}, {40, .35f, 2}, {48, .35f, 2}, {72, .35f, 2}, {80, .35f, 2},
                     {104, .35f, 2}, {112, .35f, 2}}, 128, 0.04f);
}
std::vector<float> zoomed_peaks() { // one snare hit, close up: the tail of the last sound, then the attack
    auto peaks = peaks_of({{51, .9f, 26}, {0, .3f, 30}}, content_width, 0.03f);
    for (size_t n = 0; n < peaks.size(); ++n) peaks[n] = std::min(1.0f, peaks[n] + 0.04f * float((n * 7919) % 5));
    return peaks;
}
std::vector<float> phrase_peaks() { // a sung phrase: long swells
    return peaks_of({{2, .6f, 30}, {30, .8f, 24}, {58, .5f, 16}, {72, .7f, 28}}, 128, 0.02f);
}
std::vector<int> evenly(int width, int slices) {
    std::vector<int> columns;
    for (int slice = 1; slice < slices; ++slice) columns.push_back(slice * width / slices);
    return columns;
}

const std::vector<std::string> modules {"OB-XD", "SURGE XT", "8W8", "9W9", "APHEX", "CHONK", "CLEMENTINE", "HUSH ONE", "WURL"};
const std::vector<std::string> samples {"DRUMS >", "BREAKS >", "VOX >", "BREAK 94.WAV", "BREAK 120.WAV", "DUSTY KEYS.WAV", "HORN STAB.WAV"};

} // namespace

std::vector<Screen> rail_screens() {
    auto peaks = break_peaks();
    auto subject = content(0, subject_height);
    return {
        {"play", "PLAY: PAD A05 HIT", [=](juce::Graphics& g) {
            rail(g, "KIT", "1", "A05");
            waveform(g, subject, peaks);
            g.fillRect(content_x + 1, 0, 1, subject_height); g.fillRect(width - 3, 0, 1, subject_height);
            detail(g, "BREAK 94", "2.0S");
        }},
        {"touch", "KNOB 1 TOUCHED", [](juce::Graphics& g) {
            rail(g, "KNOB", "1", "A05");
            big_parameter(g, content(4, 44), "TUNE", "+3.50", 0.57f);
            move::draw_text_centred(g, content_x, detail_y, content_width, "SEMITONES");
        }},
        {"pad-page", "PAD PAGE 2 OF 5: SHAPE", [](juce::Graphics& g) {
            rail(g, "PAD", "A05", "2/5");
            move::draw_text(g, content_x + 2, 1, "SHAPE", 2);
            knob_cells(g, content(18, 46), {"ATTACK", "DECAY", "CURVE", "VELO", "MODE", "MUTE", "LINK", "OFFSET"},
                       {0.05f, 0.6f, 0.5f, 1.0f, 0.0f, 0.25f, 0.0f, 0.1f});
        }},
        {"chop", "CHOP: SLICE 7 OF 16", [=](juce::Graphics& g) {
            rail(g, "SLC", "07", "/16");
            waveform(g, subject, peaks, 39, 44);
            cuts(g, subject, evenly(content_width, 16), 5);
            detail(g, "BEATS 1/8", "94");
        }},
        {"chop-fine", "CHOP: FIXING A CUT, CLOSE UP", [](juce::Graphics& g) {
            rail(g, "SLC", "07", "STRT");
            auto close = content(0, 46);
            waveform(g, close, zoomed_peaks());
            cuts(g, close, {50}, 0);
            for (int x : {8, 50, 93}) g.fillRect(content_x + x, 48, 1, 3); // the hits the jogwheel jumps between
            detail(g, "+12 MS", "X16");
        }},
        {"chop-take", "CHOP: A MANUAL TAKE", [=](juce::Graphics& g) {
            rail(g, "TAKE", "05", "TAP");
            waveform(g, subject, peaks);
            cuts(g, subject, {13, 25, 38, 51}, -1);
            playhead(g, subject, 60);
            detail(g, "PAD 6 CUTS NEXT");
        }},
        {"to-pads", "CHOP: TO PADS", [](juce::Graphics& g) {
            rail(g, "SLC", "16", "PADS");
            move::draw_text(g, content_x + 1, 1, "16 SLICES TO PADS");
            list(g, content(11, 36), {"POLY", "MONO", "THRU", "KIT  9 SOUNDS"}, 3, 1);
            detail(g, "FROM A01", "+PATTERN");
        }},
        {"record", "RECORD: ARMED ON THE MIC", [](juce::Graphics& g) {
            rail(g, "REC", "", "MIC");
            g.setColour(off); dot(g, 12, 30, 5); g.setColour(on);
            meter(g, content(6, 24).reduced(2, 0), 0.62f, 0.55f, 0.35f);
            move::draw_text(g, content_x + 2, 38, "4 BARS ONTO A06");
            detail(g, "WAITS -24 DB");
        }},
        {"recording", "RECORD: TAKING", [=](juce::Graphics& g) {
            rail(g, "REC", "", "A06");
            g.setColour(off); dot(g, 12, 30, 5); g.setColour(on);
            move::draw_text(g, content_x + 2, 1, "0:03.2", 2);
            auto growing = peaks;
            for (size_t n = 56; n < growing.size(); ++n) growing[n] = 0;
            waveform(g, content(20, 32), growing);
            detail(g, "4 BARS", "BAR 2");
        }},
        {"capture", "CAPTURE: THE LAST PHRASE", [](juce::Graphics& g) {
            rail(g, "CAP", "8", "SEC");
            waveform(g, content(0, subject_height), phrase_peaks());
            detail(g, "> A07", "PUSH: KEEP");
        }},
        {"load-samples", "LOAD: SAMPLES ONTO A05", [](juce::Graphics& g) {
            rail(g, "LOAD", "A05", "SMP");
            list(g, content(0, height), samples, 3, 1);
        }},
        {"load-synths", "LOAD: SCHWUNG SYNTHS, TRACK 2", [](juce::Graphics& g) {
            rail(g, "LOAD", "2", "SYN");
            list(g, content(0, height), modules, 1, 1);
        }},
        {"module", "OB-XD: FILTER PAGE", [](juce::Graphics& g) {
            rail(g, "OBXD", "2", "2/5");
            move::draw_text(g, content_x + 2, 1, "FILTER", 2);
            knob_cells(g, content(18, 46), {"CUTOFF", "RESO", "ENVAMT", "KEYTRK", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"},
                       {0.62f, 0.3f, 0.75f, 0.5f, 0.05f, 0.45f, 0.8f, 0.35f});
        }},
        {"module-touch", "OB-XD: CUTOFF TOUCHED", [](juce::Graphics& g) {
            rail(g, "KNOB", "1", "OBXD");
            big_parameter(g, content(4, 44), "CUTOFF", "2.4K", 0.62f);
            move::draw_text_centred(g, content_x, detail_y, content_width, "HERTZ");
        }},
    };
}

bool write_png(const juce::Image& image, const juce::File& file) {
    file.deleteFile();
    juce::FileOutputStream output(file);
    return output.openedOk() && juce::PNGImageFormat().writeImageToStream(image, output);
}

juce::Image screen_image(const Draw& draw, int scale) {
    juce::Image image(juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g(image);
        g.fillAll(off);
        g.setColour(on);
        draw(g);
    }
    return move::frame_image(move::pack(image), scale);
}

juce::Image surface_image(const Surface& surface, int w, int h) {
    juce::Image image(juce::Image::RGB, w, h, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff2a2d33));
    auto colour = [](uint8_t index) {
        if (!index) return juce::Colour(0xff121316);
        auto rgb = move::palette_rgb(index);
        return juce::Colour(rgb.r, rgb.g, rgb.b);
    };
    // Laid out as on the Move: the track buttons left of the pads, the
    // steps under them, Play bottom left.
    int cell = w / 11, gap = cell / 6, size = cell - gap;
    int left = cell + cell / 2, top = gap * 2;
    for (unsigned row = 0; row < move::pad_rows; ++row) {
        int y = top + int(move::pad_rows - 1 - row) * cell;
        g.setColour(colour(surface.tracks[move::pad_rows - 1 - row]));
        g.fillEllipse(float(gap * 2), float(y + size / 4), float(size / 2), float(size / 2));
        for (unsigned column = 0; column < move::pad_columns; ++column) {
            g.setColour(colour(surface.pads[row * move::pad_columns + column]));
            g.fillRoundedRectangle(float(left + int(column) * cell), float(y), float(size), float(size), float(gap));
        }
    }
    int steps_y = top + 4 * cell + gap, step = (8 * cell) / 16;
    for (unsigned n = 0; n < move::step_count; ++n) {
        g.setColour(colour(surface.steps[n]));
        g.fillRect(left + int(n) * step, steps_y, step - gap, size / 3);
    }
    g.setColour(colour(surface.play));
    juce::Path triangle;
    float px = float(gap * 2), py = float(steps_y - gap / 2), s = float(size / 2);
    triangle.addTriangle(px, py, px, py + s, px + s, py + s / 2);
    g.fillPath(triangle);
    g.setColour(juce::Colour(0xffb8c2cc));
    move::draw_text(g, left, h - 9 * 2, surface.note, 2);
    return image;
}

juce::Image sheet(const char* title, const char* subtitle, const std::vector<Screen>& screens, const std::vector<Panel>& panels) {
    constexpr int scale = 4, gap = 24, caption = 30, top = 92;
    int screen_w = width * scale, screen_h = height * scale;
    int rows = int(screens.size() + 1) / 2, panel_rows = int(panels.size() + 1) / 2;
    int panels_top = top + rows * (screen_h + caption + gap);
    juce::Image image(juce::Image::RGB, gap + 2 * (screen_w + gap), panels_top + panel_rows * (screen_h + caption + gap), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff1b1d22));
    g.setColour(juce::Colour(0xffd8f0ff));
    move::draw_text(g, gap, 20, title, 4);
    g.setColour(juce::Colour(0xff8a96a3));
    move::draw_text(g, gap, 60, subtitle, 2);
    auto place = [&](size_t n, int from) { return juce::Point<int>(gap + int(n % 2) * (screen_w + gap), from + int(n / 2) * (screen_h + caption + gap)); };
    for (size_t n = 0; n < screens.size(); ++n) {
        auto at = place(n, top);
        g.drawImageAt(screen_image(screens[n].draw, scale), at.x, at.y);
        g.setColour(juce::Colour(0xff8a96a3));
        move::draw_text(g, at.x, at.y + screen_h + 8, screens[n].caption, 2);
    }
    for (size_t n = 0; n < panels.size(); ++n) {
        auto at = place(n, panels_top);
        g.drawImageAt(surface_image(panels[n].surface, screen_w, screen_h), at.x, at.y);
        g.setColour(juce::Colour(0xff8a96a3));
        move::draw_text(g, at.x, at.y + screen_h + 8, panels[n].caption, 2);
    }
    return image;
}

} // namespace mockup

int write_mockups(const std::string& directory) {
    using namespace mockup;
    auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory.c_str()));
    folder.createDirectory();
    struct Set { const char* name; const char* title; const char* subtitle; std::vector<Screen> screens; std::vector<Panel> panels; };
    std::vector<Set> sets {
        {"rail", "MELODEE ON MOVE: RAIL", "SAMPLER, CHOP AND SCHWUNG MODULES. DOCS/UI.MD", rail_screens(), {}},
        {"tracks", "MELODEE ON MOVE: TRACKS", "16 TRACKS, TRANSPORT, TEMPO ON THE MOVE'S OWN KEYS. DOCS/UI.MD", tracks_screens(), tracks_panels()},
    };
    bool ok = true;
    for (const auto& set : sets) {
        auto each = folder.getChildFile(set.name);
        each.createDirectory();
        for (const auto& screen : set.screens)
            ok = write_png(screen_image(screen.draw, 4), each.getChildFile(juce::String(screen.name) + ".png")) && ok;
        for (const auto& panel : set.panels)
            ok = write_png(surface_image(panel.surface, width * 4, height * 4), each.getChildFile(juce::String(panel.name) + ".png")) && ok;
        auto file = folder.getChildFile(juce::String(set.name) + ".png");
        ok = write_png(sheet(set.title, set.subtitle, set.screens, set.panels), file) && ok;
        std::cout << file.getFullPathName() << '\n';
    }
    ok = write_controls_mockups(directory) && ok;
    ok = write_pads_mockups(directory) && ok;
    ok = write_kit_mockups(directory) && ok;
    return ok ? 0 : 1;
}

} // namespace ui
