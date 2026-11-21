// SPDX-License-Identifier: AGPL-3.0-only
// A keygroup track's Play screen (docs/UI.md, "Instruments";
// docs/ui/instruments.png): one instrument (an SFZ, an MPC keygroup
// program, an EXS, a SoundFont preset, a DecentSampler preset) or one
// sample across the pads, as Keys & Scale lays them out. The Device View's
// items along the top: the track, its instrument, its effects; the wheel's
// push on the first two opens the instrument browser. Shift + the wheel
// turns its pages, each eight knobs on its sound (the track's pad 1):
// ZONES (an instrument's: a zone picked, its tune and level) or TRIM (one
// sample's region and loop), PLAY, AMP ENV, FILTER (with its envelope), ARP
// and CHORD. Plus and Minus move the octave (Drums: 16 pads).
#include "app/instruments.h"
#include "app/screens/note_pages.h"
#include "melodee/engine/command.h"
#include "melodee/engine/sample_data.h"
#include "melodee/model/scales.h"

namespace app::screens {
namespace {

constexpr int lowest_octave = -3, highest_octave = 4;
enum class Page : uint8_t { Zones, Play, Amp, Filter, Arp, Chord, Count };
constexpr unsigned page_count = unsigned(Page::Count);

class KeysPlay final : public Screen {
public:
    using Screen::Screen;
    void enter() override { held.fill(false); }

    void input(const move::Input& input) override {
        if (input.kind == move::Input::Kind::Pad && input.index < pad_count) {
            held[input.index] = input.pressed;
            if (input.pressed) last_pad = int(input.index);
        }
    }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        switch (job) {
        case Job::PlayNote: return true; // its sound went from the audio thread
        case Job::TurnKnob: turn(unsigned(use.index), use.delta, false); return true;
        case Job::TurnKnobFine: turn(unsigned(use.index), use.delta, true); return true;
        case Job::Octave: studio.say(octave_or_bank(studio, use.control == bindings::Control::Plus ? 1 : -1, lowest_octave, highest_octave)); return true;
        case Job::Focus: focus = std::clamp(focus + (use.delta > 0 ? 1 : -1), 0, 3); return true;
        case Job::ShowFocus: studio.say(item(focus)); return true;
        case Job::OpenBrowser:
            if (focus <= 1) studio.open("load-instruments");
            else studio.say("EFFECTS: NOT YET");
            return true;
        case Job::Page: {
            auto& at = page_of[studio.track()];
            at = uint8_t(std::clamp(int(at) + (use.delta > 0 ? 1 : -1), 0, int(page_count) - 1));
            return true;
        }
        default: return false;
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        key_pads(studio, lights, held, last_pad);
        steps_playhead(studio, lights);
        int octave = studio.octave(studio.track());
        for (auto button : {Button::Left, Button::Right, Button::Loop, Button::Menu, Button::Back}) lights.button(button, 0);
        lights.button(Button::Up, octave < highest_octave ? white_dim : 0);
        lights.button(Button::Down, octave > lowest_octave ? white_dim : 0);
    }

    void draw(juce::Graphics& g) override {
        const unsigned t = studio.track();
        const Page page = Page(page_of[t]);
        ui::rail(g, "KEYS", track_number(studio), std::to_string(unsigned(page) + 1) + "/" + std::to_string(page_count));
        items(g, {item(0), item(1), item(2), item(3)}, focus);
        const Sample* sample = studio.keys_sample(t);
        if (!sample) {
            move::draw_text_centred(g, ui::content_x, 22, ui::content_width, "NO INSTRUMENT");
            ui::detail(g, "PUSH: INSTRUMENTS");
            return;
        }
        const auto& sound = studio.current().pads[0];
        const bool instrument = sample->instrument != nullptr;
        switch (page) {
        case Page::Zones: instrument ? draw_zones(g, *sample) : draw_trim(g, *sample, sound); return;
        case Page::Amp: {
            move::draw_text(g, ui::content_x + 1, 11, "AMP ENV");
            move::draw_text(g, ui::width - move::text_width("A D S R"), 11, "A D S R");
            draw_envelope(g, ui::content(21, 28).reduced(3, 0), sound.attack / 100.0f, sound.decay / 100.0f, sound.sustain / 100.0f, sound.release / 100.0f);
            ui::detail(g, "ATT " + std::to_string(sound.attack), "REL " + std::to_string(sound.release));
            return;
        }
        case Page::Arp: case Page::Chord: {
            auto note_page = page == Page::Arp ? NotePage::Arp : NotePage::Chord;
            draw_note_page(g, studio, t, note_page, note_page_right(studio, t, note_page));
            auto [left, right] = note_page_detail(studio, t, note_page);
            ui::detail(g, left, right);
            return;
        }
        default: break;
        }
        move::draw_text(g, ui::content_x + 1, 11, page == Page::Play ? "PLAY" : "FILTER");
        std::string right = page == Page::Play ? lowest_note(studio) : filter_name(sound.filter);
        move::draw_text(g, ui::width - move::text_width(right), 11, right);
        std::array<std::string, 8> names;
        std::array<float, 8> values {};
        for (unsigned n = 0; n < 8; ++n) {
            auto k = knob(n);
            names[n] = k.name.substr(0, 5);
            values[n] = k.fraction;
        }
        ui::knob_cells(g, ui::content(20, ui::subject_height - 20), names, values);
        if (keys_mode(studio.current()) == KeysMode::Drums && last_pad >= 0) { // the drum hit last, named
            auto drum = pad_notes(studio.project(), studio.current(), unsigned(last_pad));
            if (drum.count) { ui::detail(g, drum_name(drum.keys[0]).substr(0, 16)); return; }
        }
        ui::detail(g, detail_left(*sample), page == Page::Play ? (sound.mono ? "MONO" : "POLY") : "");
    }

    Knob knob(unsigned n) const override {
        const unsigned t = studio.track();
        const Sample* sample = studio.keys_sample(t);
        if (!sample) return {};
        const auto& sound = studio.current().pads[0];
        auto on_off = [](bool on) { return std::string(on ? "ON" : "OFF"); };
        auto percent = [](int v) { return v / 100.0f; };
        switch (Page(page_of[t])) {
        case Page::Zones:
            if (sample->instrument) {
                const auto& multi = *sample->data->multi;
                if (multi.zones.empty()) return {};
                unsigned z = std::min<unsigned>(zone, unsigned(multi.zones.size()) - 1);
                const auto* trim = melodee::find_trim(studio.current().zone_trims, 0, z, sound.sample);
                if (n == 0) return {"ZONE", std::to_string(z + 1) + "/" + std::to_string(multi.zones.size()), float(z) / float(std::max<size_t>(1, multi.zones.size() - 1))};
                if (n == 1) { int c = trim ? trim->tune : 0; return {"TUNE", std::to_string(c) + " CT", (c + 100) / 200.0f}; }
                if (n == 2) { int l = trim ? trim->level : 0; char text[16]; std::snprintf(text, sizeof text, "%+.1f DB", l / 10.0); return {"LEVEL", text, (l + 240) / 300.0f}; }
                return {};
            }
            switch (n) { // one sample: its region and loop
            case 0: return {"START", seconds(sound.start), fraction_of(*sample, sound.start)};
            case 1: return {"END", seconds(end_of(*sample, sound)), fraction_of(*sample, end_of(*sample, sound))};
            case 2: return {"LOOP", on_off(sound.loop), sound.loop ? 1.0f : 0.0f};
            case 3: return {"LOOP PT", seconds(sound.loop_point()), fraction_of(*sample, sound.loop_point())};
            case 4: return {"XFADE", std::to_string(sound.loop_xfade) + " MS", sound.loop_xfade / 100.0f};
            case 5: return {"REVERSE", on_off(sound.reverse), sound.reverse ? 1.0f : 0.0f};
            default: return {};
            }
        case Page::Play:
            switch (n) {
            case 0: return {"TUNE", (sound.pitch > 0 ? "+" : "") + std::to_string(sound.pitch) + " ST", (sound.pitch + 24) / 48.0f};
            case 1: return {"FINE", (sound.fine > 0 ? "+" : "") + std::to_string(sound.fine) + " CT", (sound.fine + 50) / 100.0f};
            case 2: return {"LEVEL", std::to_string(sound.level), sound.level / 127.0f};
            case 3: return {"PAN", std::to_string(sound.pan), (sound.pan + 50) / 100.0f};
            case 4: return {"VEL", std::to_string(sound.velocity_sense) + "%", percent(sound.velocity_sense)};
            case 5: return {"MONO", on_off(sound.mono), sound.mono ? 1.0f : 0.0f};
            case 6: return {"MODE", sound.play == melodee::PadPlay::Gate ? "NOTE ON" : "ONE SHOT", sound.play == melodee::PadPlay::Gate ? 1.0f : 0.0f};
            case 7: { // the program's own (an MPC program's Portamento): shown, not set here
                float glide = sample->data->multi ? sample->data->multi->glide : 0.0f;
                char text[16];
                std::snprintf(text, sizeof text, "%.2f S", glide);
                return {"GLIDE", glide > 0 ? text : "OFF", std::min(1.0f, glide)};
            }
            default: return {};
            }
        case Page::Amp:
            switch (n) {
            case 0: return {"ATTACK", std::to_string(sound.attack), percent(sound.attack)};
            case 1: return {"DECAY", std::to_string(sound.decay), percent(sound.decay)};
            case 2: return {"SUSTAIN", std::to_string(sound.sustain), percent(sound.sustain)};
            case 3: return {"RELEASE", std::to_string(sound.release), percent(sound.release)};
            default: return {};
            }
        case Page::Filter:
            switch (n) {
            case 0: return {"TYPE", filter_name(sound.filter), float(sound.filter) / float(unsigned(melodee::PadFilter::Count) - 1)};
            case 1: return {"CUTOFF", std::to_string(sound.cutoff), percent(sound.cutoff)};
            case 2: return {"RESO", std::to_string(sound.resonance), percent(sound.resonance)};
            case 3: return {"AMOUNT", std::to_string(sound.filter_amount), percent(sound.filter_amount)};
            case 4: return {"ATTACK", std::to_string(sound.filter_attack), percent(sound.filter_attack)};
            case 5: return {"DECAY", std::to_string(sound.filter_decay), percent(sound.filter_decay)};
            case 6: return {"SUSTAIN", std::to_string(sound.filter_sustain), percent(sound.filter_sustain)};
            case 7: return {"RELEASE", std::to_string(sound.filter_release), percent(sound.filter_release)};
            default: return {};
            }
        case Page::Arp: return note_page_knob(studio, t, NotePage::Arp, n);
        case Page::Chord: return note_page_knob(studio, t, NotePage::Chord, n);
        default: return {};
        }
    }
    std::string knob_owner() const override { return "KEYS"; }

    std::string status() const override {
        return " keys_page=" + std::to_string(unsigned(page_of[studio.track()]) + 1) + " keys_zone=" + std::to_string(zone + 1);
    }

private:
    std::array<bool, pad_count> held {};
    int last_pad = -1;
    int focus = 1; // the Device View's item: 0 the track, 1 its instrument, 2-3 the effects
    std::array<uint8_t, melodee::track_count> page_of {};
    unsigned zone = 0; // ZONES' zone picked

    static std::string filter_name(melodee::PadFilter filter) {
        static const char* names[] {"OFF", "CLASSIC", "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "BP 24"};
        return names[unsigned(filter) % 8];
    }
    static uint32_t end_of(const Sample& sample, const melodee::PadSound& sound) { return sound.end ? sound.end : sample.data->model_frames(); }
    static float fraction_of(const Sample& sample, uint32_t frame) { return float(frame) / float(std::max<uint32_t>(1, sample.data->model_frames())); }

    std::string item(int which) const {
        switch (which) {
        case 0: return "KEYS";
        case 1: {
            const Sample* sample = studio.keys_sample(studio.track());
            return sample ? upper(sample->name).substr(0, 4) : "----";
        }
        default: return "FX" + std::to_string(which - 1);
        }
    }

    std::string detail_left(const Sample& sample) const {
        if (sample.instrument)
            return (sample.instrument->format + " " + std::to_string(sample.data->multi->zones.size()) + " ZONES").substr(0, 12);
        return upper(sample.name).substr(0, 12);
    }

    void draw_zones(juce::Graphics& g, const Sample& sample) {
        const auto& multi = *sample.data->multi;
        const auto& ins = *sample.instrument;
        const bool loading = !ins.done();
        unsigned z = multi.zones.empty() ? 0 : std::min<unsigned>(zone, unsigned(multi.zones.size()) - 1);
        std::string right = loading ? std::to_string(ins.landed.load()) + "/" + std::to_string(ins.parts.size())
                                    : "Z" + std::to_string(z + 1) + "/" + std::to_string(multi.zones.size());
        move::draw_text(g, ui::content_x + 1, 11, "ZONES");
        move::draw_text(g, ui::width - move::text_width(right), 11, right);
        // The zones over the piano's keys (21-108), a tick a zone's lowest key, the one picked filled.
        auto area = ui::content(21, 13).reduced(1, 0);
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
        const auto* trim = melodee::find_trim(studio.current().zone_trims, 0, z, studio.current().pads[0].sample);
        char text[32];
        std::snprintf(text, sizeof text, "%s-%s %+d %+.0fDB", note_name(picked.low_key).c_str(), note_name(picked.high_key).c_str(), trim ? trim->tune : 0,
                      trim ? trim->level / 10.0 : 0.0);
        move::draw_text(g, ui::content_x + 1, 41, text);
        std::string file = picked.part < ins.parts.size() ? ins.parts[picked.part] : "";
        auto slash = file.find_last_of('/');
        if (slash != std::string::npos) file = file.substr(slash + 1);
        ui::detail(g, upper(file).substr(0, 12), note_name(picked.root));
    }

    void draw_trim(juce::Graphics& g, const Sample& sample, const melodee::PadSound& sound) {
        move::draw_text(g, ui::content_x + 1, 11, "TRIM");
        if (sound.loop) move::draw_text(g, ui::width - move::text_width("LOOP"), 11, "LOOP");
        auto area = ui::content(20, 30);
        ui::waveform(g, area, peaks_of(sample.overview));
        auto column = [&](uint32_t frame) { return int(fraction_of(sample, frame) * float(area.getWidth() - 1)); };
        std::vector<int> marks {column(sound.start), column(end_of(sample, sound))};
        if (sound.loop) marks.push_back(column(sound.loop_point()));
        ui::cuts(g, area, marks, 0);
        const auto& ref = studio.project().samples[sound.sample];
        ui::detail(g, upper(sample.name).substr(0, 11), ref.root >= 0 ? note_name(melodee::keyed_root(studio.project(), studio.current())) : "C3");
    }

    static void draw_envelope(juce::Graphics& g, juce::Rectangle<int> area, float a, float d, float s, float r) {
        int w = area.getWidth(), h = area.getHeight(), x0 = area.getX(), y0 = area.getBottom() - 1;
        int xa = x0 + int(a * float(w) * 0.25f), xd = xa + int(d * float(w) * 0.25f), xs = x0 + int(float(w) * 0.72f);
        int xr = std::min(xs + int(r * float(w) * 0.28f), area.getRight() - 1), ys = y0 - int(s * float(h - 1));
        juce::Path path;
        path.startNewSubPath(float(x0), float(y0));
        path.lineTo(float(xa), float(area.getY()));
        path.lineTo(float(xd), float(ys));
        path.lineTo(float(xs), float(ys));
        path.lineTo(float(xr), float(y0));
        g.setColour(juce::Colours::white);
        g.strokePath(path, juce::PathStrokeType(1.0f));
        g.fillRect(x0, y0, w, 1);
    }

    void turn(unsigned n, int delta, bool fine) {
        const unsigned t = studio.track();
        const Sample* sample = studio.keys_sample(t);
        if (!sample || !delta) return;
        const Page page = Page(page_of[t]);
        if (page == Page::Arp) { note_page_turn(studio, t, NotePage::Arp, n, delta); return; }
        if (page == Page::Chord) { note_page_turn(studio, t, NotePage::Chord, n, delta); return; }
        auto sound = studio.current().pads[0];
        auto step = [&](auto& field, int low, int high) { field = std::decay_t<decltype(field)>(std::clamp(int(field) + delta, low, high)); };
        auto flip = [&](bool& field) { field = delta > 0; };
        if (page == Page::Zones && sample->instrument) {
            const auto& multi = *sample->data->multi;
            if (multi.zones.empty()) return;
            if (n == 0) { zone = unsigned(std::clamp(int(zone) + delta, 0, int(multi.zones.size()) - 1)); return; }
            if (n > 2) return;
            auto& trims = studio.current().zone_trims;
            unsigned z = std::min<unsigned>(zone, unsigned(multi.zones.size()) - 1);
            const auto* had = melodee::find_trim(trims, 0, z, sound.sample);
            int tune = had ? had->tune : 0, level = had ? had->level : 0;
            if (n == 1) tune = std::clamp(tune + delta, -100, 100);
            else level = std::clamp(level + delta * (fine ? 1 : 5), -240, 60);
            if (!melodee::set_trim(trims, 0, z, sound.sample, tune, level)) { studio.say("TRIMS FULL"); return; }
            melodee::ZoneTrim trim;
            trim.pad = 0; trim.slot = sound.sample; trim.zone = uint16_t(z); trim.tune = int8_t(tune); trim.level = int16_t(level);
            studio.send(melodee::zone_trim_command(t, trim));
            studio.changed();
            return;
        }
        if (page == Page::Zones) { // one sample: its region and loop, a 256th of it a detent (Shift: a 4096th)
            const uint32_t frames = sample->data->model_frames(), unit = std::max<uint32_t>(1, frames / (fine ? 4096 : 256));
            auto move_frame = [&](uint32_t at) { return uint32_t(std::clamp<int64_t>(int64_t(at) + int64_t(delta) * unit, 0, frames)); };
            switch (n) {
            case 0: sound.start = std::min(move_frame(sound.start), end_of(*sample, sound) - 1); break;
            case 1: sound.end = std::max(move_frame(end_of(*sample, sound)), sound.start + 1); if (sound.end >= frames) sound.end = 0; break;
            case 2: flip(sound.loop); break;
            case 3: sound.loop_lock = false; sound.loop_start = std::clamp(move_frame(sound.loop_point()), sound.start, end_of(*sample, sound) - 1); break;
            case 4: step(sound.loop_xfade, 0, 100); break;
            case 5: flip(sound.reverse); break;
            default: return;
            }
        } else if (page == Page::Play) {
            switch (n) {
            case 0: step(sound.pitch, -24, 24); break;
            case 1: step(sound.fine, -50, 50); break;
            case 2: step(sound.level, 0, 127); break;
            case 3: step(sound.pan, -50, 50); break;
            case 4: step(sound.velocity_sense, 0, 100); break;
            case 5: flip(sound.mono); break;
            case 6: sound.play = delta > 0 ? melodee::PadPlay::Gate : melodee::PadPlay::OneShot; break;
            default: return; // GLIDE is the program's
            }
        } else if (page == Page::Amp) {
            switch (n) {
            case 0: step(sound.attack, 0, 100); break;
            case 1: step(sound.decay, 0, 100); break;
            case 2: step(sound.sustain, 0, 100); break;
            case 3: step(sound.release, 0, 100); break;
            default: return;
            }
        } else if (page == Page::Filter) {
            switch (n) {
            case 0: { int f = std::clamp(int(sound.filter) + delta, 0, int(melodee::PadFilter::Count) - 1); sound.filter = melodee::PadFilter(f); break; }
            case 1: step(sound.cutoff, 0, 100); break;
            case 2: step(sound.resonance, 0, 100); break;
            case 3: step(sound.filter_amount, 0, 100); break;
            case 4: step(sound.filter_attack, 0, 100); break;
            case 5: step(sound.filter_decay, 0, 100); break;
            case 6: step(sound.filter_sustain, 0, 100); break;
            case 7: step(sound.filter_release, 0, 100); break;
            default: return;
            }
        }
        studio.set_pad(t, 0, sound);
    }
};

RegisterScreen keys("keys-play", make<KeysPlay>, {}, kind_bit(TrackKind::Keys));

} // namespace
} // namespace app::screens
