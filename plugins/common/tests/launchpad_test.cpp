// ssp::launchpad alone: positions, decoding pads and keys, Programmer mode, and the LED SysEx a frame
// change sends, for the Mini MK3 and X; built and run by test_launchpad.py. Bytes from the Programmer's
// Reference Manuals.

#include <cstdio>
#include <vector>

#include "launchpad/Launchpad.h"

using namespace ssp::launchpad;
using Bytes = std::vector<uint8_t>;

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond); \
            failures++;                                           \
        }                                                         \
    } while (0)

static bool decoded(Bytes m, int x, int y, bool down, const Model& model = MINI_MK3, int velocity = 0) {
    Event e;
    return decode(model, m.data(), int(m.size()), e) && e.x == x && e.y == y && e.down == down &&
           e.velocity == velocity;
}

static bool rejected(Bytes m) {
    Event e;
    return !decode(MINI_MK3, m.data(), int(m.size()), e) && !decode(X, m.data(), int(m.size()), e);
}

static void testPositions() {
    CHECK(index(0, 0) == 11 && index(7, 0) == 18 && index(0, 7) == 81 && index(7, 7) == 88);
    CHECK(index(8, 0) == 19 && index(8, 7) == 89);  // side keys
    CHECK(index(0, 8) == 91 && index(7, 8) == 98);  // top row: up .. User
    CHECK(index(8, 8) == 99);                       // logo
    CHECK(isPort("Launchpad Mini MK3 LPMiniMK3 MI", MINI_MK3));  // the SSP's
    CHECK(!isPort("Launchpad Mini MK3 LPMiniMK3 DA", MINI_MK3));
    CHECK(isPort("Launchpad Mini MK3 LPMiniMK3 MIDI", MINI_MK3) && !isPort("Launchpad Mini MK3 LPMiniMK3 MIDI", X));
    CHECK(isPort("Launchpad X LPX MIDI", X) && !isPort("Launchpad X LPX DAW", X));
    CHECK(!isPort("Launchpad X LPX MIDI", MINI_MK3));
}

static void testDecode() {
    CHECK(decoded({ 0x90, 11, 127 }, 0, 0, true));
    CHECK(decoded({ 0x90, 11, 0 }, 0, 0, false));  // the device's release
    CHECK(decoded({ 0x80, 88, 64 }, 7, 7, false));
    CHECK(decoded({ 0x90, 54, 40 }, 3, 4, true));   // any velocity is a press
    CHECK(decoded({ 0xB0, 91, 127 }, 0, 8, true));  // up
    CHECK(decoded({ 0xB0, 98, 0 }, 7, 8, false));   // User
    CHECK(decoded({ 0xB0, 19, 127 }, 8, 0, true));  // the bottom side key
    CHECK(decoded({ 0xB0, 89, 127 }, 8, 7, true));
    CHECK(rejected({ 0x91, 11, 127 }));  // channel 2
    CHECK(rejected({ 0x90, 19, 127 }));  // a key's index as a note
    CHECK(rejected({ 0xB0, 11, 127 }));  // a pad's index as a CC
    CHECK(rejected({ 0x90, 10, 127 }));
    CHECK(rejected({ 0x90, 20, 127 }));
    CHECK(rejected({ 0xB0, 100, 127 }));
    CHECK(rejected({ 0xA0, 11, 127 }));  // aftertouch, polyphonic
    CHECK(rejected({ 0xD0, 64 }));        // and channel
    CHECK(rejected({ 0xF8 }));
    CHECK(rejected({ 0x90, 11 }));
}

// the X's pads send their velocity; its keys, and the Mini's pads, none
static void testVelocity() {
    CHECK(decoded({ 0x90, 11, 37 }, 0, 0, true, X, 37));
    CHECK(decoded({ 0x90, 11, 127 }, 0, 0, true, X, 127));
    CHECK(decoded({ 0x90, 11, 0 }, 0, 0, false, X, 0));
    CHECK(decoded({ 0x80, 11, 64 }, 0, 0, false, X, 0));  // a release's velocity is not a press's
    CHECK(decoded({ 0xB0, 91, 127 }, 0, 8, true, X, 0));
    CHECK(decoded({ 0x90, 11, 37 }, 0, 0, true, MINI_MK3, 0));
}

static void testProgrammerMode() {
    CHECK(programmerMode(MINI_MK3, true) == Bytes({ 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, 0x01, 0xF7 }));
    CHECK(programmerMode(MINI_MK3, false) == Bytes({ 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, 0x00, 0xF7 }));
    CHECK(programmerMode(X, true) == Bytes({ 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7 }));
    CHECK(programmerMode(X, false) == Bytes({ 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x00, 0xF7 }));
}

static void testSurface() {
    Surface s(MINI_MK3);
    Frame f;
    Bytes out;
    // first update: every LED, as the device's state is unknown
    CHECK(s.update(f, out) == LEDS);
    CHECK(out.size() == 6 + 1 + LEDS * 3 + 1 && out[6] == 0x03 && out.back() == 0xF7);
    out.clear();
    CHECK(s.update(f, out) == 0 && out.empty());
    // the manual's example: static yellow, flashing green (dim to bright), pulsing turquoise
    f.at(0, 0) = Led::palette(13);
    f.at(1, 0) = Led::flash(21, 23);
    f.at(2, 0) = Led::pulse(37);
    CHECK(s.update(f, out) == 3);
    CHECK(out == Bytes({ 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x03, 0x00, 0x0B, 0x0D, 0x01, 0x0C, 0x15, 0x17, 0x02, 0x0D,
                         0x25, 0xF7 }));
    out.clear();
    f.at(8, 7) = Led::rgb(127, 64, 200);  // 200 masked to 7 bits
    CHECK(s.update(f, out) == 1);
    CHECK(out == Bytes({ 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x03, 0x03, 89, 127, 64, 72, 0xF7 }));
    out.clear();
    s = Surface(MINI_MK3);  // a connect: every LED again
    CHECK(s.update(f, out) == LEDS);
    // every LED in RGB: one message, within the manual's 81 specs
    Frame all;
    for (auto& l : all.leds) l = Led::rgb(1, 2, 3);
    out.clear();
    CHECK(s.update(all, out) == LEDS && out.size() == 6 + 1 + LEDS * 5 + 1);
    // the X's manual has the same example, under its own header
    Surface x(X);
    Frame g;
    x.update(g, out);
    out.clear();
    g.at(0, 0) = Led::palette(13);
    g.at(1, 0) = Led::flash(21, 23);
    g.at(2, 0) = Led::pulse(37);
    CHECK(x.update(g, out) == 3);
    CHECK(out == Bytes({ 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03, 0x00, 0x0B, 0x0D, 0x01, 0x0C, 0x15, 0x17, 0x02, 0x0D,
                         0x25, 0xF7 }));
}

int main() {
    testPositions();
    testDecode();
    testVelocity();
    testProgrammerMode();
    testSurface();
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
