// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/instruments.h"
#include "app/screens/common.h"
#include "melodee/engine/command.h"
#include "melodee/engine/sample_data.h"
#include <algorithm>
#include <cstdio>
#include <string>

// ZONES (Melodee's "instrument C", MELODEE.md › Sampler, SFZ): an
// instrument's zones on a keygroup track's sound or a kit pad, the one in
// hand picked by knob 1, its tune (knob 2, -100..100 cents) and level (knob
// 3, -24..+6 dB in tenths) trimmed on that pad without touching the file
// (the track's ZTRM, at most 64). Shared by keys-play (pad 0) and the kit's
// pad pages (TRIM's place on an instrument pad, docs/ui/kit.png).
namespace app::screens {

// The pad's instrument, if what it plays is one with its zones built.
inline const Sample* zones_of(const Sample* sample) {
    return sample && sample->instrument && sample->data && sample->data->multi ? sample : nullptr;
}

inline unsigned zone_in_hand(const Sample& sample, unsigned zone) {
    const auto& zones = sample.data->multi->zones;
    return zones.empty() ? 0 : std::min<unsigned>(zone, unsigned(zones.size()) - 1);
}

inline Knob zone_knob(Studio& studio, const Sample& sample, unsigned pad, unsigned zone, unsigned which) {
    const auto& zones = sample.data->multi->zones;
    if (zones.empty()) return {};
    unsigned z = zone_in_hand(sample, zone);
    const auto& track = studio.current();
    const auto* trim = melodee::find_trim(track.zone_trims, pad, z, track.pads[pad % melodee::sampler_pads].sample);
    if (which == 0) return {"ZONE", std::to_string(z + 1) + "/" + std::to_string(zones.size()), float(z) / float(std::max<size_t>(1, zones.size() - 1))};
    // "ZONE TUNE", "ZONE LEVEL": not the pad's TUNE and LEVEL, which name a kit pad's Motion lanes (sequencer.cpp)
    if (which == 1) { int c = trim ? trim->tune : 0; return {"ZONE TUNE", std::to_string(c) + " CT", (c + 100) / 200.0f}; }
    if (which == 2) {
        int l = trim ? trim->level : 0;
        char text[16];
        std::snprintf(text, sizeof text, "%+.1f DB", l / 10.0);
        return {"ZONE LEVEL", text, (l + 240) / 300.0f};
    }
    return {};
}

// Knob `which` turned on ZONES: the zone walked, or its trim set and sent to the engine.
inline void zone_turn(Studio& studio, const Sample& sample, unsigned pad, unsigned& zone, unsigned which, int delta, bool fine) {
    const auto& zones = sample.data->multi->zones;
    if (zones.empty() || !delta) return;
    if (which == 0) { zone = unsigned(std::clamp(int(zone) + delta, 0, int(zones.size()) - 1)); return; }
    if (which > 2) return;
    auto& track = studio.current();
    const unsigned slot = track.pads[pad % melodee::sampler_pads].sample;
    unsigned z = zone_in_hand(sample, zone);
    const auto* had = melodee::find_trim(track.zone_trims, pad, z, slot);
    int tune = had ? had->tune : 0, level = had ? had->level : 0;
    if (which == 1) tune = std::clamp(tune + delta, -100, 100);
    else level = std::clamp(level + delta * (fine ? 1 : 5), -240, 60);
    if (!melodee::set_trim(track.zone_trims, pad, z, slot, tune, level)) { studio.say("TRIMS FULL"); return; }
    melodee::ZoneTrim trim;
    trim.pad = uint8_t(pad);
    trim.slot = uint16_t(slot);
    trim.zone = uint16_t(z);
    trim.tune = int8_t(tune);
    trim.level = int16_t(level);
    studio.send(melodee::zone_trim_command(studio.track(), trim));
    studio.changed();
}

// The zones over the piano's keys (21-108), a tick a zone's lowest key, the one in hand filled; while its files land, a
// bar of how many have; then the zone's keys, trims, file and root.
inline void draw_zones(juce::Graphics& g, Studio& studio, const Sample& sample, unsigned pad, unsigned zone, int top = 11) {
    const auto& multi = *sample.data->multi;
    const auto& ins = *sample.instrument;
    const bool loading = !ins.done();
    unsigned z = zone_in_hand(sample, zone);
    std::string right = loading ? std::to_string(ins.landed.load()) + "/" + std::to_string(ins.parts.size())
                                : "Z" + std::to_string(z + 1) + "/" + std::to_string(multi.zones.size());
    move::draw_text(g, ui::content_x + 1, top, "ZONES");
    move::draw_text(g, ui::width - move::text_width(right), top, right);
    auto area = ui::content(top + 10, 13).reduced(1, 0);
    auto x_of = [&](int key) { return area.getX() + (std::clamp(key, 21, 109) - 21) * area.getWidth() / 88; };
    g.setColour(juce::Colours::white);
    g.drawRect(area);
    for (size_t n = 0; n < multi.zones.size(); ++n) {
        const auto& each = multi.zones[n];
        if (each.on_release) continue;
        int from = x_of(each.low_key), to = x_of(each.high_key + 1);
        if (n == z && !loading) g.fillRect(from, area.getY(), std::max(1, to - from), area.getHeight());
        else g.fillRect(from, area.getY() + area.getHeight() / 2, 1, area.getHeight() / 2);
    }
    for (int c = 24; c <= 108; c += 12) g.fillRect(x_of(c), area.getBottom() + 1, 1, 2);
    if (loading) {
        g.drawRect(ui::content_x + 1, 41, ui::content_width - 4, 5);
        g.fillRect(ui::content_x + 2, 42, int((ui::content_width - 6) * ins.landed.load() / std::max<size_t>(1, ins.parts.size())), 3);
        ui::detail(g, "LOADING", ins.format);
        return;
    }
    if (multi.zones.empty()) return;
    const auto& picked = multi.zones[z];
    const auto& track = studio.current();
    const auto* trim = melodee::find_trim(track.zone_trims, pad, z, track.pads[pad % melodee::sampler_pads].sample);
    char text[32];
    std::snprintf(text, sizeof text, "%s-%s %+d %+.0fDB", note_name(picked.low_key).c_str(), note_name(picked.high_key).c_str(), trim ? trim->tune : 0,
                  trim ? trim->level / 10.0 : 0.0);
    move::draw_text(g, ui::content_x + 1, 41, text);
    std::string file = picked.part < ins.parts.size() ? ins.parts[picked.part] : "";
    auto slash = file.find_last_of('/');
    if (slash != std::string::npos) file = file.substr(slash + 1);
    ui::detail(g, upper(file).substr(0, 12), note_name(picked.root));
}

} // namespace app::screens
