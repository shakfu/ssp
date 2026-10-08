// Native test of radio::RadioEngine and its file layer; built and run by test_engine.py.
// argv[1] is an empty scratch directory.

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <vector>

#include "RadioEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

using namespace radio;
using namespace ssp::engine;

static constexpr float SR = 48000.0f;
static constexpr int BLOCK = 128;

static void put16(FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }
static void put32(FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }

// headerless 16-bit mono; sample i is value(i)
template <typename F>
static void writeRaw(const std::string& path, int frames, F value) {
    FILE* f = std::fopen(path.c_str(), "wb");
    for (int i = 0; i < frames; i++) put16(f, uint16_t(int16_t(std::lround(value(i) * 32768.0f))));
    std::fclose(f);
}

// tag 1 (PCM) or 3 (float); a "LIST" chunk before "data" checks that unknown chunks are skipped
template <typename F>
static void writeWav(const std::string& path, int frames, int rate, int chans, int bits, int tag, F value) {
    FILE* f = std::fopen(path.c_str(), "wb");
    int bpf = chans * bits / 8;
    std::fwrite("RIFF", 1, 4, f);
    put32(f, uint32_t(4 + 24 + 12 + 8 + frames * bpf));
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, uint16_t(tag));
    put16(f, uint16_t(chans));
    put32(f, uint32_t(rate));
    put32(f, uint32_t(rate * bpf));
    put16(f, uint16_t(bpf));
    put16(f, uint16_t(bits));
    std::fwrite("LIST", 1, 4, f);
    put32(f, 3);
    std::fwrite("abc\0", 1, 4, f);  // odd size, padded
    std::fwrite("data", 1, 4, f);
    put32(f, uint32_t(frames * bpf));
    for (int i = 0; i < frames; i++)
        for (int c = 0; c < chans; c++) {
            float v = value(i, c);
            if (tag == 3) {
                std::fwrite(&v, 4, 1, f);
            } else if (bits == 16) {
                put16(f, uint16_t(int16_t(std::lround(v * 32767.0f))));
            } else {
                int32_t s = int32_t(std::lround(v * 8388607.0f));
                std::fwrite(&s, 3, 1, f);
            }
        }
    std::fclose(f);
}

static void testFiles(const std::string& dir) {
    std::string bank = dir + "/files";
    mkdir(bank.c_str(), 0755);
    writeRaw(bank + "/b.raw", 1000, [](int i) { return float(i) / 32768.0f; });
    writeWav(bank + "/A.wav", 500, 22050, 1, 16, 1, [](int, int) { return 0.5f; });
    writeWav(bank + "/c.wav", 300, 96000, 2, 24, 1, [](int, int c) { return c == 0 ? 0.5f : -0.25f; });
    writeWav(bank + "/d.wav", 200, 44100, 1, 32, 3, [](int i, int) { return float(i) / 200.0f; });
    writeRaw(bank + "/notes.txt", 10, [](int) { return 0.0f; });

    auto st = scanBank(bank);
    CHECK(st.size() == 4);
    if (st.size() != 4) return;
    CHECK(st[0].name == "A.wav" && st[1].name == "b.raw");  // case-insensitive order
    CHECK(st[0].rate == 22050 && st[0].frames == 500 && st[0].format == Station::PCM16);
    CHECK(st[1].rate == 0 && st[1].frames == 1000);
    CHECK(st[2].channels == 2 && st[2].format == Station::PCM24 && st[2].frames == 300);
    CHECK(st[3].format == Station::FLOAT32 && st[3].frames == 200);

    Reader r;
    float buf[8];
    CHECK(r.open(st[1], 998));
    CHECK(r.read(buf, 4) == 4);  // loops at the end
    NEAR(buf[0], 998.0f / 32768.0f, 1e-6);
    NEAR(buf[1], 999.0f / 32768.0f, 1e-6);
    NEAR(buf[2], 0.0f, 1e-6);
    NEAR(buf[3], 1.0f / 32768.0f, 1e-6);
    CHECK(r.open(st[2], 0) && r.read(buf, 2) == 2);
    NEAR(buf[0], 0.125f, 1e-5);  // stereo mixed to mono
    CHECK(r.open(st[3], 100) && r.read(buf, 1) == 1);
    NEAR(buf[0], 0.5f, 1e-6);

    auto banks = scanBanks(dir);
    CHECK(banks.size() == 1 && banks[0] == bank);
    banks = scanBanks(bank);
    CHECK(banks.size() == 1 && banks[0] == bank);  // no subdirectories: the root is the bank
}

struct Rig {
    RadioEngine e;
    std::vector<float> in[RadioEngine::DECKS * RadioEngine::I_PER_DECK], out[RadioEngine::O_MAX];
    const float* ip[RadioEngine::DECKS * RadioEngine::I_PER_DECK];
    float* op[RadioEngine::O_MAX];

    explicit Rig(const std::string& root) {
        for (size_t c = 0; c < std::size(in); c++) {
            in[c].assign(BLOCK, 0.0f);
            ip[c] = in[c].data();
        }
        for (size_t c = 0; c < std::size(out); c++) {
            out[c].assign(BLOCK, 0.0f);
            op[c] = out[c].data();
        }
        e.prepare(SR, BLOCK);
        e.setRawRate(SR);
        e.setRoot(root);
    }

    // processes `seconds`, calling idle() once per block as the worker would
    void run(float seconds) {
        int blocks = int(std::ceil(seconds * SR / BLOCK));
        for (int b = 0; b < blocks; b++) {
            e.idle();
            e.process(ip, op, BLOCK);
        }
    }
    float a(int i = BLOCK - 1) const { return out[RadioEngine::O_A][size_t(i)]; }
    void cv(int deck, int input, float v) { in[size_t(deck * RadioEngine::I_PER_DECK + input)].assign(BLOCK, v); }
};

static void testEngine(const std::string& dir) {
    std::string root = dir + "/radio";
    mkdir(root.c_str(), 0755);
    mkdir((root + "/0").c_str(), 0755);
    mkdir((root + "/1").c_str(), 0755);
    static constexpr int L = 30000;
    writeRaw(root + "/0/0.raw", 4800, [](int) { return 0.25f; });
    writeRaw(root + "/0/1.raw", 4800, [](int) { return -0.5f; });
    writeRaw(root + "/0/2.raw", L, [](int i) { return float(i) / 32768.0f; });  // a ramp: value = frame
    writeWav(root + "/1/slow.wav", 24000, 24000, 1, 16, 1, [](int i, int) { return float(i % 20000) / 32767.0f; });

    Rig r(root);
    auto& c0 = r.e.controls(0);
    r.run(0.05f);
    CHECK(r.e.info(0).banks == 2 && r.e.info(0).stations == 3);
    NEAR(r.a(), 0.25f, 1e-3);  // station 0 at the knob's start
    CHECK(r.e.info(0).stationName == "0.raw");

    // a clean switch waits for the choice to settle
    c0.station = 0.5f;
    r.run(0.1f);
    NEAR(r.a(), 0.25f, 1e-3);
    r.run(0.15f);
    NEAR(r.a(), -0.5f, 1e-3);

    // the CV input adds to the knob: 5 V sweeps the whole bank
    r.cv(0, RadioEngine::I_STATION, 0.5f);
    r.run(0.3f);
    CHECK(r.e.info(0).station == 2);
    r.cv(0, RadioEngine::I_STATION, 0.0f);

    // free-running: the ramp station sounds at the clock's position, not its start
    c0.station = 1.0f;
    c0.noise = 1.0f;  // switch at once; static decays within 0.1 s
    r.run(0.5f);
    float v = r.a() * 32768.0f;
    CHECK(v > 1000.0f);
    float prev = r.a(BLOCK - 2) * 32768.0f;
    NEAR(v - prev, 1.0f, 0.05);  // one source frame per output frame at matched rates

    // reset jumps to the start offset
    c0.noise = 0.0f;
    c0.start = 0.5f;
    c0.reset = true;
    r.run(0.03f);
    c0.reset = false;
    v = r.a() * 32768.0f;
    CHECK(v >= L * 0.5f && v < L * 0.5f + 0.03f * SR + BLOCK);

    // speed CV is V/oct: +1 V doubles the step
    r.cv(0, RadioEngine::I_SPEED, ssp::engine::CV_PER_VOLT);
    r.run(0.02f);
    NEAR((r.a() - r.a(BLOCK - 2)) * 32768.0f, 2.0f, 0.05);
    r.cv(0, RadioEngine::I_SPEED, 0.0f);

    // bank 2 holds a 24 kHz wav: the source rate is honoured
    c0.bank = 0.99f;
    c0.station = 0.0f;
    r.run(0.4f);
    CHECK(r.e.info(0).bank == 1 && r.e.info(0).stationName == "slow.wav");
    NEAR((r.a() - r.a(BLOCK - 3)) * 32767.0f, 1.0f, 0.05);  // half a frame per output frame

    // split route: deck A only on the left
    r.e.controls(1).level = 0.0f;
    r.e.setRoute(Route::Split);
    r.run(0.01f);
    CHECK(std::fabs(r.out[RadioEngine::O_L][BLOCK - 1]) > 0.0f);
    CHECK(r.out[RadioEngine::O_R][BLOCK - 1] == 0.0f);

    // a missing root: no banks, silence, no crash
    r.e.setRoot(dir + "/missing");
    r.run(0.05f);
    CHECK(r.e.info(0).stations == 0 && !r.e.info(0).playing);
    CHECK(r.a() == 0.0f);
}

// audio and worker on their own threads while the knobs sweep; run under ThreadSanitizer
static void testThreads(const std::string& dir) {
    std::string root = dir + "/radio";
    Rig r(root);
    std::atomic<bool> quit{ false };
    std::thread worker([&] {
        while (!quit) {
            r.e.idle();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    for (int b = 0; b < 4000; b++) {
        auto& c = r.e.controls(b & 1);
        c.station = float(b % 97) / 96.0f;
        c.bank = float(b % 389) / 388.0f;
        c.noise = b % 3 == 0 ? 1.0f : 0.0f;
        c.reset = b % 50 == 0;
        r.e.process(r.ip, r.op, BLOCK);
        if (b % 100 == 0) r.e.setRoot(b % 200 ? root : dir + "/missing");
        if (b % 10 == 0) r.e.info(0);
    }
    quit = true;
    worker.join();
}

static void testOrderAndSettings(const std::string& dir) {
    CHECK(nameLess("NUMBER1.raw", "NUMBER2.raw"));
    CHECK(nameLess("NUMBER2.raw", "NUMBER10.raw"));
    CHECK(!nameLess("NUMBER10.raw", "NUMBER2.raw"));
    CHECK(nameLess("2", "10") && nameLess("9", "10") && nameLess("0", "1"));
    CHECK(nameLess("apple", "Banana") && nameLess("B_CNTRY", "BERLINFR"));
    CHECK(!nameLess("x01", "x1") && !nameLess("x1", "x01"));  // equal numbers tie

    std::string root = dir + "/order";
    for (const char* b : { "/order", "/order/10", "/order/2", "/order/1" }) mkdir((dir + b).c_str(), 0755);
    auto banks = scanBanks(root);
    CHECK(banks.size() == 3 && banks[0] == root + "/1" && banks[1] == root + "/2" && banks[2] == root + "/10");

    auto none = readSettings(root);
    CHECK(none.fadeMs == -1 && none.startPotImmediate == -1 && none.startCvImmediate == -1);
    // the user's card, newer firmware
    std::FILE* f = std::fopen((root + "/SETTINGS.TXT").c_str(), "w");
    std::fputs("crossfade=0\ncrossfadeTime=25\nshowMeter=1\nstartPotImmediate=1\nstartCVImmediate=0\n", f);
    std::fclose(f);
    auto st = readSettings(root);
    CHECK(st.fadeMs == 25 && st.startPotImmediate == 1 && st.startCvImmediate == 0);
    // older firmware, as on the wiki, lower-case file name
    std::remove((root + "/SETTINGS.TXT").c_str());
    f = std::fopen((root + "/settings.txt").c_str(), "w");
    std::fputs("MUTE=1\r\nDECLICK=15\r\nStartCVImmediate=1\r\n", f);
    std::fclose(f);
    st = readSettings(root);
    CHECK(st.fadeMs == 15 && st.startPotImmediate == -1 && st.startCvImmediate == 1);
}

// switches fade, and Start Immediate jumps without a reset
static void testFadeAndStart(const std::string& dir) {
    std::string root = dir + "/radio";
    Rig r(root);
    auto& c0 = r.e.controls(0);
    r.e.setFade(10.0f);
    r.run(0.1f);
    NEAR(r.a(), 0.25f, 1e-3);
    c0.station = 0.5f;  // to the -0.5 station
    float maxStep = 0.0f, prev = r.a();
    for (int b = 0; b < int(0.4f * SR / BLOCK); b++) {
        r.e.idle();
        r.e.process(r.ip, r.op, BLOCK);
        for (int i = 0; i < BLOCK; i++) {
            maxStep = std::max(maxStep, std::fabs(r.a(i) - prev));
            prev = r.a(i);
        }
    }
    NEAR(r.a(), -0.5f, 1e-3);
    CHECK(maxStep < 0.75f / (0.01f * SR) * 1.5f);  // a 10 ms ramp, not a 0.75 step

    c0.station = 1.0f;  // the ramp station
    r.run(0.3f);
    r.e.setStartImmediate(true, false);
    c0.start = 0.5f;
    r.run(0.05f);
    float v = r.a() * 32768.0f;
    CHECK(v >= 15000.0f && v < 15000.0f + 0.05f * SR + BLOCK);
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    if (argc > 2 && std::string(argv[2]) == "threads") {
        testEngine(argv[1]);
        testThreads(argv[1]);
        return failures ? 1 : 0;
    }
    testFiles(argv[1]);
    testOrderAndSettings(argv[1]);
    testEngine(argv[1]);
    testFadeAndStart(argv[1]);
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
