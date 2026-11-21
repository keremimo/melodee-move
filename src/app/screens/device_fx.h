// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/screens/common.h"
#include "ui/mix_widgets.h"
#include <vector>

// The Device View's mix and effects (docs/UI.md, "Mixer and effects";
// docs/MOVE_CONTROLS.md, Decisions: effects in the Device View), shared by
// the Play screens and a bus's or the master's chain: the items after the
// sound (a kit pad's own effects, then the track's, each chain ending in a
// `+` while it has room), what the knobs do on them, what the subject
// shows, and the track item's mix (level, pan, the four sends, its meter).
namespace app::screens {

class DeviceFx {
public:
    explicit DeviceFx(Studio& studio) : studio(studio) {}

    // One effect item: slot `slot` of `chain` (the next free one, on a `+`).
    struct Item { unsigned chain = 0, slot = 0; bool plus = false, pad = false; };
    // A track's effect items: on a kit, its last pad's own first.
    std::vector<Item> track_items() const;
    // A bus's or the master's: its slots, then `+`.
    std::vector<Item> chain_items(unsigned chain) const;
    ui::mix::Item row_item(const Item& item) const; // as the Device View's row draws it
    std::string rail_bottom(const Item& item) const; // FX1-FX6, a pad's own A05
    // The row: the track item, the sound item, then `items`; `focus` 0 the
    // track, 1 the sound, 2 on the effects.
    void draw_row(juce::Graphics& g, const std::string& track_item, const std::string& sound_item, const std::vector<Item>& items, int focus) const;

    // An effect item's jobs: its knobs, its push (the browser), Shift + push
    // (its options), Mute, Delete and Copy held + push, Shift + turn (a
    // Schwung effect's pages). False: not an effect item's job.
    bool job(const Item& item, bindings::Job job, const bindings::Use& use);
    void input(const move::Input& input); // Copy let go: a pending paste forgotten
    void draw(juce::Graphics& g, const Item& item) const; // the subject under the row
    Knob knob(const Item& item, unsigned which) const;
    // Knob n as a Motion lane (Melodee's mix in Motion: Seq Record writes it, a held step locks it): an effect's
    // parameter on the track's own chain or the master's (melodee/model/limits.h motion_fx_param, motion_master_fx).
    // A bus's chain, a kit pad's own and a Schwung effect's have none.
    bool lane(const Item& item, unsigned which, unsigned& param) const;

    // The track item: its mix on the knobs (1 level, 2 pan, 3-6 sends A-D);
    // on a kit, Shift + turn to its last pad's own sends A-D (knobs 1-4).
    bool mix_job(unsigned track, bindings::Job job, const bindings::Use& use);
    void draw_mix(juce::Graphics& g, unsigned track) const;
    Knob mix_knob(unsigned track, unsigned which) const;
    // The track item's knob n as a Motion lane: level, pan, sends A-D of the selected track (motion_level ..
    // motion_send); on a kit's pad sends, that pad's own (`pad`).
    bool mix_lane(unsigned track, unsigned which, unsigned& param, unsigned& pad) const;
    std::string mix_bottom() const { return pad_sends ? "SND" : "MIX"; } // the rail's

private:
    Studio& studio;
    bool copied = false; // Copy held + push copied: another push, Copy still held, pastes
    bool pad_sends = false; // the kit's track item on its last pad's sends
    FxModule* schwung(const Item& item) const;
};

// A level 0..1 from a linear peak, as the meters draw it (-48 to 0 dB).
float meter_fraction(float peak);

} // namespace app::screens
