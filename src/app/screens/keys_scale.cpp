// SPDX-License-Identifier: AGPL-3.0-only
// Keys & Scale (Shift + Step 9, the Move's own; its manual, 9.1; docs/UI.md,
// "Instruments"): the Move's list, MODE (In-Key, Chromatic; then Melodee's
// Prog and Drums), ROWS (Octaves or 4ths, In-Key's), KEY and SCALE (the
// project's, in Ableton's order of scales), then Melodee's TRANSPOSE; on
// Prog its PROG (the progressions, loaded as the wheel turns), VOICING and
// KEY SHIFT; STORE CHORD takes the pads held. The wheel walks the rows, a
// push makes the value the wheel's (push again: done), Back leaves. A kit
// track has the project's KEY and SCALE alone. The pads play on as they
// are laid out, so a layout is heard as it is chosen.
//
// The chord builder (STORE CHORD): the pads held are taken, the pads turn to
// the progression, its empty pads pulse and the pad pressed next takes the
// chord, saved into Melodee's Progressions as Mine N.
#include "app/pad_layout.h"
#include "app/screens/note_pages.h"
#include "melodee/model/progression.h"
#include "melodee/model/scales.h"
#include "melodee/shell/progression_file.h"
#include <chrono>
#include <filesystem>

namespace app::screens {
namespace {

// The Move's scales in Ableton's order (Live 12's, Push 3's), as indices of
// Melodee's scale table; Melodee's Chromatic last. Those the table lacks
// (a melodee-sample before its Move scales) are passed over.
constexpr uint8_t move_scales[] {0, 1, 2, 5, 4, 3, 6, 13, 14, 15, 11, 10, 9, 7, 16, 17, 18, 8,
                                 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 12};
std::vector<uint8_t> scale_order() {
    std::vector<uint8_t> out;
    for (uint8_t index : move_scales) if (index < melodee::scales().size()) out.push_back(index);
    return out;
}
std::string scale_name(unsigned index) { return upper(melodee::scales()[index % melodee::scales().size()].name); }
bool note_track(const melodee::Track& track) {
    return track.engine == melodee::EngineKind::Keygroup || track.engine == melodee::EngineKind::Plugin || track.engine == melodee::EngineKind::Midi;
}

enum class Row : uint8_t { Mode, Rows, Key, Scale, Transpose, Prog, Voicing, KeyShift, Store };

class StoreChord;

class KeysScale final : public Screen {
public:
    using Screen::Screen;
    void enter() override { cursor = 0; editing = false; held.fill(false); }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void input(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Pad && input.index < pad_count) held[input.index] = input.pressed;
    }

    bool job(bindings::Job job, const bindings::Use& use) override;

    void lights(move::Lights& lights) override {
        using namespace move;
        if (note_track(studio.current())) key_pads(studio, lights, held);
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu, Button::Up, Button::Down}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        std::string key = melodee::note_letter(studio.project().root);
        ui::rail(g, "SCAL", key, scale_name(studio.project().scale).substr(0, 3));
        auto shown = rows();
        std::vector<std::string> text;
        for (size_t n = 0; n < shown.size(); ++n) text.push_back(row_text(shown[n], editing && int(n) == cursor));
        ui::list(g, ui::content(0, 50), text, cursor, 1);
        auto right = std::to_string(cursor + 1) + "/" + std::to_string(shown.size());
        if (editing && shown[size_t(cursor)] == Row::Scale) {
            auto order = scale_order();
            auto at = std::find(order.begin(), order.end(), studio.project().scale);
            right = std::to_string(at - order.begin() + 1) + "/" + std::to_string(order.size());
        }
        ui::detail(g, editing ? "PUSH: OK" : "PUSH: CHANGE", right);
    }

    std::string status() const override {
        return studio.top() == this ? " keys_row=" + std::to_string(cursor) + " keys_editing=" + std::to_string(int(editing)) : std::string();
    }

    std::vector<int> held_keys() const {
        std::vector<int> keys;
        for (unsigned at = 0; at < pad_count; ++at) {
            if (!held[at]) continue;
            auto notes = pad_notes(studio.project(), studio.current(), at, studio.key_shift(studio.track()));
            for (unsigned k = 0; k < notes.count; ++k) keys.push_back(notes.keys[k]);
        }
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        return keys;
    }

private:
    int cursor = 0;
    bool editing = false;
    std::array<bool, pad_count> held {};

    std::vector<Row> rows() const {
        const auto& track = studio.current();
        if (!note_track(track)) return {Row::Key, Row::Scale}; // a kit: the project's key and scale
        switch (keys_mode(track)) {
        case KeysMode::Prog: return {Row::Mode, Row::Prog, Row::Voicing, Row::KeyShift, Row::Store};
        case KeysMode::Drums: return {Row::Mode, Row::Key, Row::Scale};
        case KeysMode::Chromatic: return {Row::Mode, Row::Key, Row::Scale, Row::Transpose, Row::Store};
        default: return {Row::Mode, Row::Rows, Row::Key, Row::Scale, Row::Transpose, Row::Store};
        }
    }

    std::string row_text(Row row, bool edit) const {
        const auto& track = studio.current();
        auto fit = [&](std::string label, std::string value) {
            if (edit) value = "[" + value + "]";
            value = value.substr(0, 14);
            label = label.substr(0, 16 - value.size() - 1);
            return label + std::string(16 - label.size() - value.size(), ' ') + value;
        };
        auto signed_text = [](int v) { return (v > 0 ? "+" : "") + std::to_string(v); };
        switch (row) {
        case Row::Mode: return fit("MODE", keys_mode_name(keys_mode(track)));
        case Row::Rows: return fit("ROWS", keys_fourths(track) ? "4THS" : "OCTAVES");
        case Row::Key: return fit("KEY", melodee::note_letter(studio.project().root));
        case Row::Scale: return fit("SCALE", scale_name(studio.project().scale));
        case Row::Transpose: return fit("TRANSPOSE", signed_text(track.transpose));
        case Row::Prog: return fit("PROG", track.prog.empty() ? "NONE >" : upper(track.prog.name).substr(0, 9) + " >");
        case Row::Voicing: return fit("VOICING", upper(melodee::prog_voicing_name(track.prog_voicing)));
        case Row::KeyShift: return fit("KEY SHIFT", signed_text(track.prog_shift));
        case Row::Store: return fit("STORE CHORD", ">");
        }
        return {};
    }

    void change(Row row, int delta) {
        const unsigned t = studio.track();
        auto& track = studio.current();
        switch (row) {
        case Row::Mode: {
            int mode = std::clamp(int(keys_mode(track)) + delta, 0, int(KeysMode::Count) - 1);
            studio.set_keys(t, KeysMode(mode), keys_fourths(track) && mode == int(KeysMode::InKey));
            break;
        }
        case Row::Rows: studio.set_keys(t, KeysMode::InKey, delta > 0); break;
        case Row::Key: studio.set_key(unsigned((int(studio.project().root) + delta % 12 + 12) % 12), studio.project().scale); break;
        case Row::Scale: {
            auto order = scale_order();
            auto at = std::find(order.begin(), order.end(), studio.project().scale);
            int index = at == order.end() ? 0 : int(at - order.begin());
            index = std::clamp(index + delta, 0, int(order.size()) - 1);
            studio.set_key(studio.project().root, order[size_t(index)]);
            break;
        }
        case Row::Transpose: studio.set_transpose(t, track.transpose + delta); break;
        case Row::Voicing: track.prog_voicing = uint8_t(std::clamp(int(track.prog_voicing) + delta, 0, int(melodee::ProgVoicing::Count) - 1)); studio.publish_pads(); studio.changed(); break;
        case Row::KeyShift: track.prog_shift = int8_t(std::clamp(int(track.prog_shift) + delta, -6, 5)); studio.publish_pads(); studio.changed(); break;
        default: break;
        }
    }
};

// The progressions on storage, the one the wheel is on loaded onto the
// track as it turns (as the Move's preset browser loads), push or Back done.
class Progressions final : public Screen {
public:
    using Screen::Screen;
    void enter() override {
        files = studio.progressions();
        cursor = 0;
        std::string loaded = studio.current().prog.file;
        for (size_t n = 0; n < files.size(); ++n)
            if (!loaded.empty() && melodee::progression_stem(files[n]) == loaded) cursor = int(n);
    }
    bindings::Screen screen_kind() const override { return bindings::Screen::Browser; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::BrowseTurn:
            if (files.empty()) return true;
            cursor = std::clamp(cursor + use.delta, 0, int(files.size()) - 1);
            if (auto failed = studio.load_progression(studio.track(), files[size_t(cursor)]); !failed.empty()) studio.say(upper(failed).substr(0, 16));
            return true;
        case Job::BrowsePush: case Job::Back: studio.close(); return true;
        case Job::PlayNote: case Job::TurnKnob: case Job::TurnKnobFine: return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        std::array<bool, pad_count> none {};
        key_pads(studio, lights, none);
        lights.button(move::Button::Back, move::white_dim);
    }
    void draw(juce::Graphics& g) override {
        ui::rail(g, "LOAD", track_number(studio), "PROG");
        if (files.empty()) {
            move::draw_text_centred(g, ui::content_x, 18, ui::content_width, "NO PROGRESSIONS");
            ui::detail(g, "MELODEE/PROGRESSIONS");
            return;
        }
        std::vector<std::string> rows;
        for (const auto& file : files) rows.push_back(upper(melodee::progression_stem(file)).substr(0, 16));
        ui::list(g, ui::content(0, 50), rows, cursor, 1);
        auto where = upper(std::filesystem::path(files[size_t(cursor)]).parent_path().filename().string());
        ui::detail(g, where.substr(0, 7), "PUSH: OK");
    }
    std::string status() const override { return studio.top() == this ? " progressions=" + std::to_string(files.size()) : std::string(); }

private:
    std::vector<std::string> files;
    int cursor = 0;
};

// STORE CHORD: the chord held, waiting for the pad it goes on.
class StoreChord final : public Screen {
public:
    using Screen::Screen;
    void take(std::vector<int> held) { keys = std::move(held); }
    void enter() override {
        // The pads turn to the progression (a new one: Mine N as it is saved).
        auto& track = studio.current();
        studio.set_keys(studio.track(), KeysMode::Prog, false);
        (void)track;
    }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::PlayNote: {
            auto& track = studio.current();
            unsigned index = 16 * track.bank + logical_pad(uint8_t(use.index), pad_layout());
            if (auto failed = studio.store_chord(studio.track(), index, keys); !failed.empty()) studio.say(failed);
            else studio.say("STORED ON " + std::to_string(index + 1));
            studio.close();
            return true;
        }
        case Job::Back: case Job::ListPush: studio.close(); return true;
        case Job::TurnKnob: case Job::TurnKnobFine: case Job::ListTurn: return true;
        default: return false;
        }
    }
    void lights(move::Lights& lights) override {
        auto colour = ui::track_colour(studio.track());
        const auto& track = studio.current();
        bool on = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 250 % 2;
        for (unsigned at = 0; at < pad_count; ++at) {
            auto notes = pad_notes(studio.project(), track, at);
            lights.pad(at, notes.count ? (notes.root ? colour.bright : colour.dim) : on ? move::colour::white : move::colour::off); // the empty ones pulse
        }
        lights.button(move::Button::Back, move::white_dim);
    }
    void draw(juce::Graphics& g) override {
        ui::rail(g, "PROG", "", "STOR");
        auto name = keys.empty() ? std::string() : melodee::name_chord(keys.data(), unsigned(keys.size()));
        if (name.empty() && !keys.empty()) name = note_name(keys[0]);
        move::draw_text_centred(g, ui::content_x, 4, ui::content_width, upper(name).substr(0, 5), 3);
        move::draw_text_centred(g, ui::content_x, 30, ui::content_width, "PRESS A PAD", 1);
        std::string into = studio.current().prog.file[0] ? upper(studio.current().prog.file).substr(0, 7) : "MINE";
        ui::detail(g, into, "BACK: NO");
    }

private:
    std::vector<int> keys;
};

bool KeysScale::job(bindings::Job job, const bindings::Use& use) {
    using bindings::Job;
    auto shown = rows();
    cursor = std::clamp(cursor, 0, int(shown.size()) - 1);
    switch (job) {
    case Job::ListTurn: case Job::ListTurnFine:
        if (editing) change(shown[size_t(cursor)], use.delta);
        else cursor = std::clamp(cursor + use.delta, 0, int(shown.size()) - 1);
        return true;
    case Job::ListPush: {
        Row row = shown[size_t(cursor)];
        if (row == Row::Prog) { studio.push("progressions"); return true; }
        if (row == Row::Store) {
            auto keys = held_keys();
            if (keys.empty()) { studio.say("HOLD THE CHORD"); return true; }
            if (auto* store = dynamic_cast<StoreChord*>(studio.screen("store-chord"))) store->take(keys);
            studio.push("store-chord");
            return true;
        }
        editing = !editing;
        return true;
    }
    case Job::Back:
        if (editing) editing = false;
        else studio.close();
        return true;
    case Job::KeysAndScales: studio.close(); return true; // Shift + Step 9 again
    case Job::PlayNote: case Job::TurnKnob: case Job::TurnKnobFine: return true;
    default: return false;
    }
}

RegisterScreen keys_scale("keys-scale", make<KeysScale>, {{bindings::Job::KeysAndScales}});
RegisterScreen progressions("progressions", make<Progressions>);
RegisterScreen store_chord("store-chord", make<StoreChord>);

} // namespace
} // namespace app::screens
