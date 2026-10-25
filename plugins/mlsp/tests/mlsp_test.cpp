// Native test of mlsp::UseqEngine against uSEQ's signal engine; built and run by test_mlsp.py.
// argv[1] == "threads" runs the threaded test only; "check" compiles each program file after it; "time" also
// prints each one's compile time, the median of 5 runs, in ms.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "UseqEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

using mlsp::UseqEngine;
static constexpr int CH = UseqEngine::CHANNELS;
static constexpr int BLOCK = 128;
static constexpr float SR = 48000.0f;
static constexpr long BEAT = 24000;  // frames at 120 bpm
static constexpr int TICK = UseqEngine::TICK;
static constexpr int A1 = UseqEngine::GATES;  // out 5

// a test can reach the engine's protected members through this
struct Engine : UseqEngine {
    using UseqEngine::compile;
};

struct Rig {
    Engine e;
    std::vector<float> in[CH], out[CH];
    const float* ip[CH];
    float* op[CH];
    long frame = 0;  // whole blocks run, so a run may pass its target by up to BLOCK - 1
    float last[CH] = {};
    std::vector<long> rises[CH];  // frames where an output went from 0 to above 0

    explicit Rig(const std::string& code) {
        for (int c = 0; c < CH; c++) {
            in[c].assign(BLOCK, 0.0f);
            out[c].assign(BLOCK, 0.0f);
            ip[c] = in[c].data();
            op[c] = out[c].data();
        }
        e.prepare(SR, BLOCK);
        e.idle();
        if (!code.empty()) load(code);
    }
    void load(const std::string& code) {
        e.loadCode(code, "");
        e.idle();
    }
    // Ctrl-E: `part`, with `before` the text above it
    void part(const std::string& part, const std::string& before = "") {
        e.evaluate(part, before, "");
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
                    if (last[c] <= 0.0f && v > 0.0f) rises[c].push_back(frame + i);
                    last[c] = v;
                }
            }
            frame += BLOCK;
        }
    }
    float at(int c) const { return out[c][BLOCK - 1]; }  // the last frame run
};

static bool nearFrame(long got, long want) {
    return std::labs(got - want) <= TICK;
}

static void testBeatGate() {
    Rig r("(d1 (sqr beat))");
    CHECK(r.e.error().empty());
    r.runTo(4 * BEAT + BEAT / 2);
    CHECK(r.rises[0].size() == 5);
    for (size_t i = 0; i < r.rises[0].size(); i++) CHECK(nearFrame(r.rises[0][i], long(i) * BEAT));
    CHECK(r.rises[1].empty());
    CHECK(r.e.status().find("d1") != std::string::npos);
}

static void testTempo() {
    Rig r("(set-bpm 90)\n(d2 (sqr beat))");
    const long beat = long(SR * 60 / 90);
    r.runTo(3 * beat + beat / 2);
    CHECK(r.rises[1].size() == 4);
    for (size_t i = 0; i < r.rises[1].size(); i++) CHECK(nearFrame(r.rises[1][i], long(i) * beat));
    CHECK(r.e.runState().find("90 bpm") != std::string::npos);
}

static void testCv() {
    Rig r("(a1 0.5)\n(a2 2)\n(a3 -1)\n(a4 (+ 0.25 0.25))");
    r.runTo(BLOCK);
    NEAR(r.at(A1), 0.5f, 1e-6);  // 2.5 V
    NEAR(r.at(A1 + 1), 1.0f, 1e-6);  // clamped to 5 V
    NEAR(r.at(A1 + 2), 0.0f, 1e-6);
    NEAR(r.at(A1 + 3), 0.5f, 1e-6);
}

static void testInputs() {
    Rig r("(a1 ain3)\n(d4 in8)\n(a2 ain8)");
    for (int i = 0; i < BLOCK; i++) {
        r.in[2][i] = 0.3f;
        r.in[7][i] = 0.5f;  // 2.5 V: a gate, and 0.5 as CV
    }
    r.runTo(BLOCK);
    NEAR(r.at(A1), 0.3f, 1e-6);
    CHECK(r.at(3) == 1.0f);
    NEAR(r.at(A1 + 1), 0.5f, 1e-6);
    for (int i = 0; i < BLOCK; i++) r.in[7][i] = 0.1f;  // 0.5 V: below the gate threshold
    r.runTo(2 * BLOCK);
    CHECK(r.at(3) == 0.0f);
    r.load("(a1 ain9)");
    CHECK(r.e.error().find("line 1") != std::string::npos);
}

static void testParam() {
    Rig r("; @p1 level 0 2\n(a1 (* 0.25 p1))");
    CHECK(r.e.spec(0).label == "level");
    r.e.setParam(0, 0.5f);  // 1.0 in 0..2
    r.runTo(BLOCK);
    NEAR(r.at(A1), 0.25f, 1e-6);
    r.e.setParam(0, 1.0f);
    r.runTo(2 * BLOCK);
    NEAR(r.at(A1), 0.5f, 1e-6);
}

static void testErrorKeepsProgram() {
    Rig r("(d1 (sqr beat))\n(a1 0.5)");
    r.runTo(BEAT / 2);
    // the first form is valid; the second's unknown name fails on the fresh engine, so neither runs
    r.load("(a1 0.25)\n\n(d1 (sqr nope))");
    CHECK(r.e.error().find("line 3") != std::string::npos);
    r.runTo(2 * BEAT + BEAT / 2);
    CHECK(r.rises[0].size() == 3);
    NEAR(r.at(A1), 0.5f, 1e-6);
    r.load("(a1 0.25)\n(d1 (sqr beat)");
    CHECK(r.e.error() == "line 2: form not closed");
    r.load("(a1 0.25))");
    CHECK(r.e.error() == "line 1: unexpected closing bracket");
    r.runTo(3 * BEAT);
    NEAR(r.at(A1), 0.5f, 1e-6);
}

static void testReplaceDropsOutputs() {
    Rig r("(d1 (sqr beat))\n(a1 0.5)");
    r.runTo(BLOCK);
    NEAR(r.at(A1), 0.5f, 1e-6);
    r.load("(d1 (sqr beat))");
    CHECK(r.e.error().empty());
    r.runTo(2 * BLOCK);
    NEAR(r.at(A1), 0.0f, 1e-6);
    CHECK(r.at(0) == 1.0f);
    CHECK(r.e.status().find("a1") == std::string::npos);
}

static void testPart() {
    Rig r("(d1 (sqr beat))");
    r.runTo(BEAT / 2);
    r.part("(a2 0.75)");
    CHECK(r.e.note() == "ok");
    r.part("(+ 1 2)");
    CHECK(r.e.note() == "= 3");
    r.runTo(BEAT + BEAT / 2);
    NEAR(r.at(A1 + 1), 0.75f, 1e-6);
    CHECK(r.rises[0].size() == 2);
    r.part("(a3 nope)", "one\ntwo\n");
    CHECK(r.e.error().find("line 3") != std::string::npos);
    // a part's cell is seen by the next part
    r.part("(define lvl 0.2)");
    r.part("(a3 lvl)");
    r.runTo(2 * BEAT);
    NEAR(r.at(A1 + 2), 0.2f, 1e-6);
}

static void testClear() {
    Rig r("(d1 (sqr beat))\n(a1 0.5)");
    r.runTo(BEAT / 4);
    r.e.clearParts();  // Ctrl-.
    r.e.idle();
    CHECK(r.e.note() == "cleared");
    r.runTo(BEAT / 4 + BLOCK);
    CHECK(r.at(0) == 0.0f);
    NEAR(r.at(A1), 0.0f, 1e-6);
    CHECK(r.e.status().find("no outputs") != std::string::npos);
    // an empty program, as the editor's reset runs
    r.load("(a1 0.5)");
    r.load("");
    CHECK(r.e.error().empty());
    r.runTo(BEAT / 4 + 3 * BLOCK);
    NEAR(r.at(A1), 0.0f, 1e-6);
}

static void testPause() {
    Rig r("(a1 (/ beat 2))");
    r.runTo(BEAT / 2);
    r.part("(useq-pause)");
    r.runTo(BEAT / 2 + BLOCK);
    float held = r.at(A1);
    r.runTo(BEAT);
    CHECK(r.at(A1) == held);
    CHECK(r.e.runState() == "paused");
    r.part("(useq-play)");
    r.runTo(BEAT + BEAT / 4);
    CHECK(r.at(A1) != held);
}

static void testReset() {
    Rig r("(d1 (sqr beat))");
    r.runTo(BEAT + BEAT / 2);
    for (int i = 0; i < BLOCK; i++) r.in[0][i] = 1.0f;
    r.runTo(BEAT + BEAT / 2 + BLOCK);
    for (int i = 0; i < BLOCK; i++) r.in[0][i] = 0.0f;
    r.runTo(2 * BEAT + BEAT / 2);
    CHECK(r.rises[0].size() == 3);  // no @reset: In 1 is only an input

    Rig s("; @reset\n(d1 (sqr beat))");
    s.runTo(BEAT + BEAT / 2);
    for (int i = 0; i < BLOCK; i++) s.in[0][i] = 1.0f;
    long at = s.frame;
    s.runTo(at + BLOCK);
    for (int i = 0; i < BLOCK; i++) s.in[0][i] = 0.0f;
    s.runTo(at + BEAT + BEAT / 2);
    CHECK(s.rises[0].size() == 4);
    if (s.rises[0].size() == 4) {
        CHECK(nearFrame(s.rises[0][2], at));
        CHECK(nearFrame(s.rises[0][3], at + BEAT));
    }
    CHECK(s.e.status().find("reset In 1") != std::string::npos);
}

static void testClockInput() {
    // 140 bpm pulses, 4 a quarter, on In 2: beat follows them, not the program's 120
    Rig r("; @clock\n(d1 (sqr beat))");
    const long pulse = long(SR * 60 / 140 / 4);
    while (r.frame < 8 * 4 * pulse) {
        for (int i = 0; i < BLOCK; i++) r.in[1][i] = (r.frame + i) % pulse < pulse / 2 ? 1.0f : 0.0f;
        r.runTo(r.frame + BLOCK);
    }
    CHECK(r.rises[0].size() >= 7);
    for (size_t i = 2; i < r.rises[0].size(); i++)
        CHECK(std::labs(r.rises[0][i] - r.rises[0][i - 1] - 4 * pulse) <= 2 * TICK);
    CHECK(r.e.status().find("clock In 2 x4") != std::string::npos);
    CHECK(r.e.runState().find("140 bpm") != std::string::npos);
}

static void testClockMidi() {
    Rig r("; @clock midi\n(d1 (sqr beat))");
    // MIDI clock at 100 bpm: 24 ticks a quarter, delivered before each block as the input thread would
    const double tickSec = 60.0 / 100 / 24;
    double now = 0;
    r.e.midiStart();
    while (r.frame < long(SR * 60 / 100 * 4)) {
        double end = (r.frame + BLOCK) / double(SR);
        while (now < end) {
            r.e.midiClock(now);
            now += tickSec;
        }
        r.runTo(r.frame + BLOCK);
    }
    const long beat = long(SR * 60 / 100);
    CHECK(r.rises[0].size() >= 3);
    for (size_t i = 2; i < r.rises[0].size(); i++) CHECK(std::labs(r.rises[0][i] - r.rises[0][i - 1] - beat) <= 2 * TICK + BLOCK);
}

static void testSplit() {
    std::vector<UseqEngine::Form> f;
    std::string err;
    CHECK(UseqEngine::split("; c\n(a1 \"x)\")\n  bpm [1 2]\n(d1\n ; )\n 1)", 1, f, err));
    CHECK(f.size() == 4);
    if (f.size() == 4) {
        CHECK(f[0].line == 2 && f[1].line == 3 && f[2].line == 3 && f[3].line == 4);
        CHECK(f[1].length == 3);
    }
    CHECK(!UseqEngine::split("(a1\n\"abc", 1, f, err) && err == "line 2: string not closed");
    CHECK(!UseqEngine::split(std::string("(a1 ") + std::string(70000, '1') + ")", 1, f, err) && err == "line 1: form too long");
}

static void testOptions() {
    auto o = UseqEngine::options("; @clock 24\n;  @reset\n");
    CHECK(o.clock == ssp::engine::Clock::INPUT && o.ppqn == 24 && o.reset);
    o = UseqEngine::options(";; @clock midi");
    CHECK(o.clock == ssp::engine::Clock::MIDI && !o.reset);
    o = UseqEngine::options("(d1 1) ; @clock");
    CHECK(o.clock == ssp::engine::Clock::FREE);
}

static void testLongProgram() {
    // more tokens than one uSEQ eval takes, as separate forms
    std::string code;
    for (int i = 0; i < 100; i++) code += "(define c" + std::to_string(i) + " (+ 1 2 3))\n";
    code += "(a1 0.5)";
    Rig r(code);
    CHECK(r.e.error().empty());
    // one form over the limit is an error, not a truncated form
    std::string big = "(a1 (+";
    for (int i = 0; i < 300; i++) big += " 0";
    Rig s(big + "))");
    CHECK(s.e.error().find("Too many tokens") != std::string::npos);
}

static void testBuiltin() {
    Rig r("");
    CHECK(r.e.error().empty());
    r.runTo(4 * BEAT);
    for (int k = 0; k < UseqEngine::GATES; k++) CHECK(!r.rises[k].empty());
    CHECK(r.e.status().find("d1 d2 d3 d4 a1 a2 a3 a4") != std::string::npos);
}

static void testTwoInstances() {
    Rig a("(d1 (sqr beat))\n(define x 1)");
    Rig b("(define x 0.25)\n(a1 x)");
    a.runTo(BLOCK);
    b.runTo(BLOCK);
    NEAR(b.at(A1), 0.25f, 1e-6);
    NEAR(a.at(A1), 0.0f, 1e-6);
}

static void testThreads() {
    Rig r("(d1 (sqr beat))");
    Rig q("(a1 (usin bar))");
    std::atomic<bool> stop{ false };
    auto churn = [&](Rig& x, int seed) {
        for (int i = seed; !stop.load(); i++) {
            x.e.loadCode(i % 3 ? "(d1 (sqr (fast 2 beat)))\n(a1 p1)" : "; @clock midi\n(d2 (sqr beat))", "");
            if (i % 5 == 0) x.e.evaluate("(a2 (* 0.5 beat))", "", "");
            if (i % 7 == 0) x.e.clearParts();
            x.e.idle();
            (void)x.e.status();
            x.e.setParam(0, float(i % 10) / 10);
        }
    };
    std::thread w1([&] { churn(r, 0); });
    std::thread w2([&] { churn(q, 1); });
    std::thread clock([&] {  // as the MIDI input thread
        for (int i = 0; !stop.load(); i++) {
            r.e.midiClock(i * 0.02);
            if (i % 50 == 0) r.e.midiStart();
            if (i % 50 == 25) r.e.midiStop();
        }
    });
    std::thread audio2([&] {
        for (int b = 0; b < 3000; b++) q.e.process(q.ip, q.op, BLOCK);
    });
    for (int b = 0; b < 3000; b++) r.e.process(r.ip, r.op, BLOCK);
    audio2.join();
    stop = true;
    w1.join();
    w2.join();
    clock.join();
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "threads") {
        testThreads();
    } else if (argc > 1 && (std::string(argv[1]) == "check" || std::string(argv[1]) == "time")) {
        bool timed = std::string(argv[1]) == "time";
        Engine e;
        for (int a = 2; a < argc; a++) {
            std::string text, err;
            bool ok = UseqEngine::readText(argv[a], text) && e.compile(text, argv[a], err);
            if (!ok) std::fprintf(stderr, "%s: %s\n", argv[a], err.c_str());
            CHECK(ok);
            if (!ok || !timed) continue;
            std::vector<double> ms;
            for (int k = 0; k < 5; k++) {
                auto t0 = std::chrono::steady_clock::now();
                e.compile(text, argv[a], err);
                ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }
            std::sort(ms.begin(), ms.end());
            std::printf("%8.3f ms %7zu bytes  %s\n", ms[2], text.size(), argv[a]);
        }
    } else {
        testBeatGate();
        testTempo();
        testCv();
        testInputs();
        testParam();
        testErrorKeepsProgram();
        testReplaceDropsOutputs();
        testPart();
        testClear();
        testPause();
        testReset();
        testClockInput();
        testClockMidi();
        testSplit();
        testOptions();
        testLongProgram();
        testBuiltin();
        testTwoInstances();
        if (failures == 0) std::printf("all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
