// ssp::engine::Clock alone: free tempo, swing, the clock and reset inputs, MIDI clock and transport, and
// a position that never wraps; built and run by test_clock.py. alda_test.cpp covers it inside a score.

#include <cmath>
#include <cstdio>

#include "engine/Clock.h"

using ssp::engine::Clock;

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond); \
            failures++;                                           \
        }                                                         \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

static constexpr float RATE = 48000.0f, HIGH = 1.0f;  // 5 V

// one block of n samples with constant inputs, as an owner without a loop; the pulses each block took
static int run(Clock& c, Clock::Source s, int n, float reset = 0.0f, float clock = 0.0f) {
    int pulses = 0;
    c.follow(s, RATE);
    c.transport();
    while (c.midiPulse()) pulses++;
    for (int i = 0; i < n; i++) {
        c.resetInput(reset);
        pulses += c.clockInput(clock);
        c.advance();
    }
    return pulses;
}

// a block of one sample whose clock input rises, without the advance: the position the pulse moved to
static bool edge(Clock& c, Clock::Source s) {
    c.follow(s, RATE);
    c.transport();
    return c.clockInput(HIGH);
}

static void testFree() {
    Clock c;
    run(c, Clock::FREE, 48000);
    NEAR(c.position(), 960.0, 1e-6);  // 120 bpm: 2 quarters a second
    c.setTempo(2.0);
    run(c, Clock::FREE, 48000);
    NEAR(c.position(), 2880.0, 1e-6);
    NEAR(c.bpm(), 240.0, 1e-9);
}

static void testSwing() {
    Clock c;
    c.setSwing(0.5);  // 75%: the first 16th of a pair lasts 1.5 16ths
    run(c, Clock::FREE, 3000);  // 60 ticks, half a 16th
    NEAR(c.swung(), 40.0, 1e-6);
    run(c, Clock::FREE, 6000);  // 180 ticks: the second 16th starts
    NEAR(c.swung(), 120.0, 1e-6);
}

static void testClockInput() {
    Clock c;
    c.setPpqn(4);  // a pulse is a 16th, 120 ticks
    CHECK(edge(c, Clock::INPUT));
    NEAR(c.position(), 120.0, 1e-9);
    run(c, Clock::INPUT, 2999);
    CHECK(edge(c, Clock::INPUT));  // 3000 samples later: 240 bpm from here
    NEAR(c.position(), 240.0, 1e-9);
    NEAR(c.bpm(), 240.0, 1e-6);
    run(c, Clock::INPUT, 1500);
    NEAR(c.position(), 300.0, 1e-6);
    run(c, Clock::INPUT, 48000);  // no pulse: halts short of the next
    CHECK(c.position() < 360.0 && c.position() > 359.99);
    CHECK(run(c, Clock::INPUT, 10, 0.0f, HIGH) == 1);  // a held input is one pulse
    run(c, Clock::INPUT, 10);
    CHECK(run(c, Clock::FREE, 10, 0.0f, HIGH) == 0);  // the free source ignores the input
}

static void testSourceChange() {
    Clock c;
    c.setPpqn(4);
    run(c, Clock::FREE, 6500);  // 130 ticks
    edge(c, Clock::INPUT);
    NEAR(c.position(), 240.0, 1e-9);  // from the pulse at or before 130, not from 130
}

static void testReset() {
    Clock c;
    run(c, Clock::FREE, 4800);
    c.follow(Clock::FREE, RATE);
    CHECK(c.resetInput(HIGH));
    NEAR(c.position(), 0.0, 0.0);
    CHECK(!c.resetInput(HIGH));  // held: one edge
    CHECK(!c.resetInput(0.0f));
    CHECK(c.resetInput(HIGH));
}

static void testMidi() {
    Clock c;
    run(c, Clock::FREE, 4800);
    c.midiStart();
    for (int i = 0; i < 4; i++) c.midiClock(i * 0.02);  // 24 a quarter, 0.02 s apart: 125 bpm
    c.follow(Clock::MIDI, RATE);
    CHECK(c.transport() == Clock::START);
    NEAR(c.position(), 0.0, 0.0);
    int moved = 0;
    while (c.midiPulse()) moved++;
    CHECK(moved == 4);
    NEAR(c.position(), 80.0, 1e-9);  // 20 ticks a tick
    NEAR(c.bpm(), 125.0, 1e-6);
    c.midiStop();
    c.midiClock(0.08);
    c.follow(Clock::MIDI, RATE);
    CHECK(c.transport() == Clock::STOP);
    CHECK(!c.midiPulse() && !c.advance());  // halted: ticks and time do not move it
    NEAR(c.position(), 80.0, 1e-9);
    c.midiContinue();
    c.midiClock(0.10);
    c.follow(Clock::MIDI, RATE);
    CHECK(c.transport() == Clock::CONTINUE);
    CHECK(c.midiPulse() && !c.midiPulse());
    NEAR(c.position(), 100.0, 1e-9);
    // ticks after transport() wait for the next block
    c.midiClock(0.12);
    c.follow(Clock::MIDI, RATE);
    c.transport();
    c.midiClock(0.14);
    CHECK(c.midiPulse() && !c.midiPulse());
    c.transport();
    CHECK(c.midiPulse());
}

static void testWrap() {
    Clock c;
    c.setPpqn(4);
    edge(c, Clock::INPUT);
    run(c, Clock::INPUT, 100);
    c.wrap(100.0);  // the owner's loop: position and last pulse move together
    edge(c, Clock::INPUT);
    NEAR(c.position(), 140.0, 1e-9);  // 120 - 100 + 120
}

static void testNoWrap() {
    Clock c;
    for (int s = 0; s < 600; s++) run(c, Clock::FREE, 48000);  // ten minutes
    NEAR(c.position(), 576000.0, 1e-3);
}

int main() {
    testFree();
    testSwing();
    testClockInput();
    testSourceChange();
    testReset();
    testMidi();
    testWrap();
    testNoWrap();
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
