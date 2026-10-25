#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "Grid.h"

struct monome;  // libmonome's, global
struct monome_poll_group;
struct monome_event;

namespace ssp::monome {

// A monome grid of at least 16 x 8, through libmonome's libftdi backend: the SSP's kernel has no driver for
// its FTDI chip (docs/dev/monome.md). One instance in the process holds a grid at a time; the others wait.
// A thread of its own finds the grid among the FTDI devices, reads its keys, sends the latest frame's changes,
// and finds it again after an unplug. The grid goes dark when the instance goes.
class GridDevice {
public:
    using Handler = std::function<void(const Key&)>;  // the device's thread
    static constexpr int FIND_MS = 1000;  // between looks for a grid
    static constexpr int WAIT_MS = 8;     // the longest a new frame waits for a key read to end

    explicit GridDevice(Handler h);
    ~GridDevice();

    void show(const Frame& f);  // any thread: the frame to send next
    bool isOpen() const { return open_.load(std::memory_order_relaxed); }

private:
    void run();
    bool open();              // device thread: the first grid free to open
    void close(bool darken);  // device thread
    void flush();             // device thread: the latest frame's changed quadrants
    static void key(const ::monome_event* e, void* data);

    Handler handler_;
    ::monome* m_ = nullptr;
    ::monome_poll_group* group_ = nullptr;
    Surface surface_;
    std::vector<Quad> quads_;

    std::mutex frameLock_;
    Frame frame_;
    bool dirty_ = false;

    std::atomic<bool> open_{ false }, quit_{ false };
    std::thread thread_;
};

}  // namespace ssp::monome
