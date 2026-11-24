// SPDX-License-Identifier: AGPL-3.0-only
#include "move/power.h"
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <vector>

extern char** environ;

namespace move {

namespace power {

bool shut_down(std::string& why) {
    const char* program = "/usr/bin/dbus-send";
    if (access(program, X_OK) != 0) { why = std::string("no ") + program; return false; }
    const char* arguments[] {"dbus-send", "--system", "--print-reply", "--reply-timeout=10000", "--dest=com.ableton.system",
                             "/com/ableton/System/Power", "com.ableton.system.shutDown", nullptr};
    // Our environment but the library path the launcher sets for our own
    // libraries: dbus-send is the Move's, with the Move's.
    std::vector<char*> environment;
    for (char** entry = environ; entry && *entry; ++entry)
        if (std::strncmp(*entry, "LD_LIBRARY_PATH=", 16) && std::strncmp(*entry, "LD_PRELOAD=", 11)) environment.push_back(*entry);
    environment.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0); // its "method return"; errors stay in our log
    pid_t child = 0;
    int error = posix_spawn(&child, program, &actions, nullptr, const_cast<char* const*>(arguments), environment.data());
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) { why = std::string("cannot run dbus-send: ") + std::strerror(error); return false; }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
    why = "com.ableton.system did not take shutDown (dbus-send " +
          (WIFEXITED(status) ? "exit " + std::to_string(WEXITSTATUS(status)) : std::string("killed")) + ")";
    return false;
}

} // namespace power

bool PowerQuestion::report(uint8_t state, uint8_t charge, Clock::time_point now) {
    if (charge <= 100) battery = charge;
    if (!(state & power::asking_flag)) { // nothing asked, or the XMOS took the question back
        answered = false;
        if (asked != Power::Asking) return false;
        asked = Power::On;
        return true;
    }
    if (asked != Power::On) return false;
    if (answered && now - answered_at < std::chrono::seconds(1)) return false; // the no's own press, not yet cleared
    answered = false;
    asked = Power::Asking;
    return true;
}

bool PowerQuestion::take(const Input& input, Answer& answer, Clock::time_point now) {
    answer = Answer::None;
    bool push = input.kind == Input::Kind::Button && input.index == uint8_t(Button::MainPush);
    bool back = input.kind == Input::Kind::Button && input.index == uint8_t(Button::Back);
    if ((push || back) && !input.pressed) { // a release: the question's if it took the press
        bool& held = push ? push_held : back_held;
        bool ours = held;
        held = false;
        return ours;
    }
    if (asked == Power::On) return false;
    if (push) {
        push_held = true;
        if (asked == Power::Asking) { answer = Answer::Yes; asked = Power::ShuttingDown; }
        return true;
    }
    if (back) {
        back_held = true;
        if (asked == Power::Asking) {
            answer = Answer::No;
            asked = Power::On;
            answered = true;
            answered_at = now;
        }
        return true;
    }
    return input.kind == Input::Kind::MainKnob || (input.kind == Input::Kind::Touch && input.index == touch_main);
}

} // namespace move
