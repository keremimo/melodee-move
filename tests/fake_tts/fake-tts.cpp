// SPDX-License-Identifier: AGPL-3.0-only
// A stand-in for Schwung's `schwung-tts` sidecar, for the simulated Move
// (tools/check.sh, sim_speech): it attaches to the segment Melodee made
// (MELODEE_TTS_SEGMENT), says the user's screen reader is on, writes each
// request's text as a line of FAKE_TTS_LOG, and answers each with 100 ms of
// a tone in the ring, as the sidecar answers with speech. It ends when its
// parent does, as the sidecar does.
#include "move/schwung_tts.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace move::schwung_tts;

int main() {
    const char* name = std::getenv("MELODEE_TTS_SEGMENT");
    const char* log = std::getenv("FAKE_TTS_LOG");
    pid_t parent = getppid();
    Shared* s = nullptr;
    for (int tries = 0; tries < 200 && !s; ++tries) {
        int fd = shm_open(name && *name ? name : segment, O_RDWR, 0);
        if (fd >= 0) {
            void* map = mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            close(fd);
            if (map != MAP_FAILED) s = static_cast<Shared*>(map);
        }
        if (!s) usleep(50000);
    }
    if (!s || __atomic_load_n(&s->magic, __ATOMIC_ACQUIRE) != magic || s->version != version) return 1;

    uint32_t last_speak = s->speak_count, last_seq = s->req_seq;
    s->sidecar_pid = uint32_t(getpid());
    s->discard_until = s->audio_write;
    __atomic_store_n(&s->speak_ack, last_speak, __ATOMIC_RELEASE);
    s->state = Settings {1, 70, engine_espeak, 1.0f, 110.0f};
    __atomic_store_n(&s->state_seq, s->state_seq + 1, __ATOMIC_RELEASE);

    static char text[text_length];
    uint32_t tone_left = 0;
    double phase = 0;
    while (getppid() == parent) {
        uint32_t seq = __atomic_load_n(&s->req_seq, __ATOMIC_ACQUIRE);
        if (!(seq & 1) && seq != last_seq) {
            uint32_t count = s->speak_count;
            std::memcpy(text, s->text, sizeof text);
            text[sizeof text - 1] = '\0';
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (__atomic_load_n(&s->req_seq, __ATOMIC_RELAXED) == seq) {
                last_seq = seq;
                if (count != last_speak) {
                    last_speak = count;
                    if (log)
                        if (FILE* file = std::fopen(log, "a")) {
                            std::fprintf(file, "%s\n", text);
                            std::fclose(file);
                        }
                    s->discard_until = s->audio_write;
                    __atomic_store_n(&s->speak_ack, count, __ATOMIC_RELEASE);
                    tone_left = 4410;
                }
            }
        }
        uint32_t write = s->audio_write;
        while (tone_left > 0) {
            uint32_t used = write - __atomic_load_n(&s->audio_read, __ATOMIC_ACQUIRE);
            if (used > ring_samples) used = ring_samples;
            if (used >= 8192) break; // the sidecar's fill-ahead
            auto value = int16_t(8000.0 * std::sin(phase));
            phase += 2.0 * M_PI * 440.0 / 44100.0;
            s->ring[write & (ring_samples - 1)] = value;
            s->ring[(write + 1) & (ring_samples - 1)] = value;
            write += 2;
            --tone_left;
        }
        __atomic_store_n(&s->audio_write, write, __ATOMIC_RELEASE);
        usleep(5000);
    }
    return 0;
}
