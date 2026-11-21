// SPDX-License-Identifier: AGPL-3.0-only
#include "move/speech.h"
#include "move/schwung_tts.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace move {

Speech::~Speech() { stop(); }

bool Speech::start() {
    const char* sidecar = std::getenv("MELODEE_TTS_SIDECAR");
    const char* segment = std::getenv("MELODEE_TTS_SEGMENT");
    return start(sidecar && *sidecar ? sidecar : schwung_tts::sidecar, segment && *segment ? segment : schwung_tts::segment);
}

bool Speech::start(const std::string& sidecar, const std::string& segment) {
    if (shared) return true;
    if (access(sidecar.c_str(), X_OK) != 0) return false; // no Schwung, or one without the sidecar
    int fd = shm_open(segment.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0) return false;
    // Schwung's shim (the `ableton` user) opens it again once Move is back:
    // a run as root must not leave it root's alone.
    fchmod(fd, 0666);
    if (ftruncate(fd, sizeof(schwung_tts::Shared)) != 0) { close(fd); return false; }
    void* map = mmap(nullptr, sizeof(schwung_tts::Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return false;
    // The shim that made it last is gone with Move; start it afresh, as the
    // shim does, and the sidecar takes what it finds.
    auto* fresh = static_cast<schwung_tts::Shared*>(map);
    std::memset(static_cast<void*>(fresh), 0, sizeof *fresh);
    fresh->version = schwung_tts::version;
    __atomic_store_n(&fresh->magic, schwung_tts::magic, __ATOMIC_RELEASE);

    // The sidecar as Schwung's shim starts it: in a session of its own, at
    // ordinary priority, its output to /dev/null, none of our files or
    // libraries (it ends by itself when this process does).
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    sigset_t none, all;
    sigemptyset(&none);
    sigfillset(&all);
    sched_param ordinary {};
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setsigdefault(&attributes, &all);
    posix_spawnattr_setschedpolicy(&attributes, SCHED_OTHER);
    posix_spawnattr_setschedparam(&attributes, &ordinary);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSCHEDULER);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclosefrom_np(&actions, 3);
    std::vector<char*> env;
    for (char** each = environ; each && *each; ++each)
        if (std::strncmp(*each, "LD_", 3) != 0 && std::strncmp(*each, "FONTCONFIG_", 11) != 0) env.push_back(*each);
    env.push_back(nullptr);
    char* argv[] = {const_cast<char*>("schwung-tts"), nullptr};
    int error = posix_spawn(&child, sidecar.c_str(), &actions, &attributes, argv, env.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (error != 0) {
        child = -1;
        munmap(map, sizeof(schwung_tts::Shared));
        return false;
    }
    shared = fresh;
    segment_name = segment;
    attached.store(true, std::memory_order_release);
    return true;
}

void Speech::stop() {
    // The mapping stays: the audio thread may be mixing from it still.
    attached.store(false, std::memory_order_release);
    if (child <= 0) return;
    kill(child, SIGTERM);
    for (int tries = 0; tries < 100; ++tries) {
        if (waitpid(child, nullptr, WNOHANG) == child) { child = -1; return; }
        usleep(10000);
    }
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    child = -1;
}

bool Speech::ready() {
    if (!shared) return false;
    uint32_t state = __atomic_load_n(&shared->state_seq, __ATOMIC_ACQUIRE);
    if (state != seen_state) {
        seen_state = state;
        on = shared->state.enabled != 0;
        // Go on from the count the sidecar took in.
        speak_count.store(shared->speak_count, std::memory_order_release);
        is_ready = true;
    }
    return is_ready;
}

bool Speech::say(const std::string& text) {
    if (!enabled() || text.empty()) return false;
    auto* s = shared;
    uint32_t seq = s->req_seq;
    __atomic_store_n(&s->req_seq, seq + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    s->want = s->state; // the user's voice, speed and volume, as the sidecar loaded them
    size_t length = std::min(text.size(), size_t(schwung_tts::text_length - 1));
    std::memcpy(s->text, text.data(), length);
    s->text[length] = '\0';
    uint32_t count = speak_count.load(std::memory_order_relaxed) + 1;
    s->speak_count = count;
    __atomic_store_n(&s->req_seq, seq + 2, __ATOMIC_RELEASE);
    speak_count.store(count, std::memory_order_release);
    return true;
}

void Speech::mix(AudioBlock& output) {
    if (!attached.load(std::memory_order_acquire)) return;
    auto* s = shared;
    uint32_t read = s->audio_read;
    uint32_t ack = __atomic_load_n(&s->speak_ack, __ATOMIC_ACQUIRE);
    uint32_t write = __atomic_load_n(&s->audio_write, __ATOMIC_ACQUIRE);
    uint32_t available = 0;
    if (ack != speak_count.load(std::memory_order_acquire)) {
        read = write; // what comes now is the utterance before the one asked for
    } else {
        if (ack != seen_ack) { // below discard_until: the one before
            uint32_t from = s->discard_until;
            if (int32_t(from - read) > 0) read = from;
            seen_ack = ack;
        }
        available = write - read;
        if (available > schwung_tts::ring_samples) { read = write; available = 0; } // the sidecar started again
    }
    uint32_t frames = std::min(available / 2, uint32_t(audio_frames));
    for (uint32_t n = 0; n < frames * 2; ++n) {
        int64_t sum = int64_t(output[n]) + (int64_t(s->ring[(read + n) & (schwung_tts::ring_samples - 1)]) << 16);
        output[n] = int32_t(std::clamp<int64_t>(sum, INT32_MIN, INT32_MAX));
    }
    read += frames * 2;
    if (read != s->audio_read) __atomic_store_n(&s->audio_read, read, __ATOMIC_RELEASE);
    if (frames) mixed_frames.fetch_add(frames, std::memory_order_relaxed);
}

} // namespace move
