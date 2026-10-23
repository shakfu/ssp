#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Launchpad Mini MK3 in Programmer mode: the messages to and from its MIDI interface, without JUCE or a
// device. docs/dev/gseq-launchpad.md has the protocol's sources.
//
// A position is x 0-8 left to right, y 0-8 bottom to top: the pads are x, y < 8, the side keys x 8, the top
// row y 8, and the logo (8, 8). Its Programmer-mode index is 10 * (y + 1) + x + 1: 11 bottom left, 99 the logo.
namespace ssp::launchpad {

constexpr int SIZE = 9, LEDS = SIZE * SIZE;
// the MIDI interface's ports, by name: ALSA cuts names to 31 characters, so on the SSP the port is
// "Launchpad Mini MK3 LPMiniMK3 MI" (and the DAW interface's "... LPMiniMK3 DA")
constexpr const char* PORT = "LPMiniMK3 MI";

inline bool isPort(const std::string& name) {
    return name.find(PORT) != std::string::npos;
}
inline int index(int x, int y) {
    return 10 * (y + 1) + x + 1;
}

struct Event {
    int x = 0, y = 0;
    bool down = false;
};
// A pad's note or a key's CC, channel 1; false for anything else (SysEx replies, clock, other channels).
bool decode(const uint8_t* data, int size, Event& e);

struct Led {
    // the LED SysEx's lighting types
    enum Type : uint8_t {
        STATIC,
        FLASH,
        PULSE,
        RGB
    };
    Type type = STATIC;
    uint8_t a = 0, b = 0, c = 0;  // palette entry; FLASH: B, A; RGB: red, green, blue, each 0-127

    static Led off() { return {}; }
    static Led palette(uint8_t i) { return { STATIC, uint8_t(i & 127) }; }
    // alternates with `from`, a beat each
    static Led flash(uint8_t i, uint8_t from = 0) { return { FLASH, uint8_t(i & 127), uint8_t(from & 127) }; }
    static Led pulse(uint8_t i) { return { PULSE, uint8_t(i & 127) }; }
    static Led rgb(uint8_t r, uint8_t g, uint8_t b) {
        return { RGB, uint8_t(r & 127), uint8_t(g & 127), uint8_t(b & 127) };
    }
    bool operator==(const Led& o) const { return type == o.type && a == o.a && b == o.b && c == o.c; }
    bool operator!=(const Led& o) const { return !(*this == o); }
};

struct Frame {
    std::array<Led, LEDS> leds{};
    Led& at(int x, int y) { return leds[size_t(y * SIZE + x)]; }
    const Led& at(int x, int y) const { return leds[size_t(y * SIZE + x)]; }
};

// SysEx to enter Programmer mode, or to leave it for Live mode
std::vector<uint8_t> programmerMode(bool on);

// What the device shows, and the SysEx that brings it to a new frame.
class Surface {
public:
    // Appends one LED SysEx with the LEDs that differ from what the device shows; nothing if none do.
    // Returns the LEDs sent.
    int update(const Frame& next, std::vector<uint8_t>& out);
    // The device's LEDs are unknown, after a connect: the next update sends every LED.
    void invalidate() { known_ = false; }

private:
    Frame shown_;
    bool known_ = false;
};

}  // namespace ssp::launchpad
