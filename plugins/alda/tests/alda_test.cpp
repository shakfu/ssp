// Native test of alda::AldaEngine against psnd's Alda core; built and run by test_alda.py.
// argv[1] == "threads" runs the threaded test only; "check" builds each score file after it; "time" also
// prints each one's build time, the median of 5 runs, in ms.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "AldaEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

using alda::AldaEngine;
static constexpr int CH = AldaEngine::CHANNELS;
static constexpr int BLOCK = 128;
static constexpr float SR = 48000.0f;
static constexpr int QUARTER = 24000;  // frames at 120 bpm
static constexpr float VOLT = 0.2f;

struct Rig {
    AldaEngine e;
    std::vector<float> in[CH], out[CH];
    const float* ip[CH];
    float* op[CH];
    long frame = 0;  // whole blocks run, so a run may pass its target by up to BLOCK - 1
    float last[CH] = {};
    std::vector<long> rises[CH];          // frames where an output went from 0 to above 0
    std::vector<float> cvAtRise[CH];      // output CH/2 + k at each rise of output k

    explicit Rig(const std::string& code) {
        for (int c = 0; c < CH; c++) {
            in[c].assign(BLOCK, 0.0f);
            out[c].assign(BLOCK, 0.0f);
            ip[c] = in[c].data();
            op[c] = out[c].data();
        }
        for (int i = 0; i < AldaEngine::PARAMS; i++) e.setParam(i, std::max(e.spec(i).def, 0.0f));
        e.prepare(SR, BLOCK);
        if (!code.empty()) load(code);
    }
    // every MIDI message queued so far, as "90 3c 45" strings
    std::vector<std::string> midi() {
        std::vector<std::string> v;
        AldaEngine::Midi m;
        while (e.midiOut(m)) {
            char s[12];
            if (m.size == 2) std::snprintf(s, sizeof s, "%02x %02x", m.bytes[0], m.bytes[1]);
            else std::snprintf(s, sizeof s, "%02x %02x %02x", m.bytes[0], m.bytes[1], m.bytes[2]);
            v.push_back(s);
        }
        return v;
    }
    void load(const std::string& code) {
        e.loadCode(code, "");
        e.idle();
    }
    // runs until `frames` have passed in all, with idle() between blocks as the worker would
    void runTo(long frames) {
        while (frame < frames) {
            e.idle();
            e.process(ip, op, BLOCK);
            for (int i = 0; i < BLOCK; i++) {
                for (int c = 0; c < CH; c++) {
                    float v = out[c][i];
                    if (c < AldaEngine::VOICES && last[c] <= 0.0f && v > 0.0f) {
                        rises[c].push_back(frame + i);
                        cvAtRise[c].push_back(out[AldaEngine::VOICES + c][i]);
                    }
                    last[c] = v;
                }
            }
            frame += BLOCK;
        }
    }
};

static bool nearFrame(long got, long want) {
    return std::labs(got - want) <= 1;
}

static void testNotes() {
    Rig r("piano: c4 d e f");
    CHECK(r.e.error().empty());
    r.runTo(4 * QUARTER + QUARTER / 2);
    // four quarters, then the loop's second pass
    CHECK(r.rises[0].size() == 5);
    for (size_t i = 0; i < r.rises[0].size() && i < 5; i++) CHECK(nearFrame(r.rises[0][i], long(i) * QUARTER));
    const float want[] = { 0.0f, 2.0f / 12, 4.0f / 12, 5.0f / 12, 0.0f };
    for (size_t i = 0; i < r.cvAtRise[0].size() && i < 5; i++) NEAR(r.cvAtRise[0][i], want[i] * VOLT, 1e-6);
    CHECK(r.rises[1].empty());
}

static void testGateLength() {
    // Alda's default quantization holds a note for 90% of its length
    Rig r("piano: c4 d e f");
    long high = 0;
    while (r.frame < QUARTER) {
        r.runTo(r.frame + BLOCK);
        for (int i = 0; i < BLOCK; i++) high += r.out[0][i] > 0.0f;
    }
    NEAR(high, 0.9 * QUARTER, 0.01 * QUARTER);
}

static void testParts() {
    Rig r("piano: o4 c2 e\nbass: o2 g1");
    r.runTo(2 * QUARTER + QUARTER / 2);
    CHECK(r.rises[0].size() == 2);
    CHECK(r.rises[1].size() == 1);
    if (!r.cvAtRise[1].empty()) NEAR(r.cvAtRise[1][0], (43 - 60) / 12.0f * VOLT, 1e-6);
    CHECK(r.e.status().find("piano bass") != std::string::npos);
}

static void testChordLastWins() {
    Rig r("piano: c/e/g1");
    r.runTo(BLOCK);
    CHECK(r.rises[0].size() == 1);
    NEAR(r.out[AldaEngine::VOICES][BLOCK - 1], (67 - 60) / 12.0f * VOLT, 1e-6);
}

static void testLoopDirective() {
    Rig r("# @loop 2\npiano: c4 d e f");
    r.runTo(2 * QUARTER + QUARTER / 2);
    CHECK(r.rises[0].size() == 3);
    if (r.rises[0].size() == 3) CHECK(nearFrame(r.rises[0][2], 2 * QUARTER));
    if (r.cvAtRise[0].size() == 3) NEAR(r.cvAtRise[0][2], 0.0f, 1e-6);  // c again, not e

    AldaEngine::Score s;
    std::string err;
    CHECK(!AldaEngine::build("# @loop zero\npiano: c", "", s, err));
    CHECK(err.find("@loop") != std::string::npos);
}

static void testSwapAtLoopEnd() {
    Rig r("piano: c4 d e f");
    r.runTo(QUARTER + QUARTER / 2);
    r.load("piano: g4 g g g");
    r.runTo(4 * QUARTER + QUARTER / 2);
    // the first loop plays out; the new score starts at its end
    CHECK(r.rises[0].size() == 5);
    if (r.cvAtRise[0].size() == 5) {
        NEAR(r.cvAtRise[0][3], 5.0f / 12 * VOLT, 1e-6);
        NEAR(r.cvAtRise[0][4], 7.0f / 12 * VOLT, 1e-6);
        CHECK(nearFrame(r.rises[0][4], 4 * QUARTER));
    }
}

static void testErrorKeepsScore() {
    Rig r("piano: c4 d e f");
    r.runTo(QUARTER / 2);
    r.load("piano: c4 (((");
    CHECK(!r.e.error().empty());
    r.runTo(4 * QUARTER + QUARTER / 2);
    CHECK(r.rises[0].size() == 5);
    if (r.cvAtRise[0].size() == 5) NEAR(r.cvAtRise[0][4], 0.0f, 1e-6);
}

static void testReset() {
    Rig r("piano: c4 d e f");
    r.runTo(2 * QUARTER + QUARTER / 2);
    // a 5 V edge at the start of the next block restarts the loop
    for (int i = 0; i < BLOCK; i++) r.in[0][i] = 1.0f;
    long at = r.frame;
    r.runTo(at + BLOCK);
    for (int i = 0; i < BLOCK; i++) r.in[0][i] = 0.0f;
    r.runTo(at + QUARTER + QUARTER / 2);
    // c restarts at the edge, its gate after the retrigger gap, since e was sounding; d a quarter later
    CHECK(r.rises[0].size() == 5);
    if (r.rises[0].size() == 5) {
        CHECK(nearFrame(r.rises[0][3], at + long(AldaEngine::GAP_MS * SR / 1000)));
        NEAR(r.cvAtRise[0][3], 0.0f, 1e-6);
        CHECK(nearFrame(r.rises[0][4], at + QUARTER));
    }
}

static void testTempo() {
    Rig r("(tempo! 60)\npiano: c4 d");
    r.runTo(2 * QUARTER + QUARTER / 2);
    CHECK(r.rises[0].size() == 2);
    if (r.rises[0].size() == 2) CHECK(nearFrame(r.rises[0][1], 2 * QUARTER));
}

static void testBuiltin() {
    Rig r("");
    CHECK(r.e.error().empty());
    r.runTo(4 * QUARTER);
    CHECK(!r.rises[0].empty());
    CHECK(!r.rises[1].empty());
}

static bool same(const std::vector<std::string>& got, const std::vector<std::string>& want) {
    if (got == want) return true;
    for (const auto& s : got) std::fprintf(stderr, "  got %s\n", s.c_str());
    return false;
}

// Alda's channel settings precede the first note: program, track volume (CC 11), pan (CC 10)
static void testMidi() {
    Rig r("piano: c4 d");
    r.runTo(QUARTER + QUARTER / 2);
    CHECK(same(r.midi(), { "c0 00", "b0 0b 64", "b0 0a 40", "90 3c 45", "80 3c 00", "90 3e 45" }));
    CHECK(r.e.midiDropped() == 0);
}

// a reset releases the sounding note, and the loop's settings and first note follow
static void testMidiReset() {
    Rig r("piano: c2");
    r.runTo(QUARTER / 2);
    r.midi();
    for (int i = 0; i < BLOCK; i++) r.in[0][i] = 1.0f;
    r.runTo(r.frame + BLOCK);
    CHECK(same(r.midi(), { "80 3c 00", "c0 00", "b0 0b 64", "b0 0a 40", "90 3c 45" }));
}

// a fifth part plays as MIDI only; percussion is on channel 10
static void testMidiParts() {
    Rig r("piano: c1\nviolin: c1\ncello: c1\nflute: c1\noboe: o5 c1\nmidi-percussion: o2 c1");
    r.runTo(BLOCK);
    auto m = r.midi();
    int ons = 0;
    bool fifth = false, drum = false;
    for (const auto& s : m) {
        if (s.compare(0, 1, "9") != 0) continue;
        ons++;
        fifth = fifth || s.compare(3, 2, "48") == 0;  // C5 is MIDI 72
        drum = drum || s.compare(0, 2, "99") == 0;
    }
    CHECK(ons == 6 && fifth && drum);
    for (int k = 0; k < AldaEngine::VOICES; k++) CHECK(r.rises[k].size() == 1);
}

// a note cut by the loop's end is released at the wrap, once
static void testMidiLoopCut() {
    Rig r("# @loop 1\npiano: c2");
    r.midi();
    r.runTo(QUARTER + QUARTER / 2);
    auto m = r.midi();
    int offs = 0, ons = 0;
    for (const auto& s : m) {
        offs += s.compare(0, 2, "80") == 0;
        ons += s.compare(0, 2, "90") == 0;
    }
    CHECK(ons == 2 && offs == 1);
}

// Ctrl-.: gates close and notes release at the next block; a score run then starts at once
static void testStop() {
    Rig r("piano: c1 d1");
    r.runTo(QUARTER);
    r.midi();
    r.e.clearParts();
    r.runTo(r.frame + BLOCK);  // idle() runs the stop, the next block applies it
    CHECK(r.out[0][BLOCK - 1] == 0.0f);
    CHECK(same(r.midi(), { "80 3c 00" }));
    CHECK(r.e.status() == "stopped");
    r.runTo(r.frame + 4 * QUARTER);
    CHECK(r.rises[0].size() == 1 && r.midi().empty());  // stays silent
    long at = r.frame;
    r.load("piano: e4");
    r.runTo(at + BLOCK);
    CHECK(r.rises[0].size() == 2);
    if (r.rises[0].size() == 2) CHECK(r.rises[0][1] >= at && r.rises[0][1] < at + BLOCK);
}

// a score with no notes, as the editor's reset runs, stops
static void testEmptyStops() {
    Rig r("piano: c1");
    r.runTo(QUARTER);
    r.load("\n");
    CHECK(r.e.error().empty());
    r.runTo(r.frame + 2 * BLOCK);
    CHECK(r.out[0][BLOCK - 1] == 0.0f && r.e.status() == "stopped");
    CHECK(r.e.canReset());
}

// `# @clock`: rising edges on input 2, 4 a quarter; the score follows their rate and halts without them
static void testClockInput() {
    Rig r("# @clock\npiano: c4 d e f");
    auto pulses = [&](int count, int every) {
        for (int p = 0; p < count; p++) {
            long at = r.frame;
            for (int i = 0; i < BLOCK; i++) r.in[1][i] = 1.0f;
            r.runTo(at + BLOCK);
            for (int i = 0; i < BLOCK; i++) r.in[1][i] = 0.0f;
            r.runTo(at + every);
        }
    };
    pulses(7, 3072);  // 16ths 3072 frames apart, from frame 0: 234.375 bpm
    // c at the start; each pulse moves a 16th, so d at the fourth pulse and e at the eighth
    CHECK(r.rises[0].size() == 2);
    if (r.rises[0].size() == 2) CHECK(r.rises[0][1] >= 3 * 3072 && r.rises[0][1] < 3 * 3072 + 2 * BLOCK);
    NEAR(std::atof(r.e.status().c_str() + r.e.status().find("  ") + 2), 234.375, 0.5);
    r.runTo(r.frame + 4 * QUARTER);  // no clock: halted before e
    CHECK(r.rises[0].size() == 2);
    pulses(1, 3072);
    CHECK(r.rises[0].size() == 3);
}

// `# @clock midi`: 24 ticks a quarter, Start, Stop and Continue
static void testClockMidi() {
    Rig r("# @clock midi\npiano: c4 d e f");
    double t = 0.0;
    auto ticks = [&](int count) {
        for (int p = 0; p < count; p++) {
            r.e.midiClock(t);
            t += 0.5 / 24;  // 120 bpm
            r.runTo(r.frame + 1000);
        }
    };
    r.e.midiStart();
    ticks(30);
    CHECK(r.rises[0].size() == 2);  // c, then d at tick 24
    r.midi();
    r.e.midiStop();
    ticks(48);  // ticks while stopped move nothing
    CHECK(r.rises[0].size() == 2);
    CHECK(same(r.midi(), { "80 3e 00" }));  // Stop releases d
    r.e.midiContinue();
    ticks(20);  // e at tick 48: 18 more after the 30 before the stop
    CHECK(r.rises[0].size() == 3);
    r.e.midiStart();  // back to c
    ticks(1);
    CHECK(r.rises[0].size() == 4);
    if (r.cvAtRise[0].size() == 4) NEAR(r.cvAtRise[0][3], 0.0f, 1e-6);
}

static void testClockLine() {
    AldaEngine::Score s;
    std::string err;
    CHECK(AldaEngine::build("# @clock 24\npiano: c", "", s, err) && s.clock == AldaEngine::INPUT && s.ppqn == 24);
    CHECK(AldaEngine::build("# @clock midi\npiano: c", "", s, err) && s.clock == AldaEngine::MIDI);
    CHECK(AldaEngine::build("piano: c", "", s, err) && s.clock == AldaEngine::FREE);
    CHECK(!AldaEngine::build("# @clock fast\npiano: c", "", s, err) && err.find("@clock") != std::string::npos);
}

static void setControl(Rig& r, int i, float v) {
    r.e.setParam(i, r.e.spec(i).unmap(v));
}

// p1 Transpose: MIDI notes and pitch CV; a note releases as sent, though the control moved meanwhile
static void testTranspose() {
    Rig r("piano: c2 d");
    setControl(r, AldaEngine::TRANSPOSE, 12.0f);
    r.runTo(QUARTER);
    CHECK(same(r.midi(), { "c0 00", "b0 0b 64", "b0 0a 40", "90 48 45" }));
    NEAR(r.out[AldaEngine::VOICES][BLOCK - 1], 1.0f * VOLT, 1e-6);
    setControl(r, AldaEngine::TRANSPOSE, 0.0f);
    r.runTo(2 * QUARTER + QUARTER / 2);
    CHECK(same(r.midi(), { "80 48 00", "90 3e 45" }));
}

// p2 Tempo doubles the speed; p3 Velocity scales MIDI velocities, and 0 sends no note
static void testTempoVelocity() {
    Rig r("piano: c4 d");
    setControl(r, AldaEngine::TEMPO, 2.0f);
    setControl(r, AldaEngine::VELOCITY, 0.5f);
    r.runTo(QUARTER);
    CHECK(r.rises[0].size() == 2);
    if (r.rises[0].size() == 2) CHECK(nearFrame(r.rises[0][1], QUARTER / 2));
    auto m = r.midi();
    CHECK(std::count(m.begin(), m.end(), "90 3c 23") == 1);  // 69 x 0.5, rounded
    setControl(r, AldaEngine::VELOCITY, 0.0f);
    r.runTo(r.frame + 2 * QUARTER);
    m = r.midi();
    CHECK(std::none_of(m.begin(), m.end(), [](const std::string& s) { return s.compare(0, 2, "90") == 0; }));
    CHECK(r.rises[0].size() > 2);  // gates still play
}

// p4 Swing 75%: the second 16th of each pair starts 3/4 of the way through it, not halfway
static void testSwing() {
    Rig r("piano: c16 d");
    setControl(r, AldaEngine::SWING, 75.0f);
    r.runTo(QUARTER);
    CHECK(r.rises[0].size() == 2);
    if (r.rises[0].size() == 2) CHECK(nearFrame(r.rises[0][1], QUARTER * 3 / 8));
}

// p5 Mute 1: voice 1's gate and its part's MIDI notes; voice 2 plays on
static void testMute() {
    Rig r("piano: c4 d\nviolin: e4 f");
    setControl(r, AldaEngine::MUTE, 1.0f);
    r.runTo(QUARTER + QUARTER / 2);
    CHECK(r.rises[0].empty() && r.rises[1].size() == 2);
    auto m = r.midi();
    CHECK(std::none_of(m.begin(), m.end(), [](const std::string& s) { return s.compare(0, 5, "90 3c") == 0; }));
    CHECK(std::count_if(m.begin(), m.end(), [](const std::string& s) { return s.compare(0, 4, "91 4") == 0; }) == 2);  // violin, channel 2
}

static void testControlsDeclared() {
    AldaEngine e;
    CHECK(e.spec(AldaEngine::TRANSPOSE).label == "Transpose" && e.spec(AldaEngine::MUTE + 3).label == "Mute 4");
    NEAR(e.spec(AldaEngine::TEMPO).map(e.spec(AldaEngine::TEMPO).def), 1.0, 1e-5);
    NEAR(e.spec(AldaEngine::TRANSPOSE).map(e.spec(AldaEngine::TRANSPOSE).def), 0.0, 1e-5);
}

// The worker compiles while the audio thread plays: run under ThreadSanitizer.
static void testThreads() {
    Rig r("piano: c8 d e f");
    std::atomic<bool> stop{ false };
    std::thread worker([&] {
        for (int i = 0; !stop.load(); i++) {
            r.e.loadCode(i % 3 ? "piano: c8 d e f" : i % 2 ? "# @clock midi\npiano: c d" : "# @loop 1\nbass: o2 c16 d", "");
            if (i % 7 == 0) r.e.clearParts();
            r.e.idle();
            (void)r.e.status();
        }
    });
    std::thread clock([&] {  // as the MIDI input thread
        for (int i = 0; !stop.load(); i++) {
            r.e.midiClock(i * 0.02);
            if (i % 50 == 0) r.e.midiStart();
            if (i % 50 == 25) r.e.midiStop();
        }
    });
    std::thread midi([&] {  // as the plugin's MIDI thread
        AldaEngine::Midi m;
        while (!stop.load())
            while (r.e.midiOut(m)) {
            }
    });
    for (int b = 0; b < 4000; b++) r.e.process(r.ip, r.op, BLOCK);
    stop = true;
    worker.join();
    clock.join();
    midi.join();
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "threads") {
        testThreads();
    } else if (argc > 1 && (std::string(argv[1]) == "check" || std::string(argv[1]) == "time")) {
        bool timed = std::string(argv[1]) == "time";
        for (int a = 2; a < argc; a++) {
            std::string text, err;
            AldaEngine::Score s;
            bool ok = AldaEngine::readText(argv[a], text) && AldaEngine::build(text, argv[a], s, err);
            if (!ok) std::fprintf(stderr, "%s: %s\n", argv[a], err.c_str());
            CHECK(ok && !s.events.empty());
            if (!ok || !timed) continue;
            std::vector<double> ms;
            for (int r = 0; r < 5; r++) {
                auto t0 = std::chrono::steady_clock::now();
                AldaEngine::build(text, argv[a], s, err);
                ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }
            std::sort(ms.begin(), ms.end());
            std::printf("%8.2f ms %6zu events %7zu bytes  %s\n", ms[2], s.events.size(), text.size(), argv[a]);
        }
    } else {
        testNotes();
        testGateLength();
        testParts();
        testChordLastWins();
        testLoopDirective();
        testSwapAtLoopEnd();
        testErrorKeepsScore();
        testReset();
        testTempo();
        testBuiltin();
        testMidi();
        testMidiReset();
        testMidiParts();
        testMidiLoopCut();
        testStop();
        testEmptyStops();
        testClockInput();
        testClockMidi();
        testClockLine();
        testTranspose();
        testTempoVelocity();
        testSwing();
        testMute();
        testControlsDeclared();
    }
    if (failures) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
