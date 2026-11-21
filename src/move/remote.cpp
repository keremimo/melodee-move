// SPDX-License-Identifier: AGPL-3.0-only
#include "move/remote.h"
#include "move/controls.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

namespace move {
namespace {

constexpr unsigned gap_ms = 12; // between messages: a few exchanges, so each is its own cycle

struct Named { const char* name; Button button; };
constexpr Named named[] {
    {"play", Button::Play}, {"rec", Button::Rec}, {"shift", Button::Shift}, {"back", Button::Back},
    {"note-session", Button::Menu}, {"menu", Button::Menu}, {"capture", Button::Capture}, {"sampling", Button::Sample},
    {"sample", Button::Sample}, {"loop", Button::Loop}, {"mute", Button::Mute}, {"undo", Button::Undo}, {"copy", Button::Copy},
    {"delete", Button::Delete}, {"plus", Button::Up}, {"minus", Button::Down}, {"up", Button::Up}, {"down", Button::Down},
    {"left", Button::Left}, {"right", Button::Right}, {"wheel-push", Button::MainPush}, {"push", Button::MainPush},
    {"track1", Button::Row1}, {"track2", Button::Row2}, {"track3", Button::Row3}, {"track4", Button::Row4},
};

int number_after(const std::string& word, const char* prefix) {
    size_t n = std::strlen(prefix);
    if (word.compare(0, n, prefix) || word.size() == n) return -1;
    for (size_t at = n; at < word.size(); ++at)
        if (!std::isdigit(static_cast<unsigned char>(word[at]))) return -1;
    return std::atoi(word.c_str() + n);
}

// A control's press and release, for buttons, pads and steps.
bool edges(const std::string& word, std::array<uint8_t, 3>& down, std::array<uint8_t, 3>& up) {
    for (const auto& each : named)
        if (word == each.name) {
            down = {0xb0, button_cc(each.button), 127};
            up = {0xb0, button_cc(each.button), 0};
            return true;
        }
    if (int pad = number_after(word, "pad"); pad >= 1 && pad <= int(pad_count)) {
        down = {0x90, pad_note(unsigned(pad - 1)), 100};
        up = {0x80, pad_note(unsigned(pad - 1)), 0};
        return true;
    }
    if (int step = number_after(word, "step"); step >= 1 && step <= int(step_count)) {
        down = {0x90, step_note(unsigned(step - 1)), 127};
        up = {0x80, step_note(unsigned(step - 1)), 0};
        return true;
    }
    return false;
}

uint8_t detents(int n) { return uint8_t(n < 0 ? 128 + std::max(n, -63) : std::min(n, 63)); }

} // namespace

bool press_messages(const std::string& spec, std::vector<Injected>& out, std::string& error) {
    std::string words = spec;
    for (auto& c : words) c = char(std::tolower(static_cast<unsigned char>(c)));
    // Turns: wheel+N, knobN+V, volume+V (no modifiers before them but held ones).
    std::vector<std::string> parts;
    for (size_t at = 0;;) {
        // '+' joins words, but a turn's sign is part of its word: split on '+' only before a letter.
        size_t next = at;
        while (next < words.size() && !(words[next] == '+' && next + 1 < words.size() && std::isalpha(static_cast<unsigned char>(words[next + 1])))) ++next;
        parts.push_back(words.substr(at, next - at));
        if (next >= words.size()) break;
        at = next + 1;
    }
    std::string last = parts.back();
    parts.pop_back();
    std::vector<std::array<uint8_t, 3>> held_up;
    for (const auto& part : parts) { // held around what comes last
        std::array<uint8_t, 3> down {}, up {};
        if (!edges(part, down, up)) { error = "not a control: " + part; return false; }
        out.push_back({down, gap_ms});
        held_up.push_back(up);
    }
    auto turn = [&](uint8_t cc, int n) { out.push_back({{0xb0, cc, detents(n)}, gap_ms}); };
    std::array<uint8_t, 3> down {}, up {};
    if (!last.compare(0, 5, "wheel") && last.size() > 5 && (last[5] == '+' || last[5] == '-')) turn(main_knob_cc, std::atoi(last.c_str() + 5));
    else if (!last.compare(0, 6, "volume") && last.size() > 6 && (last[6] == '+' || last[6] == '-')) turn(master_knob_cc, std::atoi(last.c_str() + 6));
    else if (!last.compare(0, 4, "knob") && last.size() > 6 && last[4] >= '1' && last[4] <= '8' && (last[5] == '+' || last[5] == '-'))
        turn(uint8_t(knob_first_cc + (last[4] - '1')), std::atoi(last.c_str() + 5));
    else if (!last.compare(0, 5, "hold-") && edges(last.substr(5), down, up)) out.push_back({down, gap_ms});
    else if (!last.compare(0, 8, "release-") && edges(last.substr(8), down, up)) out.push_back({up, gap_ms});
    else if (!last.compare(0, 5, "long-") && edges(last.substr(5), down, up)) { out.push_back({down, 500}); out.push_back({up, gap_ms}); }
    else if (edges(last, down, up)) { out.push_back({down, gap_ms}); out.push_back({up, gap_ms}); }
    else { error = "not a control: " + last; return false; }
    for (auto at = held_up.rbegin(); at != held_up.rend(); ++at) out.push_back({*at, gap_ms});
    return true;
}

ControlChannel::~ControlChannel() { close(); }

bool ControlChannel::open(const std::string& path, std::string& error) {
    close();
    sockaddr_un address {};
    if (path.size() >= sizeof address.sun_path) { error = "control path too long"; return false; }
    listening = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listening < 0) { error = std::strerror(errno); return false; }
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof address.sun_path - 1);
    unlink(path.c_str()); // one left by a run that did not end well
    if (bind(listening, reinterpret_cast<sockaddr*>(&address), sizeof address) || listen(listening, 4)) {
        error = std::strerror(errno);
        ::close(listening);
        listening = -1;
        return false;
    }
    chmod(path.c_str(), 0600); // the Move's own users only
    where = path;
    return true;
}

void ControlChannel::poll(const std::function<std::string(const std::string&)>& answer) {
    if (listening < 0) return;
    while (true) {
        int client = accept4(listening, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) return;
        // A line, waited for a moment at most.
        std::string line;
        pollfd ready {client, POLLIN, 0};
        char buffer[512];
        while (line.find('\n') == std::string::npos && line.size() < 4096 && ::poll(&ready, 1, 200) > 0) {
            ssize_t got = read(client, buffer, sizeof buffer);
            if (got <= 0) break;
            line.append(buffer, size_t(got));
        }
        if (auto end = line.find('\n'); end != std::string::npos) line.resize(end);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::string reply = line.empty() ? "empty" : answer(line);
        reply += '\n';
        for (size_t sent = 0; sent < reply.size();) {
            ssize_t n = write(client, reply.data() + sent, reply.size() - sent);
            if (n <= 0) break;
            sent += size_t(n);
        }
        ::close(client);
    }
}

void ControlChannel::close() {
    if (listening >= 0) ::close(listening);
    listening = -1;
    if (!where.empty()) unlink(where.c_str());
    where.clear();
}

int send_command(const std::string& path, const std::string& line) {
    sockaddr_un address {};
    if (path.size() >= sizeof address.sun_path) { std::cerr << "melodee-move: control path too long\n"; return 1; }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof address.sun_path - 1);
    if (fd < 0 || connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address)) {
        std::cerr << "melodee-move: not running (" << path << "): " << std::strerror(errno) << '\n';
        if (fd >= 0) ::close(fd);
        return 1;
    }
    std::string sent = line + "\n";
    if (write(fd, sent.data(), sent.size()) != ssize_t(sent.size())) { ::close(fd); return 1; }
    char buffer[4096];
    ssize_t got;
    while ((got = read(fd, buffer, sizeof buffer)) > 0) std::cout.write(buffer, got);
    ::close(fd);
    return 0;
}

} // namespace move
