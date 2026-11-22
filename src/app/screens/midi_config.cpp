// SPDX-License-Identifier: AGPL-3.0-only
// MIDI Config (docs/UI.md, "MIDI and sync"): Melodee's MIDI settings for the
// Move's one port (USB-A and USB-C, docs/MIDI.md), from Setup's MIDI and
// MIDI SYNC rows (Shift + Step 2). The wheel picks a row; push edits it
// (`[MTC]`), the wheel sets it, push again (or Back) done; push on PANIC
// sends it. The rail's bottom says IN and OUT as MIDI passes, the detail
// line EXT CLOCK and the tempo heard, or EXT MTC, while following. The
// settings are the Move's, in Move.settings (Melodee's own keys).
#include "app/screens/common.h"

namespace app::screens {
namespace {

enum Row { InChannel, OutChannel, PadIn, ScaleKeys, PadOut, SyncIn, SyncOut, Thru, ProgramChange, RemoteChannel, TrackCc, PatternCc, SequenceCc, Panic, RowCount };
const char* const labels[] {"IN CHANNEL", "OUT CHANNEL", "PAD IN", "SCALE KEYS", "PAD OUT", "SYNC IN", "SYNC OUT", "THRU", "PROG CHANGE", "REMOTE CH",
                            "TRACK CC", "PATTERN CC", "SEQUENCE CC", "PANIC"};
const char* const syncs[] {"OFF", "CLOCK", "MTC"};
const char* const pad_outs[] {"NEVER", "ALWAYS", "EMPTY"};

// A row as the list shows it: the label, the value flush right in 16.
std::string row_text(const std::string& label, const std::string& value) {
    if (value.empty()) return label;
    size_t gap = label.size() + value.size() < 16 ? 16 - label.size() - value.size() : 1;
    return label + std::string(gap, ' ') + value;
}

class MidiConfig final : public Screen {
public:
    using Screen::Screen;

    bindings::Screen screen_kind() const override { return bindings::Screen::List; }
    void enter() override { editing = panicked = false; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine:
            if (editing) change(use.delta);
            else row = std::clamp(row + use.delta, 0, RowCount - 1);
            return true;
        case Job::ListPush:
            if (row == Panic) { studio.midi.panic(studio); panicked = true; }
            else editing = !editing;
            return true;
        case Job::Back:
            if (panicked) panicked = false; // back to the list
            else if (editing) editing = false;
            else studio.close();
            return true;
        default: return false;
        }
    }

    void lights(move::Lights& lights) override { lights.button(move::Button::Back, move::white_dim); }

    void draw(juce::Graphics& g) override {
        auto now = Studio::Clock::now();
        auto& midi = studio.midi;
        ui::rail(g, "MIDI", "", midi.out_lately(now) ? "OUT" : midi.in_lately(now) ? "IN" : "");
        if (panicked) { // what PANIC sent
            move::draw_text_centred(g, ui::content_x, 12, ui::content_width, "PANIC", 2);
            move::draw_text_centred(g, ui::content_x, 32, ui::content_width, "ALL NOTES OFF");
            move::draw_text_centred(g, ui::content_x, 42, ui::content_width, "16 CHANNELS");
            ui::detail(g, "BACK: MIDI");
            return;
        }
        std::vector<std::string> rows;
        for (int n = 0; n < RowCount; ++n) {
            std::string shown = value(n);
            if (editing && n == row) shown = "[" + shown + "]";
            rows.push_back(row_text(labels[n], shown));
        }
        ui::list(g, ui::content(0, 50), rows, row, 1);
        ui::detail(g, detail(), midi.following() && !midi.following_mtc() && midi.tempo_in() ? tenths(midi.tempo_in()) : "");
    }

    std::string status() const override {
        if (studio.top() != this) return {};
        return " midi_row=" + std::to_string(row) + " midi_editing=" + std::to_string(int(editing)) + " midi_panicked=" + std::to_string(int(panicked));
    }

private:
    static std::string tenths(unsigned value) { return std::to_string(value / 10) + "." + std::to_string(value % 10); }
    static std::string channel_or(unsigned channel, const char* zero) { return channel ? std::to_string(channel) : zero; }

    std::string value(int n) const {
        const auto& c = studio.midi.config;
        switch (n) {
        case InChannel: return channel_or(c.in_channel, "ALL");
        case OutChannel: return std::to_string(c.out_channel);
        case PadIn: return c.pad_in ? "ON" : "OFF";
        case ScaleKeys: return studio.midi.scale_keys ? "ON" : "OFF";
        case PadOut: return pad_outs[unsigned(c.pad_out) % 3];
        case SyncIn: return syncs[unsigned(c.sync_in) % 3];
        case SyncOut: return syncs[unsigned(c.sync_out) % 3];
        case Thru: return c.thru ? "ON" : "OFF";
        case ProgramChange: return c.program_change ? "ON" : "OFF";
        case RemoteChannel: return channel_or(c.remote_channel, "OFF");
        case TrackCc: return channel_or(c.track_cc, "OFF");
        case PatternCc: return channel_or(c.pattern_cc, "OFF");
        case SequenceCc: return channel_or(c.scene_cc, "OFF");
        default: return {};
        }
    }

    std::string detail() const {
        const auto& midi = studio.midi;
        if (midi.following_mtc()) return "EXT MTC";
        if (midi.following()) return "EXT CLOCK";
        switch (row) {
        case InChannel: return "PLAYS THE TRACK";
        case ScaleKeys: return "WHITE KEYS: SCALE";
        case PadOut: return "PADS AND PATTERNS";
        case SyncOut: return midi.config.sync_out == melodee::MidiSync::Mtc ? "MTC 25 FPS OUT" : "";
        case ProgramChange: return "LAUNCHES PATTERN";
        case TrackCc: case PatternCc: case SequenceCc: return "ON REMOTE CH";
        case Panic: return "PUSH: ALL OFF";
        default: return {};
        }
    }

    void change(int delta) {
        auto& c = studio.midi.config;
        int way = delta > 0 ? 1 : -1;
        auto cycle = [&](unsigned value, unsigned count) { return (value + count + unsigned(way)) % count; };
        switch (row) {
        case InChannel: c.in_channel = uint8_t(std::clamp(int(c.in_channel) + delta, 0, 16)); break;
        case OutChannel: c.out_channel = uint8_t(std::clamp(int(c.out_channel) + delta, 1, 16)); break;
        case PadIn: c.pad_in = way > 0; break;
        case ScaleKeys: studio.midi.scale_keys = way > 0; break;
        case PadOut: c.pad_out = melodee::PadMidiOut(cycle(unsigned(c.pad_out), 3)); break;
        case SyncIn: c.sync_in = melodee::MidiSync(cycle(unsigned(c.sync_in), 3)); break;
        case SyncOut: c.sync_out = melodee::MidiSync(cycle(unsigned(c.sync_out), 3)); break;
        case Thru: c.thru = way > 0; break;
        case ProgramChange: c.program_change = way > 0; break;
        case RemoteChannel: c.remote_channel = uint8_t(std::clamp(int(c.remote_channel) + delta, 0, 16)); break;
        case TrackCc: c.track_cc = uint8_t(std::clamp(int(c.track_cc) + delta, 0, 127)); break;
        case PatternCc: c.pattern_cc = uint8_t(std::clamp(int(c.pattern_cc) + delta, 0, 127)); break;
        case SequenceCc: c.scene_cc = uint8_t(std::clamp(int(c.scene_cc) + delta, 0, 127)); break;
        default: return;
        }
        studio.changed(); // Move.settings goes with the next save
    }

    int row = 0;
    bool editing = false, panicked = false;
};

// Opened from Setup's MIDI and MIDI SYNC rows (move-projects' screens/setup.cpp).
RegisterScreen config("midi-config", make<MidiConfig>);

} // namespace
} // namespace app::screens
