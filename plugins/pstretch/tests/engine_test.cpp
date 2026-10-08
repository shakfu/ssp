// Native test of pstretch::PstretchEngine; built and run by test_pstretch.py.
// argv[1] is an empty scratch directory.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "PstretchEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

using namespace pstretch;
using E = PstretchEngine;

static constexpr float SR = 48000.0f;
static constexpr int BLOCK = 128;
static constexpr float PI = 3.14159265f;

static void writeWav(const std::string& path, int frames, std::function<float(int)> value) {
    FILE* f = std::fopen(path.c_str(), "wb");
    auto put16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    auto put32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    put32(uint32_t(36 + frames * 2));
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(16), put16(1), put16(1), put32(48000), put32(96000), put16(2), put16(16);
    std::fwrite("data", 1, 4, f);
    put32(uint32_t(frames * 2));
    for (int i = 0; i < frames; i++) put16(uint16_t(int16_t(std::lround(value(i) * 32767.0f))));
    std::fclose(f);
}

struct Rig {
    E e;
    std::vector<float> in[E::I_MAX], out[E::O_MAX];
    const float* ip[E::I_MAX];
    float* op[E::O_MAX];
    int64_t t = 0;  // samples processed
    std::function<float(int64_t)> input = [](int64_t) { return 0.0f; };
    bool worker = true;  // run idle() between blocks, on this thread

    Rig() {
        for (int c = 0; c < E::I_MAX; c++) in[c].assign(BLOCK, 0.0f), ip[c] = in[c].data();
        for (int c = 0; c < E::O_MAX; c++) out[c].assign(BLOCK, 0.0f), op[c] = out[c].data();
        e.prepare(SR, BLOCK);
    }
    void cv(int ch, float v) { std::fill(in[ch].begin(), in[ch].end(), v); }
    // runs `secs`; returns output `ch` over the run
    std::vector<float> run(float secs, int ch = E::O_A) {
        std::vector<float> rec;
        for (int b = 0; b < int(secs * SR / BLOCK); b++) {
            for (int i = 0; i < BLOCK; i++) in[E::I_IN][size_t(i)] = input(t + i);
            e.process(ip, op, BLOCK);
            if (worker && b % 16 == 0) e.idle();
            t += BLOCK;
            rec.insert(rec.end(), out[ch].begin(), out[ch].end());
        }
        return rec;
    }
};

static double rms(const std::vector<float>& v, size_t from = 0) {
    double a = 0.0;
    for (size_t i = from; i < v.size(); i++) a += double(v[i]) * v[i];
    return v.size() > from ? std::sqrt(a / double(v.size() - from)) : 0.0;
}

static double tailRms(const std::vector<float>& v, float secs) {
    return rms(v, v.size() - size_t(secs * SR));
}

static int crossings(const std::vector<float>& v, size_t from) {
    int n = 0;
    for (size_t i = from + 1; i < v.size(); i++) n += (v[i - 1] < 0.0f) != (v[i] < 0.0f);
    return n;
}

static std::function<float(int64_t)> sine(float hz, float until = 1e9f) {
    return [hz, until](int64_t t) { return float(t) < until * SR ? 0.5f * std::sin(2.0f * PI * hz * float(t) / SR) : 0.0f; };
}

static void testLive() {
    Rig r;
    r.e.controls(0).stretch = 0.0f;  // 1x
    r.e.controls(0).diffuse = 0.0f;  // clean resynthesis
    r.input = sine(440.0f);
    auto v = r.run(2.0f);
    bool finite = true;
    for (float x : v) finite = finite && std::isfinite(x);
    CHECK(finite);
    double out = tailRms(v, 1.0f);
    CHECK(out > 0.15 && out < 0.6);  // the input's rms is 0.35
    // a wash keeps the level
    Rig w;
    w.input = sine(440.0f);
    double washed = tailRms(w.run(3.0f), 1.0f);
    CHECK(washed > 0.1 && washed < 0.8);
    // mix 0 is the dry input, through an open tone filter
    Rig d;
    d.e.controls(0).mix = 0.0f;
    d.input = sine(440.0f);
    auto dry = d.run(0.1f);
    CHECK(std::fabs(dry.back() - d.input(d.t - 1)) < 1e-6f);
}

// With the input gone, a live deck at 1x falls silent; a frozen one holds its grain.
static void testFreezeAndGate() {
    Rig r;
    r.e.controls(0).stretch = 0.0f;
    r.input = sine(440.0f, 1.0f);
    CHECK(tailRms(r.run(2.5f), 0.5f) < 1e-3);

    Rig f;
    f.e.controls(0).stretch = 0.0f;
    f.input = sine(440.0f, 1.0f);
    f.run(0.9f);
    f.e.controls(0).freeze = true;  // the button's rising edge toggles freeze
    f.run(0.01f);
    CHECK(f.e.info(0).frozen);
    f.e.controls(0).freeze = false;
    CHECK(tailRms(f.run(1.6f), 0.5f) > 0.05);
    // the gate input toggles it back
    f.cv(E::I_GATE, 1.0f);
    f.run(0.01f);
    f.cv(E::I_GATE, 0.0f);
    CHECK(!f.e.info(0).frozen);
    CHECK(tailRms(f.run(1.5f), 0.5f) < 1e-3);
}

// Capture loops the recent input; Grab re-captures, here a silence.
static void testCapture() {
    Rig r;
    r.e.controls(0).stretch = 0.0f;
    r.input = sine(440.0f, 2.0f);
    r.run(1.9f);
    r.e.controls(0).source = Source::Capture;
    CHECK(tailRms(r.run(3.0f), 1.0f) > 0.05);
    r.e.controls(0).grab = true;
    CHECK(tailRms(r.run(2.0f), 0.5f) < 1e-3);
    // the gate re-captures too
    Rig g;
    g.e.controls(0).stretch = 0.0f;
    g.e.controls(0).source = Source::Capture;
    g.input = sine(440.0f);
    g.run(1.0f);  // captured nothing at the start: silent
    g.cv(E::I_GATE, 1.0f);
    CHECK(tailRms(g.run(2.0f), 0.5f) > 0.05);
}

// One octave up at 1x and no diffusion doubles the zero crossings.
static void testPitch() {
    auto count = [](float pitch, float volts) {
        Rig r;
        r.e.controls(0).stretch = 0.0f;
        r.e.controls(0).diffuse = 0.0f;
        r.e.controls(0).pitch = pitch;
        r.cv(E::I_PITCH, volts * ssp::engine::CV_PER_VOLT);
        r.input = sine(300.0f);
        return crossings(r.run(3.0f), size_t(1.0f * SR));
    };
    int base = count(0.5f, 0.0f);
    CHECK(std::fabs(base / (2.0 * 2.0 * 300.0) - 1.0) < 0.05);  // 2 s at 300 Hz
    CHECK(std::fabs(double(count(1.0f, 0.0f)) / base - 2.0) < 0.1);
    CHECK(std::fabs(double(count(0.5f, -1.0f)) / base - 0.5) < 0.05);  // V/oct
}

// Pitched up at 1x, a grain reads 2 windows of input. Once the input stops, it must not reach
// past the write head into the ring's 5-second-old contents.
static void testPitchedGrainStaysInWrittenInput() {
    Rig r;
    r.e.controls(0).stretch = 0.0f;
    r.e.controls(0).diffuse = 0.0f;
    r.e.controls(0).pitch = 1.0f;
    r.input = sine(440.0f, 6.0f);
    CHECK(tailRms(r.run(7.5f), 0.3f) < 1e-3);
}

static void testFile(const std::string& dir) {
    std::string root = dir + "/clips";
    mkdir(root.c_str(), 0755);
    writeWav(root + "/1.wav", 48000 * 3, [](int i) { return 0.5f * std::sin(2.0f * PI * 440.0f * float(i) / SR); });
    writeWav(root + "/2.wav", 48000, [](int) { return 0.0f; });
    Rig r;
    r.e.setRoot(root);
    r.e.controls(0).stretch = 0.0f;
    r.e.controls(0).source = Source::File;
    CHECK(tailRms(r.run(2.0f), 0.5f) > 0.1);
    CHECK(r.e.info(0).clips == 2);
    CHECK(r.e.info(0).clip == 0 && r.e.info(0).clipName == "1.wav");
    r.e.controls(0).clip = 0.99f;  // the silent clip, after the settle
    auto v = r.run(2.0f);
    CHECK(r.e.info(0).clip == 1);
    CHECK(tailRms(v, 0.5f) < 1e-3);
    // a stretched file plays on past its length: it loops
    Rig s;
    s.e.setRoot(root);
    s.e.controls(0).source = Source::File;
    s.e.controls(0).stretch = 0.0f;
    CHECK(tailRms(s.run(5.0f), 0.5f) > 0.1);
    // no clips: silence, no crash
    Rig n;
    n.e.setRoot(dir + "/none");
    n.e.controls(0).source = Source::File;
    CHECK(rms(n.run(0.5f)) == 0.0);
}

// The work budget keeps each FIFO ahead of playback: a clean 1x sine has no runs of silence.
static void testBudgetKeepsUp() {
    for (int w : E::WINDOWS) {
        for (int block : { 16, 128, 256 }) {
            E e;
            e.prepare(SR, block);
            e.setWindow(w);
            e.idle();
            for (int d = 0; d < E::DECKS; d++) e.controls(d).stretch = e.controls(d).diffuse = 0.0f;
            std::vector<float> in[E::I_MAX], out[E::O_MAX];
            const float* ip[E::I_MAX];
            float* op[E::O_MAX];
            for (int c = 0; c < E::I_MAX; c++) in[c].assign(size_t(block), 0.0f), ip[c] = in[c].data();
            for (int c = 0; c < E::O_MAX; c++) out[c].assign(size_t(block), 0.0f), op[c] = out[c].data();
            auto src = sine(300.0f);
            int64_t t = 0;
            int run[E::DECKS] = {}, longest = 0;
            for (int b = 0; b < int(4.0f * SR) / block; b++) {
                for (int i = 0; i < block; i++) in[E::I_IN][size_t(i)] = in[E::I_PER_DECK + E::I_IN][size_t(i)] = src(t + i);
                e.process(ip, op, block);
                t += block;
                if (t < int64_t(2.0f * SR)) continue;  // past the start-up latency
                for (int d = 0; d < E::DECKS; d++)
                    for (int i = 0; i < block; i++) {
                        run[d] = out[E::O_A + d][size_t(i)] == 0.0f ? run[d] + 1 : 0;
                        longest = std::max(longest, run[d]);
                    }
            }
            if (longest >= 4) std::fprintf(stderr, "window %d block %d: %d zeros\n", w, block, longest);
            CHECK(longest < 4);
        }
    }
}

static void testWindowAndOutputs() {
    Rig r;
    r.input = sine(440.0f);
    r.e.controls(0).stretch = 0.0f;
    r.run(0.5f);
    CHECK(r.e.info(0).window == 8192);
    r.e.setWindow(32768);
    auto v = r.run(3.0f);
    CHECK(r.e.info(0).window == 32768);
    CHECK(tailRms(v, 1.0f) > 0.1);

    // LFO outputs stay in 0..1; at rate 1 (7.7 Hz) the gate pulses about 7.7 times a second
    Rig l;
    l.e.controls(0).modRate = 1.0f;
    auto lfo = l.run(2.0f, E::O_LFO_A);
    float lo = 1.0f, hi = 0.0f;
    for (float x : lfo) lo = std::min(lo, x), hi = std::max(hi, x);
    CHECK(lo >= 0.0f && hi <= 1.0f && hi - lo > 0.9f);
    Rig g;
    g.e.controls(0).modRate = 1.0f;
    auto gate = g.run(2.0f, E::O_GATE_A);
    int pulses = 0;
    for (size_t i = 1; i < gate.size(); i++) pulses += gate[i] > 0.5f && gate[i - 1] < 0.5f;
    CHECK(pulses >= 14 && pulses <= 16);
}

// The worker runs on its own thread while the audio thread switches sources, clips and windows,
// and a UI thread reads the status.
static void testThreads(const std::string& dir) {
    std::string root = dir + "/tclips";
    mkdir(root.c_str(), 0755);
    for (int k = 0; k < 3; k++)
        writeWav(root + "/" + std::to_string(k) + ".wav", 48000, [k](int i) { return 0.1f * float(k) * std::sin(float(i)); });
    Rig r;
    r.worker = false;
    r.e.setRoot(root);
    std::atomic<bool> done{ false };
    std::thread worker([&] {
        while (!done.load()) {
            r.e.idle();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    std::thread ui([&] {
        size_t sum = 0;
        while (!done.load())
            for (int d = 0; d < E::DECKS; d++) sum += r.e.info(d).clipName.size() + size_t(r.e.info(d).window);
        (void)sum;
    });
    r.input = sine(220.0f);
    for (int i = 0; i < 60; i++) {
        auto& c = r.e.controls(i & 1);
        c.source = Source(i % 3);
        c.clip = float(i % 4) / 4.0f;
        c.position = float(i % 5) / 5.0f;
        c.freeze = i % 7 == 0;
        if (i % 20 == 0) r.e.setWindow(E::WINDOWS[(i / 20) % 4]);
        if (i == 30) r.e.setRoot(root);
        r.run(0.05f);
    }
    done = true;
    worker.join();
    ui.join();
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string dir = argv[1];
    if (argc > 2 && std::strcmp(argv[2], "threads") == 0) {
        testThreads(dir);
    } else {
        testLive();
        testFreezeAndGate();
        testCapture();
        testPitch();
        testPitchedGrainStaysInWrittenInput();
        testFile(dir);
        testBudgetKeepsUp();
        testWindowAndOutputs();
    }
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
