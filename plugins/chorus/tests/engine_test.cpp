// Native test of FaustEngine with the chorus kernel; built and run by test_chorus.py.

#include <cmath>
#include <cstdio>
#include <vector>

#include "ChorusKernel.h"
#include "engine/FaustEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

using Engine = ssp::faust::FaustEngine<ssp::faust::chorus::mydsp>;

static constexpr int BLOCK = 128;
// the kernel sorts its controls by label
enum { DELAY, DEPTH, MIX, RATE, CONTROLS };
static constexpr int NIN = 2 + CONTROLS;

struct Rig {
    Engine e;
    std::vector<float> in[NIN], out[2];
    const float* ip[NIN];
    float* op[2];
    float sr;

    explicit Rig(float rate = 48000.0f) : sr(rate) {
        for (int c = 0; c < NIN; c++) in[c].assign(BLOCK, 0.0f), ip[c] = in[c].data();
        for (int c = 0; c < 2; c++) out[c].assign(BLOCK, 0.0f), op[c] = out[c].data();
        e.prepare(sr, BLOCK);
    }
    void cv(int control, float v) {
        auto& b = in[2 + control];
        std::fill(b.begin(), b.end(), v);
    }
    // runs `blocks` blocks, returning output channel `ch`; `fill(block, channel)` sets the audio inputs
    template <typename Fill>
    std::vector<float> run(int blocks, int ch, Fill fill) {
        std::vector<float> rec;
        for (int b = 0; b < blocks; b++) {
            fill(b, in[0]);
            fill(b, in[1]);
            e.process(ip, op, BLOCK);
            rec.insert(rec.end(), out[ch].begin(), out[ch].end());
        }
        return rec;
    }
    std::vector<float> run(int blocks, int ch, float level = 0.0f) {
        return run(blocks, ch, [&](int, std::vector<float>& b) { std::fill(b.begin(), b.end(), level); });
    }
};

static void testSpec() {
    Rig r;
    auto& s = r.e.spec();
    CHECK(s.ins == 2 && s.outs == 2);
    CHECK(s.controls.size() == CONTROLS);
    const char* labels[] = { "delay", "depth", "mix", "rate" };
    const float inits[] = { 0.4f, 0.5f, 0.5f, 0.3f };
    for (int i = 0; i < CONTROLS && i < int(s.controls.size()); i++) {
        CHECK(s.controls[size_t(i)].label == labels[i]);
        CHECK(s.controls[size_t(i)].init == inits[i]);
        CHECK(s.controls[size_t(i)].lo == 0.0f && s.controls[size_t(i)].hi == 1.0f);
    }
}

// mix 0 passes the input through unchanged
static void testDry() {
    Rig r;
    r.e.set(MIX, 0.0f);
    auto v = r.run(20, 1, 0.3f);
    for (float x : v) CHECK(x == 0.3f);
}

// mix 1, delay 0, depth 0: an impulse comes back 5 ms later
static void testDelay() {
    for (float rate : { 48000.0f, 44100.0f }) {
        Rig r(rate);
        r.e.set(MIX, 1.0f);
        r.e.set(DELAY, 0.0f);
        r.e.set(DEPTH, 0.0f);
        r.run(int(rate / BLOCK), 0);  // the controls' smoothing settles
        auto v = r.run(8, 0, [](int b, std::vector<float>& x) {
            std::fill(x.begin(), x.end(), 0.0f);
            if (b == 0) x[0] = 1.0f;
        });
        // 220.5 samples at 44.1 kHz: the fractional delay splits the impulse over two samples
        double sum = 0.0, at = 0.0;
        for (size_t i = 0; i < v.size(); i++) sum += v[i], at += double(i) * v[i];
        CHECK(std::fabs(sum - 1.0) < 1e-3);
        CHECK(std::fabs(at / sum - 0.005 * rate) < 0.01);
    }
}

// a CV adds to its control, clamped to the range: 1.0 spans it
static void testCv() {
    auto noise = [](int b, std::vector<float>& x) {
        unsigned s = 12345u + unsigned(b);
        for (auto& v : x) s = s * 1664525u + 1013904223u, v = float(s >> 8) / float(1 << 24) - 0.5f;
    };
    Rig a, b, c;
    a.e.set(MIX, 1.0f);
    b.e.set(MIX, 0.0f);
    b.cv(MIX, 1.0f);
    c.e.set(MIX, 0.8f);
    c.cv(MIX, 1.0f);
    auto va = a.run(100, 0, noise);
    CHECK(va == b.run(100, 0, noise));
    CHECK(va == c.run(100, 0, noise));
    // defaults on noise: finite and bounded
    Rig d;
    bool finite = true;
    float peak = 0.0f;
    for (float x : d.run(400, 1, noise)) finite = finite && std::isfinite(x), peak = std::max(peak, std::fabs(x));
    CHECK(finite);
    CHECK(peak > 0.1f && peak <= 1.0f);
}

int main() {
    testSpec();
    testDry();
    testDelay();
    testCv();
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
