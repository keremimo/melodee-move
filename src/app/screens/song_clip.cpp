// SPDX-License-Identifier: AGPL-3.0-only
// A clip's jobs (docs/UI.md "Song"): the wheel's push on a clip on Song opens
// them as a list, as Melodee's Song has them on its keys: open its notes (the
// track's Play screen on its pattern, the sequencer's), duplicate it after
// itself, split it at the cursor, give it its own pattern, repeat it to the
// next marker, loop its bars, mute it, delete it. The wheel picks, its push
// does it and goes back to Song; Back goes back.
#include "app/screens/common.h"
#include "app/song_desk.h"

namespace app::screens {
namespace {

using bindings::Job;

class SongClip final : public Screen {
public:
    using Screen::Screen;
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override { row = 0; }

    bool job(Job job, const bindings::Use& use) override {
        auto& desk = studio.song_desk();
        int clip = desk.clip_here(studio.track());
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: row = std::clamp(row + use.delta, 0, int(rows().size()) - 1); return true;
        case Job::Back: studio.close(); return true;
        case Job::ListPush: {
            if (clip < 0) { studio.close(); return true; }
            melodee::Clip c = desk.song().clips[size_t(clip)];
            studio.close();
            switch (row) {
            case 0: { // its notes: the track's Play screen on its pattern
                studio.project().tracks[c.track].pattern = c.pattern;
                studio.send({melodee::Command::Type::Pattern, c.track, 0, 0, int16_t(c.pattern)});
                studio.select(c.track);
                studio.home();
                studio.say("PATTERN " + pattern_name(c.pattern));
                break;
            }
            case 1: desk.duplicate(clip); break;
            case 2: desk.split(clip, desk.cursor); break;
            case 3: desk.own_pattern(clip); break;
            case 4: desk.repeat_to_marker(clip); break;
            case 5: desk.loop_clip(clip); break;
            case 6: desk.toggle_mute(clip); break;
            case 7: desk.erase(clip); break;
            default: break;
            }
            return true;
        }
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Copy, Button::Delete}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        const auto& desk = studio.song_desk();
        int clip = desk.clip_here(studio.track());
        std::string name = clip >= 0 ? pattern_name(desk.song().clips[size_t(clip)].pattern) : "";
        ui::rail(g, "CLIP", name, "T" + std::to_string(studio.track() + 1));
        ui::list(g, ui::content(0, 52), rows(), row, 1);
        ui::detail(g, "PUSH: DO", "BACK");
    }

    std::string status() const override { return studio.top() == this ? " clip_row=" + std::to_string(row) : std::string(); }

private:
    int row = 0;
    std::vector<std::string> rows() const {
        const auto& desk = studio.song_desk();
        int clip = desk.clip_here(studio.track());
        bool muted = clip >= 0 && desk.song().clips[size_t(clip)].muted;
        return {"OPEN NOTES", "DUPLICATE", "SPLIT AT CURSOR", "OWN PATTERN", "REPEAT TO MARK", "LOOP ITS BARS", muted ? "UNMUTE" : "MUTE", "DELETE"};
    }
};

RegisterScreen song_clip("song-clip", make<SongClip>); // the wheel's push on a clip, on Song

} // namespace
} // namespace app::screens
