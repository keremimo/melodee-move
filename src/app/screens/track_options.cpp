// SPDX-License-Identifier: AGPL-3.0-only
// A track's options (Shift + a track button, the Move's own; docs/ui/
// projects.png): its colour (the wheel turns it, the track button showing it
// as it turns; a push keeps it, Back leaves it as it was) and its name (the
// name sheet). Both are undo steps. The MIDI channels are move-midi's: its
// rows go in rows() below.
#include "app/screens/common.h"
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
            if (editing) {
                int at = std::clamp(colour_index(t) + use.delta, 0, 16);
                studio.extras().track_colour[t] = at ? ui::track_colours[size_t(at - 1)].bright : 0;
            } else {
                cursor = std::clamp(cursor + use.delta, 0, int(rows().size()) - 1);
            }
            return true;
        case Job::ListPush: {
            auto row = rows()[size_t(cursor)];
            if (row == "COLOUR") {
                if (editing) { editing = false; studio.name_step("COLOUR T" + std::to_string(t + 1), int(t)); studio.changed(); }
                else { editing = true; was = studio.extras().track_colour[t]; }
                return true;
            }
            if (row == "NAME") {
                Studio* where = &studio;
                studio.close();
                studio.ask_name("NAME TRACK " + std::to_string(t + 1), studio.project().tracks[t].name, melodee::name_length - 1,
                                [where, t](const std::string& name) {
                                    where->project().tracks[t].set_name(name.c_str());
                                    where->name_step("NAME T" + std::to_string(t + 1), int(t));
                                    where->changed();
                                });
                return true;
            }
            return true;
        }
        case Job::Back:
            if (editing) { studio.extras().track_colour[t] = was; editing = false; return true; }
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
            list.push_back(drawn);
        }
        sys::setup_list(g, "TRK", "OPT", list, cursor);
        g.setColour(juce::Colours::black); // the track's number in the rail
        move::draw_text_centred(g, 0, 25, ui::rail_width, std::to_string(t + 1), 2);
        g.setColour(juce::Colours::white);
    }

    std::string status() const override {
        if (studio.top() != this) return {};
        return " options_track=" + std::to_string(studio.options_track + 1) + " options_row=" + std::to_string(cursor);
    }

private:
    int cursor = 0;
    bool editing = false;
    uint8_t was = 0;

    // The rows; move-midi adds the MIDI channels here.
    std::vector<std::string> rows() const { return {"COLOUR", "NAME"}; }
    int colour_index(unsigned t) const {
        uint8_t own = studio.extras().track_colour[t];
        for (unsigned n = 0; n < 16; ++n)
            if (ui::track_colours[n].bright == own) return int(n) + 1;
        return 0;
    }
};

RegisterScreen registered("track-options", make<TrackOptions>);

} // namespace
} // namespace app::screens
