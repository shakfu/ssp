// Native test of fstr::FaustRuntime against the host build of libfaust; built and run by test_faust.py.
// usage: faust_test <scratch dir> <libraries> [threads]
//        faust_test example <program.dsp> <libraries>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "FaustRuntime.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

using fstr::FaustRuntime;
static constexpr int CH = FaustRuntime::CHANNELS;
static constexpr int BLOCK = 128;

struct Rig {
    FaustRuntime e;
    std::vector<float> in[CH], out[CH];
    const float* ip[CH];
    float* op[CH];

    explicit Rig(const std::string& libraries) : e(libraries) {
        for (int c = 0; c < CH; c++) {
            in[c].assign(BLOCK, 0.0f);
            out[c].assign(BLOCK, 0.0f);
            ip[c] = in[c].data();
            op[c] = out[c].data();
        }
        e.prepare(48000.0f, BLOCK);
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

static std::string write(const std::string& dir, const char* name, const std::string& text) {
    std::string path = dir + "/" + name;
    std::ofstream(path) << text;
    return path;
}

// input 1 through a gain in dB, which needs the libraries; input 2 to output 2
static const char* GAIN = R"(import("stdfaust.lib");
g = hslider("gain [unit:dB]", -6, -60, 0, 0.1) : ba.db2linear;
process = *(g), _;
)";

// [nvoices:4]: notes set freq, gain and gate in a free voice; level is p1, in every voice. Output 1
// sums gate * gain * level, output 2 gate * freq / 10000.
static const char* POLY = R"(declare options "[nvoices:4]";
freq = hslider("freq", 440, 20, 20000, 1);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");
level = hslider("level", 1, 0, 1, 0.01);
process = gate * gain * level, gate * freq / 10000;
)";

static void testEngine(const std::string& dir, const std::string& libraries) {
    Rig r(libraries);
    // the built-in sounds with its controls at 0, as a new module starts
    CHECK(r.e.error().empty());
    CHECK(r.run(20, 0) > 1e-3 && r.run(1, 1) > 1e-3);
    auto pitch = r.e.spec(0);
    CHECK(pitch.label == "pitch" && pitch.unit == "Hz" && pitch.log);
    NEAR(pitch.min, 110, 1e-3);
    NEAR(pitch.max, 880, 1e-3);
    NEAR(pitch.def, 0, 1e-6);
    CHECK(r.e.spec(2).label == "cutoff");
    CHECK(r.e.spec(3).label.empty());

    // a program with an import from the libraries; its default normalised
    std::string gain = write(dir, "gain.dsp", GAIN);
    r.load(gain);
    CHECK(r.e.error().empty());
    CHECK(r.e.path() == gain);
    auto g = r.e.spec(0);
    CHECK(g.label == "gain" && g.unit == "dB" && !g.log);
    NEAR(g.def, 54.0 / 60.0, 1e-6);
    CHECK(r.e.spec(1).label.empty());
    std::fill(r.in[0].begin(), r.in[0].end(), 0.5f);
    std::fill(r.in[1].begin(), r.in[1].end(), 0.25f);
    r.e.setParam(0, 1.0f);  // 0 dB
    r.run(1);
    NEAR(r.out[0][BLOCK - 1], 0.5, 1e-6);
    NEAR(r.out[1][BLOCK - 1], 0.25, 1e-6);
    r.e.setParam(0, 0.0f);  // -60 dB
    r.run(1);
    NEAR(r.out[0][BLOCK - 1], 0.0005, 1e-6);
    CHECK(r.run(1, 2) == 0.0);  // past the program's outputs

    // a program that does not compile leaves the previous one running
    std::string bad = write(dir, "bad.dsp", "process = _ +;\n");
    r.load(bad);
    CHECK(!r.e.error().empty());
    CHECK(r.e.path() == bad);
    r.run(1);
    NEAR(r.out[1][BLOCK - 1], 0.25, 1e-6);

    // an unreadable file, likewise
    r.load(dir + "/missing.dsp");
    CHECK(r.e.error().find("cannot read") == 0);
    r.run(1);
    NEAR(r.out[1][BLOCK - 1], 0.25, 1e-6);

    // the built-in again
    r.load("");
    CHECK(r.e.error().empty() && r.e.path().empty());
    CHECK(r.e.spec(0).label == "pitch");

    // a non-finite output is silenced and the program's state cleared
    std::string inf = write(dir, "inf.dsp", "process = log;\n");  // log(0) = -inf
    std::fill(r.in[0].begin(), r.in[0].end(), 0.0f);
    r.load(inf);
    CHECK(r.e.error().empty());
    unsigned before = r.e.resets();
    CHECK(r.run(3) == 0.0);
    CHECK(r.e.resets() == before + 3);
    CHECK(r.e.status() == "non-finite output: 3 blocks silenced");
    r.load("");  // a new program clears the count
    CHECK(r.e.resets() == 0 && r.e.status().empty());

    // a failed program with nothing running falls back to the built-in
    Rig q(libraries);
    q.e.load(bad);
    q.e.prepare(48000.0f, BLOCK);
    CHECK(q.e.error().find("built-in running") != std::string::npos);
    CHECK(q.run(20) > 1e-3);
}

// audio and worker threads together while programs swap; run under ThreadSanitizer
static void testThreads(const std::string& dir, const std::string& libraries) {
    Rig r(libraries);
    std::string gain = write(dir, "gain.dsp", GAIN), poly = write(dir, "poly.dsp", POLY);
    std::atomic<bool> quit{ false };
    std::thread worker([&] {
        for (int k = 0; !quit; k++) {
            r.e.load(k % 3 == 0 ? gain : k % 3 == 1 ? poly : std::string());
            r.e.idle();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    std::thread notes([&] {  // as the MIDI input thread
        for (int k = 0; !quit; k++) {
            r.e.midi(40 + k % 30, k % 2 ? 100 : 0);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });
    for (int b = 0; b < 3000; b++) {
        r.e.setParam(b % 4, float(b % 100) / 100.0f);
        r.e.process(r.ip, r.op, BLOCK);
    }
    quit = true;
    worker.join();
    notes.join();
}

// One example, with a 220 Hz sine on inputs 1 and 2, every control at its default and note 60 held:
// prints each output's peak, or the compile error on stderr.
static int runExample(const std::string& path, const std::string& libraries) {
    Rig r(libraries);
    r.load(path);
    if (!r.e.error().empty()) {
        std::fprintf(stderr, "%s\n", r.e.error().c_str());
        return 1;
    }
    for (int p = 0; p < FaustRuntime::PARAMS; p++) r.e.setParam(p, std::max(r.e.spec(p).def, 0.0f));
    r.e.midi(60, 100);  // a polyphonic example sounds; the rest drop it
    float peak[CH] = {};
    for (int b = 0, t = 0; b < 400; b++, t += BLOCK) {
        for (int f = 0; f < BLOCK; f++)
            r.in[0][size_t(f)] = r.in[1][size_t(f)] = 0.5f * std::sin(6.2831853f * 220.0f * float(t + f) / 48000.0f);
        r.e.process(r.ip, r.op, BLOCK);
        for (int c = 0; c < CH; c++)
            for (float v : r.out[c]) peak[c] = std::max(peak[c], std::fabs(v));
    }
    for (int c = 0; c < CH; c++) std::printf("peak %d %g\n", c, peak[c]);
    return 0;
}

// [cv:N] on a control: input N moves it, as `cv N` on an @pN line
static void testCv(const std::string& dir, const std::string& libraries) {
    Rig r(libraries);
    r.load(write(dir, "cv.dsp", "x = hslider(\"x [cv:3]\", 15, 10, 20, 0.01);\n"
                                "y = hslider(\"y [scale:log][cv:4]\", 1000, 100, 10000, 1);\n"
                                "process = x / 100, y / 100000;\n"));
    CHECK(r.e.error().empty());
    CHECK(r.e.spec(0).cv == 2 && r.e.spec(1).cv == 3 && r.e.spec(1).log);
    r.e.setParam(0, 0.5f);  // 15
    r.e.setParam(1, 0.5f);  // 1000
    auto at = [&](float in3, float in4) {
        std::fill(r.in[2].begin(), r.in[2].end(), in3);
        std::fill(r.in[3].begin(), r.in[3].end(), in4);
        r.run(1);
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


static void testMidi(const std::string& dir, const std::string& libraries) {
    Rig r(libraries);
    r.e.midi(60, 100);  // the built-in drone has no voices: dropped
    r.run(1);
    r.load(write(dir, "poly.dsp", POLY));
    CHECK(r.e.error().empty());
    CHECK(r.e.spec(0).label == "level");  // the note controls stay off the encoders
    CHECK(r.e.spec(1).label.empty());
    r.e.setParam(0, 1.0f);
    r.run(1);
    NEAR(r.out[0][BLOCK - 1], 0.0f, 1e-6);
    r.e.midi(69, 127);
    r.run(1);
    NEAR(r.out[0][BLOCK - 1], 1.0f, 1e-5);
    NEAR(r.out[1][BLOCK - 1], 0.044f, 1e-5);  // 440 Hz
    r.e.midi(81, 127);  // a second voice: 880 Hz
    r.run(1);
    NEAR(r.out[0][BLOCK - 1], 2.0f, 1e-5);
    NEAR(r.out[1][BLOCK - 1], 0.132f, 1e-5);
    r.e.setParam(0, 0.5f);  // level reaches both voices
    r.run(1);
    NEAR(r.out[0][BLOCK - 1], 1.0f, 1e-5);
    r.e.midi(69, 0);
    r.run(1);
    NEAR(r.out[1][BLOCK - 1], 0.088f, 1e-5);
    r.e.midi(81, 0);
    r.run(1);
    NEAR(r.out[0][BLOCK - 1], 0.0f, 1e-6);
    for (int k = 0; k < 6; k++) r.e.midi(60 + k, 127);  // 6 notes, 4 voices: 4 sound
    r.run(1);
    NEAR(r.out[0][BLOCK - 1], 2.0f, 1e-5);
    CHECK(r.e.status() == "MIDI notes: 9");
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    if (argc > 3 && std::string(argv[1]) == "example") return runExample(argv[2], argv[3]);
    if (argc > 3 && std::string(argv[3]) == "threads") testThreads(argv[1], argv[2]);
    else {
        testEngine(argv[1], argv[2]);
        testCv(argv[1], argv[2]);
        testMidi(argv[1], argv[2]);
    }
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
