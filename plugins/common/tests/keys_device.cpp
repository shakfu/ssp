// A second keyboard user for the SSP, standing in for a plugin library beside ttyp: a separate process
// shares KeyInput's claim file, as libraries in Synthor do. "On screen" for SHOWN seconds (setTarget at
// the editor timer's 15 Hz), then off screen for HIDDEN seconds. Prints each key and each change of the
// keyboard's state.
//
// usage: keys_device SHOWN HIDDEN   (TCP port 7779)

#include "keys/KeyInput.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
struct Target : ssp::KeyTarget {
    void keyDown(uint8_t u) override { std::printf("key down 0x%02x\n", u), std::fflush(stdout); }
    void keyUp(uint8_t) override {}
    void keyTap(uint8_t, uint8_t) override {}
    void releaseAll() override { std::printf("release all\n"), std::fflush(stdout); }
} target;
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    using clock = std::chrono::steady_clock;
    const auto shown = std::chrono::duration<double>(std::atof(argv[1]));
    const auto hidden = std::chrono::duration<double>(std::atof(argv[2]));
    ssp::KeyInput::acquire(7779, "KEYS_DEVICE_KEYBOARD");
    bool had = false;
    auto report = [&](const char* phase) {
        bool has = ssp::KeyInput::keyboardConnected();
        if (has != had) std::printf("%s: keyboard %s\n", phase, has ? "held" : "released"), std::fflush(stdout);
        had = has;
    };
    std::printf("on screen for %.0f s\n", shown.count()), std::fflush(stdout);
    for (auto end = clock::now() + shown; clock::now() < end;) {
        ssp::KeyInput::setTarget(&target);
        report("on screen");
        std::this_thread::sleep_for(std::chrono::milliseconds(67));
    }
    std::printf("off screen for %.0f s\n", hidden.count()), std::fflush(stdout);
    for (auto end = clock::now() + hidden; clock::now() < end;) {
        report("off screen");
        std::this_thread::sleep_for(std::chrono::milliseconds(67));
    }
    ssp::KeyInput::clearTarget(&target);
    ssp::KeyInput::release();
    std::printf("done\n");
    return 0;
}
