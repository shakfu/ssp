// Native test of csnd::CsoundEngine against the host build of Csound; built and run by test_csound.py.
// argv[1] is an empty scratch directory; argv[2] == "threads" runs the threaded test only.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "CsoundEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

using csnd::CsoundEngine;
static constexpr int CH = CsoundEngine::CHANNELS;
static constexpr int BLOCK = 128;

struct Rig {
    CsoundEngine e;
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
    // mean |x| of channel c over `blocks` blocks
    double run(int blocks, int c = 0) {
        double acc = 0.0;
        for (int b = 0; b < blocks; b++) {
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

static std::string write(const std::string& dir, const char* name, const std::string& orc) {
    std::string path = dir + "/" + name;
    std::ofstream(path) << "\xEF\xBB\xBF<CsoundSynthesizer>\r\n<CsInstruments>\r\n" << orc
                        << "\r\n</CsInstruments>\r\n</CsoundSynthesizer>\r\n";
    return path;
}

// 8 channels, ksmps 10 (not a divisor of the block), default 0dbfs of 32768: input 3 to output 5
static const char* PASS = R"(
ksmps = 10
nchnls = 8
nchnls_i = 8
instr 1
  outch 5, inch(3)
endin
schedule 1, 0, -1)";

static void testEngine(const std::string& dir) {
    Rig r;
    CHECK(r.e.error().empty() && r.e.path().empty());
    double quiet = r.run(40);
    CHECK(quiet > 0.01);  // the built-in drone sounds unpatched
    r.e.setParam(1, 1.0f);
    double loud = r.run(40);
    CHECK(loud > quiet * 2.0);  // p2 is its level
    CHECK(r.run(1, 2) == 0.0);  // two-channel orchestra: outputs 3-8 silent

    std::string pass = write(dir, "pass.csd", PASS);
    r.load(pass);
    CHECK(r.e.error().empty() && r.e.path() == pass);
    // a ramp in on input 3 leaves output 5 one k-cycle late, unscaled by 0dbfs
    int t = 0;
    bool exact = true;
    for (int b = 0; b < 20; b++) {
        for (int f = 0; f < BLOCK; f++) r.in[2][size_t(f)] = float(t + f) * 1e-4f;
        r.e.process(r.ip, r.op, BLOCK);
        for (int f = 0; f < BLOCK; f++) {
            int src = t + f - 10;
            float want = src < 0 ? 0.0f : float(src) * 1e-4f;
            if (b > 0 && std::fabs(r.out[4][size_t(f)] - want) > 1e-5f) exact = false;
        }
        t += BLOCK;
    }
    CHECK(exact);

    // a program that fails to compile leaves the previous one running
    std::string bad = write(dir, "bad.csd", "instr 1\n  asig nosuchopcode 1\nendin\n");
    r.load(bad);
    CHECK(!r.e.error().empty());
    std::fprintf(stderr, "compile error reads: %s\n", r.e.error().c_str());
    CHECK(r.e.path() == bad);
    r.in[2].assign(BLOCK, 0.5f);
    r.run(2);
    NEAR(r.out[4][BLOCK - 1], 0.5f, 1e-6);

    r.load(dir + "/missing.csd");
    CHECK(r.e.error().find("cannot read") == 0);

    std::ofstream(dir + "/notcsd.csd") << "instr 1\nendin\n";
    r.load(dir + "/notcsd.csd");
    CHECK(r.e.error().find("not a .csd") == 0);

    // the host's rate overrides the orchestra's
    std::string rate = write(dir, "rate.csd", "sr = 48000\nksmps = 16\nnchnls = 1\n0dbfs = 1\n"
                                              "instr 1\n  out a(sr / 100000)\nendin\nschedule 1, 0, -1");
    Rig r44(44100.0f);
    r44.load(rate);
    CHECK(r44.e.error().empty());
    r44.run(2);
    NEAR(r44.out[0][BLOCK - 1], 0.441f, 1e-6);

    // MIDI: a held note sounds its key number; note off ends it
    std::string midi = write(dir, "midi.csd", "ksmps = 16\nnchnls = 1\n0dbfs = 1\nmassign 0, 2\n"
                                              "instr 2\n  out a(notnum() / 128)\nendin");
    r.load(midi);
    CHECK(r.e.error().empty());
    r.e.midi(0x90, 64, 100);
    r.run(2);
    NEAR(r.out[0][BLOCK - 1], 0.5f, 1e-6);
    r.e.midi(0x80, 64, 0);
    r.run(4);
    NEAR(r.out[0][BLOCK - 1], 0.0f, 1e-6);

    // GEN01 reads a sound file next to the .csd, through the static libsndfile; -1: not normalised
    std::string sub = dir + "/sub";
    std::system(("mkdir -p " + sub).c_str());
    {
        // 16-bit mono WAV, 4 frames of 0.5
        std::ofstream w(sub + "/half.wav", std::ios::binary);
        auto u32 = [&](uint32_t v) { w.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { w.write(reinterpret_cast<const char*>(&v), 2); };
        w.write("RIFF", 4); u32(36 + 8); w.write("WAVEfmt ", 8); u32(16); u16(1); u16(1); u32(48000);
        u32(96000); u16(2); u16(16); w.write("data", 4); u32(8);
        for (int k = 0; k < 4; k++) u16(16384);
    }
    std::string table = write(sub, "table.csd", "ksmps = 16\nnchnls = 1\n0dbfs = 1\n"
                                                "gi1 ftgen 1, 0, 0, -1, \"half.wav\", 0, 0, 0\n"
                                                "instr 1\n  out a(table:i(2, 1))\nendin\nschedule 1, 0, -1");
    r.load(table);
    CHECK(r.e.error().empty());
    r.run(2);
    NEAR(r.out[0][BLOCK - 1], 0.5f, 1e-4);

    // declared labels and ranges
    {
        auto sp = CsoundEngine::parseSpecs("; @p1 cutoff 20 20000 Hz log\n// @p2 mix\n  ;@p3 depth -1 1\n"
                                           "; @p4 bad 1 x\n@p5 notacomment\n; @p6 width 0 1 log\n; @p16 last 1 2\n"
                                           "; @p17 none\n; @p0 none\n; @p1x none\n; @p123 none\n");
        CHECK(sp[0].label == "cutoff" && sp[0].min == 20.0f && sp[0].max == 20000.0f && sp[0].unit == "Hz" && sp[0].log);
        CHECK(sp[1].label == "mix" && sp[1].min == 0.0f && sp[1].max == 1.0f && !sp[1].log);
        CHECK(sp[2].label == "depth" && sp[2].min == -1.0f && sp[2].max == 1.0f);
        CHECK(sp[3].label == "bad" && sp[3].min == 0.0f && sp[3].max == 1.0f);  // unreadable range: 0..1
        CHECK(sp[4].label.empty());                                             // not a comment line
        CHECK(sp[5].label == "width" && !sp[5].log);                            // log needs a positive range
        CHECK(sp[15].label == "last" && sp[15].max == 2.0f);                    // two digits
        int named = 0;
        for (auto& x : sp) named += !x.label.empty();
        CHECK(named == 6);                                                      // p0, p17, p1x, p123 ignored
        CHECK(sp[0].label == "cutoff");                                         // "@p1x" did not overwrite p1
        NEAR(sp[0].map(0.5f), 632.456f, 0.01);
        NEAR(sp[2].map(0.25f), -0.5f, 1e-6);
    }
    std::string ranged = write(dir, "ranged.csd", "; @p1 x 10 20\n; @p2 y 100 10000 Hz log\nksmps = 16\nnchnls = 2\n0dbfs = 1\n"
                                                  "massign 0, 0\ninstr 1\n  outs a(chnget:k(\"p1\")) / 100, a(chnget:k(\"p2\")) / 100000\n"
                                                  "endin\nschedule 1, 0, -1");
    r.load(ranged);
    CHECK(r.e.error().empty() && r.e.spec(0).label == "x" && r.e.spec(1).unit == "Hz");
    r.e.setParam(0, 0.5f);
    r.e.setParam(1, 0.5f);
    r.run(2);
    NEAR(r.out[0][BLOCK - 1], 0.15f, 1e-6);   // 15, linear
    NEAR(r.out[1][BLOCK - 1], 0.01f, 1e-6);   // 1000, logarithmic
    r.load(write(dir, "p16.csd", "; @p16 z 0 10\nksmps = 16\nnchnls = 1\n0dbfs = 1\nmassign 0, 0\n"
                                 "instr 1\n  out a(chnget:k(\"p16\")) / 10\nendin\nschedule 1, 0, -1"));
    r.e.setParam(15, 0.25f);
    r.run(2);
    NEAR(r.out[0][BLOCK - 1], 0.25f, 1e-6);   // the 16th control, 2.5 in 0..10
    r.load(ranged);
    r.load(bad);
    CHECK(r.e.spec(0).label == "x");          // a failed load keeps the running program's labels

    // back to the built-in
    r.load("");
    CHECK(r.e.error().empty() && r.e.path().empty());
    CHECK(r.run(20) > 0.01);

    // a failure with nothing running falls back to the built-in
    CsoundEngine fresh;
    fresh.load(bad);
    std::vector<float> o[CH];
    float* op[CH];
    for (int c = 0; c < CH; c++) {
        o[c].assign(BLOCK, 0.0f);
        op[c] = o[c].data();
    }
    fresh.prepare(48000.0f, BLOCK);
    CHECK(fresh.error().find("built-in running") != std::string::npos);
    double acc = 0.0;
    for (int b = 0; b < 20; b++) {
        fresh.process(r.ip, op, BLOCK);
        for (float v : o[0]) acc += std::fabs(v);
    }
    CHECK(acc > 0.0);
}

// audio, worker and MIDI threads together while programs swap; run under ThreadSanitizer
static void testThreads(const std::string& dir) {
    Rig r;
    std::string pass = write(dir, "pass.csd", PASS);
    std::atomic<bool> quit{ false };
    std::thread worker([&] {
        for (int k = 0; !quit; k++) {
            r.e.load(k % 2 ? pass : std::string());
            r.e.idle();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    std::thread midi([&] {
        for (int k = 0; !quit; k++) {
            r.e.midi(k % 2 ? 0x90 : 0x80, uint8_t(60 + k % 12), 100);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });
    for (int b = 0; b < 3000; b++) {
        r.e.setParam(b % 8, float(b % 100) / 100.0f);
        r.e.process(r.ip, r.op, BLOCK);
    }
    quit = true;
    worker.join();
    midi.join();
}

// One example program, with a 220 Hz sine on inputs 1 and 2, every control at 0.5 and a MIDI note:
// prints each output's peak, or the compile error on stderr.
static int runExample(const std::string& path) {
    Rig r;
    r.load(path);
    if (!r.e.error().empty()) {
        std::fprintf(stderr, "%s\n", r.e.error().c_str());
        return 1;
    }
    for (int p = 0; p < CH; p++) r.e.setParam(p, 0.5f);
    r.e.midi(0x90, 60, 100);
    float peak[CH] = {};
    for (int b = 0, t = 0; b < 400; b++, t += BLOCK) {
        for (int f = 0; f < BLOCK; f++) r.in[0][size_t(f)] = r.in[1][size_t(f)] = 0.5f * std::sin(6.2831853f * 220.0f * float(t + f) / 48000.0f);
        r.e.process(r.ip, r.op, BLOCK);
        for (int c = 0; c < CH; c++)
            for (float v : r.out[c]) peak[c] = std::max(peak[c], std::fabs(v));
    }
    for (int c = 0; c < CH; c++) std::printf("peak %d %g\n", c, peak[c]);
    return 0;
}

// `cv N` on an @pN line: input N moves the control, an octave per volt on a log range, else a tenth of
// the range per volt, within the range
static void testCv(const std::string& dir) {
    auto sp = CsoundEngine::parseSpecs("; @p1 cutoff 20 20000 Hz log cv 3\n; @p2 mix cv 9\n; @p3 depth -1 1 cv 1\n");
    CHECK(sp[0].cv == 2 && sp[0].log && sp[0].unit == "Hz" && sp[0].max == 20000.0f);
    CHECK(sp[1].label == "mix" && sp[1].cv == -1);  // there is no input 9
    CHECK(sp[2].cv == 0 && sp[2].min == -1.0f && sp[2].max == 1.0f);

    Rig r;
    r.load(write(dir, "cv.csd",
                 "; @p1 x 10 20 cv 3\n; @p2 y 100 10000 Hz log cv 4\nksmps = 16\nnchnls = 2\n0dbfs = 1\n"
                 "massign 0, 0\ninstr 1\n  outs a(chnget:k(\"p1\")) / 100, a(chnget:k(\"p2\")) / 100000\n"
                 "endin\nschedule 1, 0, -1"));
    CHECK(r.e.error().empty());
    r.e.setParam(0, 0.5f);  // 15
    r.e.setParam(1, 0.5f);  // 1000
    auto at = [&](float in3, float in4) {
        std::fill(r.in[2].begin(), r.in[2].end(), in3);
        std::fill(r.in[3].begin(), r.in[3].end(), in4);
        r.e.readCv(r.ip);
        r.run(2);
    };
    at(0.0f, 0.0f);
    NEAR(r.out[0][BLOCK - 1], 0.15f, 1e-6);
    NEAR(r.out[1][BLOCK - 1], 0.01f, 1e-6);
    at(0.2f, 0.2f);  // 1 V
    NEAR(r.out[0][BLOCK - 1], 0.16f, 1e-6);
    NEAR(r.out[1][BLOCK - 1], 0.02f, 1e-6);
    at(-0.2f, 2.0f);  // -1 V; 10 V, held at the top of the range
    NEAR(r.out[0][BLOCK - 1], 0.14f, 1e-6);
    NEAR(r.out[1][BLOCK - 1], 0.1f, 1e-6);
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    if (argc > 2 && std::string(argv[1]) == "example") return runExample(argv[2]);
    if (argc > 2 && std::string(argv[2]) == "threads") testThreads(argv[1]);
    else {
        testEngine(argv[1]);
        testCv(argv[1]);
    }
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
