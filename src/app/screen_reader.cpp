// SPDX-License-Identifier: AGPL-3.0-only
#include "app/screen_reader.h"
#include <cctype>
#include <map>

namespace app {

namespace {

void add(std::string& text, const std::string& part) {
    if (part.empty()) return;
    if (!text.empty()) text += ' ';
    text += part;
    if (text.back() != '.' && text.back() != '?' && text.back() != '!') text += '.';
}

} // namespace

std::string ScreenReader::next(const View& now, Clock::time_point at) {
    if (!(now == seen)) {
        seen = now;
        changed = at;
    }
    if (started && seen == said) return {};
    if (at - changed < debounce) return {};
    std::string text;
    if (!started) {
        started = true;
        add(text, "Melodee");
        add(text, speakable(seen.view));
        add(text, speakable(seen.track));
        add(text, quit_hint);
    } else if (seen.view != said.view) {
        add(text, speakable(seen.view));
        add(text, speakable(seen.track));
    } else if (seen.track != said.track) {
        add(text, speakable(seen.track));
    }
    if (!seen.knob.empty() && seen.knob != said.knob) add(text, speakable(seen.knob));
    if (!seen.message.empty() && seen.message != said.message) add(text, speakable(seen.message));
    said = seen;
    return text;
}

std::string ScreenReader::view_name(const std::string& id, bool play) {
    if (play) return "Play";
    static const std::map<std::string, std::string> names {
        {"auto-sample", "Auto sample"}, {"audio-input", "Audio input"}, {"to-pads", "To Pads"},
        {"slice-options", "Slice options"}, {"keys-scale", "Keys and Scale"}, {"store-chord", "Store chord"},
        {"copy-pad", "Copy pad"}, {"load-instruments", "Load instrument"}, {"load-modules", "Load module"},
        {"kit-pages", "Pad pages"}, {"kit-sources", "Kit source"}, {"looper-more", "Loop sheet"},
        {"looper-ask", "Looper question"}, {"load-kit", "Load kit"}, {"browser-options", "Browser options"},
        {"load-samples", "Load sample"}, {"delete-file", "Delete file"}, {"midi-config", "MIDI settings"},
        {"midi-sounds", "MIDI sounds"}, {"fx-browser", "Effects"}, {"fx-options", "Effect options"},
        {"chain", "Effect chain"}, {"knob-fx", "Knob FX"}, {"pad-fx", "Pad FX"}, {"pad-fx-options", "Pad FX options"},
        {"flex", "Flex Beat"}, {"new-track", "New track"}, {"sample-options", "Sample options"},
        {"unfreeze", "Unfreeze"}, {"project-options", "Project options"}, {"session", "Patterns"},
        {"session-copy", "Copy pattern"}, {"recall", "Audio recall"}, {"loop", "Loop Mode"},
        {"seq-pages", "Sequence pages"}, {"seq-jobs", "Step jobs"}, {"roll-jobs", "Roll"},
        {"macro-lanes", "Macro lanes"}, {"workflow", "Workflow"}, {"repeat", "Note Repeat"},
        {"setup-pads", "Setup, pads"}, {"song-clip", "Song clip"}, {"song-export", "Song export"},
        {"track-options", "Track options"}, {"tracks", "Tracks"}, {"delete-track", "Delete track"},
        {"copy-track", "Copy track"}, {"undo-list", "Undo list"},
    };
    if (auto found = names.find(id); found != names.end()) return found->second;
    std::string words = id;
    for (char& c : words)
        if (c == '-' || c == '_') c = ' ';
    if (!words.empty()) words[0] = char(std::toupper(static_cast<unsigned char>(words[0])));
    return words;
}

std::string ScreenReader::speakable(const std::string& text) {
    std::string out;
    size_t n = 0;
    while (n < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[n]))) {
            if (!out.empty() && out.back() != ' ') out += ' ';
            ++n;
            continue;
        }
        size_t end = n;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
        std::string word = text.substr(n, end - n);
        size_t letters = 0, capitals = 0;
        for (char c : word) {
            letters += std::isalpha(static_cast<unsigned char>(c)) != 0;
            capitals += std::isupper(static_cast<unsigned char>(c)) != 0;
        }
        if (letters >= 4 && capitals == letters)
            for (char& c : word) c = char(std::tolower(static_cast<unsigned char>(c)));
        out += word;
        n = end;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

} // namespace app
