// Native test of ck::ChuckEngine against the host build of ChucK; built and run by test_chuck.py.
// argv[1] is an empty scratch directory; argv[2] == "threads" runs the threaded test only.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ChuckEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

using ck::ChuckEngine;
static constexpr int CH = ChuckEngine::CHANNELS;
static constexpr int BLOCK = 128;

struct Rig {
    ChuckEngine e;
    std::vector<float> in[CH], out[CH];
    const float* ip[CH];
    float* op[CH];

    explicit Rig(float sr = 48000.0f) {
        for (int c = 0; c < CH; c++) {
            in[c].assign(BLOCK, 0.0f);
            out[c].assign(BLOCK, 0.0f);
            ip[c] = in[c].data();
            op[c] = out[c].data();
        }
        e.prepare(sr, BLOCK);
    }
    // mean |x| of channel c over `blocks` blocks, with idle() between blocks as the worker would
    double run(int blocks, int c = 0) {
        double acc = 0.0;
        for (int b = 0; b < blocks; b++) {
            e.idle();
            e.process(ip, op, BLOCK);
            for (float v : out[c]) acc += std::fabs(v);
        }
        return acc / (blocks * BLOCK);
    }
    void load(const std::string& path) {
        e.load(path);
        e.idle();
    }
};

static std::string write(const std::string& dir, const char* name, const std::string& code) {
    std::string path = dir + "/" + name;
    std::ofstream(path) << "\xEF\xBB\xBF" << code;
    return path;
}

// input 3 to output 5, and global p4 as a level on output 8
static const char* PASS = R"(global float p4;
adc.chan(2) => dac.chan(4);
Step s => dac.chan(7);
while (true) { p4 => s.next; 1::samp => now; }
)";

static void testEngine(const std::string& dir) {
    Rig r;
    CHECK(r.e.error().empty() && r.e.path().empty());
    double quiet = r.run(40);
    CHECK(quiet > 0.01);  // the built-in drone sounds unpatched
    r.e.setParam(1, 1.0f);
    double loud = r.run(40);
    CHECK(loud > quiet * 2.0);  // p2 is its level
    CHECK(r.run(1, 2) == 0.0);  // outputs 3-8 silent

    // loaded twice: links between the VM's own UGens (adc => dac) must not accumulate
    std::string pass = write(dir, "pass.ck", PASS);
    r.load(pass);
    r.load(pass);
    CHECK(r.e.error().empty() && r.e.path() == pass);
    r.run(2);
    CHECK(r.run(4, 0) == 0.0);  // the drone's shreds are gone
    for (int f = 0; f < BLOCK; f++) r.in[2][size_t(f)] = float(f) * 1e-3f;
    r.run(1);
    bool same = true;
    for (int f = 0; f < BLOCK; f++)
        if (std::fabs(r.out[4][size_t(f)] - float(f) * 1e-3f) > 1e-6f) same = false;
    CHECK(same);
    r.e.setParam(3, 0.25f);
    r.run(2);
    NEAR(r.out[7][BLOCK - 1], 0.25f, 1e-6);

    // a program that fails to compile leaves the previous one running
    std::string bad = write(dir, "bad.ck", "SinOsc s => nosuchthing;\n");
    r.load(bad);
    CHECK(r.e.error().find("bad.ck:1:") == 0);
    r.e.setParam(3, 0.5f);
    r.run(2);
    NEAR(r.out[7][BLOCK - 1], 0.5f, 1e-6);

    r.load(dir + "/missing.ck");
    CHECK(r.e.error().find("cannot read") == 0);

    // me.dir() is the program's directory: SndBuf reads a file next to it
    {
        std::ofstream w(dir + "/half.wav", std::ios::binary);
        auto u32 = [&](uint32_t v) { w.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { w.write(reinterpret_cast<const char*>(&v), 2); };
        w.write("RIFF", 4); u32(36 + 2 * 4800); w.write("WAVEfmt ", 8); u32(16); u16(1); u16(1); u32(48000);
        u32(96000); u16(2); u16(16); w.write("data", 4); u32(2 * 4800);
        for (int k = 0; k < 4800; k++) u16(16384);
    }
    std::string buf = write(dir, "buf.ck", "SndBuf b => dac.chan(0);\nme.dir() + \"half.wav\" => b.read;\n"
                                           "1 => b.loop;\nwhile (true) 1::second => now;\n");
    r.load(buf);
    CHECK(r.e.error().empty());
    r.run(2);
    NEAR(r.out[0][BLOCK - 1], 0.5f, 1e-4);

    // the host's rate
    Rig r44(44100.0f);
    r44.load(write(dir, "rate.ck", "Step s => dac.chan(0);\nsecond / samp / 100000 => s.next;\n"
                                   "while (true) 1::second => now;\n"));
    CHECK(r44.e.error().empty());
    r44.run(2);
    NEAR(r44.out[0][BLOCK - 1], 0.441f, 1e-6);

    // a declared range reaches the program scaled
    r.load(write(dir, "ranged.ck", "// @p1 x 10 20\nglobal float p1;\nStep s => dac.chan(0);\n"
                                   "while (true) { p1 / 100 => s.next; 1::ms => now; }\n"));
    CHECK(r.e.error().empty() && r.e.spec(0).label == "x");
    r.e.setParam(0, 0.5f);
    r.run(4);
    NEAR(r.out[0][BLOCK - 1], 0.15f, 1e-6);

    // back to the built-in
    r.load("");
    CHECK(r.e.error().empty() && r.e.path().empty());
    CHECK(r.run(20) > 0.01);
}

// audio and worker threads together while programs swap; run under ThreadSanitizer
static void testThreads(const std::string& dir) {
    Rig r;
    std::string pass = write(dir, "pass.ck", PASS);
    std::atomic<bool> quit{ false };
    std::thread worker([&] {
        for (int k = 0; !quit; k++) {
            if (k % 20 == 0) r.e.load(k % 40 ? pass : std::string());
            r.e.idle();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    for (int b = 0; b < 3000; b++) {
        r.e.setParam(b % 8, float(b % 100) / 100.0f);
        r.e.process(r.ip, r.op, BLOCK);
    }
    quit = true;
    worker.join();
}

// One example program, with a 220 Hz sine on inputs 1 and 2, every control at 0.5:
// prints each output's peak, or the compile error on stderr.
static int runExample(const std::string& path) {
    Rig r;
    r.load(path);
    if (!r.e.error().empty()) {
        std::fprintf(stderr, "%s\n", r.e.error().c_str());
        return 1;
    }
    for (int p = 0; p < CH; p++) r.e.setParam(p, 0.5f);
    float peak[CH] = {};
    for (int b = 0, t = 0; b < 400; b++, t += BLOCK) {
        for (int f = 0; f < BLOCK; f++) r.in[0][size_t(f)] = r.in[1][size_t(f)] = 0.5f * std::sin(6.2831853f * 220.0f * float(t + f) / 48000.0f);
        r.e.idle();
        r.e.process(r.ip, r.op, BLOCK);
        for (int c = 0; c < CH; c++)
            for (float v : r.out[c]) peak[c] = std::max(peak[c], std::fabs(v));
    }
    for (int c = 0; c < CH; c++) std::printf("peak %d %g\n", c, peak[c]);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    if (argc > 2 && std::string(argv[1]) == "example") return runExample(argv[2]);
    if (argc > 2 && std::string(argv[2]) == "threads") testThreads(argv[1]);
    else testEngine(argv[1]);
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
