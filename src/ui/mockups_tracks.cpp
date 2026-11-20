// SPDX-License-Identifier: AGPL-3.0-only
// The tracks, transport and tempo screens (docs/UI.md, "Tracks"), as
// mockups: tracks.png. Three ways to pick among sixteen tracks (A, B, C),
// the new-track sheet, copy and delete, each track type's Play screen, the
// tempo screen, and the lights.
#include "ui/mockups.h"
#include "move/display.h"
#include "move/font.h"
#include "ui/track_style.h"
#include "ui/widgets.h"
#include <algorithm>
#include <cmath>

namespace ui {
namespace mockup {
namespace {

const juce::Colour on = juce::Colours::white, off = juce::Colours::black;

// The example project: sixteen tracks, nine in use, track 5 selected.
struct Example { const char* tag; const char* name; };
constexpr Example example[16] {
    {"KIT", "DRUMS"}, {"OBXD", "LEAD"}, {"KEYS", "GRAND"}, {"MIDI", "TB-03"},
    {"KIT", "BREAKS"}, {"AUD", "VOX"}, {"SURG", "PAD"}, {"", ""},
    {"KIT", "FOLEY"}, {"", ""}, {"", ""}, {"", ""},
    {"", ""}, {"", ""}, {"", ""}, {"", ""},
};
constexpr unsigned selected = 4; // track 5

std::string two(unsigned n) { return (n < 10 ? "0" : "") + std::to_string(n); }

// The sixteen tracks as a grid in reading order, a row a bank: 1-4, 5-8,
// 9-12, 13-16, as the steps run 1-16. A cell holds the track's kind (its
// module's short name on a synth), the selected one inverted.
void track_grid(juce::Graphics& g, juce::Rectangle<int> area, unsigned chosen) {
    int cell_w = area.getWidth() / 4, cell_h = area.getHeight() / 4;
    for (unsigned t = 0; t < 16; ++t) {
        int x = area.getX() + int(t % 4) * cell_w, y = area.getY() + int(t / 4) * cell_h;
        bool used = example[t].tag[0] != 0;
        if (t == chosen) {
            g.setColour(on);
            g.fillRect(x, y, cell_w - 1, cell_h - 1);
            g.setColour(off);
        } else {
            g.setColour(on);
            if (used) g.drawRect(x, y, cell_w - 1, cell_h - 1);
        }
        std::string text = used ? example[t].tag : std::to_string(t + 1);
        move::draw_text_centred(g, x, y + (cell_h - 7) / 2, cell_w - 1, text);
        g.setColour(on);
    }
}

// Two octaves of keys from C, the held ones filled.
void keyboard(juce::Graphics& g, juce::Rectangle<int> area, std::vector<int> held) {
    constexpr int whites = 14;
    int key = area.getWidth() / whites;
    auto is_held = [&](int note) { return std::find(held.begin(), held.end(), note) != held.end(); };
    static const int white_notes[7] {0, 2, 4, 5, 7, 9, 11};
    for (int n = 0; n < whites; ++n) {
        int note = 12 * (n / 7) + white_notes[n % 7];
        int x = area.getX() + n * key;
        g.setColour(on);
        if (is_held(note)) g.fillRect(x, area.getY(), key - 1, area.getHeight());
        else g.drawRect(x, area.getY(), key, area.getHeight());
    }
    static const int black_after[5] {0, 1, 3, 4, 5}; // the white key each black one follows
    static const int black_notes[5] {1, 3, 6, 8, 10};
    for (int octave = 0; octave < 2; ++octave)
        for (int b = 0; b < 5; ++b) {
            int x = area.getX() + (7 * octave + black_after[b] + 1) * key - key / 3;
            int note = 12 * octave + black_notes[b];
            g.setColour(off);
            g.fillRect(x - 1, area.getY(), key * 2 / 3 + 2, area.getHeight() * 3 / 5 + 1);
            g.setColour(on);
            if (is_held(note)) g.fillRect(x, area.getY(), key * 2 / 3, area.getHeight() * 3 / 5);
            else g.drawRect(x, area.getY(), key * 2 / 3, area.getHeight() * 3 / 5);
        }
}

std::vector<float> break_peaks() {
    std::vector<float> peaks(128, 0.04f);
    const struct { int at; float level, decay; } hits[] {{0, .95f, 10}, {64, .95f, 10}, {32, .75f, 7}, {96, .75f, 7}, {56, .45f, 3},
                                                         {88, .45f, 3}, {120, .5f, 4}, {16, .35f, 2}, {48, .35f, 2}, {80, .35f, 2}};
    for (int column = 0; column < 128; ++column)
        for (const auto& hit : hits)
            if (column >= hit.at) peaks[size_t(column)] = std::min(1.0f, peaks[size_t(column)] + hit.level * std::exp(-(column - hit.at) / hit.decay));
    return peaks;
}

// The lights of the example: the bank of the selected track on the track
// buttons, dim, the selected one bright; empty tracks dark.
std::array<uint8_t, 4> bank_buttons(unsigned chosen = selected) {
    std::array<uint8_t, 4> lights {};
    unsigned first = bank_of(chosen) * tracks_a_bank;
    for (unsigned n = 0; n < 4; ++n) {
        unsigned t = first + n;
        lights[n] = !example[t].tag[0] ? 0 : t == chosen ? track_colour(t).bright : track_colour(t).dim;
    }
    return lights;
}
std::array<uint8_t, move::pad_count> kit_pads(uint8_t colour, int last) {
    std::array<uint8_t, move::pad_count> pads {};
    for (unsigned n = 0; n < 9; ++n) pads[n] = colour; // nine pads with sounds, bottom left first
    if (last >= 0) pads[size_t(last)] = move::colour::white;
    return pads;
}

} // namespace

std::vector<Screen> tracks_screens() {
    auto peaks = break_peaks();
    return {
        // Picking a track: three ways, one to choose.
        {"pick-a", "A: HOLD A TRACK BUTTON. STEPS PICK", [](juce::Graphics& g) {
            rail(g, "TRKS", "5", "/16");
            track_grid(g, content(0, 52), selected);
            detail(g, "05 KIT BREAKS");
        }},
        {"pick-b", "B: SHIFT + TRACK BUTTON: BANK 2", [](juce::Graphics& g) {
            rail(g, "BANK", "2", "5-8");
            std::vector<std::string> rows;
            for (unsigned t = 4; t < 8; ++t)
                rows.push_back(std::to_string(t + 1) + " " + (example[t].tag[0] ? std::string(example[t].tag) + "  " + example[t].name : "-"));
            list(g, content(0, 52), rows, int(selected - 4), 1);
            detail(g, "ROW 1-4: TRACK");
        }},
        {"pick-c", "C: SHIFT + MENU: TRACK LIST", [](juce::Graphics& g) {
            rail(g, "TRKS", "5", "/16");
            std::vector<std::string> rows;
            for (unsigned t = 0; t < 16; ++t)
                rows.push_back(two(t + 1) + " " + (example[t].tag[0] ? std::string(example[t].tag) + " " + example[t].name : "-"));
            list(g, content(0, height), rows, int(selected), 1);
        }},
        {"new-track", "EMPTY STEP HELD: NEW TRACK 8", [](juce::Graphics& g) {
            rail(g, "NEW", "8", "TRK");
            list(g, content(0, 50), {"KIT      32 PADS", "SYNTH    SCHWUNG", "KEYGROUP     SFZ", "MIDI         OUT", "AUDIO      INPUT"}, 0, 1);
            detail(g, "PUSH: MAKE");
        }},
        {"copy", "COPY + STEPS 5, 9: ONTO A USED TRACK", [](juce::Graphics& g) {
            rail(g, "COPY", "5", "> 9");
            move::draw_text(g, content_x + 2, 4, "TRACK 5 TO 9", 1);
            move::draw_text(g, content_x + 2, 18, "9 HAS FOLEY:", 1);
            move::draw_text(g, content_x + 2, 28, "REPLACE IT?", 1);
            detail(g, "PUSH YES", "BACK NO");
        }},
        {"delete", "DELETE + STEP 5", [](juce::Graphics& g) {
            rail(g, "DEL", "5", "KIT");
            move::draw_text(g, content_x + 2, 4, "DELETE TRACK 5?", 1);
            move::draw_text(g, content_x + 2, 18, "BREAKS, 9 PADS", 1);
            move::draw_text(g, content_x + 2, 28, "UNDO BRINGS IT", 1);
            detail(g, "PUSH YES", "BACK NO");
        }},
        // Each kind of track, playing.
        {"play-kit", "KIT TRACK 5, PLAYING: BAR 3 BEAT 2", [=](juce::Graphics& g) {
            rail(g, "KIT", "5", "A05");
            auto area = content(0, subject_height);
            waveform(g, area, peaks);
            playhead(g, area, 41);
            detail(g, "BREAK 94", "3.2");
        }},
        {"play-synth", "SYNTH TRACK 2: A SCHWUNG MODULE", [](juce::Graphics& g) {
            rail(g, "OBXD", "2", "1/5");
            move::draw_text(g, content_x + 1, 1, "MAIN");
            move::draw_text(g, width - 2 * move::glyph_advance, 1, "C2");
            knob_cells(g, content(11, subject_height - 11), {"CUTOFF", "RESO", "ENVAMT", "DETUNE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"},
                       {0.62f, 0.3f, 0.75f, 0.2f, 0.05f, 0.45f, 0.8f, 0.35f});
            detail(g, "BRASS LEAD", "012");
        }},
        {"play-keys", "KEYGROUP TRACK 3 (SCREENS: WAVE 2)", [](juce::Graphics& g) {
            rail(g, "KEYS", "3", "C2");
            move::draw_text(g, content_x + 1, 1, "GRAND PIANO");
            keyboard(g, content(12, 38).reduced(1, 0), {4, 7, 12});
            detail(g, "SFZ 88 ZONES", "E2");
        }},
        {"play-midi", "MIDI TRACK 4 (SCREENS: WAVE 2)", [](juce::Graphics& g) {
            rail(g, "MIDI", "4", "CH1");
            move::draw_text(g, content_x + 2, 2, "CH 1", 3);
            move::draw_text(g, content_x + 2, 30, "TB-03");
            move::draw_text(g, content_x + 2, 40, "LAST C3 100");
            detail(g, "USB MIDI OUT");
        }},
        {"play-audio", "AUDIO TRACK 6 (SCREENS: WAVE 2)", [](juce::Graphics& g) {
            rail(g, "AUD", "6", "IN");
            meter(g, content(6, 24).reduced(2, 0), 0.48f, 0.44f, 0.0f);
            move::draw_text(g, content_x + 2, 38, "TAKE 2  4 BARS");
            detail(g, "LINE IN", "MON");
        }},
        {"play-empty", "AN EMPTY TRACK, 8", [](juce::Graphics& g) {
            rail(g, "----", "8", "");
            move::draw_text_centred(g, content_x, 18, content_width, "EMPTY", 2);
            detail(g, "PUSH: NEW TRACK");
        }},
        // Transport and tempo.
        {"tempo", "SHIFT + PLAY: TEMPO", [](juce::Graphics& g) {
            rail(g, "TMPO", "", "4/4");
            move::draw_text(g, content_x + 4, 4, "120.0", 3);
            move::draw_text(g, content_x + 4, 28, "BPM");
            move::draw_text(g, content_x + 4, 40, "SWING 54%");
            move::draw_text(g, width - move::text_width("CLICK"), 40, "CLICK");
            g.fillRect(width - move::text_width("CLICK"), 48, move::text_width("CLICK") - 1, 1); // on
            detail(g, "PUSH: CLICK", "3.2");
        }},
        {"tempo-touch", "TEMPO: KNOB 1 TOUCHED", [](juce::Graphics& g) {
            rail(g, "KNOB", "1", "TMPO");
            big_parameter(g, content(4, 44), "TEMPO", "120.0", 0.4f);
            move::draw_text_centred(g, content_x, detail_y, content_width, "BPM, SHIFT: 0.1");
        }},
    };
}

std::vector<Panel> tracks_panels() {
    using namespace move::colour;
    auto five = track_colour(selected);
    std::vector<Panel> panels;
    {   // A kit track playing: the bank on the track buttons, the playhead on the steps.
        Surface s;
        s.tracks = bank_buttons();
        s.pads = kit_pads(five.bright, 4);
        s.steps[5] = white;
        s.play = green;
        s.note = "KIT TRACK 5 PLAYING";
        panels.push_back({"lights-play", "LIGHTS: KIT 5, PLAYING, STEP 6", s});
    }
    {   // A: a track button held: the steps are the sixteen tracks.
        Surface s;
        s.tracks = bank_buttons();
        s.tracks[0] = white;
        s.pads = kit_pads(five.dim, -1);
        for (unsigned t = 0; t < 16; ++t)
            s.steps[t] = !example[t].tag[0] ? dark_grey : t == selected ? white : track_colour(t).bright;
        s.play = green;
        s.note = "A: ROW 1 HELD, STEPS = TRACKS";
        panels.push_back({"lights-pick-a", "LIGHTS A: STEP 8 GREY = EMPTY: NEW", s});
    }
    {   // B: Shift held: the track buttons are the four banks.
        Surface s;
        for (unsigned b = 0; b < 4; ++b) s.tracks[b] = b == bank_of(selected) ? white : b < 3 ? light_grey : dark_grey;
        s.pads = kit_pads(five.dim, -1);
        s.play = green;
        s.note = "B: SHIFT HELD, ROWS = BANKS";
        panels.push_back({"lights-pick-b", "LIGHTS B: SHIFT HELD", s});
    }
    {   // A synth track: every C in the track's colour, the other naturals dim.
        Surface s;
        s.tracks = bank_buttons(1);
        auto two_colour = track_colour(1);
        for (unsigned pad = 0; pad < move::pad_count; ++pad) {
            int note = int(pad % 8) + 5 * int(pad / 8);
            int in_octave = note % 12;
            bool natural = in_octave != 1 && in_octave != 3 && in_octave != 6 && in_octave != 8 && in_octave != 10;
            s.pads[pad] = in_octave == 0 ? two_colour.bright : natural ? dark_grey : 0;
        }
        s.pads[9] = white;
        s.steps[9] = white;
        s.play = green;
        s.note = "SYNTH TRACK 2, A NOTE HELD";
        panels.push_back({"lights-synth", "LIGHTS: SYNTH 2 IN BANK 1", s});
    }
    {   // The tempo screen: the steps count the beats.
        Surface s;
        s.tracks = bank_buttons();
        s.pads = kit_pads(five.dim, -1);
        for (unsigned beat = 0; beat < 4; ++beat) s.steps[beat * 4] = beat == 1 ? white : light_grey;
        s.play = green;
        s.note = "TEMPO: BEAT 2 OF 4";
        panels.push_back({"lights-tempo", "LIGHTS: TEMPO, THE BEAT ON THE STEPS", s});
    }
    return panels;
}

} // namespace mockup
} // namespace ui
