#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace ttyp {

class Teletype;

// Keys for the active teletype instance, from a USB keyboard and from a TCP port on 127.0.0.1.
// One per process: every instance would otherwise get every key. Instances hold a reference while
// they exist, and the thread stops with the last, before Synthor can unload the plugin.
//
// TTYP_KEYBOARD=0 in the environment turns the USB keyboard off.
//
// TCP records are 4 bytes: kind (0 down, 1 up, 2 tap, 3 release all), HID usage, modifier byte
// (taps only), 0. Reach the port through ssh: `ssh -L PORT:127.0.0.1:PORT root@ssp`.
class KeyInput {
public:
    static constexpr uint16_t PORT = 7770;

    static void acquire();
    static void release();
    // the instance that receives keys; the last one shown
    static void setTarget(Teletype* t);
    static void clearTarget(Teletype* t);
    static bool keyboardConnected();
    // off: TCP only; tests set it, so they never grab the desktop's keyboard
    static void setKeyboardScan(bool on);

private:
    static KeyInput& get();
    void start();
    void stop();
    void run();
    void deliver(uint8_t kind, uint8_t usage, uint8_t mod);

    std::mutex lock_;     // target_; deliver() takes it on the thread
    std::mutex refLock_;  // refs_ and the thread's start and stop; the thread never takes it
    Teletype* target_ = nullptr;
    int refs_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> keyboard_{ false };
    std::atomic<bool> scan_{ true };
};

}  // namespace ttyp
