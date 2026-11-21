// SPDX-License-Identifier: AGPL-3.0-only
// A track's options (Shift + a track button, as on the Move since 1.3; its
// MIDI rows as Move 1.5 has them; Kerem, 2026-11-20): its colour, MIDI IN
// (AUTO, or a channel 1-16 that plays this track whichever is selected:
// Melodee's track channel) and MIDI OUT (AUTO, or the channel its notes go
// out on; a MIDI track's is its DEVICE channel). The wheel picks a row,
// push edits it, the wheel sets it, push again or Back done; Back leaves.
#include "app/screens/common.h"
#include "melodee/engine/midi_engine.h"

namespace app::screens {
namespace {

namespace mt = melodee::midi_track;
enum Row { Colour, MidiIn, MidiOut, RowCount };
const char* const colour_names[] {"AZURE", "ORANGE", "VIOLET", "GREEN", "YELLOW", "MAGENTA", "CYAN", "RED",
                                  "ROYAL", "PINK", "MINT", "PURPLE", "LIME", "ROSE", "SKY", "TAN"}; // ui/track_style.h

std::string row_text(const std::string& label, const std::string& value) {
    size_t gap = label.size() + value.size() < 16 ? 16 - label.size() - value.size() : 1;
    return label + std::string(gap, ' ') + value;
}

class TrackOptions final : public Screen {
public:
    using Screen::Screen;

    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override { editing = false; row = MidiIn; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (editing) change(use.delta);
            else row = std::clamp(row + use.delta, 0, RowCount - 1);
            return true;
        case Job::ListPush:
            if (row == Colour) studio.say("COLOUR: ITS PLACE"); // the track's colour follows its number, for now
            else editing = !editing;
            return true;
        case Job::Back:
            if (editing) editing = false;
            else studio.close();
            return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override { lights.button(move::Button::Back, move::white_dim); }

    void draw(juce::Graphics& g) override {
        ui::rail(g, "TRK", track_number(studio), "OPT");
        std::vector<std::string> rows;
        for (int n = 0; n < RowCount; ++n) {
            std::string shown = value(n);
            if (editing && n == row) shown = "[" + shown + "]";
            rows.push_back(row_text(n == Colour ? "COLOUR" : n == MidiIn ? "MIDI IN" : "MIDI OUT", shown));
        }
        ui::list(g, ui::content(0, 50), rows, row, 1);
        ui::detail(g, detail());
    }

    std::string status() const override {
        if (studio.top() != this) return {};
        const auto& track = studio.current();
        return " midi_in_channel=" + std::to_string(track.midi_channel) + " midi_out_channel=" +
               std::to_string(studio.midi.out_channel(studio, studio.track())) + " options_row=" + std::to_string(row);
    }

private:
    bool midi_track() const { return studio.current().engine == melodee::EngineKind::Midi; }

    std::string value(int n) const {
        const auto& track = studio.current();
        switch (n) {
        case Colour: return colour_names[studio.track() % 16];
        case MidiIn: return track.midi_channel ? std::to_string(track.midi_channel) : "AUTO";
        case MidiOut:
            if (midi_track()) return std::to_string(track.params[mt::Channel]);
            return studio.midi.track_out[studio.track()] ? std::to_string(studio.midi.track_out[studio.track()]) : "AUTO";
        default: return {};
        }
    }

    std::string detail() const {
        const auto& track = studio.current();
        switch (row) {
        case MidiIn: return track.midi_channel ? "CH " + std::to_string(track.midi_channel) + ": TRACK " + track_number(studio) : "AUTO: SELECTED";
        case MidiOut:
            if (midi_track()) return "DEVICE CHANNEL";
            return studio.midi.track_out[studio.track()] ? "ITS NOTES OUT" : "AUTO: CH " + std::to_string(studio.midi.out_channel(studio, studio.track()));
        default: return {};
        }
    }

    void change(int delta) {
        auto& track = studio.current();
        unsigned t = studio.track();
        if (row == MidiIn) {
            track.midi_channel = uint8_t(std::clamp(int(track.midi_channel) + delta, 0, 16)); // the project keeps it (MIDC)
        } else if (row == MidiOut) {
            if (midi_track()) {
                int channel = std::clamp(int(track.params[mt::Channel]) + delta, 1, 16);
                track.params[mt::Channel] = int16_t(channel);
                studio.send({melodee::Command::Type::Param, uint8_t(t), 0, uint16_t(mt::Channel), int16_t(channel)});
            } else {
                studio.midi.track_out[t] = uint8_t(std::clamp(int(studio.midi.track_out[t]) + delta, 0, 16)); // the project's MVMO section
            }
        }
        studio.changed();
    }

    int row = MidiIn;
    bool editing = false;
};

RegisterScreen options("track-options", make<TrackOptions>, {{bindings::Job::TrackOptions}});

} // namespace
} // namespace app::screens
