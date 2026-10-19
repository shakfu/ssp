// Native test of brks::BreaksEngine; built and run by test_breaks.py and run_on_ssp.sh.
//
//   engine_test golden        the generated firmware's output against the x86 build's
//   engine_test CARD          the tests, with CARD a card folder of three banks (test_breaks.py makes one)
//   engine_test bench CARD    render cost per 48 kHz block, playing CARD's first bank

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "BreaksEngine.h"
extern "C" uint64_t brks_golden(void);  // golden.c

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

using namespace brks;
using E = BreaksEngine;

static constexpr float SR = 48000.0f;
static std::string card;

struct Rig {
    E e;
    int block;
    float rate;
    std::vector<std::vector<float>> in, out;
    int clockEdges = 0, trigEdges = 0;

    explicit Rig(int block = 128, float rate = SR)
        : block(block), rate(rate), in(E::I_MAX, std::vector<float>(size_t(block))),
          out(E::O_MAX, std::vector<float>(size_t(block))) {
        e.prepare(rate, block);
    }
    // renders `seconds`; returns mean |x| of both channels over the last `tail` seconds
    float run(float seconds, float tail = 0.1f, bool* finite = nullptr) {
        int blocks = (int(seconds * rate) + block - 1) / block, from = blocks - int(tail * rate) / block;
        std::vector<const float*> ip;
        std::vector<float*> op;
        for (auto& v : in) ip.push_back(v.data());
        for (auto& v : out) op.push_back(v.data());
        double sum = 0;
        long count = 0;
        float clockWas = out[E::O_CLOCK][size_t(block - 1)], trigWas = out[E::O_TRIG][size_t(block - 1)];
        for (int b = 0; b < blocks; b++) {
            e.process(ip.data(), op.data(), block);
            for (int i = 0; i < block; i++) {
                for (int c = 0; c < 2; c++) {
                    float x = out[size_t(E::O_L + c)][size_t(i)];
                    if (finite && !(std::isfinite(x) && std::fabs(x) <= 1.5f)) *finite = false;
                    if (b >= from) sum += std::fabs(x), count++;
                }
                float k = out[E::O_CLOCK][size_t(i)], t = out[E::O_TRIG][size_t(i)];
                clockEdges += k > 0.5f && clockWas <= 0.5f;
                trigEdges += t > 0.5f && trigWas <= 0.5f;
                clockWas = k, trigWas = t;
            }
        }
        return count ? float(sum / double(count)) : 0.0f;
    }
    // a 1 ms trigger on input `i`, then `after` seconds
    void trigger(int i, float after = 0.3f) {
        std::fill(in[size_t(i)].begin(), in[size_t(i)].end(), 1.0f);
        run(float(block) / rate);
        std::fill(in[size_t(i)].begin(), in[size_t(i)].end(), 0.0f);
        run(after);
    }
    // a gate held `seconds` on input `i`, then `after` seconds
    void gate(int i, float seconds, float after = 0.3f) {
        std::fill(in[size_t(i)].begin(), in[size_t(i)].end(), 1.0f);
        run(seconds);
        std::fill(in[size_t(i)].begin(), in[size_t(i)].end(), 0.0f);
        run(after);
    }
    bool waitLoaded(float seconds = 10.0f) {
        for (int i = 0; i < int(seconds * 100); i++) {
            run(0.01f);
            if (e.status().loaded) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
};

// The x86 build's hash of golden.c's render. A different value on the SSP is a miscompile: clang 18
// and 20 in ARM (not Thumb) mode drop a shift in WS2812_set_brightness.
static constexpr uint64_t GOLDEN = 0x6ac7a5920f5cc629ULL;

static void testGolden() {
    uint64_t h = brks_golden();
    std::printf("golden %016llx\n", (unsigned long long)h);
    CHECK(h == GOLDEN);
}

static void testSilentWithoutCard() {
    Rig r;
    bool finite = true;
    CHECK(r.run(0.5f, 0.1f, &finite) == 0.0f);
    CHECK(finite);
    CHECK(!r.e.status().loaded);
    r.e.load("/nonexistent/brks");
    CHECK(r.e.message().find("no card folder") == 0);
    CHECK(r.e.folder().empty());
}

static void testResolve() {
    CHECK(E::resolve(card) == card);
    CHECK(E::resolve(card + "/") == card);
    CHECK(E::resolve(card + "/bank2") == card);
    CHECK(E::resolve(card + "/bank1/0.0.wav") == card);
    CHECK(E::resolve(card + "/..") != "");  // the folder holding the card: its first card
    CHECK(E::resolve(card + "/bank1/missing.wav") == "");
    CHECK(E::resolve(card + "/settings") == "");
}

static void testPlays() {
    Rig r;
    r.e.load(card);
    CHECK(r.waitLoaded());
    CHECK(r.e.folder() == card);
    bool finite = true;
    float level = r.run(2.0f, 0.5f, &finite);
    CHECK(level > 0.02f);
    CHECK(finite);
    CHECK(r.clockEdges > 2);  // the internal clock at the card's tempo
    Status s = r.e.status();
    CHECK(s.bank == 0 && !s.stopped && s.tempo >= 30);
}

static void testBankTrigger() {
    Rig r;
    r.e.load(card);
    CHECK(r.waitLoaded());
    r.run(0.5f);
    CHECK(r.e.status().bank == 0);
    r.trigger(E::I_BANK, 3.0f);  // 2.7 ms: the press is stretched; the bank changes at the 2 s loop's end
    CHECK(r.e.status().bank == 1);
    CHECK(r.run(1.0f, 0.3f) > 0.02f);  // the new bank plays
    // the firmware's Bank tap counts only if released within 200 ms; the gate's edge is the step
    r.gate(E::I_BANK, 0.5f, 3.0f);
    CHECK(r.e.status().bank == 2);
}

// Left and Right: steps add up before the loop's end, wrap, and a step back cancels
static void testStepBank() {
    Rig r;
    r.e.load(card);
    CHECK(r.waitLoaded());
    r.run(0.5f);
    r.e.stepBank(1);
    r.run(0.1f);
    r.e.stepBank(1);
    r.run(3.0f);
    CHECK(r.e.status().bank == 2);
    r.e.stepBank(1);  // past the last: the first
    r.run(3.0f);
    CHECK(r.e.status().bank == 0);
    r.e.stepBank(-1);  // before the first: the last
    r.run(3.0f);
    CHECK(r.e.status().bank == 2);
    r.e.stepBank(-1);
    r.run(0.1f);
    r.e.stepBank(1);
    r.run(3.0f);
    CHECK(r.e.status().bank == 2);
    CHECK(r.run(1.0f, 0.3f) > 0.02f);
}

// Clock sets Clock Out's division; in 4 is Mult's tap, one step faster however long its gate
static void testClock() {
    Rig r;
    r.e.load(card);
    CHECK(r.waitLoaded());
    r.e.panel.division = 0;
    r.run(0.5f);
    r.clockEdges = 0;
    r.run(2.0f);
    int fast = r.clockEdges;
    r.e.panel.division = 3;
    r.run(0.5f);
    r.clockEdges = 0;
    r.run(2.0f);
    int beat = r.clockEdges;
    CHECK(r.e.status().division == 3);
    CHECK(beat > 0 && fast >= 6 * beat);  // 8 per beat against 1
    r.gate(E::I_MULT, 1.5f);  // a hold would divide after 1 s
    CHECK(r.e.status().division == 2);
    r.e.panel.division = 5;
    r.run(0.1f);
    CHECK(r.e.status().division == 5);
}

static void testPlayToggles() {
    Rig r;
    r.e.load(card);
    CHECK(r.waitLoaded());
    r.run(0.5f);
    r.trigger(E::I_PLAY, 0.5f);
    CHECK(r.e.status().stopped);
    CHECK(r.run(0.5f, 0.2f) < 1e-4f);
    r.e.panel.play = true;  // the on-screen button
    r.run(0.05f);
    r.e.panel.play = false;
    CHECK(r.run(1.0f, 0.3f) > 0.02f);
    CHECK(!r.e.status().stopped);
}

static void testVolumeWithoutTapTempo() {
    Rig r;
    r.e.load(card);
    CHECK(r.waitLoaded());
    r.run(0.5f);
    unsigned tempo = r.e.status().tempo;
    r.e.panel.volume = 0.0f;
    CHECK(r.run(0.5f, 0.2f) < 1e-3f);
    // edits 0.3 s apart would tap a tempo of 200 if each pressed Tap
    r.e.panel.filter = 0.2f;
    r.run(0.3f);
    r.e.panel.gating = 0.5f;
    r.run(0.3f);
    r.e.panel.volume = 0.5f;
    CHECK(r.run(1.0f, 0.3f) > 0.02f);
    CHECK(r.e.status().tempo == tempo);
}

// 2 PPQN at 150 BPM on In 1 for `seconds`: a 5 ms pulse every 0.2 s
static void clock150(Rig& r, float seconds) {
    int period = int(0.2f * SR), width = int(0.005f * SR), t = 0;
    for (int b = 0; b < int(seconds * SR) / r.block; b++) {
        for (int i = 0; i < r.block; i++, t++) r.in[E::I_CLOCK][size_t(i)] = (t % period) < width ? 1.0f : 0.0f;
        r.run(float(r.block) / SR);
    }
}

static void testExternalClock() {
    Rig r;
    r.e.load(card);
    CHECK(r.waitLoaded());
    clock150(r, 4.0f);
    unsigned tempo = r.e.status().tempo;
    CHECK(tempo >= 147 && tempo <= 153);
}

// Tempo sets the tempo when it changes, as Tap does; with a clock at In 1, the clock's stays
static void testTempo() {
    Rig r;
    r.e.load(card);
    CHECK(r.waitLoaded());
    r.run(0.5f);
    CHECK(r.e.status().tempo == 170);  // the card's: Tempo's default (120) is not applied
    r.e.panel.tempo = 90;
    r.run(0.1f);
    CHECK(r.e.status().tempo == 90);
    int edges = r.clockEdges;
    r.run(4.0f);
    edges = r.clockEdges - edges;
    CHECK(edges >= 11 && edges <= 13);  // 2 pulses a beat at 90 BPM: 12 in 4 s
    clock150(r, 4.0f);
    r.e.panel.tempo = 60;
    clock150(r, 1.0f);
    unsigned tempo = r.e.status().tempo;
    CHECK(tempo >= 147 && tempo <= 153);
}

static void testRestore() {
    Rig r;
    Saved s;
    s.tempo = 150;
    s.bank = 1;
    s.triggerMode = 2;
    r.e.restore(s);
    r.e.load(card, s.bank);
    CHECK(r.waitLoaded());
    r.run(0.1f);
    Saved got = r.e.saved();
    CHECK(got.tempo == 150 && got.bank == 1 && got.triggerMode == 2);
    // restored playing: the first Play stops
    r.run(1.0f);
    r.trigger(E::I_PLAY, 0.5f);
    CHECK(r.e.status().stopped);
    CHECK(r.run(0.3f, 0.2f) < 1e-4f);
}

static void testBlockSizesAndRates() {
    for (float rate : { 44100.0f, 48000.0f, 96000.0f }) {
        for (int block : { 1, 64, 441, 512 }) {
            Rig r(block, rate);
            r.e.load(card);
            CHECK(r.waitLoaded());
            bool finite = true;
            CHECK(r.run(1.0f, 0.3f, &finite) > 0.01f);
            CHECK(finite);
        }
    }
}

static int bench(int argc, char** argv) {
    if (argc < 3) return 2;
    Rig r(128);
    r.e.load(argv[2]);
    if (!r.waitLoaded(30.0f)) {
        std::fprintf(stderr, "no card at %s: %s\n", argv[2], r.e.message().c_str());
        return 1;
    }
    r.e.panel.knobs[Panel::K_BREAK] = 1.0f;  // effects as likely as they get
    float level = r.run(2.0f, 1.0f);
    Status st = r.e.status();
    std::printf("%s: bank %u sample %u, %u bpm, level %.3f; %s\n", r.e.folder().c_str(), st.bank + 1, st.slot + 1,
                st.tempo, level, r.e.message().c_str());
    std::vector<const float*> ip;
    std::vector<float*> op;
    for (auto& v : r.in) ip.push_back(v.data());
    for (auto& v : r.out) op.push_back(v.data());
    const int blocks = int(20.0f * SR) / r.block;
    std::vector<double> us(static_cast<size_t>(blocks));
    for (int b = 0; b < blocks; b++) {
        auto t0 = std::chrono::steady_clock::now();
        r.e.process(ip.data(), op.data(), r.block);
        us[size_t(b)] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    }
    double sum = 0;
    for (double u : us) sum += u;
    std::sort(us.begin(), us.end());
    double budget = 1e6 * r.block / SR;
    long over = std::count_if(us.begin(), us.end(), [](double u) { return u > 1000.0; });
    std::printf("128-sample block: mean %.1f us (%.1f%%), p99 %.1f us, max %.1f us, %ld of %d over 1 ms; budget %.0f us\n",
                sum / blocks, 100.0 * sum / blocks / budget, us[size_t(blocks * 99 / 100)], us.back(), over, blocks,
                budget);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string a = argv[1];
    if (a == "bench") return bench(argc, argv);
    testGolden();
    if (a != "golden") {
        card = a;
        testSilentWithoutCard();
        testResolve();
        testPlays();
        testBankTrigger();
        testStepBank();
        testClock();
        testPlayToggles();
        testVolumeWithoutTapTempo();
        testExternalClock();
        testTempo();
        testRestore();
        testBlockSizesAndRates();
    }
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    else std::printf("all passed\n");
    return failures ? 1 : 0;
}
