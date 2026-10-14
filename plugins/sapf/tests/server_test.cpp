// Step 2 of docs/dev/sapf-plugin.md: sapf-sc's interpreter playing through a ScWorld that a thread runs
// at SPEED times real time, as the plugin's audio callback will at 1. Off 1, the World's clock leaves the
// wall clock, as a stalled or drifting host's does. Evaluates PROGRAM twice with the same seed: with
// >nrt, which writes the schedule to OUTDIR/sched.osc, then with play, recording output 1. Prints the
// frames where the recording rises above 0.5, as "onset N" lines; test_server.py compares them with the
// schedule. Then a line "stats NAN INF FINITE MIN MAX" for the recording, MIN and MAX over its finite
// samples, and what post sent to the handler, after a line "== post".
//
// usage: sapf_server_test PRELUDE UGENS OUTDIR PROGRAM SECONDS SPEED

#include "SapfServer.h"
#include "sapf/Engine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr float RATE = 48000.f;
constexpr int BLOCK = 256;  // the host's block: a multiple of scsynth's 64

void evaluate(Thread& th, const std::string& code) {
    try {
        P<Fun> fun;
        if (th.compile(code.c_str(), fun, true)) fun->runREPL(th);
    } catch (...) {
        post("error in: %s\n", code.c_str());
    }
    th.clearStack();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 7) {
        std::fprintf(stderr, "usage: %s PRELUDE UGENS OUTDIR PROGRAM SECONDS SPEED\n", argv[0]);
        return 2;
    }
    const std::string program = argv[4];
    const double seconds = std::atof(argv[5]), speed = std::atof(argv[6]);
    setenv("SAPF_SYNTHDEFS", argv[3], 1);

    std::mutex postLock;
    std::string posted;
    setPostHandler([&](const char* s) {
        std::lock_guard<std::mutex> lock(postLock);
        posted += s;
    });

    scsy::ScWorld world;
    std::string error;
    if (!world.open(RATE, argv[2], error)) {
        std::fprintf(stderr, "open: %s\n", error.c_str());
        return 1;
    }
    sapf::SapfServer server(world);
    setEmbeddedScsynth(&server);

    // the audio callback, paced to the sample rate times speed
    std::vector<float> recording;
    std::atomic<bool> recordingOn{ false }, running{ true };
    std::thread audio([&] {
        static float in[scsy::ScWorld::CHANNELS][BLOCK] = {}, out[scsy::ScWorld::CHANNELS][BLOCK];
        const float* ins[scsy::ScWorld::CHANNELS];
        float* outs[scsy::ScWorld::CHANNELS];
        for (int c = 0; c < scsy::ScWorld::CHANNELS; c++) {
            ins[c] = in[c];
            outs[c] = out[c];
        }
        auto next = std::chrono::steady_clock::now();
        while (running) {
            world.process(ins, outs, BLOCK);
            if (recordingOn) recording.insert(recording.end(), out[0], out[0] + BLOCK);
            next += std::chrono::microseconds(int64_t(1e6 * BLOCK / RATE / speed));
            std::this_thread::sleep_until(next);
        }
    });

    SapfEngineConfig config;
    config.sampleRate = RATE;
    config.preludeFile = argv[1];
    SapfEngine& engine = GetSapfEngine();
    engine.configure(config);
    engine.initialize();
    Thread th;
    engine.loadPrelude(th);

    evaluate(th, "1 setseed " + program + " " + std::to_string(seconds) + " \"sched\" >nrt");
    recordingOn = true;
    evaluate(th, "1 setseed " + program + " play");
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds / speed));
    evaluate(th, "stop");
    recordingOn = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // ola threads see stop and end

    setEmbeddedScsynth(nullptr);
    running = false;
    audio.join();
    world.close();

    bool high = false;
    for (size_t i = 0; i < recording.size(); i++) {
        bool now = recording[i] > 0.5f;
        if (now && !high) std::printf("onset %zu\n", i);
        high = now;
    }
    size_t nans = 0, infs = 0, finite = 0;
    float lo = INFINITY, hi = -INFINITY;
    for (float x : recording) {
        if (std::isnan(x)) {
            nans++;
        } else if (std::isinf(x)) {
            infs++;
        } else {
            finite++;
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
    }
    std::printf("stats %zu %zu %zu %g %g\n", nans, infs, finite, lo, hi);
    std::printf("== post\n%s", posted.c_str());
    return 0;
}
