// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstddef>

// Lock-free hand-offs between the runtime's threads and JACK's process
// callback, which may not lock, allocate or wait.
namespace move {

// One producer thread, one consumer thread. Holds Size - 1 items.
template <class T, size_t Size> class SpscRing {
    static_assert(Size >= 2 && (Size & (Size - 1)) == 0, "Size must be a power of two");
public:
    bool push(const T& item) {
        size_t head = write.load(std::memory_order_relaxed), next = (head + 1) & (Size - 1);
        if (next == read.load(std::memory_order_acquire)) return false;
        items[head] = item;
        write.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& item) {
        size_t tail = read.load(std::memory_order_relaxed);
        if (tail == write.load(std::memory_order_acquire)) return false;
        item = items[tail];
        read.store((tail + 1) & (Size - 1), std::memory_order_release);
        return true;
    }
    bool empty() const { return read.load(std::memory_order_acquire) == write.load(std::memory_order_acquire); }

private:
    std::array<T, Size> items {};
    std::atomic<size_t> write {0}, read {0};
};

// The newest of a stream of values, one writer thread and one reader thread:
// the writer fills back() and publish()es it; the reader take()s the newest
// published one, if any came since, and reads front(). Neither ever waits.
template <class T> class TripleBuffer {
public:
    T& back() { return buffers[back_index]; }
    void publish() { back_index = middle.exchange(back_index | fresh, std::memory_order_acq_rel) & index_mask; }
    bool take() {
        if (!(middle.load(std::memory_order_acquire) & fresh)) return false;
        front_index = middle.exchange(front_index, std::memory_order_acq_rel) & index_mask;
        return true;
    }
    const T& front() const { return buffers[front_index]; }

private:
    static constexpr unsigned fresh = 4, index_mask = 3;
    std::array<T, 3> buffers {};
    unsigned back_index = 0, front_index = 1;
    std::atomic<unsigned> middle {2};
};

} // namespace move
