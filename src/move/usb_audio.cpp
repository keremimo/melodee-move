// SPDX-License-Identifier: AGPL-3.0-only
#include "move/usb_audio.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sstream>
#include <sys/ioctl.h>
#include <sound/asound.h>
#include <unistd.h>

namespace move {

const char* usb_state_name(UsbState state) {
    static const char* const names[] {"stopped", "missing", "waiting", "running", "failed"};
    return names[std::min<unsigned>(unsigned(state), 4)];
}

namespace {

// --- The gadget's PCM through ALSA's ioctls (the kernel's sound/asound.h) ---

snd_mask* mask_of(snd_pcm_hw_params& p, int param) { return &p.masks[param - SNDRV_PCM_HW_PARAM_FIRST_MASK]; }
snd_interval* interval_of(snd_pcm_hw_params& p, int param) { return &p.intervals[param - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL]; }

// Everything allowed, for the kernel to narrow (HW_REFINE) or choose from.
void allow_all(snd_pcm_hw_params& p) {
    std::memset(&p, 0, sizeof p);
    for (int n = SNDRV_PCM_HW_PARAM_FIRST_MASK; n <= SNDRV_PCM_HW_PARAM_LAST_MASK; ++n)
        std::fill(std::begin(mask_of(p, n)->bits), std::end(mask_of(p, n)->bits), ~0u);
    for (int n = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; ++n) {
        interval_of(p, n)->min = 0;
        interval_of(p, n)->max = UINT_MAX;
    }
    p.rmask = ~0u;
    p.info = ~0u;
}
void only(snd_pcm_hw_params& p, int param, unsigned value) {
    auto* m = mask_of(p, param);
    std::fill(std::begin(m->bits), std::end(m->bits), 0u);
    m->bits[value / 32] = 1u << (value % 32);
}
bool allows(snd_pcm_hw_params& p, int param, unsigned value) { return mask_of(p, param)->bits[value / 32] & (1u << (value % 32)); }
void exactly(snd_pcm_hw_params& p, int param, unsigned value) {
    auto* i = interval_of(p, param);
    i->min = i->max = value;
    i->integer = 1;
}

class GadgetPcm final : public PcmOut {
public:
    ~GadgetPcm() override { close(); }

    // u_audio names its card's driver UAC2_Gadget: " 0 [UAC2Gadget ]: UAC2_Gadget - UAC2_Gadget".
    std::string find() override {
        std::ifstream cards("/proc/asound/cards");
        std::string line;
        while (std::getline(cards, line)) {
            unsigned card = 0;
            if (std::sscanf(line.c_str(), " %u [", &card) != 1) continue;
            auto from = line.find("]: ");
            if (from == std::string::npos) continue;
            auto to = line.find(" - ", from + 3);
            if (line.substr(from + 3, to == std::string::npos ? std::string::npos : to - from - 3) != "UAC2_Gadget") continue;
            return "/dev/snd/pcmC" + std::to_string(card) + "D0p";
        }
        return {};
    }

    bool open(const std::string& path, unsigned rate, Opened& opened) override {
        close();
        fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return false;
        if (!configure(rate, opened)) {
            close();
            return false;
        }
        return true;
    }

    void close() override {
        if (fd < 0) return;
        ioctl(fd, SNDRV_PCM_IOCTL_DROP);
        ::close(fd);
        fd = -1;
    }

    int wait(int ms) override {
        pollfd waiting {fd, POLLOUT, 0};
        int ready = poll(&waiting, 1, ms);
        if (ready < 0) return errno == EINTR ? 0 : -errno;
        if (ready == 0) return 0;
        if (waiting.revents & (POLLHUP | POLLNVAL)) return -ENODEV;
        return 1; // Room, or POLLERR for an xrun the next write reports
    }

    long write(const uint8_t* bytes, unsigned frames) override {
        snd_xferi transfer {};
        transfer.buf = const_cast<uint8_t*>(bytes);
        transfer.frames = frames;
        if (ioctl(fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &transfer) < 0) return -errno;
        return long(transfer.result);
    }

    bool prepare() override { return ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) == 0; }

private:
    // u_audio offers one format and channel count (the gadget's p_ssize and
    // p_chmask), at most PAGE_SIZE a period and 16 pages a buffer: for 18
    // channels of S32 that is 56-frame periods and a 896-frame (20 ms)
    // buffer. The largest buffer it allows, periods of at most 256 frames.
    bool configure(unsigned rate, Opened& opened) {
        snd_pcm_hw_params p;
        allow_all(p);
        if (ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &p) < 0) return false;
        static const std::pair<unsigned, unsigned> formats[] {
            {SNDRV_PCM_FORMAT_S32_LE, 4}, {SNDRV_PCM_FORMAT_S24_3LE, 3}, {SNDRV_PCM_FORMAT_S16_LE, 2}};
        unsigned format = 0, width = 0;
        for (auto [f, w] : formats)
            if (allows(p, SNDRV_PCM_HW_PARAM_FORMAT, f)) { format = f; width = w; break; }
        unsigned channels = interval_of(p, SNDRV_PCM_HW_PARAM_CHANNELS)->min;
        auto* rates = interval_of(p, SNDRV_PCM_HW_PARAM_RATE);
        if (!width || !channels || rate < rates->min || rate > rates->max) return false;
        unsigned frame = channels * width;
        unsigned period_most = interval_of(p, SNDRV_PCM_HW_PARAM_PERIOD_BYTES)->max / frame;
        unsigned period_least = (interval_of(p, SNDRV_PCM_HW_PARAM_PERIOD_BYTES)->min + frame - 1) / frame;
        unsigned period = std::min(256u, period_most);
        if (!period || period < period_least) return false;
        unsigned periods = std::min(interval_of(p, SNDRV_PCM_HW_PARAM_PERIODS)->max,
                                    interval_of(p, SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->max / (period * frame));
        if (periods < std::max(2u, interval_of(p, SNDRV_PCM_HW_PARAM_PERIODS)->min)) return false;

        snd_pcm_hw_params q;
        allow_all(q);
        only(q, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
        only(q, SNDRV_PCM_HW_PARAM_FORMAT, format);
        only(q, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
        exactly(q, SNDRV_PCM_HW_PARAM_CHANNELS, channels);
        exactly(q, SNDRV_PCM_HW_PARAM_RATE, rate);
        exactly(q, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, period);
        exactly(q, SNDRV_PCM_HW_PARAM_PERIODS, periods);
        if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &q) < 0) return false;
        opened.channels = channels;
        opened.width = width;
        opened.period = interval_of(q, SNDRV_PCM_HW_PARAM_PERIOD_SIZE)->min;
        opened.buffer = interval_of(q, SNDRV_PCM_HW_PARAM_BUFFER_SIZE)->min;

        // Started once the buffer is full; an empty buffer is an xrun (the
        // writer prepares and fills it again). Woken with a few periods' room.
        snd_pcm_sw_params s {};
        s.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
        s.period_step = 1;
        s.avail_min = std::min(opened.buffer / 2, 4 * opened.period);
        s.start_threshold = opened.buffer;
        s.stop_threshold = opened.buffer;
        s.proto = SNDRV_PCM_VERSION;
        if (ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &s) < 0) return false;
        return ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) == 0;
    }

    int fd = -1;
};

} // namespace

std::unique_ptr<PcmOut> gadget_pcm() { return std::make_unique<GadgetPcm>(); }

// --- UsbAudioOut ---

UsbAudioOut::UsbAudioOut(std::unique_ptr<PcmOut> device) : pcm(std::move(device)) {}

UsbAudioOut::~UsbAudioOut() { stop(); }

void UsbAudioOut::start() {
    stop();
    next_check = {};
    running.store(true);
    set_state(UsbState::Missing);
    thread = std::thread([this] { run(); });
}

void UsbAudioOut::stop() {
    running.store(false);
    if (thread.joinable()) thread.join();
    close_device(UsbState::Stopped);
}

void UsbAudioOut::push(const int32_t* mix, const float* submix, unsigned frames) {
    if (!accepting_now.load(std::memory_order_acquire)) return;
    frames = std::min(frames, usb_block_frames);
    for (unsigned f = 0; f < frames; ++f) {
        float* frame = block.data() + usb_channels * f;
        frame[0] = float(double(mix[2 * f]) / 2147483648.0);
        frame[1] = float(double(mix[2 * f + 1]) / 2147483648.0);
        for (unsigned c = 0; c < 2 * usb_submixes; ++c) frame[2 + c] = submix ? submix[c * frames + f] : 0.0f;
    }
    ring->push(block.data(), frames);
}

// Below the SPI thread (FIFO 70 on core 3), on the other cores. Refused for
// the `ableton` user Schwung runs tools as: then it runs as any thread.
void UsbAudioOut::run() {
    cpu_set_t cores;
    CPU_ZERO(&cores);
    for (int core = 0; core < 3; ++core) CPU_SET(core, &cores);
    if (sysconf(_SC_NPROCESSORS_ONLN) > 3) pthread_setaffinity_np(pthread_self(), sizeof cores, &cores);
    sched_param priority {};
    priority.sched_priority = 20;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority);
    pthread_setname_np(pthread_self(), "melodee-usb");
    while (running.load()) {
        cycle(Clock::now(), 50);
        if (!open) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void UsbAudioOut::cycle(Clock::time_point now, int wait_ms) {
    if (now >= next_check) {
        next_check = now + std::chrono::milliseconds(250);
        check(now);
    }
    if (!open) return;
    int ready = pcm->wait(wait_ms);
    if (ready < 0) { close_device(UsbState::Failed); return; }
    if (ready == 0) return; // No room: the computer is not taking frames, or not yet
    const unsigned frame_bytes = opened.channels * opened.width;
    while (open) {
        if (!pending) fill_period();
        long taken = pcm->write(raw.data() + size_t(offset) * frame_bytes, pending);
        if (taken == -EAGAIN) { full_once = true; break; }
        if (taken == 0 || taken == -EINTR) break;
        if (taken == -EPIPE) { // Late: the buffer ran dry. Fill it again from here.
            xruns.fetch_add(1, std::memory_order_relaxed);
            full_once = false;
            if (!pcm->prepare()) close_device(UsbState::Failed);
            break;
        }
        if (taken < 0) { close_device(UsbState::Failed); break; }
        offset += unsigned(taken);
        pending -= unsigned(taken);
        // Room after the buffer was once full: the computer took frames.
        if (full_once) {
            last_room = now;
            if (UsbState(current.load(std::memory_order_relaxed)) == UsbState::Waiting) {
                ring->flush();
                accepting_now.store(true, std::memory_order_release);
                set_state(UsbState::Running);
            }
        }
        fill_sum += ring->buffered();
        ++fill_count;
    }
}

// Every 250 ms: open the device when it is there, notice the computer stop
// recording, trim the conversion.
void UsbAudioOut::check(Clock::time_point now) {
    if (!open) {
        std::string path = pcm->find();
        if (path.empty()) { set_state(UsbState::Missing); return; }
        PcmOut::Opened got;
        if (!pcm->open(path, rate, got) || !got.channels || !got.width || !got.period) { set_state(UsbState::Failed); return; }
        open = true;
        opened = got;
        opens.fetch_add(1, std::memory_order_relaxed);
        open_channels.store(got.channels); open_width.store(got.width); open_period.store(got.period); open_buffer.store(got.buffer);
        raw.assign(size_t(got.period) * got.channels * got.width, 0);
        staged.clear();
        staged.reserve(size_t(usb_channels) * (got.period + 4 * chunk_frames));
        pending = offset = 0;
        full_once = false;
        fill_sum = 0; fill_count = 0;
        last_room = now;
        resampler->reset(rate, rate);
        trim_ppm.store(0);
        ring->flush();
        set_state(UsbState::Waiting); // Until the computer takes frames
        return;
    }
    if (UsbState(current.load(std::memory_order_relaxed)) != UsbState::Running) return;
    if (now - last_room > std::chrono::milliseconds(250)) { // The computer stopped recording
        accepting_now.store(false, std::memory_order_release);
        ring->flush();
        set_state(UsbState::Waiting);
        return;
    }
    if (fill_count) {
        double factor = drift_trim(fill_sum / fill_count, prime_frames);
        resampler->trim(factor);
        trim_ppm.store(int(std::lround((factor - 1.0) * 1e6)));
        fill_sum = 0; fill_count = 0;
    }
}

// One period from the ring (silence while it primes, or while nothing is
// accepted), through the converter, encoded for the device. Channels the
// device has beyond the ring's are silent; a device with fewer gets the first.
bool UsbAudioOut::fill_period() {
    const unsigned period = opened.period, channels = opened.channels, width = opened.width;
    while (staged.size() < size_t(usb_channels) * period) {
        ring->pull(chunk.data(), chunk_frames, prime_frames);
        for (unsigned n = 0; n < chunk_frames; ++n)
            resampler->feed(chunk.data() + usb_channels * n,
                            [&](const float* frame) { staged.insert(staged.end(), frame, frame + usb_channels); });
    }
    for (unsigned f = 0; f < period; ++f) {
        const float* in = staged.data() + size_t(usb_channels) * f;
        uint8_t* out = raw.data() + size_t(f) * channels * width;
        for (unsigned c = 0; c < channels; ++c) pcm_store(c < usb_channels ? in[c] : 0.0f, out + c * width, width);
    }
    staged.erase(staged.begin(), staged.begin() + std::ptrdiff_t(usb_channels) * period);
    pending = period;
    offset = 0;
    return true;
}

// u_audio sends its last packet again while its PCM is not running, so the
// computer would hear the last few frames buzz on after the device closes:
// silence goes out first, up to a buffer's worth, while the computer takes it.
void UsbAudioOut::close_device(UsbState then) {
    accepting_now.store(false, std::memory_order_release);
    ring->flush();
    if (open) {
        const unsigned frame_bytes = opened.channels * opened.width;
        std::vector<uint8_t> quiet(size_t(opened.period) * frame_bytes, 0);
        unsigned sent = 0;
        for (int tries = 0; tries < 10 && sent < 2 * opened.buffer; ++tries) {
            if (pcm->wait(10) <= 0) break;
            long taken;
            while ((taken = pcm->write(quiet.data(), opened.period)) > 0) sent += unsigned(taken);
            if (taken != -EAGAIN) break;
        }
        pcm->close();
        open = false;
    }
    pending = offset = 0;
    staged.clear();
    open_channels.store(0); open_width.store(0); open_period.store(0); open_buffer.store(0); trim_ppm.store(0);
    set_state(then);
}

UsbStats UsbAudioOut::stats() const {
    UsbStats now;
    now.state = UsbState(current.load(std::memory_order_acquire));
    now.channels = open_channels.load();
    now.bits = open_width.load() * 8;
    now.period = open_period.load();
    now.buffer = open_buffer.load();
    now.buffered = ring->buffered();
    now.overflows = ring->overflows.load();
    now.underruns = ring->underruns.load();
    now.xruns = xruns.load();
    now.opens = opens.load();
    now.trim_ppm = trim_ppm.load();
    return now;
}

std::string UsbAudioOut::status() const {
    auto now = stats();
    std::ostringstream out;
    out << "usb_audio=" << usb_state_name(now.state);
    if (now.state == UsbState::Running || now.state == UsbState::Waiting)
        out << " usb_channels=" << now.channels << " usb_bits=" << now.bits << " usb_fill=" << now.buffered
            << " usb_trim_ppm=" << now.trim_ppm << " usb_underruns=" << now.underruns << " usb_xruns=" << now.xruns;
    return out.str();
}

} // namespace move
