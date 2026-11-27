// SPDX-License-Identifier: AGPL-3.0-only
// A track's options (Shift + a track button, the Move's own; docs/ui/
// projects.png): its colour (the wheel turns it, the track button showing it
// as it turns; a push keeps it, Back leaves it as it was) and its name (the
// name sheet). Both are undo steps. The MIDI channels are move-midi's (Kerem,
// 2026-11-20: as the Move 1.5 has them): MIDI IN (AUTO, or a channel 1-16
// that plays the track whichever is selected: Melodee's track channel) and
// MIDI OUT (AUTO, or the channel its notes go out on: Studio::midi's
// track_out; a MIDI track's is its DEVICE channel), turned as the colour is.
#include "app/screens/common.h"
#include "melodee/engine/midi_engine.h"
#include "ui/system_draw.h"

namespace app::screens {
namespace {

namespace sys = ui::system;
constexpr const char* colour_names[17] {"OWN", "AZURE", "ORANGE", "VIOLET", "GREEN", "YELLOW", "MAGENTA", "CYAN", "RED",
                                        "ROYAL", "PINK", "MINT", "PURPLE", "LIME", "ROSE", "SKY", "TAN"};

class TrackOptions final : public Screen {
public:
    using Screen::Screen;

    void enter() override { cursor = 0; editing = false; }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        unsigned t = studio.options_track % melodee::track_count;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (editing && rows()[size_t(cursor)] != "COLOUR") {
                set_channel(t, rows()[size_t(cursor)], channel(t, rows()[size_t(cursor)]) + use.delta);
            } else if (editing) {
                studio.project().tracks[t].colour = uint8_t(std::clamp(colour_index(t) + use.delta, 0, 16)); // Track::colour (TCOL)
            } else {
                cursor = std::clamp(cursor + use.delta, 0, int(rows().size()) - 1);
            }
            return true;
        case Job::ListPush: {
            auto row = rows()[size_t(cursor)];
            if (row == "COLOUR") {
                if (editing) { editing = false; studio.name_step("COLOUR T" + std::to_string(t + 1), int(t)); studio.changed(); }
                else { editing = true; was = studio.project().tracks[t].colour; }
                return true;
            }
            if (row == "MIDI IN" || row == "MIDI OUT") {
                if (editing) { editing = false; studio.name_step(row + " T" + std::to_string(t + 1), int(t)); studio.changed(); }
                else { editing = true; was_channel = channel(t, row); }
                return true;
            }
            if (row == "NAME") {
                Studio* where = &studio;
                studio.close();
                studio.ask_name("NAME TRACK " + std::to_string(t + 1), studio.project().tracks[t].name, melodee::name_length - 1,
                                [where, t](const std::string& name) {
                                    where->project().tracks[t].set_name(name.c_str());
                                    where->project().tracks[t].named = true; // kept when its sound or module changes (TNAM)
                                    where->name_step("NAME T" + std::to_string(t + 1), int(t));
                                    where->changed();
                                });
                return true;
            }
            return true;
        }
        case Job::Back:
            if (editing && rows()[size_t(cursor)] != "COLOUR") { set_channel(t, rows()[size_t(cursor)], was_channel); editing = false; return true; }
            if (editing) { studio.project().tracks[t].colour = was; editing = false; return true; }
            studio.close();
            return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        if (auto* play = studio.screen(home_screen(studio.kind())); play && play != this) play->lights(lights);
        lights.button(move::Button::Back, move::white_dim);
    }

    void draw(juce::Graphics& g) override {
        unsigned t = studio.options_track % melodee::track_count;
        std::vector<sys::SetupRow> list;
        for (const auto& row : rows()) {
            sys::SetupRow drawn;
            drawn.name = row;
            if (row == "COLOUR") { drawn.value = colour_names[colour_index(t)]; drawn.editing = editing; }
            if (row == "NAME") { drawn.value = upper(studio.project().tracks[t].name).substr(0, 7); drawn.door = false; }
            if (row == "MIDI IN" || row == "MIDI OUT") {
                int value = channel(t, row);
                drawn.value = value ? std::to_string(value) : "AUTO";
                drawn.editing = editing && rows()[size_t(cursor)] == row;
            }
            list.push_back(drawn);
        }
        sys::setup_list(g, "TRK", "OPT", list, cursor);
        const auto& track = studio.project().tracks[t];
        if (rows()[size_t(cursor)] == "MIDI IN") // what the row means, on the detail line
            ui::detail(g, track.midi_channel ? "CH " + std::to_string(track.midi_channel) + ": TRACK " + std::to_string(t + 1) : "AUTO: SELECTED");
        else if (rows()[size_t(cursor)] == "MIDI OUT")
            ui::detail(g, midi_track(t) ? "DEVICE CHANNEL" : studio.midi.track_out[t] ? "ITS NOTES OUT" : "AUTO: CH " + std::to_string(studio.midi.out_channel(studio, t)));
        g.setColour(juce::Colours::black); // the track's number in the rail
        move::draw_text_centred(g, 0, 25, ui::rail_width, std::to_string(t + 1), 2);
        g.setColour(juce::Colours::white);
    }

    std::string status() const override {
        if (studio.top() != this) return {};
        unsigned t = studio.options_track % melodee::track_count;
        return " options_track=" + std::to_string(studio.options_track + 1) + " options_row=" + std::to_string(cursor) +
               " midi_in_channel=" + std::to_string(studio.project().tracks[t].midi_channel) + " midi_out_channel=" +
               std::to_string(studio.midi.out_channel(studio, t));
    }

private:
    int cursor = 0;
    bool editing = false;
    uint8_t was = 0;
    int was_channel = 0;

    std::vector<std::string> rows() const { return {"COLOUR", "NAME", "MIDI IN", "MIDI OUT"}; }

    // MIDI IN and MIDI OUT (move-midi): 0 AUTO, else the channel.
    bool midi_track(unsigned t) const { return studio.project().tracks[t].engine == melodee::EngineKind::Midi; }
    int channel(unsigned t, const std::string& row) const {
        const auto& track = studio.project().tracks[t];
        if (row == "MIDI IN") return track.midi_channel;
        return midi_track(t) ? track.params[melodee::midi_track::Channel] : studio.midi.track_out[t];
    }
    void set_channel(unsigned t, const std::string& row, int value) {
        auto& track = studio.project().tracks[t];
        if (row == "MIDI IN") {
            track.midi_channel = uint8_t(std::clamp(value, 0, 16)); // the project keeps it (MIDC)
        } else if (midi_track(t)) {
            value = std::clamp(value, 1, 16);
            track.params[melodee::midi_track::Channel] = int16_t(value);
            studio.send({melodee::Command::Type::Param, uint8_t(t), 0, uint16_t(melodee::midi_track::Channel), int16_t(value)});
        } else {
            studio.midi.track_out[t] = uint8_t(std::clamp(value, 0, 16)); // the project's MVMO section
        }
    }
    // 0 OWN (its place's), 1-16 the track colour picked (ui::track_colours).
    int colour_index(unsigned t) const { return std::min<int>(studio.project().tracks[t].colour, 16); }
};

RegisterScreen registered("track-options", make<TrackOptions>);

} // namespace
} // namespace app::screens
