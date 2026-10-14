#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Firmware.h"
#include "keys/KeyInput.h"

namespace ttyp {

// ttyp's key port, and the variable whose value 0 turns its USB keyboard off (see ssp::KeyInput)
constexpr uint16_t KEY_PORT = 7770;
constexpr const char* KEYBOARD_ENV = "TTYP_KEYBOARD";

// The teletype firmware on its own thread, ticked every 1 ms as the hardware's timer does. A script
// can run for milliseconds (a 16000-step loop took 2.5-5.7 ms on the SSP), so the audio thread never
// calls the firmware: it exchanges inputs and outputs through atomics. No JUCE, so it tests natively.
class Teletype : public ssp::KeyTarget {
public:
    static constexpr unsigned TRIGS = 8;
    static constexpr unsigned OUTS = 4;
    static constexpr unsigned SCREEN_W = 128;
    static constexpr unsigned SCREEN_H = 64;

    Teletype(const void* image, size_t size);
    ~Teletype();

    bool ok() const { return fw_ != nullptr; }
    const std::string& error() const { return error_; }

    // the 1 ms thread; without it, tick() runs the firmware from the caller (tests)
    void start();
    void stop();
    void tick();

    // audio thread, lock-free
    void setTrigger(unsigned i, bool high);  // a rise between ticks still reaches the firmware
    void setIn(float volts) { in_.store(volts, std::memory_order_relaxed); }      // 0-10 V
    void setParam(float unit) { param_.store(unit, std::memory_order_relaxed); }  // 0-1
    float cv(unsigned i) const { return cv_[i].load(std::memory_order_relaxed); }  // 0-10 V
    bool tr(unsigned i) const { return tr_[i].load(std::memory_order_relaxed); }
    // what the inputs last delivered, for display
    float in() const { return in_.load(std::memory_order_relaxed); }  // 0-10 V, before the ADC clips
    bool trigger(unsigned i) const { return i < TRIGS && level_[i].load(std::memory_order_relaxed); }

    // any thread
    void keyDown(uint8_t usage) override { push({ K_DOWN, usage, 0 }); }  // HID usage, 0xE0-0xE7 for modifiers
    void keyUp(uint8_t usage) override { push({ K_UP, usage, 0 }); }
    void releaseAll() override { push({ K_RELEASE_ALL, 0, 0 }); }  // keyboard gone
    void keyTap(uint8_t usage, uint8_t mod) override { push({ K_TAP, usage, mod }); }  // typed text
    void setFront(bool pressed) { front_.store(pressed, std::memory_order_relaxed); }

    // UI thread: copy of the screen, one byte per pixel, 0-15; returns a count that changes with it
    uint32_t screen(uint8_t* out);

    // message thread: the scenes (NVRAM) and the live scene (VRAM)
    std::vector<uint8_t> saveState();
    bool loadState(const uint8_t* data, size_t size);

    // message thread: a scene slot (0-31) in teletype's text format, as its USB stick holds scenes
    std::string exportScene(unsigned slot);
    bool importScene(unsigned slot, const std::string& text);

    // teletype's USB stick, with a folder for the stick: every slot written to dir/ttNNs.txt, then
    // each dir/ttNN.txt that exists read into its slot. The live scene is unchanged until a scene loads.
    struct DiskResult {
        unsigned written = 0, read = 0;
        std::string error;
    };
    DiskResult syncDisk(const std::string& dir);

private:
    enum Kind : uint8_t { K_DOWN, K_UP, K_TAP, K_RELEASE_ALL };
    struct Key {
        Kind kind;
        uint8_t usage;
        uint8_t mod;
    };
    void push(const Key& k);
    void applyKey(const Key& k);
    void sendReport();
    void run();

    std::unique_ptr<Firmware> fw_;
    std::string error_;
    std::mutex fwLock_;  // every firmware call; held for a whole tick

    std::thread thread_;
    std::atomic<bool> running_{ false };

    std::atomic<uint32_t> rises_[TRIGS] = {};
    std::atomic<bool> level_[TRIGS] = {};
    uint32_t seenRises_[TRIGS] = {};
    std::atomic<float> in_{ 0.0f };
    std::atomic<float> param_{ 0.5f };
    std::atomic<bool> front_{ false };
    bool frontSent_ = false;
    std::atomic<float> cv_[OUTS] = {};
    std::atomic<bool> tr_[OUTS] = {};

    std::mutex keyLock_;
    std::vector<Key> keys_, keysDrained_;
    uint8_t mods_ = 0;
    uint8_t pressed_[6] = {};  // boot report key slots, in press order

    uint8_t screenFw_[SCREEN_W * SCREEN_H] = {};  // the firmware draws here, on its thread
    uint8_t screenShared_[SCREEN_W * SCREEN_H] = {};
    std::mutex screenLock_;
    uint32_t screenSeq_ = 0;
    unsigned ticks_ = 0;
};

}  // namespace ttyp
