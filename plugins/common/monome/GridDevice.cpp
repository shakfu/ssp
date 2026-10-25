#include "GridDevice.h"

#include <chrono>
#include <string>

#include <ftdi.h>
#include <monome.h>

#include "ssp/Log.h"

namespace ssp::monome {

namespace {
constexpr int FTDI_VID = 0x0403, FTDI_PID = 0x6001;  // the monome's FTDI chip
std::atomic<bool> held{ false };                    // an instance holds a grid

// the serial numbers of the attached FTDI devices of the monome's kind
std::vector<std::string> serials() {
    std::vector<std::string> out;
    ftdi_context* f = ftdi_new();
    if (!f) return out;
    ftdi_device_list* list = nullptr;
    if (ftdi_usb_find_all(f, &list, FTDI_VID, FTDI_PID) > 0)
        for (ftdi_device_list* d = list; d; d = d->next) {
            char serial[64] = {};
            if (ftdi_usb_get_strings(f, d->dev, nullptr, 0, nullptr, 0, serial, sizeof serial) == 0 && serial[0])
                out.push_back(serial);
        }
    ftdi_list_free(&list);
    ftdi_free(f);
    return out;
}
}  // namespace

GridDevice::GridDevice(Handler h) : handler_(std::move(h)) {
    thread_ = std::thread([this] { run(); });
}

GridDevice::~GridDevice() {
    quit_ = true;
    thread_.join();
}

void GridDevice::show(const Frame& f) {
    std::lock_guard<std::mutex> l(frameLock_);
    frame_ = f;
    dirty_ = true;
}

void GridDevice::run() {
    using namespace std::chrono;
    while (!quit_.load(std::memory_order_relaxed)) {
        if (!m_) {
            if (open()) continue;
            for (int ms = 0; ms < FIND_MS && !quit_.load(std::memory_order_relaxed); ms += 50)
                std::this_thread::sleep_for(milliseconds(50));
            continue;
        }
        if (monome_poll_group_wait(group_, WAIT_MS) < 0) {
            ssp::log("monome: grid gone");
            close(false);
            continue;
        }
        flush();
    }
    if (m_) close(true);
}

bool GridDevice::open() {
    if (held.exchange(true)) return false;
    for (const auto& serial : serials()) {
        monome_t* m = monome_open(("ftdi:" + serial).c_str());
        if (!m) continue;
        if (monome_get_cols(m) < COLS || monome_get_rows(m) < ROWS) {  // an arc, or a smaller grid
            monome_close(m);
            continue;
        }
        m_ = m;
        group_ = monome_poll_group_new();
        monome_poll_group_add(group_, m_);
        monome_register_handler(m_, MONOME_BUTTON_DOWN, key, this);
        monome_register_handler(m_, MONOME_BUTTON_UP, key, this);
        surface_.forget();
        {
            std::lock_guard<std::mutex> l(frameLock_);
            dirty_ = true;
        }
        open_ = true;
        ssp::log("monome: grid " + serial);
        return true;
    }
    held = false;
    return false;
}

void GridDevice::close(bool darken) {
    if (darken) monome_led_level_all(m_, 0);
    monome_poll_group_free(group_);
    monome_close(m_);
    m_ = nullptr;
    group_ = nullptr;
    open_ = false;
    held = false;
}

void GridDevice::flush() {
    Frame f;
    {
        std::lock_guard<std::mutex> l(frameLock_);
        if (!dirty_) return;
        f = frame_;
        dirty_ = false;
    }
    quads_.clear();
    surface_.update(f, quads_);
    for (const Quad& q : quads_) monome_led_level_map(m_, unsigned(q.x), unsigned(q.y), q.level);
}

void GridDevice::key(const ::monome_event* e, void* data) {
    auto* self = static_cast<GridDevice*>(data);
    self->handler_({ int(e->grid.x), int(e->grid.y), e->event_type == MONOME_BUTTON_DOWN });
}

}  // namespace ssp::monome
