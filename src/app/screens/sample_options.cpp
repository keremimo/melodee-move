// SPDX-License-Identifier: AGPL-3.0-only
// Sample Options (docs/MOVE_CONTROLS.md, docs/ui/kit.png): Shift + the
// wheel's push on a kit pad's sample, the Move's own door to slicing.
// Melodee's Chop is where the Move has Slice, beside Reverse, and Melodee's
// Trim Sample (the region as Samples/Edits/<NAME>-Trimmed.wav, the pad onto
// all of it), Normalize (the pad's gain, its loudest frame to full scale) and
// Save Sample (a name typed, then Samples/<name>.wav; the pad as it was). On
// LAYERS with layer 2-4 in hand (kit_pages.cpp) the rows are that layer's,
// and Remove Layer takes it off the pad. The wheel walks the list, its push
// does the row, Back leaves.
#include "app/screens/kit_common.h"
#include <cmath>
#include <filesystem>

namespace app::screens {
namespace {

enum class Row { Chop, Reverse, Trim, Normalize, Save, RemoveLayer };

class SampleOptions final : public Screen {
public:
    using Screen::Screen;

    void enter() override { cursor = 0; }
    bindings::Screen screen_kind() const override { return bindings::Screen::List; }

    bool job(bindings::Job job, const bindings::Use& use) override {
        using bindings::Job;
        auto shown = rows();
        switch (job) {
        case Job::ListTurn: case Job::ListTurnFine: cursor = std::clamp(cursor + use.delta, 0, int(shown.size()) - 1); return true;
        case Job::ListPush: act(shown[size_t(std::clamp(cursor, 0, int(shown.size()) - 1))]); return true;
        case Job::Back: studio.close(); return true;
        default: return false; // the pads and knobs: the kit's Play screen's
        }
    }

    void lights(move::Lights& lights) override {
        using namespace move;
        kit_pads(studio, lights);
        steps_playhead(studio, lights);
        for (auto button : {Button::Left, Button::Right, Button::Up, Button::Down, Button::Loop, Button::Menu}) lights.button(button, 0);
        lights.button(Button::Back, white_dim);
    }

    void draw(juce::Graphics& g) override {
        int layer = layer_in_hand();
        ui::rail(g, "OPT", pad_label(studio.pad()), layer ? "LYR" + std::to_string(layer + 1) : "SMP");
        std::vector<std::string> names;
        const auto& sound = studio.current().pads[studio.pad()];
        for (Row row : rows())
            switch (row) {
            case Row::Chop: names.push_back("CHOP >"); break;
            case Row::Reverse: names.push_back(sound.reverse ? "REVERSE       ON" : "REVERSE      OFF"); break;
            case Row::Trim: names.push_back("TRIM SAMPLE"); break;
            case Row::Normalize: names.push_back("NORMALIZE"); break;
            case Row::Save: names.push_back("SAVE SAMPLE"); break;
            case Row::RemoveLayer: names.push_back("REMOVE LAYER"); break;
            }
        ui::list(g, ui::content(0, ui::height), names, cursor, 1);
    }
    std::string status() const override {
        const auto& sound = studio.current().pads[studio.pad()];
        return " reverse=" + std::to_string(int(sound.reverse)) + " gain=" + std::to_string(sound.gain);
    }

private:
    int cursor = 0;

    // The layer Sample Options is on: LAYERS' layer in hand, else the pad's own (0).
    int layer_in_hand() const {
        const auto& target = studio.layer_target;
        return target.layer > 0 && target.track == int(studio.track()) && target.pad == int(studio.pad()) ? target.layer : 0;
    }
    std::vector<Row> rows() const {
        if (layer_in_hand()) return {Row::Reverse, Row::Normalize, Row::Save, Row::RemoveLayer};
        return {Row::Chop, Row::Reverse, Row::Trim, Row::Normalize, Row::Save};
    }

    void act(Row row) {
        unsigned track = studio.track(), pad = studio.pad();
        int layer = layer_in_hand();
        melodee::PadSound sound = studio.current().pads[pad];
        const auto one = sound.layer(unsigned(layer));
        const Sample* sample = one.used() ? studio.sample_in(one.sample) : nullptr;
        std::string name = sample ? upper(sample->name.substr(0, sample->name.rfind('.'))) : "SAMPLE";
        switch (row) {
        case Row::Chop: studio.open("chop"); return; // in this one's place, over the Play screen
        case Row::Reverse:
            sound.reverse = !sound.reverse;
            studio.set_pad(track, pad, sound);
            return;
        case Row::Trim:
            if (!sample) return;
            studio.file_jobs.push_back({true, track, pad, 0, (std::filesystem::path(studio.samples_folder()) / "Edits" / (name + "-Trimmed.wav")).string()});
            studio.say("TRIMMING");
            studio.close();
            return;
        case Row::Normalize: {
            if (!sample) return;
            float peak = peak_of(*sample, one.start, one.end);
            if (peak <= 0) { studio.say("SILENT"); return; }
            int gain = std::clamp(int(std::lround(-2000.0 * std::log10(double(peak)))), -4800, 2400); // hundredths of a dB
            if (layer) sound.layers[size_t(layer - 1)].gain = int16_t(std::clamp(gain - sound.gain, -4800, 1200));
            else sound.gain = int16_t(gain);
            studio.set_pad(track, pad, sound);
            char text[24];
            std::snprintf(text, sizeof text, "GAIN %+.1f DB", gain / 100.0);
            studio.say(text);
            return;
        }
        case Row::Save: {
            if (!sample) return;
            auto folder = studio.samples_folder();
            studio.ask_text("SAVE SAMPLE", name, [this, track, pad, layer, folder](const std::string& typed) {
                studio.file_jobs.push_back({false, track, pad, layer, (std::filesystem::path(folder) / (typed_name(typed) + ".wav")).string()});
                studio.say("SAVING");
            });
            return;
        }
        case Row::RemoveLayer:
            if (!layer) return;
            sound.layers[size_t(layer - 1)] = melodee::PadLayer {};
            studio.set_pad(track, pad, sound);
            studio.say("LAYER " + std::to_string(layer + 1) + " REMOVED");
            studio.close();
            return;
        }
    }

    static std::string typed_name(const std::string& typed) {
        std::string name;
        for (char c : typed) name += std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' ? c : '_';
        while (!name.empty() && name.back() == ' ') name.pop_back();
        return name.empty() ? "SAMPLE" : name;
    }

    // The loudest frame of the region (the project's frames): from memory, or
    // for a sample read from storage as it plays, its waveform's peaks.
    static float peak_of(const Sample& sample, uint32_t start, uint32_t end) {
        const auto& data = *sample.data;
        uint32_t total = std::max<uint32_t>(1, data.model_frames());
        if (!end) end = total;
        if (data.left.empty()) {
            float peak = 0;
            size_t from = size_t(uint64_t(start) * sample.overview.size() / total), to = size_t(uint64_t(end) * sample.overview.size() / total);
            for (size_t n = from; n < std::min(to + 1, sample.overview.size()); ++n) peak = std::max(peak, sample.overview[n] / 255.0f);
            return peak;
        }
        float peak = 0;
        for (uint32_t f = data.own(start), to = data.own(end); f < to && f < data.left.size(); ++f) {
            peak = std::max(peak, std::abs(data.left[f]));
            if (f < data.right.size()) peak = std::max(peak, std::abs(data.right[f]));
        }
        return peak;
    }
};

RegisterScreen registered("sample-options", make<SampleOptions>);

} // namespace
} // namespace app::screens
