// SPDX-License-Identifier: AGPL-3.0-only
#include "move/realtime.h"
#include <cerrno>
#include <climits>
#include <cstring>
#include <sched.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace move {

std::string helper_beside(const std::string& program) {
    auto slash = program.rfind('/');
    return slash == std::string::npos ? std::string("heal") : program.substr(0, slash + 1) + "heal";
}

bool realtime_by_helper(std::string& why) {
    char self[PATH_MAX];
    ssize_t size = readlink("/proc/self/exe", self, sizeof self - 1);
    if (size <= 0) { why = "no /proc/self/exe"; return false; }
    self[size] = '\0';
    std::string helper = helper_beside(self);
    // Only once Schwung's heal has made it root's and setuid: before that it
    // could not help, and there is nothing to run.
    struct stat info {};
    if (stat(helper.c_str(), &info) != 0) { why = "no " + helper + " (Schwung's heal installs it)"; return false; }
    if (info.st_uid != 0 || !(info.st_mode & S_ISUID)) { why = helper + " is not root's and setuid yet"; return false; }
    std::string tid = std::to_string(gettid());
    char* argv[] = {const_cast<char*>("heal"), tid.data(), nullptr};
    char* env[] = {nullptr};
    pid_t child = 0;
    if (int error = posix_spawn(&child, helper.c_str(), nullptr, nullptr, argv, env); error != 0) {
        why = std::string("cannot run ") + helper + ": " + std::strerror(error);
        return false;
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if (sched_getscheduler(0) != SCHED_FIFO) {
        why = helper + " did not make it realtime (exit " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) + ")";
        return false;
    }
    why = "SCHED_FIFO from " + helper;
    return true;
}

} // namespace move
