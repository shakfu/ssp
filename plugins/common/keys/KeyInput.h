#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace ssp {

// What KeyInput delivers keys to: HID usages (page 0x07), 0xE0-0xE7 for modifiers.
class KeyTarget {
public:
    virtual void keyDown(uint8_t usage) = 0;
    virtual void keyUp(uint8_t usage) = 0;
    virtual void keyTap(uint8_t usage, uint8_t mod) = 0;  // typed text, from TCP
    virtual void releaseAll() = 0;                        // keyboard gone, or keys held while switching

protected:
    ~KeyTarget() = default;
};

// Keys for the active instance of one plugin library, from a USB keyboard and from a TCP port on
// 127.0.0.1. One per library, since each plugin .so has its own copy: instances hold a reference while
// they exist, and the thread stops with the last, before Synthor can unload the library.
//
// The keyboard is grabbed (EVIOCGRAB), so the console and SysRq do not see it. A library wants the grab
// unless another library's editor is on screen and its own is not (setTarget within HOLD). Libraries
// share "the last one shown" through CLAIM_FILE, a word every library in the process maps. The kernel
// gives the grab to one library at a time, so with no editor on screen the holder keeps it, and when
// the holder goes, another library picks it up.
//
// TCP records are 4 bytes: kind (0 down, 1 up, 2 tap, 3 release all), HID usage, modifier byte
// (taps only), 0. Reach the port through ssh: `ssh -L PORT:127.0.0.1:PORT root@ssp`.
class KeyInput {
public:
    static constexpr std::chrono::milliseconds HOLD{ 500 };  // about 7 ticks of the 15 Hz editor timer
    static constexpr const char* CLAIM_FILE = "/tmp/ssp-keys";

    // The first acquire sets the TCP port and the environment variable whose value 0 turns the USB
    // keyboard off, for hosts whose keyboard the plugin must not grab.
    static void acquire(uint16_t port, const char* keyboardEnv);
    static void release();
    // the instance that receives keys; the last one shown. Call it from the editor's timer: it also
    // marks the editor as on screen.
    static void setTarget(KeyTarget* t);
    static void clearTarget(KeyTarget* t);
    static bool keyboardConnected();
    // off: TCP only; tests set it, so they never grab the desktop's keyboard
    static void setKeyboardScan(bool on);
    // Whether this library should hold the keyboard; the input thread grabs or releases by it.
    static bool wantsGrab();
    static bool onScreen();  // setTarget within HOLD

private:
    static KeyInput& get();
    void start();
    void stop();
    void run();
    void deliver(uint8_t kind, uint8_t usage, uint8_t mod);
    bool shown() const;
    bool otherShown() const;  // another library's editor, by the claim
    void claim();

    std::mutex lock_;     // target_; deliver() takes it on the thread
    std::mutex refLock_;  // refs_ and the thread's start and stop; the thread never takes it
    KeyTarget* target_ = nullptr;
    int refs_ = 0;
    uint16_t port_ = 0;
    std::string keyboardEnv_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> keyboard_{ false };
    std::atomic<bool> scan_{ true };
    std::atomic<int64_t> shownAt_{ 0 };  // steady_clock ticks of the last setTarget
    std::atomic<uint64_t>* claim_ = nullptr;  // CLAIM_FILE: port << 48 | steady_clock milliseconds
};

}  // namespace ssp
