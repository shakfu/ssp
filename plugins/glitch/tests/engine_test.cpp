// Native test of glitch::GlitchEngine; built and run by test_glitch.py.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "GlitchEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

using namespace glitch;

static constexpr int BLOCK = 128;
static constexpr int NIN = GlitchEngine::DECKS * GlitchEngine::I_PER_DECK;

struct Rig {
    GlitchEngine e;
    std::vector<float> in[NIN], out[GlitchEngine::O_MAX];
    const float* ip[NIN];
    float* op[GlitchEngine::O_MAX];
    float sr;

    explicit Rig(float rate = 48000.0f) : sr(rate) {
        for (int c = 0; c < NIN; c++) in[c].assign(BLOCK, 0.0f), ip[c] = in[c].data();
        for (int c = 0; c < GlitchEngine::O_MAX; c++) out[c].assign(BLOCK, 0.0f), op[c] = out[c].data();
        e.prepare(sr, BLOCK);
    }
    void cv(int deck, int input, float v) {
        auto& b = in[deck * GlitchEngine::I_PER_DECK + input];
        std::fill(b.begin(), b.end(), v);
    }
    // runs `secs`, returning output channel `ch`
    std::vector<float> run(float secs, int ch) {
        std::vector<float> rec;
        for (int b = 0; b < int(secs * sr / BLOCK); b++) {
            e.process(ip, op, BLOCK);
            rec.insert(rec.end(), out[ch].begin(), out[ch].end());
        }
        return rec;
    }
};

static double meanAbs(const std::vector<float>& v) {
    double a = 0.0;
    for (float x : v) a += std::fabs(x);
    return v.empty() ? 0.0 : a / double(v.size());
}

static int crossings(const std::vector<float>& v) {
    int n = 0;
    for (size_t i = 1; i < v.size(); i++) n += (v[i - 1] < 0.0f) != (v[i] < 0.0f);
    return n;
}

static void testEveryAlgoSounds() {
    for (float rate : { 48000.0f, 44100.0f }) {
        for (int a = 0; a < kAlgoCount; a++) {
            Rig r(rate);
            r.e.controls(0).algo = a;
            r.e.controls(1).level = 0.0f;
            auto v = r.run(1.0f, GlitchEngine::O_A);
            bool finite = true;
            float peak = 0.0f;
            for (float x : v) finite = finite && std::isfinite(x), peak = std::max(peak, std::fabs(x));
            CHECK(finite);
            CHECK(meanAbs(v) > 1e-4);
            CHECK(peak <= 1.0f);  // the voice emits 10-bit values over 512
            CHECK(r.e.algo(0) == a);
        }
    }
}

static void testOutputsAndRoute() {
    Rig r;
    r.e.controls(1).level = 0.0f;
    r.e.setRoute(Route::Split);
    r.run(0.2f, 0);
    CHECK(meanAbs(r.out[GlitchEngine::O_B]) == 0.0);
    CHECK(meanAbs(r.out[GlitchEngine::O_R]) == 0.0);  // split: A is left only
    r.e.controls(0).algo = 3;
    auto left = r.run(0.2f, GlitchEngine::O_L);
    CHECK(meanAbs(left) > 1e-3);
    // tone 0 holds the low-pass shut
    Rig q;
    q.e.controls(0).tone = q.e.controls(1).tone = 0.0f;
    CHECK(meanAbs(q.run(0.2f, GlitchEngine::O_L)) == 0.0);
}

// 1 V on the pitch input raises the pitch an octave: ring mod of two triangles crosses zero twice as often
static void testPitchCv() {
    auto count = [](float volts) {
        Rig r;
        r.e.controls(0).algo = int(Algo::RingMod);
        r.e.controls(0).p1 = 0.3f;
        r.e.controls(0).p2 = 0.6f;
        r.cv(0, GlitchEngine::I_PITCH, volts * ssp::engine::CV_PER_VOLT);
        return crossings(r.run(1.0f, GlitchEngine::O_A));
    };
    int base = count(0.0f), up = count(1.0f);
    CHECK(base > 100);
    CHECK(std::fabs(double(up) / base - 2.0) < 0.05);
    // the P1 input adds to P1: 5 V spans the range
    Rig r, s;
    r.e.controls(0).algo = s.e.controls(0).algo = int(Algo::RingMod);
    r.e.controls(0).p1 = 1.0f;
    s.e.controls(0).p1 = 0.0f;
    s.cv(0, GlitchEngine::I_P1, 1.0f);
    CHECK(r.run(0.5f, GlitchEngine::O_A) == s.run(0.5f, GlitchEngine::O_A));
}

// a rising edge on regen refills the buffer; holding it high does not refill again
static void testRegen() {
    Rig a, b;
    a.run(0.1f, 0);
    b.run(0.1f, 0);
    b.e.controls(0).regen = true;
    b.run(0.1f, 0);
    a.run(0.1f, 0);
    CHECK(a.run(0.5f, GlitchEngine::O_A) != b.run(0.5f, GlitchEngine::O_A));
    // same seeds, same controls: the same output
    Rig c, d;
    CHECK(c.run(0.5f, GlitchEngine::O_L) == d.run(0.5f, GlitchEngine::O_L));
}

// the UI thread reads the algorithm while the audio thread changes it
static void testThreads() {
    Rig r;
    std::atomic<bool> done{ false };
    std::thread ui([&] {
        int sum = 0;
        while (!done.load()) sum += r.e.algo(0) + r.e.algo(1);
        (void)sum;
    });
    for (int i = 0; i < 200; i++) {
        r.e.controls(i & 1).algo = i % kAlgoCount;
        r.run(float(BLOCK) / r.sr, 0);
    }
    done = true;
    ui.join();
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "threads") == 0) {
        testThreads();
    } else {
        testEveryAlgoSounds();
        testOutputsAndRoute();
        testPitchCv();
        testRegen();
    }
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
