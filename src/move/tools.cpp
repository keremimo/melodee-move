// SPDX-License-Identifier: AGPL-3.0-only
#include "move/tools.h"
#include "move/display.h"
#include "move/runtime.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace move {
namespace {

juce::File file_at(const std::string& path) {
    return juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(path.c_str()));
}

bool write_png(const juce::Image& image, const juce::File& file) {
    file.deleteFile();
    juce::FileOutputStream output(file);
    return output.openedOk() && juce::PNGImageFormat().writeImageToStream(image, output);
}

void write_wav(const std::string& path, const std::vector<int16_t>& samples, unsigned rate) {
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&](uint32_t value) { for (int n = 0; n < 4; ++n) out.put(char((value >> (8 * n)) & 0xff)); };
    auto u16 = [&](uint16_t value) { out.put(char(value & 0xff)); out.put(char(value >> 8)); };
    uint32_t data_bytes = uint32_t(samples.size() * sizeof(int16_t));
    out.write("RIFF", 4); u32(36 + data_bytes); out.write("WAVE", 4);
    out.write("fmt ", 4); u32(16); u16(1); u16(audio_channels); u32(rate); u32(rate * audio_channels * 2); u16(audio_channels * 2); u16(16);
    out.write("data", 4); u32(data_bytes);
    for (int16_t sample : samples) u16(uint16_t(sample));
    if (!out) throw std::runtime_error("cannot write " + path);
}

// As a host and the runtime would: pads to the audio path first, then
// everything to surface().
void send(App& app, Session& session, const SurfaceEvent& event) {
    if (event.input.kind == Input::Kind::Pad || event.input.kind == Input::Kind::PadPressure) app.audio_event(event);
    track_input(session, event.input);
    app.surface(event);
}

Frame draw_frame(App& app) {
    juce::Image image(juce::Image::ARGB, screen_width, screen_height, true);
    {
        juce::Graphics g(image);
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        app.draw(g);
    }
    return pack(image);
}

bool save_frame(App& app, const juce::File& directory, const char* name) {
    Frame frame = draw_frame(app);
    auto message = display_message(frame);
    std::ofstream bin(directory.getChildFile(juce::String(name) + ".bin").getFullPathName().toStdString(), std::ios::binary);
    bin.write(reinterpret_cast<const char*>(message.data()), std::streamsize(message.size()));
    bool written = bool(bin) && write_png(frame_image(frame, 4), directory.getChildFile(juce::String(name) + ".png"));
    if (written) std::cout << directory.getChildFile(name).getFullPathName() << ".png\n";
    return written;
}

} // namespace

SurfaceEvent make_event(uint8_t status, uint8_t number, uint8_t value) {
    SurfaceEvent event;
    event.bytes = {status, number, value};
    event.input = decode(event.bytes.data(), event.bytes.size());
    return event;
}

int self_test(App& app, const std::string& wav_path) {
    Session session;
    app.attach(session);
    app.audio_opened(AudioFormat {});
    constexpr uint8_t note_on = 0x90 | surface_channel, note_off = 0x80 | surface_channel;
    const unsigned pads[] {0, 4, 7};
    unsigned blocks_per_second = AudioFormat {}.rate / audio_frames;
    std::vector<int16_t> samples;
    AudioBlock input {}, output {};
    int32_t peak = 0;
    for (unsigned block = 0; block < blocks_per_second * 3; ++block) {
        if (block == 0) for (unsigned pad : pads) send(app, session, make_event(note_on, pad_note(pad), 100));
        if (block == blocks_per_second * 2) for (unsigned pad : pads) send(app, session, make_event(note_off, pad_note(pad), 0));
        output.fill(0);
        app.render(input, output);
        for (int32_t sample : output) {
            peak = std::max(peak, sample == INT32_MIN ? INT32_MAX : std::abs(sample));
            samples.push_back(int16_t(sample >> 16));
        }
    }
    write_wav(wav_path, samples, AudioFormat {}.rate);
    double level = double(peak) / 2147483647.0;
    std::cout << "self-test: peak " << level << " in " << wav_path << '\n';
    if (level < 0.001) { std::cerr << "self-test: the app stayed silent\n"; return 1; }
    return 0;
}

int write_previews(App& app, const std::string& directory) {
    Session session;
    app.attach(session);
    app.audio_opened(AudioFormat {});
    auto folder = file_at(directory);
    folder.createDirectory();
    bool ok = save_frame(app, folder, "home");
    constexpr uint8_t note_on = 0x90 | surface_channel, cc = 0xb0 | surface_channel;
    for (unsigned pad : {0u, 9u, 18u, 27u}) send(app, session, make_event(note_on, pad_note(pad), 96));
    for (unsigned knob = 0; knob < knob_count; ++knob)
        for (unsigned turn = 0; turn <= knob * 2; ++turn) send(app, session, make_event(cc, uint8_t(knob_first_cc + knob), 3));
    ok = save_frame(app, folder, "playing") && ok;
    return ok ? 0 : 1;
}

int crush_pictures(const std::string& input, const std::string& directory) {
    auto source = file_at(input);
    std::vector<juce::File> pictures;
    if (source.isDirectory()) {
        for (const auto& entry : juce::RangedDirectoryIterator(source, false, "*.png"))
            if (!entry.getFile().getFileName().contains("@2x")) pictures.push_back(entry.getFile());
    } else pictures.push_back(source);
    std::sort(pictures.begin(), pictures.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
    auto folder = file_at(directory);
    folder.createDirectory();
    unsigned written = 0;
    for (const auto& picture : pictures) {
        auto image = juce::ImageFileFormat::loadFrom(picture);
        if (!image.isValid()) { std::cerr << "crush: cannot read " << picture.getFullPathName() << '\n'; continue; }
        auto name = picture.getFileNameWithoutExtension();
        bool ok = write_png(frame_image(pack(crush(image, Fit::Letterbox)), 4), folder.getChildFile(name + ".letterbox.png"))
               && write_png(frame_image(pack(crush(image, Fit::Stretch)), 4), folder.getChildFile(name + ".stretch.png"))
               && write_png(frame_image(pack(crush(image, Fit::Stretch, Tone::Dither)), 4), folder.getChildFile(name + ".dither.png"));
        if (ok) ++written;
    }
    std::cout << "crush: " << written << " of " << pictures.size() << " pictures in " << folder.getFullPathName() << '\n';
    return written == pictures.size() && written ? 0 : 1;
}

} // namespace move
