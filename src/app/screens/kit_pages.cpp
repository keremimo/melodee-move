// SPDX-License-Identifier: AGPL-3.0-only
// A kit's pad pages (docs/UI.md "Kits", docs/ui/kit.png): Melodee's Stage
// pages on the last pad, as the Move's parameter banks, on Shift + turn
// from the kit's Play screen: TRIM, LAYERS, TUNE, AMP ENV, PLAY, CLOUD,
// MIX, FILTER, KIT FILT, eight knobs each (Kerem, 2026-11-20). Shift +
// turn walks them, past either end back to Play; Back too. The pads play
// and pick the pad as on Play; a knob touched shows large (its full name),
// Shift finer. On LAYERS the wheel's push loads a sample onto the layer in
// hand (knob 1) and Shift + push's Sample Options can remove it. TRIM,
// LAYERS and CLOUD draw their picture, the rest their eight knobs as cells.
#include "app/screens/kit_common.h"
#include "app/screens/zones.h"
#include "melodee/engine/sampler.h"

namespace app::screens {
namespace {

using melodee::PadSound;
namespace sp = melodee::sampler;

// What a knob edits: the pad's sound (or the layer in hand's), or one of the
// kit's own values (`kit`, a melodee::sampler::Param).
struct Edit {
    PadSound& sound;
    const melodee::Track& track;
    unsigned layer; // 0-3, LAYERS' layer in hand
    uint32_t frames; // the pad's sample, in the project's frames (1 for none)
};
struct Value {
    const char* name;  // touched: its full name
    const char* cell;  // the page's cell, three characters
    int low, high;
    int (*get)(const Edit&);
    void (*set)(Edit&, int);
    std::string (*text)(const Edit&, int);
    int kit = -1;        // a kit value: melodee::sampler::Param
    bool frames = false; // a place in the sample: a turn moves 1/256 of it (Shift 1/8192)
    int coarse = 1;      // a turn's step
};

std::string number(const Edit&, int v) { return std::to_string(v); }
std::string percent(const Edit&, int v) { return std::to_string(v) + "%"; }
std::string on_off(const Edit&, int v) { return v ? "ON" : "OFF"; }
std::string semitones(const Edit&, int v) { return (v > 0 ? "+" : "") + std::to_string(v) + " ST"; }
std::string cents(const Edit&, int v) { return (v > 0 ? "+" : "") + std::to_string(v) + " CT"; }
std::string place(const Edit&, int v) { return seconds(uint32_t(v)); }
std::string pan_text(const Edit&, int v) { return v == 0 ? "C" : (v < 0 ? "L" : "R") + std::to_string(std::abs(v)); }
std::string level_text(const Edit&, int v) {
    if (v <= 0) return "-INF";
    char text[16];
    std::snprintf(text, sizeof text, "%+.1f DB", double(melodee::volume_db(v)));
    return text;
}
std::string hundredths_db(const Edit&, int v) {
    char text[16];
    std::snprintf(text, sizeof text, "%+.1f DB", v / 100.0);
    return text;
}
std::string pad_or_off(const Edit& e, int v) { // Pad Link: a pad of the same bank
    return v ? pad_label(e.track.last_pad / 16 * 16 + unsigned(v - 1)) : "OFF";
}
std::string group_text(const Edit&, int v) { return v ? std::to_string(v) : "OFF"; }

// The sixteen track colours on the Move, by name (ui/track_style.h), then the pad's own.
constexpr const char* colour_names[] {"TRACK", "AZURE", "ORANGE", "VIOLET", "GREEN", "YELLOW", "MAGENTA", "CYAN", "RED",
                                      "ROYAL BLUE", "PINK", "MINT", "PURPLE", "LIME", "ROSE", "SKY", "TAN", "ITS OWN"};
uint32_t colour_rgb(int choice) {
    auto rgb = move::palette_rgb(ui::track_colours[size_t(choice - 1) % 16].bright);
    return uint32_t(rgb.r) << 16 | uint32_t(rgb.g) << 8 | rgb.b;
}
int colour_choice(uint32_t colour) {
    if (!colour) return 0;
    for (int n = 1; n <= 16; ++n) if (colour_rgb(n) == colour) return n;
    return 17;
}

// Warp: Off, Seq, then 50-200 % by fives, as a knob's places.
int warp_place(int warp) { return warp == melodee::warp_off ? 0 : warp == melodee::warp_seq ? 1 : 2 + (std::clamp(warp, 50, 200) - 50) / 5; }
int warp_value(int place) { return place <= 0 ? melodee::warp_off : place == 1 ? melodee::warp_seq : 50 + (place - 2) * 5; }

// A layer's values: layer 1 is the pad's own (its tune, level and pan are the pad's).
melodee::PadLayer* layer_of(Edit& e) { return e.layer ? &e.sound.layers[(e.layer - 1) % 3] : nullptr; }

const std::array<const char*, 9> page_names {"TRIM", "LAYERS", "TUNE", "AMP ENV", "PLAY", "CLOUD", "MIX", "FILTER", "KIT FILT"};
enum Page { Trim, Layers, Tune, Amp, Play, Cloud, Mix, Filter, KitFilter, PageCount };

const std::array<std::array<Value, 8>, PageCount>& pages() {
    static const std::array<std::array<Value, 8>, PageCount> table {{
        {{  // TRIM
            {"START", "STA", 0, 0, [](const Edit& e) { return int(e.sound.start); },
             [](Edit& e, int v) { e.sound.start = uint32_t(std::clamp<int64_t>(v, 0, int64_t(e.sound.end ? e.sound.end : e.frames) - melodee::min_slice)); }, place, -1, true},
            {"END", "END", 0, 0, [](const Edit& e) { return int(e.sound.end ? e.sound.end : e.frames); },
             [](Edit& e, int v) { uint32_t end = uint32_t(std::clamp<int64_t>(v, int64_t(e.sound.start) + melodee::min_slice, e.frames)); e.sound.end = end >= e.frames ? 0 : end; }, place, -1, true},
            {"LOOP POINT", "LPT", 0, 0, [](const Edit& e) { return int(e.sound.loop_point()); },
             [](Edit& e, int v) { e.sound.loop_lock = false; e.sound.loop_start = uint32_t(std::clamp<int64_t>(v, e.sound.start, int64_t(e.sound.end ? e.sound.end : e.frames) - 1)); }, place, -1, true},
            {"LOOP LOCK", "LCK", 0, 1, [](const Edit& e) { return int(e.sound.loop_lock); }, [](Edit& e, int v) { e.sound.loop_lock = v; }, on_off},
            {"GAIN", "GAN", -4800, 2400, [](const Edit& e) { return int(e.sound.gain); }, [](Edit& e, int v) { e.sound.gain = int16_t(v); }, hundredths_db, -1, false, 50},
            {}, {}, {},
        }},
        {{  // LAYERS: knob 1 the layer in hand (the screen's own)
            {"LAYER", "LYR", 1, 4, nullptr, nullptr, number},
            {"LOW VELOCITY", "LOW", 1, 127,
             [](const Edit& e) { return int(e.layer ? e.sound.layers[(e.layer - 1) % 3].low : e.sound.low_velocity); },
             [](Edit& e, int v) { if (auto* l = layer_of(e)) { l->low = uint8_t(v); l->high = std::max(l->high, l->low); } else { e.sound.low_velocity = uint8_t(v); e.sound.high_velocity = std::max(e.sound.high_velocity, e.sound.low_velocity); } }, number},
            {"HIGH VELOCITY", "HIG", 1, 127,
             [](const Edit& e) { return int(e.layer ? e.sound.layers[(e.layer - 1) % 3].high : e.sound.high_velocity); },
             [](Edit& e, int v) { if (auto* l = layer_of(e)) { l->high = uint8_t(v); l->low = std::min(l->low, l->high); } else { e.sound.high_velocity = uint8_t(v); e.sound.low_velocity = std::min(e.sound.low_velocity, e.sound.high_velocity); } }, number},
            {"LAYER TUNE", "TUN", -24, 24, [](const Edit& e) { return int(e.layer ? e.sound.layers[(e.layer - 1) % 3].pitch : e.sound.pitch); },
             [](Edit& e, int v) { if (auto* l = layer_of(e)) l->pitch = int8_t(v); else e.sound.pitch = int8_t(v); }, semitones},
            {"LAYER FINE", "FIN", -99, 99, [](const Edit& e) { return int(e.layer ? e.sound.layers[(e.layer - 1) % 3].fine : e.sound.fine); },
             [](Edit& e, int v) { if (auto* l = layer_of(e)) l->fine = int8_t(v); else e.sound.fine = int8_t(v); }, cents},
            {"LAYER LEVEL", "LVL", -4800, 1200, [](const Edit& e) { return int(e.layer ? e.sound.layers[(e.layer - 1) % 3].gain : e.sound.gain); },
             [](Edit& e, int v) { if (auto* l = layer_of(e)) l->gain = int16_t(v); else e.sound.gain = int16_t(v); }, hundredths_db, -1, false, 50},
            {"LAYER PAN", "PAN", -50, 50, [](const Edit& e) { return int(e.layer ? e.sound.layers[(e.layer - 1) % 3].pan : e.sound.pan); },
             [](Edit& e, int v) { if (auto* l = layer_of(e)) l->pan = int8_t(v); else e.sound.pan = int8_t(v); }, pan_text},
            {"HOW A HIT PICKS", "HOW", 0, int(melodee::LayerPlay::Count) - 1, [](const Edit& e) { return int(e.sound.layer_play); },
             [](Edit& e, int v) { e.sound.layer_play = melodee::LayerPlay(v); },
             [](const Edit&, int v) { return std::string(melodee::layer_play_name(melodee::LayerPlay(v))); }},
        }},
        {{  // TUNE: the pad's tune and Warp, the kit's Follow and warp mode
            {"TUNE", "TUN", -36, 36, [](const Edit& e) { return int(e.sound.pitch); }, [](Edit& e, int v) { e.sound.pitch = int8_t(v); }, semitones},
            {"FINE TUNE", "FIN", -99, 99, [](const Edit& e) { return int(e.sound.fine); }, [](Edit& e, int v) { e.sound.fine = int8_t(v); }, cents, -1, false, 5},
            {"WARP", "WRP", 0, 32, [](const Edit& e) { return warp_place(e.sound.warp); }, [](Edit& e, int v) { e.sound.warp = uint8_t(warp_value(v)); },
             [](const Edit&, int v) { int w = warp_value(v); return std::string(w == melodee::warp_off ? "OFF" : w == melodee::warp_seq ? "SEQ" : std::to_string(w) + "%"); }},
            {"# BEATS", "BTS", 0, int(melodee::max_pad_beats), [](const Edit& e) { return int(e.sound.beats); }, [](Edit& e, int v) { e.sound.beats = uint8_t(v); },
             [](const Edit&, int v) { return v ? std::to_string(v) : std::string("AUTO"); }},
            {"FOLLOW", "FLW", 0, 1, [](const Edit& e) { return int(e.track.params[sp::Follow]); }, nullptr, on_off, sp::Follow},
            {"FOLLOW MODE", "HOW", 0, 2, [](const Edit& e) { return int(e.track.params[sp::FollowHow]); }, nullptr,
             [](const Edit&, int v) { static const char* n[] {"BEATS", "TONES", "TAPE"}; return std::string(n[std::clamp(v, 0, 2)]); }, sp::FollowHow},
            {"PITCH", "PIT", 0, 1, [](const Edit& e) { return int(e.track.params[sp::PitchHow]); }, nullptr,
             [](const Edit&, int v) { return std::string(v ? "STRETCH" : "SPEED"); }, sp::PitchHow},
            {},
        }},
        {{  // AMP ENV
            {"ATTACK", "ATT", 0, 100, [](const Edit& e) { return int(e.sound.attack); }, [](Edit& e, int v) { e.sound.attack = uint8_t(v); }, number},
            {"DECAY", "DEC", 0, 100, [](const Edit& e) { return int(e.sound.decay); }, [](Edit& e, int v) { e.sound.decay = uint8_t(v); }, number},
            {"SUSTAIN", "SUS", 0, 100, [](const Edit& e) { return int(e.sound.sustain); }, [](Edit& e, int v) { e.sound.sustain = uint8_t(v); }, number},
            {"RELEASE", "REL", 0, 100, [](const Edit& e) { return int(e.sound.release); }, [](Edit& e, int v) { e.sound.release = uint8_t(v); }, number},
            {"VELOCITY SENSE", "VEL", 0, 100, [](const Edit& e) { return int(e.sound.velocity_sense); }, [](Edit& e, int v) { e.sound.velocity_sense = uint8_t(v); }, percent},
            {"DECAY FROM", "FRM", 0, 1, [](const Edit& e) { return int(e.sound.decay_from_start); }, [](Edit& e, int v) { e.sound.decay_from_start = v; },
             [](const Edit&, int v) { return std::string(v ? "START" : "END"); }},
            {}, {},
        }},
        {{  // PLAY
            {"PLAY", "PLY", 0, 1, [](const Edit& e) { return int(e.sound.play == melodee::PadPlay::Gate); },
             [](Edit& e, int v) { e.sound.play = v ? melodee::PadPlay::Gate : melodee::PadPlay::OneShot; }, [](const Edit&, int v) { return std::string(v ? "NOTE ON" : "ONE SHOT"); }},
            {"MONO", "MON", 0, 1, [](const Edit& e) { return int(e.sound.mono); }, [](Edit& e, int v) { e.sound.mono = v; }, [](const Edit&, int v) { return std::string(v ? "MONO" : "POLY"); }},
            {"MUTE GROUP", "GRP", 0, 16, [](const Edit& e) { return int(e.sound.group); }, [](Edit& e, int v) { e.sound.group = uint8_t(v); }, group_text},
            {"OFFSET", "DLY", 0, 100, [](const Edit& e) { return int(e.sound.offset); }, [](Edit& e, int v) { e.sound.offset = uint8_t(v); }, percent},
            {"PAD LINK", "LNK", 0, 16, [](const Edit& e) { return int(e.sound.link); }, [](Edit& e, int v) { e.sound.link = uint8_t(v); }, pad_or_off},
            {"LOOP", "LP", 0, 1, [](const Edit& e) { return int(e.sound.loop); }, [](Edit& e, int v) { e.sound.loop = v; }, on_off},
            {"REVERSE", "REV", 0, 1, [](const Edit& e) { return int(e.sound.reverse); }, [](Edit& e, int v) { e.sound.reverse = v; }, on_off},
            {"XFADE", "XFD", 0, int(melodee::max_loop_xfade), [](const Edit& e) { return int(e.sound.loop_xfade); }, [](Edit& e, int v) { e.sound.loop_xfade = uint8_t(v); },
             [](const Edit&, int v) { return v ? std::to_string(v) + " MS" : std::string("OFF"); }, -1, false, 5},
        }},
        {{  // CLOUD
            {"CLOUD", "ON", 0, 1, [](const Edit& e) { return int(e.sound.cloud); }, [](Edit& e, int v) { e.sound.cloud = v; }, on_off},
            {"POSITION", "POS", 0, 100, [](const Edit& e) { return int(e.sound.cloud_position); }, [](Edit& e, int v) { e.sound.cloud_position = uint8_t(v); }, percent},
            {"SIZE", "SIZ", 0, 100, [](const Edit& e) { return int(e.sound.cloud_size); }, [](Edit& e, int v) { e.sound.cloud_size = uint8_t(v); }, percent},
            {"DENSITY", "DNS", 0, 100, [](const Edit& e) { return int(e.sound.cloud_density); }, [](Edit& e, int v) { e.sound.cloud_density = uint8_t(v); }, number},
            {"SCAN", "SCN", 0, 100, [](const Edit& e) { return int(e.sound.cloud_scan); }, [](Edit& e, int v) { e.sound.cloud_scan = uint8_t(v); },
             [](const Edit&, int v) { return v == 50 ? std::string("STILL") : (v > 50 ? "+" : "") + std::to_string(v - 50); }},
            {"SPREAD", "SPR", 0, 100, [](const Edit& e) { return int(e.sound.cloud_spread); }, [](Edit& e, int v) { e.sound.cloud_spread = uint8_t(v); }, number},
            {}, {},
        }},
        {{  // MIX
            {"LEVEL", "LVL", 0, 127, [](const Edit& e) { return int(e.sound.level); }, [](Edit& e, int v) { e.sound.level = uint8_t(v); }, level_text},
            {"PAN", "PAN", -50, 50, [](const Edit& e) { return int(e.sound.pan); }, [](Edit& e, int v) { e.sound.pan = int8_t(v); }, pan_text},
            {"KIT VOLUME", "KIT", 0, 127, [](const Edit& e) { return int(e.track.params[sp::Level]); }, nullptr, level_text, sp::Level},
            {"COLOUR", "COL", 0, 17, [](const Edit& e) { return colour_choice(e.sound.colour); },
             [](Edit& e, int v) { if (v < 17) e.sound.colour = v ? colour_rgb(v) : 0; }, [](const Edit&, int v) { return std::string(colour_names[std::clamp(v, 0, 17)]); }},
            {}, {}, {}, {},
        }},
        {{  // FILTER and its envelope
            {"FILTER TYPE", "TYP", 0, int(melodee::pad_filter_count) - 1, [](const Edit& e) { return int(e.sound.filter); },
             [](Edit& e, int v) { e.sound.filter = melodee::PadFilter(v); }, [](const Edit&, int v) { return upper(melodee::pad_filter_name(melodee::PadFilter(v))); }},
            {"CUTOFF", "CUT", 0, 100, [](const Edit& e) { return int(e.sound.cutoff); }, [](Edit& e, int v) { e.sound.cutoff = uint8_t(v); }, number},
            {"RESONANCE", "RES", 0, 100, [](const Edit& e) { return int(e.sound.resonance); }, [](Edit& e, int v) { e.sound.resonance = uint8_t(v); }, number},
            {"ENVELOPE", "ENV", 0, 100, [](const Edit& e) { return int(e.sound.filter_amount); }, [](Edit& e, int v) { e.sound.filter_amount = uint8_t(v); }, percent},
            {"FILTER ATTACK", "ATT", 0, 100, [](const Edit& e) { return int(e.sound.filter_attack); }, [](Edit& e, int v) { e.sound.filter_attack = uint8_t(v); }, number},
            {"FILTER DECAY", "DEC", 0, 100, [](const Edit& e) { return int(e.sound.filter_decay); }, [](Edit& e, int v) { e.sound.filter_decay = uint8_t(v); }, number},
            {"FILTER SUSTAIN", "SUS", 0, 100, [](const Edit& e) { return int(e.sound.filter_sustain); }, [](Edit& e, int v) { e.sound.filter_sustain = uint8_t(v); }, number},
            {"FILTER RELEASE", "REL", 0, 100, [](const Edit& e) { return int(e.sound.filter_release); }, [](Edit& e, int v) { e.sound.filter_release = uint8_t(v); }, number},
        }},
        {{  // KIT FILT: one filter after every pad
            {"KIT FILTER TYPE", "TYP", 0, 2, [](const Edit& e) { return int(e.track.params[sp::FilterType]); }, nullptr,
             [](const Edit&, int v) { static const char* n[] {"LOW PASS", "BAND PASS", "HIGH PASS"}; return std::string(n[std::clamp(v, 0, 2)]); }, sp::FilterType},
            {"KIT CUTOFF", "CUT", 0, 100, [](const Edit& e) { return int(e.track.params[sp::Filter]); }, nullptr, number, sp::Filter},
            {"KIT RESONANCE", "RES", 0, 100, [](const Edit& e) { return int(e.track.params[sp::Resonance]); }, nullptr, number, sp::Resonance},
            {}, {}, {}, {}, {},
        }},
    }};
    return table;
}

class KitPages final : public Screen {
public:
    using Screen::Screen;

    void enter() override { fresh = true; }
    void leave() override { studio.layer_target = {}; }

    void input(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Touch && !input.pressed && int(input.index) == fine) fine = -1;
    }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::Page: {
            // Past either end, back to Play.
            int to = fresh ? (use.delta > 0 ? 0 : PageCount - 1) : page + (use.delta > 0 ? 1 : -1);
            fresh = false;
            if (to < 0 || to >= PageCount) { studio.close(); return true; }
            page = to;
            fine = -1;
            return true;
        }
        case Job::Back: studio.close(); return true;
        case Job::TurnKnob: turn(unsigned(use.index), use.delta, false); return true;
        case Job::TurnKnobFine: turn(unsigned(use.index), use.delta, true); return true;
        case Job::SecondParameter: fine = use.index; return true;
        case Job::OpenBrowser: // on LAYERS, the sample of the layer in hand
            if (page == Layers && layer > 0) {
                if (!studio.current().pads[studio.pad()].used()) { studio.say("EMPTY PAD"); return true; }
                studio.layer_target = {int(studio.track()), int(studio.pad()), layer};
                studio.push("load-samples");
                return true;
            }
            return false; // the Play screen's: the pad's sample
        case Job::SampleOptions:
            if (page == Layers && layer > 0) {
                studio.layer_target = {int(studio.track()), int(studio.pad()), layer};
                if (studio.current().pads[studio.pad()].layers[size_t(layer - 1)].used()) studio.push("sample-options");
                else studio.say("LAYER EMPTY");
                return true;
            }
            return false;
        default: return false; // the pads, Plus and Minus, Copy and Delete: the Play screen's
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        if (auto* play = studio.screen("kit-play")) play->lights(lights); // the pads and buttons as on Play
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        std::string number = std::to_string(page + 1) + "/" + std::to_string(int(PageCount));
        const Sample* zoned = page == Trim ? instrument() : nullptr;
        ui::rail(g, "PAD", pad_label(studio.pad()), zoned ? "ZONE" : number);
        if (fine >= 0) { draw_fine(g); return; }
        auto& sound = studio.current().pads[studio.pad()];
        const Sample* sample = studio.sample_of_pad();
        if (zoned) { draw_zones(g, studio, *zoned, studio.pad(), zone, 1); return; } // TRIM's place on an instrument pad
        switch (page) {
        case Trim: draw_trim(g, sound, sample); return;
        case Layers: draw_layers(g, sound); return;
        case Cloud: draw_cloud(g, sound, sample); return;
        default: break;
        }
        move::draw_text(g, ui::content_x + 2, 1, page_names[size_t(page)], 2);
        std::array<std::string, 8> names {};
        std::array<float, 8> values {};
        for (unsigned n = 0; n < 8; ++n) {
            auto shown = knob(n);
            if (shown.name.empty()) continue;
            names[n] = pages()[size_t(page)][n].cell;
            values[n] = shown.fraction;
        }
        ui::knob_cells(g, ui::content(18, 46), names, values);
    }

    Knob knob(unsigned which) const override {
        if (which >= 8) return {};
        if (page == Trim)
            if (const Sample* zoned = instrument()) return zone_knob(studio, *zoned, studio.pad(), zone, which);
        const Value& value = pages()[size_t(page)][which];
        if (!value.name) return {};
        const melodee::Track& kit = studio.current();
        PadSound sound = kit.pads[studio.pad()];
        Edit edit {sound, kit, unsigned(layer), frames()};
        if (page == Layers && which == 0) {
            const auto& one = sound.layer(unsigned(layer));
            const Sample* sample = one.used() ? studio.sample_in(one.sample) : nullptr;
            return {"LAYER", std::to_string(layer + 1) + (sample ? "" : " --"), layer / 3.0f};
        }
        int v = value.get(edit);
        int low = value.low, high = value.frames ? int(edit.frames) : value.high;
        float fraction = high > low ? float(v - low) / float(high - low) : 0.0f;
        return {value.name, value.text(edit, v), fraction};
    }
    std::string knob_owner() const override { return pad_label(studio.pad()); }
    std::string status() const override {
        if (studio.top() != this) return {};
        const char* name = page == Trim && instrument() ? "ZONES" : page_names[size_t(page)];
        return " kit_page=" + std::string(name) + " layer=" + std::to_string(layer + 1) + " kit_zone=" + std::to_string(zone + 1);
    }

private:
    int page = 0;
    int layer = 0; // LAYERS' layer in hand, 0-3
    int fine = -1;
    bool fresh = true; // opened: the first Shift + turn picks the first or last page
    unsigned zone = 0; // ZONES' zone in hand (an instrument pad's TRIM)

    // The pad's instrument (an SFZ, EXS or DecentSampler preset loaded onto it): its TRIM page is ZONES.
    const Sample* instrument() const { return zones_of(studio.sample_of_pad()); }

    uint32_t frames() const {
        const Sample* sample = studio.sample_of_pad();
        return sample ? std::max<uint32_t>(1, sample->data->model_frames()) : 1;
    }

    void turn(unsigned which, int delta, bool fine_turn) {
        if (which >= 8) return;
        if (page == Trim)
            if (const Sample* zoned = instrument()) { zone_turn(studio, *zoned, studio.pad(), zone, which, delta, fine_turn); return; }
        const Value& value = pages()[size_t(page)][which];
        if (!value.name) return;
        if (page == Layers && which == 0) { layer = std::clamp(layer + (delta > 0 ? 1 : -1), 0, 3); return; }
        auto& kit = studio.current();
        if (value.kit >= 0) { // a kit value
            int v = std::clamp(int(kit.params[size_t(value.kit)]) + delta, value.low, value.high);
            studio.set_kit_param(studio.track(), value.kit, v);
            return;
        }
        if (!kit.pads[studio.pad()].used()) return;
        PadSound sound = kit.pads[studio.pad()];
        Edit edit {sound, kit, unsigned(layer), frames()};
        if (page == Layers && layer > 0 && !sound.layers[size_t(layer - 1)].used() && which != 7) { studio.say("PUSH: LOAD LAYER"); return; }
        int step = value.frames ? int(std::max<uint32_t>(1, edit.frames / (fine_turn ? 8192 : 256))) : fine_turn ? 1 : value.coarse;
        int high = value.frames ? int(edit.frames) : value.high;
        int64_t v = std::clamp<int64_t>(int64_t(value.get(edit)) + int64_t(delta) * step, value.low, high);
        value.set(edit, int(v));
        studio.set_pad(studio.track(), studio.pad(), sound);
    }

    // Shift + a knob touched: its value in full, finer to turn; on TRIM the
    // start, end and loop point close up (the Move's Sample Start zoom).
    void draw_fine(juce::Graphics& g) {
        auto shown = knob(unsigned(fine));
        if (shown.name.empty()) return;
        const Sample* sample = studio.sample_of_pad();
        if (page == Trim && fine <= 2 && sample && !instrument()) {
            const auto& sound = studio.current().pads[studio.pad()];
            uint32_t frames = std::max<uint32_t>(1, sample->data->model_frames());
            uint32_t at = fine == 0 ? sound.start : fine == 1 ? (sound.end ? sound.end : frames) : sound.loop_point();
            if (close_up(g, *sample, at)) { ui::detail(g, shown.value, "X32"); return; }
        }
        ui::big_parameter(g, ui::content(4, 44), shown.name, shown.value, shown.fraction);
        ui::detail(g, "SHIFT: FINE");
    }

    void title(juce::Graphics& g, const char* name, std::string_view right) {
        move::draw_text(g, ui::content_x + 2, 1, name);
        if (!right.empty()) move::draw_text(g, ui::width - move::text_width(right), 1, right);
    }
    static void flag(juce::Graphics& g, juce::Rectangle<int> area, int column, const char* letter) {
        int x = area.getX() + std::clamp(column, 0, area.getWidth() - 1);
        g.setColour(juce::Colours::white);
        for (int y = area.getY(); y < area.getBottom(); y += 2) g.fillRect(x, y, 1, 1);
        g.fillRect(x, area.getY(), 7, 9);
        g.setColour(juce::Colours::black);
        move::draw_text(g, x + 1, area.getY() + 1, letter);
        g.setColour(juce::Colours::white);
    }

    void draw_trim(juce::Graphics& g, const PadSound& sound, const Sample* sample) {
        title(g, "TRIM", sound.loop_lock ? "LOCK" : "");
        if (!sample) { move::draw_text_centred(g, ui::content_x, 24, ui::content_width, sound.used() ? "MEMORY FULL" : "EMPTY"); return; }
        auto area = ui::content(10, 40);
        ui::waveform(g, area, peaks_of(sample->overview));
        uint32_t frames = std::max<uint32_t>(1, sample->data->model_frames()), end = sound.end ? sound.end : frames;
        int w = area.getWidth();
        flag(g, area, int(int64_t(sound.start) * w / frames), "S");
        flag(g, area, int(int64_t(end) * w / frames) - 7, "E");
        if (sound.loop && !sound.loop_lock) flag(g, area, int(int64_t(sound.loop_point()) * w / frames), "L");
        ui::detail(g, seconds(end - std::min(end, sound.start)), "OF " + seconds(frames));
    }

    void draw_layers(juce::Graphics& g, const PadSound& sound) {
        title(g, "LAYERS", sound.layer_play == melodee::LayerPlay::Velocity ? "VEL" : sound.layer_play == melodee::LayerPlay::Cycle ? "CYC" : "RND");
        for (unsigned n = 0; n < melodee::pad_layers; ++n) {
            int y = 11 + int(n) * 10;
            const auto one = sound.layer(n);
            const Sample* sample = one.used() ? studio.sample_in(one.sample) : nullptr;
            std::string name = sample ? upper(sample->name.substr(0, sample->name.rfind('.'))) : "--";
            move::draw_text(g, ui::content_x + 2, y + 1, std::to_string(n + 1));
            move::draw_text(g, ui::content_x + 10, y + 1, name.substr(0, 10));
            if (one.used()) { // its velocities across 1-127
                int x0 = 98 + one.low * 28 / 127, x1 = 98 + one.high * 28 / 127;
                g.fillRect(98, y + 7, 28, 1);
                g.fillRect(x0, y + 4, std::max(1, x1 - x0), 3);
            }
            if (int(n) == layer) g.drawRect(ui::content_x, y - 1, ui::content_width, 10);
        }
        const auto one = sound.layer(unsigned(layer));
        char text[24], gain[16];
        std::snprintf(text, sizeof text, "VEL %d-%d", one.low, one.high);
        std::snprintf(gain, sizeof gain, "%+.1f DB", one.gain / 100.0);
        ui::detail(g, layer > 0 && !one.used() ? "PUSH: LOAD" : text, layer > 0 && one.used() ? gain : "");
    }

    void draw_cloud(juce::Graphics& g, const PadSound& sound, const Sample* sample) {
        title(g, "CLOUD", sound.cloud ? "ON" : "OFF");
        if (!sample) { move::draw_text_centred(g, ui::content_x, 24, ui::content_width, "EMPTY"); return; }
        auto area = ui::content(10, 40);
        uint32_t frames = std::max<uint32_t>(1, sample->data->model_frames()), end = sound.end ? sound.end : frames;
        int w = area.getWidth();
        int from = int(int64_t(sound.start) * w / frames), to = int(int64_t(end) * w / frames);
        int at = from + (to - from) * sound.cloud_position / 100, half = std::max(1, (to - from) * sound.cloud_size / 400);
        ui::waveform(g, area, peaks_of(sample->overview), at - half, at + half);
        g.fillRect(area.getX() + std::clamp(at, 0, w - 1), area.getY(), 1, area.getHeight());
        uint32_t place = sound.start + uint32_t(uint64_t(end - std::min(end, sound.start)) * sound.cloud_position / 100);
        ui::detail(g, seconds(place), std::to_string(sound.cloud_density) + "/S");
    }
};

RegisterScreen registered("kit-pages", make<KitPages>);

} // namespace
} // namespace app::screens
