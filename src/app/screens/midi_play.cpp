// SPDX-License-Identifier: AGPL-3.0-only
// A MIDI track's Play screen (docs/UI.md, "MIDI and sync"; docs/ui/midi.png):
// the Device View's items along the top (the track, its sound, FX1, FX2:
// the wheel walks them, the push opens the sound's browser), the page's
// knobs as names and values, the last message out. Shift + the wheel walks
// its nine pages, as a module's: DEVICE, PROGRAM, CC 1-8, 9-16, 17-24,
// EXPRESS, PADS, AUDIO, SYSEX. On a CC page Shift + a knob is what that slot
// sends (Kerem, 2026-11-20: the Move's second parameter): touched, it shows;
// turned, it changes. The track's parameters are Melodee's MIDI engine's
// (engine/midi_engine.h), so motion, undo and the file come with them.
#include "app/pad_layout.h"
#include "app/screens/common.h"
#include "melodee/engine/engine_info.h"
#include "melodee/engine/midi_engine.h"
#include "melodee/model/midi_track.h"
#include "melodee/model/scales.h"
#include <cstring>

namespace app::screens {
namespace {

namespace mt = melodee::midi_track;
using melodee::Command;

constexpr int lowest_octave = -3, highest_octave = 4;
enum Page { Device, Program, Cc1, Cc2, Cc3, Express, Pads, Audio, Sysex, PageCount };
const char* const titles[] {"DEVICE  USB MIDI", "PROGRAM", "CC 1-8", "CC 9-16", "CC 17-24", "EXPRESS", "PADS", "AUDIO  LINE IN", "SYSEX"};

// A slot's address as its cell names it, in four characters.
std::string short_label(int address) {
    int n = mt::slot_number(address);
    switch (mt::slot_kind(address)) {
    case mt::SlotKind::Cc: return n == 1 ? "MOD" : n < 100 ? "CC" + std::to_string(n) : "C" + std::to_string(n);
    case mt::SlotKind::Cc14: return "C" + std::to_string(n) + "+"; // 14 bits: its MSB, the LSB at +32
    case mt::SlotKind::PitchBend: return "BEND";
    case mt::SlotKind::ChannelPressure: return "PRES";
    case mt::SlotKind::Nrpn: return "N" + std::to_string(n);
    default: return "SLOT";
    }
}
std::string long_label(int address) {
    auto label = mt::slot_label(address);
    if (mt::slot_kind(address) == mt::SlotKind::Cc)
        if (const char* name = mt::cc_name(unsigned(mt::slot_number(address)))) label += " " + upper(name);
    if (mt::slot_kind(address) == mt::SlotKind::Off) label = "NOTHING";
    return label;
}
std::string off_or(int value, int shift = 0) { return value <= 0 ? "OFF" : std::to_string(value + shift); }
const char* audio_name(int value) {
    static const char* const names[] {"NONE", "L", "R", "L+R", "MIC", "USB"};
    return names[std::clamp(value, 0, 5)];
}
const char* pressure_name(int value) {
    static const char* const names[] {"POLY", "CHAN", "OFF"};
    return names[std::clamp(value, 0, 2)];
}

class MidiPlay final : public Screen {
public:
    using Screen::Screen;

    void enter() override { held.fill(false); address_shown = -1; }

    void input(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Pad && input.index < pad_count) {
            held[input.index] = input.pressed; // physical: as lit
            // The pad by its number in the chosen layout (app/pad_layout.h): the map's pad NOTE edits.
            if (input.pressed && drums()) studio.current().last_pad = uint8_t(drum_pad(logical_pad(uint8_t(input.index), pad_layout())));
        }
        if (input.kind == move::Input::Kind::Touch && !input.pressed && int(input.index) == address_shown) address_shown = -1;
    }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        auto& track = studio.current();
        if (track.engine != melodee::EngineKind::Midi) return false;
        switch (job) {
        case Job::PlayNote: return true; // it went from the audio thread
        case Job::TurnKnob: turn(unsigned(use.index), use.delta); return true;
        case Job::TurnKnobFine: // Shift: on a CC page, what the slot sends; else finer, which nothing here needs
            if (slot_page()) address(unsigned(use.index), use.delta);
            else turn(unsigned(use.index), use.delta);
            return true;
        case Job::SecondParameter:
            if (!slot_page()) return false;
            address_shown = use.index;
            return true;
        case Job::Octave: {
            int way = use.control == bindings::Control::Plus ? 1 : -1;
            if (drums()) {
                track.bank = uint8_t(std::clamp(int(track.bank) + way, 0, int(melodee::sampler_pads / 16) - 2));
                studio.say("PADS " + pad_label(16u * track.bank) + "-" + pad_label(16u * track.bank + 31));
            } else {
                int octave = std::clamp(studio.octave(studio.track()) + way, lowest_octave, highest_octave);
                studio.set_octave(studio.track(), octave);
                studio.say("OCTAVE " + note_name(studio.note_for(0, octave)));
            }
            studio.changed();
            return true;
        }
        // The Device View: the track, its sound, its effects.
        case Job::Focus: focus = std::clamp(focus + (use.delta > 0 ? 1 : -1), 0, 3); return true;
        case Job::ShowFocus: studio.say(focus == 0 ? "MIDI TRACK" : focus == 1 ? "SOUND: " + sound_name() : "EFFECTS"); return true;
        case Job::OpenBrowser:
            if (focus == 1) studio.open("midi-sounds");
            else if (focus >= 2) studio.say("EFFECTS: NOT YET");
            else studio.say("MIDI TRACK " + std::to_string(studio.track() + 1));
            return true;
        case Job::Page: page = (page + (use.delta > 0 ? 1 : PageCount - 1)) % PageCount; address_shown = -1; return true;
        default: return false;
        }
    }

    // The touched knob as the app shows it large; with Shift on a CC page, what it sends.
    Knob knob(unsigned n) const override {
        if (n >= 8) return {};
        if (slot_page() && studio.shift) {
            int address = slot_address(n);
            return {"KNOB " + std::to_string(n + 1) + " SENDS", mt::slot_label(address), float(address + 1) / float(mt::address_last + 1)};
        }
        auto cell = cell_of(n);
        if (cell.name.empty()) return {};
        return {cell.long_name, cell.value, cell.fraction};
    }
    std::string knob_detail(unsigned n) const override {
        if (!slot_page() || n >= 8) return {};
        return studio.shift ? long_label(slot_address(n)) : "SHIFT: ADDRESS";
    }
    std::string knob_owner() const override { return slot_page() && studio.shift ? "ADR" : upper(studio.current().name).substr(0, 4); }

    void lights(move::Lights& lights) override {
        using namespace move;
        if (drums()) {
            auto colour = ui::track_colour(studio.track());
            const auto& track = studio.current();
            auto layout = pad_layout();
            for (unsigned logical = 0; logical < pad_count; ++logical) { // each drum pad lit where the layout puts it
                unsigned at = physical_pad(uint8_t(logical), layout), pad = drum_pad(logical);
                bool named = melodee::gm_drum_name(track.midi.drum_note[pad % melodee::sampler_pads]) != nullptr;
                lights.pad(at, held[at] || pad == track.last_pad ? colour::white : named ? colour.bright : colour.dim);
            }
        } else {
            note_pads(studio, lights, held, true);
        }
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu}) lights.button(button, 0);
        if (drums()) {
            lights.button(Button::Up, studio.current().bank + 2u < melodee::sampler_pads / 16 ? white_dim : 0);
            lights.button(Button::Down, studio.current().bank > 0 ? white_dim : 0);
        } else {
            int octave = studio.octave(studio.track());
            lights.button(Button::Up, octave < highest_octave ? white_dim : 0);
            lights.button(Button::Down, octave > lowest_octave ? white_dim : 0);
        }
        lights.button(Button::Back, 0);
    }

    void draw(juce::Graphics& g) override {
        const auto& track = studio.current();
        ui::rail(g, "MIDI", track_number(studio), std::to_string(page + 1) + "/" + std::to_string(PageCount));
        items(g, {upper(track.name), sound_name(), "FX1", "FX2"}, focus);
        if (address_shown >= 0 && slot_page()) { // Shift + a knob touched: what it sends
            int address = slot_address(unsigned(address_shown));
            ui::rail(g, "KNOB", std::to_string(address_shown + 1), "ADR");
            g.setColour(juce::Colours::black);
            g.fillRect(ui::content(0, ui::height));
            g.setColour(juce::Colours::white);
            ui::big_parameter(g, ui::content(4, 44), "KNOB " + std::to_string(address_shown + 1) + " SENDS", mt::slot_label(address),
                              float(address + 1) / float(mt::address_last + 1));
            ui::detail(g, long_label(address).substr(0, 16));
            return;
        }
        move::draw_text(g, ui::content_x + 1, 11, title());
        std::array<std::string, 8> names {}, values {};
        for (unsigned n = 0; n < 8; ++n) { auto cell = cell_of(n); names[n] = cell.name; values[n] = cell.value; }
        ui::knob_values(g, ui::content(20, 32), names, values);
        int sending = studio.midi.sysex_percent(studio.track());
        if (page == Sysex) ui::detail(g, upper(std::string(track.midi.sysex)).substr(0, 12), sending >= 0 ? std::to_string(sending) + "%" : "");
        else ui::detail(g, studio.midi.last_out(studio.track()).empty() ? "CH " + std::to_string(studio.midi.out_channel(studio, studio.track()))
                                                                         : "OUT " + studio.midi.last_out(studio.track()),
                        bar_beat(studio));
    }

    std::string status() const override {
        if (studio.top() != this) return {};
        return " midi_page=" + std::to_string(page + 1) + " midi_channel=" + std::to_string(studio.current().params[mt::Channel]);
    }

private:
    struct Cell { std::string name, value, long_name; float fraction = 0; };

    bool slot_page() const { return page >= Cc1 && page <= Cc3; }
    bool drums() const { return studio.current().layout == melodee::PadLayout::Drums; }
    unsigned drum_pad(unsigned logical) const { return (16u * studio.current().bank + logical) % melodee::sampler_pads; } // the bank's, by logical pad
    int slot_of(unsigned n) const { return (page - Cc1) * 8 + int(n); }
    int slot_address(unsigned n) const { return studio.current().params[size_t(mt::slot_address(slot_of(n)))]; }
    std::string sound_name() const { return studio.current().preset == 0 ? "MIDI" : "SND"; }
    std::string title() const {
        const auto& track = studio.current();
        if (page == Pads && drums()) {
            unsigned note = track.midi.drum_note[track.last_pad % melodee::sampler_pads];
            const char* name = melodee::gm_drum_name(note);
            return "PADS  " + pad_label(track.last_pad) + " " + upper(name ? name : note_name(int(note)));
        }
        if (page == Sysex) {
            auto files = studio.midi.sysex_files(studio);
            auto at = std::find(files.begin(), files.end(), std::string(track.midi.sysex));
            return files.empty() ? "SYSEX  NO FILES" : at == files.end() ? "SYSEX  " + std::to_string(files.size()) + " FILES"
                                                                         : "SYSEX  " + std::to_string(at - files.begin() + 1) + " OF " + std::to_string(files.size());
        }
        return titles[page];
    }
    int param(int id) const { return studio.current().params[size_t(id)]; }
    float fraction(int id) const {
        const auto& info = melodee::engine_info(melodee::EngineKind::Midi).params[id];
        return info.max > info.min ? float(param(id) - info.min) / float(info.max - info.min) : 0.0f;
    }
    int pattern_program_id() const { return mt::PatternProgram0 + int(studio.current().pattern % 16); }

    Cell cell_of(unsigned n) const {
        const auto& track = studio.current();
        switch (page) {
        case Device:
            if (n == 0) return {"CHAN", std::to_string(param(mt::Channel)), "MIDI CHANNEL", fraction(mt::Channel)};
            if (n == 1) return {"DLY", std::to_string(param(mt::Delay)), "DELAY MS", fraction(mt::Delay)};
            return {};
        case Program:
            if (n == 0) return {"MSB", off_or(param(mt::BankMsb), -1), "BANK MSB", fraction(mt::BankMsb)};
            if (n == 1) return {"LSB", off_or(param(mt::BankLsb), -1), "BANK LSB", fraction(mt::BankLsb)};
            if (n == 2) return {"PROG", off_or(param(mt::Program)), "PROGRAM", fraction(mt::Program)};
            if (n == 3) return {"PAT", param(pattern_program_id()) ? std::to_string(param(pattern_program_id())) : "TRK", "THIS PATTERN'S", fraction(pattern_program_id())};
            return {};
        case Cc1: case Cc2: case Cc3: {
            int address = slot_address(n);
            if (mt::slot_kind(address) == mt::SlotKind::Off) return {"SLOT", "-", "SLOT " + std::to_string(slot_of(n) + 1)};
            int value = param(mt::slot_value(slot_of(n)));
            return {short_label(address), std::to_string(value), long_label(address), float(value) / 127.0f};
        }
        case Express:
            if (n == 0) return {"BEND", std::to_string(param(mt::Bend)), "PITCH BEND", fraction(mt::Bend)};
            if (n == 1) return {"PRES", pressure_name(param(mt::PressureOut)), "PRESSURE OUT", fraction(mt::PressureOut)};
            return {};
        case Pads:
            if (n == 0) return {"LAYT", drums() ? "DRUM" : "KEYS", "PADS", drums() ? 1.0f : 0.0f};
            if (n == 1) {
                if (!drums()) return {"NOTE", "-", "A DRUM'S NOTE"};
                unsigned note = track.midi.drum_note[track.last_pad % melodee::sampler_pads];
                return {"NOTE", std::to_string(note), "NOTE " + note_name(int(note)), float(note) / 127.0f};
            }
            if (n == 2 && !drums()) return {"OCT", note_name(studio.note_for(0, studio.octave(studio.track()))), "OCTAVE", 0.5f};
            return {};
        case Audio:
            if (n == 0) return {"IN", audio_name(param(mt::AudioIn)), "INPUT", fraction(mt::AudioIn)};
            if (n == 1) return {"MON", param(mt::AudioMonitor) ? "ON" : "OFF", "MONITOR", fraction(mt::AudioMonitor)};
            if (n == 2) return {"LVL", melodee::decibels(param(mt::AudioLevel)), "LEVEL DB", fraction(mt::AudioLevel)};
            return {};
        case Sysex: {
            if (n == 0) return {"FILE", track.midi.sysex[0] ? upper(std::string(track.midi.sysex)).substr(0, 4) : "NONE", "SYSEX FILE"};
            if (n == 1) return {"LOAD", param(mt::SysexOnLoad) ? "ON" : "OFF", "SEND ON LOAD", fraction(mt::SysexOnLoad)};
            int sending = studio.midi.sysex_percent(studio.track());
            if (n == 2) return {"SEND", sending >= 0 ? std::to_string(sending) : "UP", "TURN UP: SEND", sending >= 0 ? sending / 100.0f : 0.0f};
            return {};
        }
        default: return {};
        }
    }

    // A parameter of the track's, in the project and to the engine.
    void set(int id, int value) {
        const auto& info = melodee::engine_info(melodee::EngineKind::Midi).params[id];
        value = std::clamp(value, int(info.min), int(info.max));
        auto& track = studio.current();
        if (track.params[size_t(id)] == value) return;
        track.params[size_t(id)] = int16_t(value);
        studio.send({Command::Type::Param, uint8_t(studio.track()), 0, uint16_t(id), int16_t(value)});
        studio.changed();
    }
    void step(int id, int delta) { set(id, param(id) + delta); }

    void turn(unsigned n, int delta) {
        auto& track = studio.current();
        switch (page) {
        case Device: if (n == 0) step(mt::Channel, delta); else if (n == 1) step(mt::Delay, delta); break;
        case Program:
            if (n == 0) step(mt::BankMsb, delta);
            else if (n == 1) step(mt::BankLsb, delta);
            else if (n == 2) step(mt::Program, delta);
            else if (n == 3) step(pattern_program_id(), delta);
            break;
        case Cc1: case Cc2: case Cc3:
            if (mt::slot_kind(slot_address(n)) != mt::SlotKind::Off) step(mt::slot_value(slot_of(n)), delta);
            else studio.say("SHIFT + KNOB: SENDS");
            break;
        case Express: if (n == 0) step(mt::Bend, delta); else if (n == 1) step(mt::PressureOut, delta > 0 ? 1 : -1); break;
        case Pads:
            if (n == 0) {
                track.layout = delta > 0 ? melodee::PadLayout::Drums : melodee::PadLayout::Scale;
                studio.changed();
            } else if (n == 1 && drums()) {
                auto& note = track.midi.drum_note[track.last_pad % melodee::sampler_pads];
                note = uint8_t(std::clamp(int(note) + delta, 0, 127));
                studio.changed();
            } else if (n == 2 && !drums()) {
                studio.set_octave(studio.track(), std::clamp(studio.octave(studio.track()) + (delta > 0 ? 1 : -1), lowest_octave, highest_octave));
            }
            break;
        case Audio:
            if (n == 0) set(mt::AudioIn, std::clamp(param(mt::AudioIn) + (delta > 0 ? 1 : -1), 0, int(mt::AudioRear))); // the Move's input: L, R, both
            else if (n == 1) set(mt::AudioMonitor, delta > 0 ? 1 : 0);
            else if (n == 2) step(mt::AudioLevel, delta);
            break;
        case Sysex:
            if (n == 0) {
                auto files = studio.midi.sysex_files(studio);
                if (files.empty()) { studio.say("NO .SYX IN SYSEX"); break; }
                auto at = std::find(files.begin(), files.end(), std::string(track.midi.sysex));
                int index = at == files.end() ? (delta > 0 ? 0 : int(files.size()) - 1) : std::clamp(int(at - files.begin()) + (delta > 0 ? 1 : -1), 0, int(files.size()) - 1);
                std::snprintf(track.midi.sysex, sizeof track.midi.sysex, "%s", files[size_t(index)].c_str());
                studio.changed();
            } else if (n == 1) {
                set(mt::SysexOnLoad, delta > 0 ? 1 : 0);
            } else if (n == 2 && delta > 0 && studio.midi.sysex_percent(studio.track()) < 0) {
                studio.midi.send_sysex(studio, studio.track());
            }
            break;
        default: break;
        }
    }

    // What a slot sends: off, CC 0-119, 14-bit CC 0-31, bend, pressure, NRPN 0-1023.
    void address(unsigned n, int delta) {
        int id = mt::slot_address(slot_of(n));
        set(id, std::clamp(param(id) + delta, -1, mt::address_last));
    }

    std::array<bool, pad_count> held {};
    int focus = 0, page = Device, address_shown = -1;
};

RegisterScreen midi("midi-play", make<MidiPlay>, {}, kind_bit(TrackKind::Midi));

} // namespace
} // namespace app::screens
