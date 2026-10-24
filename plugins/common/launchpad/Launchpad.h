#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Launchpad Mini MK3 and Launchpad X in Programmer mode: the messages to and from their MIDI interfaces, without
// JUCE or a device. Both share the layout and messages; the SysEx header's device byte differs. docs/dev/gseq-launchpad.md
// has the protocol's sources.
//
// A position is x 0-8 left to right, y 0-8 bottom to top: the pads are x, y < 8, the side keys x 8, the top
// row y 8, and the logo (8, 8). Its Programmer-mode index is 10 * (y + 1) + x + 1: 11 bottom left, 99 the logo.
namespace ssp::launchpad {

constexpr int SIZE = 9, LEDS = SIZE * SIZE;

struct Model {
    // the MIDI interface's ports contain this name: ALSA cuts names to 31 characters, so on the SSP the Mini's
    // port is "Launchpad Mini MK3 LPMiniMK3 MI" (and the DAW interface's "... LPMiniMK3 DA")
    const char* port;
    uint8_t id;     // the SysEx header's device byte
    bool velocity;  // the pads send their press's velocity; the Mini's send 127 whatever the force
};
constexpr Model MINI_MK3{ "LPMiniMK3 MI", 0x0D, false }, X{ "LPX MIDI", 0x0C, true };
constexpr Model MODELS[] = { X, MINI_MK3 };  // Device opens the first one listed: the X, with velocity

inline bool isPort(const std::string& name, const Model& m) {
    return name.find(m.port) != std::string::npos;
}
inline int index(int x, int y) {
    return 10 * (y + 1) + x + 1;
}

struct Event {
    int x = 0, y = 0;
    bool down = false;
    int velocity = 0;  // a pad's press, 1-127, on a model with velocity; 0 otherwise
};
// A pad's note or a key's CC, channel 1; false for anything else (SysEx replies, clock, aftertouch, other channels).
bool decode(const Model& m, const uint8_t* data, int size, Event& e);

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
std::vector<uint8_t> programmerMode(const Model& m, bool on);

// What the device shows, and the SysEx that brings it to a new frame. A new Surface sends every LED first, as
// after a connect the device's LEDs are unknown.
class Surface {
public:
    explicit Surface(const Model& m) : id_(m.id) {}

    // Appends one LED SysEx with the LEDs that differ from what the device shows; nothing if none do.
    // Returns the LEDs sent.
    int update(const Frame& next, std::vector<uint8_t>& out);

private:
    uint8_t id_;
    Frame shown_;
    bool known_ = false;
};

}  // namespace ssp::launchpad
