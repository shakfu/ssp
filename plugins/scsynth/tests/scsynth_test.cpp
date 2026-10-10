// Native test of scsy::ScWorld against the host build of libscsynth; built and run by test_scsynth.py.
// usage: scsynth_test engine|sc3|threads <UGen dir> <defs dir>
//   engine   the UGen dir holds SC's core UGens only
//   sc3      the UGen dir also holds sc3-plugins
//   threads  two Worlds at once, for ThreadSanitizer
// SCSY_PIN=1 pins each audio thread to its own core from 1 up: on the SSP, cores 1-3 are
// isolated (isolcpus), so without it every thread shares core 0.

#include <pthread.h>
#include <sched.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ScWorld.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

using scsy::ScWorld;
static constexpr int CH = ScWorld::CHANNELS;
static constexpr float SR = 48000.0f;
static std::string defs;

// Buffers for one World. run() calls process in blocks of n; in channel c, sample i is in(c, i).
struct Rig {
    ScWorld w;
    std::vector<float> in[CH], out[CH];
    long t = 0;  // frames processed

    static float input(int c, long i) { return float((i * 7 + c * 13) % 101) / 101.0f - 0.5f; }

    // processes `frames` in blocks of n; returns channel 0's output
    std::vector<float> run(long frames, int n, int channel = 0) {
        std::vector<float> got;
        for (int c = 0; c < CH; c++) {
            in[c].resize(n);
            out[c].resize(n);
        }
        const float* ip[CH];
        float* op[CH];
        for (long done = 0; done < frames; done += n) {
            for (int c = 0; c < CH; c++) {
                for (int i = 0; i < n; i++)
                    in[c][i] = input(c, t + i);
                ip[c] = in[c].data();
                op[c] = out[c].data();
            }
            w.process(ip, op, n);
            got.insert(got.end(), out[channel].begin(), out[channel].end());
            t += n;
        }
        return got;
    }
};

static void pinToNextCore() {
    static std::atomic<int> next{ 0 };
    if (!std::getenv("SCSY_PIN"))
        return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(1 + next.fetch_add(1) % 3, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof set, &set) != 0)
        std::fprintf(stderr, "SCSY_PIN: pthread_setaffinity_np failed\n");
    std::fprintf(stderr, "audio thread on core %d\n", sched_getcpu());
}

// Calls process on a thread of its own, as the host's audio callback would, while the test thread
// stands in for the worker.
struct Audio {
    Rig& rig;
    std::atomic<bool> running{ true };
    std::thread thread;
    explicit Audio(Rig& r): rig(r), thread([this] {
        pinToNextCore();
        while (running.load()) {
            rig.run(128, 128);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }) {}
    ~Audio() {
        running = false;
        thread.join();
    }
};

static bool load(Rig& r, const std::string& name, std::string& error) {
    Audio audio(r);
    return r.w.load(defs + "/" + name, error);
}

// largest deviation of x from a sinusoid of frequency hz: x[n+1] + x[n-1] = 2 cos(w) x[n]
static double sineError(const std::vector<float>& x, double hz) {
    double k = 2.0 * std::cos(2.0 * M_PI * hz / SR), e = 0.0;
    for (size_t i = 1; i + 1 < x.size(); i++)
        e = std::max(e, std::fabs(x[i + 1] + x[i - 1] - k * x[i]));
    return e;
}

static float peak(const std::vector<float>& x) {
    float p = 0.0f;
    for (float v : x)
        p = std::max(p, std::fabs(v));
    return p;
}

// the first open in a process also loads the UGen plugins
static bool timedOpen(Rig& r, const std::string& plugins, std::string& error) {
    auto t0 = std::chrono::steady_clock::now();
    bool ok = r.w.open(SR, plugins, error);
    std::chrono::duration<double, std::milli> ms = std::chrono::steady_clock::now() - t0;
    std::fprintf(stderr, "open: %.1f ms\n", ms.count());
    return ok;
}

static void engine(const std::string& plugins) {
    Rig r;
    std::string error;
    CHECK(timedOpen(r, plugins, error));

    CHECK(!r.w.load(defs + "/missing.scsyndef", error) && error.find("cannot read") != std::string::npos);
    CHECK(!r.w.load(defs + "/defs.py", error) && error.find("not a SynthDef") != std::string::npos);

    CHECK(load(r, "sine.scsyndef", error));
    CHECK(error.empty());
    auto x = r.run(4800, 128);
    CHECK(sineError(x, 440.0) < 1e-3);
    CHECK(std::fabs(peak(x) - 0.5f) < 1e-3);
    r.w.set("freq", 880.0f);
    r.run(128, 128);  // the block that performs /n_set
    x = r.run(4800, 128);
    CHECK(sineError(x, 880.0) < 1e-3);

    // No latency at 128. 64 frames through the FIFO from the first size that is not a multiple of 64;
    // the FIFO then stays in use, as it holds frames (4 here) when the 64-frame blocks start.
    CHECK(load(r, "thru.scsyndef", error));
    for (int n : { 128, 37, 511, 64 }) {
        for (int c : { 0, 7 }) {
            long t0 = r.t;
            auto y = r.run(4096, n, c);
            int latency = n == 128 ? 0 : 64;
            bool same = true;
            for (size_t i = 128; i < y.size(); i++)
                same = same && y[i] == Rig::input(c, t0 + long(i) - latency);
            if (!same)
                std::fprintf(stderr, "thru: n %d channel %d\n", n, c);
            CHECK(same);
        }
    }

    // a def using a UGen this World lacks: an error, and the running synth goes on
    CHECK(!load(r, "dfm1.scsyndef", error));
    std::fprintf(stderr, "dfm1 without sc3-plugins: %s\n", error.c_str());
    CHECK(error.find("UGen 'DFM1' not installed") != std::string::npos);
    long t0 = r.t;
    auto y = r.run(1280, 128, 3);
    bool same = true;
    for (size_t i = 0; i < y.size(); i++)
        same = same && y[i] == Rig::input(3, t0 + long(i) - 64);  // still in FIFO mode from above
    CHECK(same);
    r.w.close();
}

static void sc3(const std::string& plugins) {
    Rig r;
    std::string error;
    CHECK(timedOpen(r, plugins, error));
    CHECK(load(r, "dfm1.scsyndef", error));
    if (!error.empty())
        std::fprintf(stderr, "dfm1: %s\n", error.c_str());
    auto x = r.run(48000, 128);
    bool finite = true;
    for (float v : x)
        finite = finite && std::isfinite(v);
    CHECK(finite);
    CHECK(peak(x) > 0.05f);
}

// Two Worlds in one process: each reloads while it runs, then one closes while the other runs.
static void threads(const std::string& plugins) {
    Rig a, b;
    std::string error;
    CHECK(a.w.open(SR, plugins, error));
    CHECK(b.w.open(SR, plugins, error));
    {
        Audio audioA(a);
        Audio audioB(b);
        std::thread workerB([&] {
            std::string e;
            for (int i = 0; i < 6; i++)
                CHECK(b.w.load(defs + (i % 2 ? "/sine.scsyndef" : "/thru.scsyndef"), e));
        });
        for (int i = 0; i < 6; i++)
            CHECK(a.w.load(defs + (i % 2 ? "/sine.scsyndef" : "/thru.scsyndef"), error));
        workerB.join();
    }
    {
        Audio audioB(b);
        a.w.close();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    auto x = b.run(4800, 128);  // b ended on sine
    CHECK(sineError(x, 440.0) < 1e-3);
    CHECK(std::fabs(peak(x) - 0.5f) < 1e-3);
    // a World opened after another closed
    CHECK(a.w.open(SR, plugins, error));
    CHECK(load(a, "sine.scsyndef", error));
    CHECK(sineError(a.run(4800, 128), 440.0) < 1e-3);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s engine|sc3|threads <UGen dir> <defs dir>\n", argv[0]);
        return 2;
    }
    std::string mode = argv[1], plugins = argv[2];
    defs = argv[3];
    if (mode == "engine")
        engine(plugins);
    else if (mode == "sc3")
        sc3(plugins);
    else if (mode == "threads")
        threads(plugins);
    else
        return 2;
    if (failures)
        std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
