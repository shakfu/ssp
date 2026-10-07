#pragma once

#include <algorithm>
#include <atomic>
#include <vector>

#include "Station.h"

namespace radio {

// One open station: the worker thread reads the file ahead into a ring, the audio thread pulls from it.
// An unopened Stream is silent.
class Stream {
public:
    static constexpr size_t CAPACITY = 1 << 16;  // frames; 1.4 s at 48 kHz, read ahead in 10 ms steps

    // tuned: a new station rather than a reset, which sounds the static burst
    explicit Stream(bool tuned = true) : tuned_(tuned) {}

    // Worker thread. `rate` is the source rate to play the station at.
    bool open(const Station& st, uint64_t frame, float rate) {
        rate_ = rate;
        open_ = reader_.open(st, frame);
        if (open_) fill();
        return open_;
    }

    // Worker thread: tops the ring up from the file.
    void fill() {
        if (!open_) return;
        size_t head = head_.load(std::memory_order_relaxed);
        size_t space = CAPACITY - (head - tail_.load(std::memory_order_acquire));
        while (space > 0) {
            size_t i = head & (CAPACITY - 1);
            size_t len = std::min(space, CAPACITY - i);
            size_t got = reader_.read(buf_.data() + i, len);
            head += got;
            space -= got;
            head_.store(head, std::memory_order_release);
            if (got < len) {
                open_ = false;  // read error: play out what is buffered
                break;
            }
        }
    }

    // Audio thread: the next frame, or 0 on underrun.
    float pull() {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return 0.0f;
        float v = buf_[tail & (CAPACITY - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return v;
    }

    bool playing() const { return open_ || head_.load() != tail_.load(); }
    float rate() const { return rate_; }
    bool tuned() const { return tuned_; }

private:
    Reader reader_;
    std::vector<float> buf_ = std::vector<float>(CAPACITY);
    std::atomic<size_t> head_{ 0 }, tail_{ 0 };
    std::atomic<bool> open_{ false };
    float rate_ = 0.0f;
    const bool tuned_;
};

}  // namespace radio
