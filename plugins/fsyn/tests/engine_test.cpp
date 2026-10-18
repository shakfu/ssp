// Native test of fsyn::FluidEngine; built and run by test_fsyn.py and run_on_ssp.sh.
//
//   engine_test FONT              the tests, with FONT a GM soundfont
//   engine_test FONT threads      the race test only
//   engine_test bench FONT...     each font's load time, memory and render cost

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "FluidEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

using namespace fsyn;
using Clock = std::chrono::steady_clock;

static constexpr float SR = 48000.0f;
static std::string font;

struct Rig {
    FluidEngine e;
    std::vector<float> l, r, cv;
    int block;

    explicit Rig(int block = 256) : block(block) {
        e.prepare(SR, block);
        l.resize(size_t(block));
        r.resize(size_t(block));
        cv.assign(size_t(block), 0.0f);
        e.settings.reverb = 0.0f;  // deterministic, and no tail
        e.settings.chorus = 0.0f;
    }
    // loads `path` as the worker thread would, and waits for it
    void load(const std::string& path) {
        e.load(path);
        while (e.loading()) e.idle();
    }
    void note(int ch, int k, int v) { e.post({ uint8_t((v ? 0x90 : 0x80) | ch), uint8_t(k), uint8_t(v) }); }
    void program(int ch, int p) { e.post({ uint8_t(0xC0 | ch), uint8_t(p), 0 }); }
    // renders `seconds`; returns mean |x| of both channels over the last `tail` seconds
    float run(float seconds, float tail = 0.05f, std::vector<float>* all = nullptr) {
        int total = int(seconds * SR), from = total - int(tail * SR);
        double sum = 0;
        int count = 0;
        float* out[2] = { l.data(), r.data() };
        const float* in[FluidEngine::I_MAX] = { cv.data() };
        for (int done = 0; done < total; done += block) {
            int n = std::min(block, total - done);
            e.process(in, out, n);
            for (int i = 0; i < n; i++) {
                if (done + i >= from) {
                    sum += std::fabs(l[size_t(i)]) + std::fabs(r[size_t(i)]);
                    count += 2;
                }
                if (all) all->push_back(l[size_t(i)]);
            }
        }
        return count ? float(sum / count) : 0.0f;
    }
};

static void testSilentWithoutFont() {
    Rig r;
    r.note(0, 60, 100);
    CHECK(r.run(0.1f) == 0.0f);
    CHECK(r.e.font().empty());
    CHECK(!r.e.loading());
}

static void testNoteAndRelease() {
    Rig r;
    r.load(font);
    CHECK(r.e.font() == font);
    CHECK(r.e.error().empty());
    CHECK(r.run(0.05f) == 0.0f);
    r.note(0, 60, 100);
    CHECK(r.run(0.2f) > 1e-3f);
    CHECK(r.e.voicesPlaying() > 0);
    r.note(0, 60, 0);
    CHECK(r.run(2.0f) < 1e-5f);
    CHECK(r.e.voicesPlaying() == 0);
}

// channel 10 plays the GM drum kit; a kick decays without a note-off
static void testDrums() {
    Rig r;
    r.load(font);
    r.note(9, 36, 120);
    CHECK(r.run(0.05f) > 1e-3f);
    CHECK(r.run(2.0f) < 1e-5f);
}

// the same note at each gain, in a fresh synth: FluidSynth's output scales with it
static float level(float gain, float cv) {
    Rig r;
    r.load(font);
    r.e.settings.gain = gain;
    std::fill(r.cv.begin(), r.cv.end(), cv);
    r.note(0, 60, 100);
    return r.run(0.3f);
}

static void testGainAndCv() {
    float half = level(0.5f, 0.0f);
    CHECK(half > 1e-3f);
    CHECK(std::fabs(level(1.0f, 0.0f) / half - 2.0f) < 0.01f);
    CHECK(level(0.0f, 0.0f) == 0.0f);
    CHECK(std::fabs(level(0.0f, 0.2f) / half - 0.4f) < 0.01f);  // 1 V adds 0.2
    CHECK(std::fabs(level(1.9f, 0.2f) / half - 4.0f) < 0.01f);  // within 0..2
}

static void testVoiceCap() {
    Rig r;
    r.load(font);
    r.e.settings.voices = 4;
    for (int k = 48; k < 72; k++) r.note(0, k, 100);
    r.run(0.1f);
    CHECK(r.e.voicesPlaying() <= 4);
    CHECK(r.e.voicesPlaying() > 0);
}

static void testBlockSizes() {
    std::vector<float> ref;
    {
        Rig r(64);
        r.load(font);
        r.note(0, 60, 100);
        r.run(0.3f, 0.05f, &ref);
    }
    for (int block : { 1, 63, 100, 1000 }) {
        Rig r(block);
        r.load(font);
        r.note(0, 60, 100);
        std::vector<float> out;
        r.run(0.3f, 0.05f, &out);
        CHECK(out.size() == ref.size());
        float diff = 0;
        for (size_t i = 0; i < std::min(out.size(), ref.size()); i++) diff = std::max(diff, std::fabs(out[i] - ref[i]));
        CHECK(diff < 1e-6f);
    }
}

// a reload keeps each channel's program, and so does a load before the first block
static void testReloadKeepsPrograms() {
    std::vector<float> organ, piano, reloaded;
    {
        Rig r;
        r.load(font);
        r.note(0, 60, 100);
        r.run(0.2f, 0.05f, &piano);
    }
    {
        Rig r;
        r.load(font);
        r.program(0, 19);  // church organ
        r.note(0, 60, 100);
        r.run(0.2f, 0.05f, &organ);
    }
    {
        Rig r;
        r.program(0, 19);  // before any font: tracked, then replayed
        r.run(0.01f);
        r.load(font);
        r.note(0, 60, 100);
        r.run(0.2f, 0.05f, &reloaded);
        r.load(font);  // a second synth, from the same file
        r.note(0, 60, 100);
        std::vector<float> again;
        r.run(0.2f, 0.05f, &again);
        CHECK(again == organ);
    }
    CHECK(organ != piano);
    CHECK(reloaded.size() == organ.size());
    float diff = 0;
    for (size_t i = 0; i < std::min(organ.size(), reloaded.size()); i++)
        diff = std::max(diff, std::fabs(organ[i] - reloaded[i]));
    CHECK(diff < 1e-6f);
}

static void testBadFontKeepsPlaying() {
    Rig r;
    r.load(font);
    r.load("/nonexistent.sf2");
    CHECK(r.e.error().find("nonexistent.sf2") != std::string::npos);
    if (r.e.error().find("nonexistent.sf2") == std::string::npos) std::fprintf(stderr, "error: %s\n", r.e.error().c_str());
    CHECK(r.e.font() == font);
    CHECK(r.e.path() == font);
    r.note(0, 60, 100);
    CHECK(r.run(0.2f) > 1e-3f);
    r.load("");  // none: silent
    CHECK(r.e.font().empty());
    CHECK(r.e.error().empty());
    CHECK(r.run(0.1f) == 0.0f);
}

// a rate change drops the synth; the worker loads the font again at the new rate
static void testRateChange() {
    Rig r;
    r.load(font);
    r.e.prepare(44100.0f, 256);
    CHECK(r.e.loading());
    CHECK(r.e.font().empty());
    while (r.e.loading()) r.e.idle();
    CHECK(r.e.font() == font);
    r.note(0, 60, 100);
    CHECK(r.run(0.2f) > 1e-3f);
}

// MIDI, audio and worker threads at once, with loads and settings changing
static void testThreads() {
    Rig r(128);
    r.load(font);
    std::atomic<bool> stop{ false };
    std::thread midi([&] {
        for (int k = 0; !stop; k++) {
            r.note(k % 16, 48 + k % 24, 100);
            r.e.post({ uint8_t(0xB0 | k % 16), 7, uint8_t(k % 128) });
            r.e.post({ uint8_t(0xE0 | k % 16), 0, uint8_t(k % 128) });
            if (k % 50 == 0) r.program(k % 16, k % 128);
            r.note(k % 16, 48 + (k + 12) % 24, 0);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    std::thread worker([&] {
        for (int i = 0; !stop; i++) {
            if (i % 40 == 0) r.e.load(i % 80 ? font : std::string());
            r.e.idle();
            (void)r.e.font();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    auto until = Clock::now() + std::chrono::milliseconds(1500);
    for (int i = 0; Clock::now() < until; i++) {
        r.e.settings.voices = 16 + i / 20 % 3 * 32;
        r.e.settings.reverb = float(i / 30 % 2);
        r.run(128 / SR);
    }
    stop = true;
    midi.join();
    worker.join();
    CHECK(r.e.eventsDropped() == 0);
}

static long residentKb() {
    long pages = 0, rss = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(f, "%ld %ld", &pages, &rss) != 2) rss = 0;
        std::fclose(f);
    }
    return rss * 4;
}

// Per font: load time, the memory it adds, and the render time of a dense GM texture as a share of
// real time on one core, at 32 and 64 voices with reverb and chorus on.
static int bench(int argc, char** argv) {
    std::printf("%-24s %8s %8s %10s %10s\n", "font", "load ms", "MB", "dsp% 32v", "dsp% 64v");
    for (int a = 2; a < argc; a++) {
        Rig r(128);
        r.e.settings.reverb = 0.7f;
        r.e.settings.chorus = 0.5f;
        long before = residentKb();
        auto t0 = Clock::now();
        r.load(argv[a]);
        double loadMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (!r.e.error().empty()) {
            std::printf("%-24s %s\n", argv[a], r.e.error().c_str());
            failures++;
            continue;
        }
        double mb = double(residentKb() - before) / 1024.0;
        double dsp[2];
        int caps[2] = { 32, 64 };
        for (int c = 0; c < 2; c++) {
            r.e.settings.voices = caps[c];
            // strings, brass, piano and organ held on 8 channels, plus drums
            int programs[8] = { 0, 48, 49, 56, 61, 19, 32, 73 };
            for (int ch = 0; ch < 8; ch++) {
                r.program(ch, programs[ch]);
                for (int k = 0; k < 8; k++) r.note(ch, 48 + ch * 2 + k * 3, 90);
            }
            r.run(0.2f);
            auto t = Clock::now();
            for (int i = 0; i < 10; i++) {
                r.note(9, 36 + i % 10, 110);
                r.run(0.2f);
            }
            dsp[c] = 100.0 * std::chrono::duration<double>(Clock::now() - t).count() / 2.0;
            for (int ch = 0; ch < 16; ch++) r.e.post({ uint8_t(0xB0 | ch), 123, 0 });
            r.run(0.5f);
        }
        const char* name = std::strrchr(argv[a], '/');
        std::printf("%-24s %8.0f %8.1f %10.1f %10.1f\n", name ? name + 1 : argv[a], loadMs, mb, dsp[0], dsp[1]);
    }
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    if (std::string(argv[1]) == "bench") return bench(argc, argv);
    font = argv[1];
    if (argc > 2 && std::string(argv[2]) == "threads") {
        testThreads();
        return failures ? 1 : 0;
    }
    testSilentWithoutFont();
    testNoteAndRelease();
    testDrums();
    testGainAndCv();
    testVoiceCap();
    testBlockSizes();
    testReloadKeepsPrograms();
    testBadFontKeepsPlaying();
    testRateChange();
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    else std::printf("all passed\n");
    return failures ? 1 : 0;
}
