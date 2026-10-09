// Times Faust's LLVM JIT and interpreter against the compiled chorus kernel (chrs) on the same
// chorus.dsp. Prints each one's load: compute() time over the audio's duration, on one core.
// usage: faust_bench <chorus.dsp> <faust libraries dir> [seconds]

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ChorusKernel.h"
#include "faust/dsp/interpreter-dsp.h"
#include "faust/dsp/llvm-dsp.h"

static constexpr int RATE = 48000, BLOCK = 128;

// D: ssp::faust::chorus::mydsp or ::dsp; both have compute(int, float**, float**)
template <class D>
static double load(D& d, int ins, int outs, double secs) {
    std::vector<std::vector<float>> in{ size_t(ins), std::vector<float>(BLOCK) };
    std::vector<std::vector<float>> out{ size_t(outs), std::vector<float>(BLOCK) };
    std::vector<float*> ip, op;
    for (auto& b : in) ip.push_back(b.data());
    for (auto& b : out) op.push_back(b.data());
    unsigned s = 1;
    for (auto& b : in)
        for (auto& x : b) s = s * 1664525u + 1013904223u, x = float(s >> 8) / float(1 << 24) - 0.5f;
    int blocks = int(secs * RATE / BLOCK);
    double sink = 0.0;
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; b++) {
        d.compute(BLOCK, ip.data(), op.data());
        sink += op[0][0];
    }
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (sink == 12345.0) std::puts("");  // keeps the loop
    return dt / (double(blocks) * BLOCK / RATE);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <chorus.dsp> <faust libraries dir> [seconds]\n", argv[0]);
        return 2;
    }
    double secs = argc > 3 ? std::atof(argv[3]) : 20.0;

    ssp::faust::chorus::mydsp kernel;
    kernel.init(RATE);
    double compiled = load(kernel, 2, 2, secs);

    const char* args[] = { "-I", argv[2] };
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    auto* ifactory = createInterpreterDSPFactoryFromFile(argv[1], 2, args, err);
    auto t1 = std::chrono::steady_clock::now();
    auto* lfactory = ifactory ? createDSPFactoryFromFile(argv[1], 2, args, "", err, -1) : nullptr;
    auto t2 = std::chrono::steady_clock::now();
    if (!lfactory) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    auto* interp = ifactory->createDSPInstance();
    auto* jit = lfactory->createDSPInstance();
    interp->init(RATE);
    jit->init(RATE);
    double interpreted = load(*interp, interp->getNumInputs(), interp->getNumOutputs(), secs);
    double jitted = load(*jit, jit->getNumInputs(), jit->getNumOutputs(), secs);
    delete interp;
    delete jit;
    deleteInterpreterDSPFactory(ifactory);
    deleteDSPFactory(lfactory);

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::printf("factory     interpreter %.0f ms, llvm %.0f ms\n", ms(t0, t1), ms(t1, t2));
    std::printf("compiled    %.3f%%\n", compiled * 100.0);
    std::printf("llvm        %.3f%%\n", jitted * 100.0);
    std::printf("interpreter %.3f%%\n", interpreted * 100.0);
    return 0;
}
