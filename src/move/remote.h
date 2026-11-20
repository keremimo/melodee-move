// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The runtime's control channel (Melodee's `device.py ctl` on the MPC): a
// Unix socket the running program listens on, a line in, a reply out, so a
// command over SSH reaches it:
//
//   melodee-move --ctl 'melodee-press shift+step1'   (--home DIR: its folder)
//
// `status` is the runtime's status line; `melodee-press` (or `press`) plays
// presses into the Move's input as if a hand made them: they reach
// App::audio_event() and App::surface() as the device's own messages do. The
// rest goes to App::command() (the app's own commands: `melodee-render`).
// The runtime polls the socket on its service thread; nothing here runs on
// the audio thread but the injected messages' queue (Host::injected).
namespace move {

// One message for the Move's input, and how long to wait after it.
struct Injected {
    std::array<uint8_t, 3> bytes {};
    unsigned wait_ms = 0;
};

// A press as Melodee's melodee-press writes it, as the Move's messages:
//   BUTTON         play, rec, shift, back, note-session, capture, sampling,
//                  loop, mute, undo, copy, delete, plus, minus, left, right,
//                  wheel-push, track1-track4: pressed and let go
//   padN, stepN    pads 1-32 (bottom left first), steps 1-16
//   A+B+X          A and B held around X (shift+step1, delete+pad5,
//                  track1+copy+step3)
//   wheel+N        the wheel N detents (wheel-N back); knobN+V, knobN-V the
//                  eight knobs; volume+V the Volume knob
//   hold-X         X pressed and left down; release-X let go
//   long-X         X held 0.5 s, then let go (Undo held: the undo list)
// False with `error` set when a word is not one of these.
bool press_messages(const std::string& spec, std::vector<Injected>& out, std::string& error);

// The socket, listened on by the runtime.
class ControlChannel {
public:
    ControlChannel() = default;
    ~ControlChannel();
    ControlChannel(const ControlChannel&) = delete;
    ControlChannel& operator=(const ControlChannel&) = delete;
    bool open(const std::string& path, std::string& error);
    // Any command waiting, answered by `answer`; returns at once when none.
    void poll(const std::function<std::string(const std::string&)>& answer);
    void close();

private:
    int listening = -1;
    std::string where;
};

// The client's side: `line` sent, the reply printed. 0 on success.
int send_command(const std::string& path, const std::string& line);

} // namespace move
