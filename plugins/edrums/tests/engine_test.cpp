// Native test of edrums::EdrumsEngine; built and run by test_edrums.py.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "EdrumsEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

using namespace edrums;
using E = EdrumsEngine;

static constexpr int BLOCK = 128;

static std::string bits(const Pattern& p) {
    std::string s;
    for (int i = 0; i < p.steps(); i++) s += p.isOnset(i) ? 'x' : '.';
    return s;
}

static void testPattern() {
    Pattern p;
    p.setLength(8);
    p.setOnsets(3);
    CHECK(bits(p) == "x.x..x..");  // Christoffel order: a rotation of Bjorklund's x..x..x.
    p.setLength(16);
    p.setOnsets(4);
    CHECK(bits(p) == "x...x...x...x...");
    p.setRotation(2);
    CHECK(bits(p) == "..x...x...x...x.");
    p.setRotation(-1);
    CHECK(bits(p) == "...x...x...x...x");
    p.setRotation(0);
    p.setLength(12);  // 4 of 12: not coprime, the cell repeats
    CHECK(bits(p) == "x..x..x..x..");
    p.setOnsets(16);
    CHECK(bits(p) == "xxxxxxxxxxxx");
    p.setOnsets(0);
    CHECK(bits(p) == "............");
    // trigger() plays the rotated pattern from the reset position
    p.setLength(5);
    p.setOnsets(2);
    p.setRotation(1);
    std::string played;
    p.reset();
    for (int i = 0; i < 10; i++) played += p.trigger() ? 'x' : '.';
    CHECK(played == bits(p) + bits(p));
    CHECK(p.position() == 0);
}

struct Rig {
    E e;
    std::vector<float> clock, rst, out[E::O_MAX];
    const float* ip[E::I_MAX];
    float* op[E::O_MAX];
    float sr;

    explicit Rig(float rate = 48000.0f) : clock(BLOCK, 0.0f), rst(BLOCK, 0.0f), sr(rate) {
        ip[E::I_CLOCK] = clock.data();
        ip[E::I_RESET] = rst.data();
        for (int c = 0; c < E::O_MAX; c++) out[c].assign(BLOCK, 0.0f), op[c] = out[c].data();
        for (int d = 0; d < E::DRUMS; d++) e.controls(d).hits = 0;
        e.prepare(sr, BLOCK);
    }
    // one block; a clock pulse of `width` samples starts at `at` when at >= 0
    std::vector<float> block(int ch, int at = -1, int width = 32, bool reset = false) {
        std::fill(clock.begin(), clock.end(), 0.0f);
        std::fill(rst.begin(), rst.end(), 0.0f);
        if (at >= 0)
            for (int i = at; i < std::min(BLOCK, at + width); i++) (reset ? rst : clock)[size_t(i)] = 1.0f;
        e.process(ip, op, BLOCK);
        return out[ch];
    }
    void pulses(int n) {
        for (int i = 0; i < n; i++) {
            block(0, 0);
            block(0);  // low for a block between pulses
        }
    }
};

static bool silent(const std::vector<float>& v) {
    for (float x : v)
        if (x != 0.0f) return false;
    return true;
}

static void testClock() {
    Rig r;
    r.e.controls(0).hits = 4;  // the kick, x...x...x...x...
    for (int b = 0; b < 100; b++) CHECK(silent(r.block(E::O_L)));  // no clock, no sound
    r.pulses(16);
    CHECK(r.e.info(0).hits == 4);
    CHECK(r.e.info(0).position == 0);
    CHECK(r.e.info(0).onsets == 0x1111);

    // div 2: a step every other pulse
    r.e.controls(0).div = 2;
    r.pulses(16);
    CHECK(r.e.info(0).hits == 6);
    CHECK(r.e.info(0).position == 8);

    // a reset realigns: the next pulse plays step 0
    r.e.controls(0).div = 1;
    r.block(0, 0, 32, true);
    CHECK(r.e.info(0).position == 0);
    r.pulses(1);
    CHECK(r.e.info(0).hits == 7);
    CHECK(r.e.info(0).position == 1);
}

// The voice starts on the clock's rising edge, not at the block start. A level held between the
// Schmitt thresholds does not clock.
static void testEdgeTiming() {
    Rig r;
    r.e.controls(0).hits = 16;
    auto v = r.block(E::O_1, 70);
    for (int i = 0; i < 70; i++) CHECK(v[size_t(i)] == 0.0f);
    bool sounds = false;
    for (int i = 70; i < 73; i++) sounds = sounds || v[size_t(i)] != 0.0f;
    CHECK(sounds);
    Rig q;
    q.e.controls(0).hits = 16;
    std::fill(q.clock.begin(), q.clock.end(), 0.3f);  // 1.5 V
    for (int b = 0; b < 10; b++) q.e.process(q.ip, q.op, BLOCK);
    CHECK(q.e.info(0).hits == 0);
    // one long pulse with a dip that stays above the low threshold is one clock
    Rig s;
    s.e.controls(0).hits = 16;
    std::fill(s.clock.begin(), s.clock.end(), 1.0f);
    for (int i = 40; i < 60; i++) s.clock[size_t(i)] = 0.25f;
    s.e.process(s.ip, s.op, BLOCK);
    CHECK(s.e.info(0).hits == 1);
}

static void testChanceMuteTrigger() {
    Rig r;
    r.e.controls(0).hits = 16;
    r.e.controls(0).chance = 0.0f;
    r.pulses(16);
    CHECK(r.e.info(0).hits == 0);
    r.e.controls(0).chance = 0.5f;
    r.pulses(400);
    CHECK(r.e.info(0).hits > 150 && r.e.info(0).hits < 250);

    Rig m;
    m.e.controls(0).hits = 16;
    m.e.controls(0).mute = true;
    m.pulses(5);
    CHECK(m.e.info(0).hits == 0);
    CHECK(m.e.info(0).position == 5);  // a muted track keeps its place

    // the trigger fires once per rising edge, clock or not
    Rig t;
    t.e.controls(2).trigger = true;
    CHECK(!silent(t.block(E::O_3)));
    t.block(E::O_3);
    CHECK(t.e.info(2).hits == 1);
    t.e.controls(2).trigger = false;
    t.block(E::O_3);
    t.e.controls(2).trigger = true;
    t.block(E::O_3);
    CHECK(t.e.info(2).hits == 2);
}

static void testRouteAndModels() {
    for (float rate : { 48000.0f, 44100.0f }) {
        for (int m = 0; m < MODELS; m++) {
            Rig r(rate);
            r.e.controls(0).model = Model(m);
            r.e.controls(0).hits = 16;
            r.e.controls(0).drive = 1.0f;
            r.e.setRoute(Route::Split);
            double l = 0.0, rr = 0.0;
            bool finite = true;
            for (int b = 0; b < 40; b++) {
                r.block(E::O_L, b % 10 == 0 ? 0 : -1);
                for (int i = 0; i < BLOCK; i++) {
                    finite = finite && std::isfinite(r.out[E::O_L][size_t(i)]);
                    l += std::fabs(r.out[E::O_L][size_t(i)]);
                    rr += std::fabs(r.out[E::O_R][size_t(i)]);
                }
            }
            CHECK(finite);
            CHECK(l > 1.0);
            CHECK(rr == 0.0);  // split: drum 1 is left only
        }
    }
    // stereo: both sides carry the sum
    Rig r;
    r.e.controls(3).hits = 16;
    r.block(E::O_L, 0);
    CHECK(!silent(r.out[E::O_L]));
    CHECK(r.out[E::O_L] == r.out[E::O_R]);
    CHECK(silent(r.out[E::O_1]) && !silent(r.out[E::O_4]));
}

// x3 spreads three steps over each clock period, measured from the last two pulses; /3 steps on
// every third pulse. The first pulse has no period yet, so it steps once.
static void testMultiplyDivide() {
    Rig r;
    r.e.controls(0).hits = 16;
    r.e.controls(0).mult = 3;
    r.pulses(1);
    CHECK(r.e.info(0).hits == 1);
    r.pulses(4);  // period 256 samples: each later pulse adds 3
    CHECK(r.e.info(0).hits == 13);
    // the multiplied steps fall a third of a period apart
    std::vector<float> trig;
    for (int b = 0; b < 2; b++) {
        auto v = r.block(E::O_TRIG_1, b == 0 ? 0 : -1);
        trig.insert(trig.end(), v.begin(), v.end());
    }
    std::vector<int> at;
    int high = 0;
    for (size_t i = 0; i < trig.size(); i++) {
        if (trig[i] > 0.5f && (i == 0 || trig[i - 1] < 0.5f)) at.push_back(int(i));
        high += trig[i] > 0.5f;
    }
    CHECK(at.size() == 3);
    if (at.size() == 3) CHECK(std::abs(at[1] - at[0] - 85) <= 1 && std::abs(at[2] - at[1] - 85) <= 1);
    CHECK(high == 3 * (256 / 6));  // each trigger half the step spacing

    Rig d;
    d.e.controls(1).hits = 16;
    d.e.controls(1).div = 3;
    d.pulses(9);
    CHECK(d.e.info(1).hits == 3);
    d.e.controls(1).div = 5;  // odd and even alike; counted from the reset
    d.block(0, 0, 32, true);
    d.pulses(11);
    CHECK(d.e.info(1).hits == 6);
    // x5 at another period
    Rig m;
    m.e.controls(2).hits = 16;
    m.e.controls(2).mult = 5;
    for (int i = 0; i < 4; i++) {
        m.block(0, 0);
        for (int b = 0; b < 3; b++) m.block(0);
    }
    CHECK(m.e.info(2).hits == 1 + 3 * 5);
}

// rising edges of trigger output `ch` over `blocks`, a clock pulse every `every` blocks
// and, with `reset`, a reset on the first pulse
static std::vector<int> edges(Rig& r, int ch, int blocks, int every, bool reset = false) {
    std::vector<float> v;
    for (int b = 0; b < blocks; b++) {
        if (b == 0 && reset) {
            std::fill(r.clock.begin(), r.clock.end(), 0.0f);
            std::fill(r.rst.begin(), r.rst.end(), 0.0f);
            for (int i = 0; i < 32; i++) r.clock[size_t(i)] = r.rst[size_t(i)] = 1.0f;
            r.e.process(r.ip, r.op, BLOCK);
            v.insert(v.end(), r.out[ch].begin(), r.out[ch].end());
            continue;
        }
        auto o = r.block(ch, b % every == 0 ? 0 : -1);
        v.insert(v.end(), o.begin(), o.end());
    }
    std::vector<int> at;
    for (size_t i = 0; i < v.size(); i++)
        if (v[i] > 0.5f && (i == 0 || v[i - 1] < 0.5f)) at.push_back(int(i));
    return at;
}

// Swing delays a track's odd steps by (swing - 0.5) x 2 of its step length, at any rate.
static void testSwing() {
    auto run = [](int mult, int div, float swing) {
        Rig r;
        r.e.controls(0).hits = 16;
        r.e.controls(0).mult = mult;
        r.e.controls(0).div = div;
        r.e.controls(0).swing = swing;
        r.pulses(2);  // measure the period: 256 samples
        return edges(r, E::O_TRIG_1, 16, 2, true);  // swing counts steps from the reset
    };
    auto expect = [](const std::vector<int>& at, std::vector<int> want) {
        bool ok = at.size() >= want.size();
        for (size_t i = 0; ok && i < want.size(); i++) ok = std::abs(at[i] - want[i]) <= 1;
        if (!ok) {
            std::fprintf(stderr, "edges:");
            for (int a : at) std::fprintf(stderr, " %d", a);
            std::fprintf(stderr, "\n");
        }
        CHECK(ok);
    };
    expect(run(1, 1, 0.5f), { 0, 256, 512, 768 });
    expect(run(1, 1, 0.75f), { 0, 384, 512, 896 });    // late by half a step
    expect(run(1, 1, 0.6f), { 0, 307, 512, 819 });
    expect(run(2, 1, 0.75f), { 0, 192, 256, 448 });    // x2: steps of 128
    expect(run(1, 2, 0.75f), { 0, 768, 1024, 1792 });  // /2: steps of 512
    // a reset drops a waiting swung step and starts even again
    Rig r;
    r.e.controls(0).hits = 16;
    r.e.controls(0).swing = 0.75f;
    r.pulses(1);
    r.block(0, 0);  // the second step, odd, waits 128 samples: past this block
    r.block(0, 0, 32, true);
    CHECK(r.e.info(0).hits == 1);
    expect(edges(r, E::O_TRIG_1, 4, 2), { 0, 384 });
}

// Each hit sends a 5 ms trigger. With the voice off, a track sends only the trigger.
static void testTriggers() {
    Rig r;
    r.e.controls(0).hits = 16;
    r.e.controls(0).voice = false;
    std::vector<float> trig, audio;
    for (int b = 0; b < 8; b++) {
        auto t = r.block(E::O_TRIG_1, b % 4 == 0 ? 0 : -1);
        trig.insert(trig.end(), t.begin(), t.end());
        audio.insert(audio.end(), r.out[E::O_1].begin(), r.out[E::O_1].end());
    }
    int pulses = 0, high = 0;
    for (size_t i = 0; i < trig.size(); i++) {
        pulses += trig[i] > 0.5f && (i == 0 || trig[i - 1] < 0.5f);
        high += trig[i] > 0.5f;
    }
    CHECK(pulses == 2);
    CHECK(high == 2 * int(E::TRIG_S * 48000.0f));
    CHECK(silent(audio));
    CHECK(silent(r.out[E::O_TRIG_2]));
    // a mute stops the trigger too
    Rig m;
    m.e.controls(0).hits = 16;
    m.e.controls(0).mute = true;
    CHECK(silent(m.block(E::O_TRIG_1, 0)));
    // a second hit while the first trigger is high still makes an edge: clock edges at 0 and 60,
    // the first with no measured period yet, so a full 5 ms trigger
    Rig g;
    g.e.controls(3).hits = 16;
    std::fill(g.clock.begin(), g.clock.end(), 0.0f);
    for (int i : { 0, 60 })
        for (int k = 0; k < 10; k++) g.clock[size_t(i + k)] = 1.0f;
    g.e.process(g.ip, g.op, BLOCK);
    auto& v = g.out[E::O_TRIG_4];
    CHECK(v[59] == 1.0f && v[60] == 0.0f && v[61] == 1.0f);
}

static void testThreads() {
    Rig r;
    for (int d = 0; d < E::DRUMS; d++) r.e.controls(d).hits = 5;
    std::atomic<bool> done{ false };
    std::thread ui([&] {
        uint32_t sum = 0;
        while (!done.load())
            for (int d = 0; d < E::DRUMS; d++) {
                auto i = r.e.info(d);
                sum += i.hits + uint32_t(i.position) + i.onsets + uint32_t(i.steps);
            }
        (void)sum;
    });
    for (int i = 0; i < 200; i++) {
        r.e.controls(i % E::DRUMS).steps = 1 + i % 16;
        r.pulses(1);
    }
    done = true;
    ui.join();
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "threads") == 0) {
        testThreads();
    } else {
        testPattern();
        testClock();
        testEdgeTiming();
        testChanceMuteTrigger();
        testRouteAndModels();
        testMultiplyDivide();
        testTriggers();
        testSwing();
    }
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
