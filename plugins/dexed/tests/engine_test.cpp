// Native test of dexed::DexedEngine, dexed::Cartridge and the Synth_Dexed patches; built and run by
// test_dexed.py. argv[1] is an empty scratch directory; argv[2] "threads" runs the race test only.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "DexedEngine.h"
#include "synth_dexed/dexed.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

using namespace dexed;

static constexpr float SR = 48000.0f;
static constexpr int OP_OUTPUT = 16;  // in each operator's 21 bytes; operator 6 first

struct Rig {
    DexedEngine e;
    std::vector<float> l, r, cv[DexedEngine::I_MAX];
    int block;

    explicit Rig(int block = 256) : block(block) {
        e.prepare(SR, block);
        l.resize(size_t(block));
        r.resize(size_t(block));
        for (auto& c : cv) c.assign(size_t(block), 0.0f);
    }
    void hold(int input, float v) { std::fill(cv[input].begin(), cv[input].end(), v); }
    void note(int k, int v) { e.post({ uint8_t(v ? 0x90 : 0x80), uint8_t(k), uint8_t(v) }); }
    void cc(int c, int v) { e.post({ 0xB0, uint8_t(c), uint8_t(v) }); }
    // renders `seconds`; returns mean |x| over the last `tail` seconds
    float run(float seconds, float tail = 0.05f, std::vector<float>* all = nullptr) {
        int total = int(seconds * SR), from = total - int(tail * SR);
        double sum = 0;
        int count = 0;
        float* out[2] = { l.data(), r.data() };
        const float* in[DexedEngine::I_MAX] = { cv[0].data(), cv[1].data() };
        for (int done = 0; done < total; done += block) {
            int n = std::min(block, total - done);
            e.process(in, out, n);
            for (int i = 0; i < n; i++) {
                if (done + i >= from) {
                    sum += std::fabs(l[size_t(i)]);
                    count++;
                }
                if (all) all->push_back(l[size_t(i)]);
                if (l[size_t(i)] != r[size_t(i)]) failures++;
            }
        }
        return count ? float(sum / count) : 0.0f;
    }
};

// the init voice, as a cartridge would hold it
static void initVoice(uint8_t* v) {
    struct D : Dexed {
        D() : Dexed(1, 48000) {}
        const uint8_t* get() const { return init_voice; }
    } d;
    std::memcpy(v, d.get(), Cartridge::VOICE_BYTES);
}

static void testNoteAndRelease() {
    Rig r;
    CHECK(r.run(0.05f) == 0.0f);
    r.note(60, 100);
    float on = r.run(0.2f);
    CHECK(on > 0.01f);
    CHECK(r.e.notesPlaying() == 1);
    r.note(60, 0);
    CHECK(r.run(0.5f) < 1e-4f);
    CHECK(r.e.notesPlaying() == 0);  // reaped once silent
}

static void testBlockSizes() {
    std::vector<float> ref;
    {
        Rig r(64);
        r.note(60, 100);
        r.note(67, 90);
        r.run(0.3f, 0.05f, &ref);
    }
    for (int block : { 1, 63, 65, 256, 1000 }) {
        Rig r(block);
        r.note(60, 100);
        r.note(67, 90);
        std::vector<float> out;
        r.run(0.3f, 0.05f, &out);
        CHECK(out == ref);
    }
}

static void testVoiceCap() {
    Rig r;
    r.e.settings.voices = 4;
    for (int k = 60; k < 66; k++) r.note(k, 100);
    r.run(0.1f);
    CHECK(r.e.notesPlaying() == 4);
    for (int k = 60; k < 66; k++) r.note(k, 0);
    r.run(0.5f);
    CHECK(r.e.notesPlaying() == 0);
}

// Synth_Dexed stamped the loop counter's voice, not the started one, so a steal took the wrong voice
static void testStealsOldest() {
    struct D : Dexed {
        D() : Dexed(4, 48000) {}
    } d;
    for (int k : { 60, 62, 64, 65 }) {
        d.keydown(uint8_t(k), 100);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));  // millis() orders the voices
    }
    d.keydown(67, 100);
    int shift = 48 - TRANSPOSE_FIX;  // the init voice's transpose, which keydown adds
    std::vector<int> held;
    for (int i = 0; i < 4; i++)
        if (d.voices[i].keydown) held.push_back(d.voices[i].midi_note - shift);
    std::sort(held.begin(), held.end());
    CHECK((held == std::vector<int>{ 62, 64, 65, 67 }));
}

static void testTransposeWhileHeld() {
    Rig r;
    r.note(60, 100);
    r.run(0.05f);
    r.e.settings.transpose = 12;
    r.note(60, 0);  // releases the pitch it started, not 72
    r.run(0.5f);
    CHECK(r.e.notesPlaying() == 0);
}

static void testSustain() {
    Rig r;
    r.cc(64, 127);
    r.note(60, 100);
    r.run(0.05f);
    r.note(60, 0);
    CHECK(r.run(0.5f) > 0.01f);
    CHECK(r.e.notesPlaying() == 1);
    r.cc(64, 0);
    r.run(0.5f);
    CHECK(r.e.notesPlaying() == 0);
}

static void testControllers() {
    std::vector<float> plain, bent;
    {
        Rig r;
        r.note(60, 100);
        r.run(0.2f, 0.05f, &plain);
    }
    {
        Rig r;
        r.e.post({ 0xE0, 0x7F, 0x7F });  // full up
        r.note(60, 100);
        r.run(0.2f, 0.05f, &bent);
    }
    CHECK(plain != bent);

    Rig r;
    for (int k = 60; k < 64; k++) r.note(k, 100);
    r.run(0.05f);
    r.cc(123, 0);  // all notes off: released, then reaped
    r.run(0.5f);
    CHECK(r.e.notesPlaying() == 0);
    r.note(60, 100);
    r.run(0.05f);
    r.cc(120, 0);  // all sound off: no release, so no voice is left after one block
    r.run(256 / SR);
    CHECK(r.e.notesPlaying() == 0);
    CHECK(r.run(0.3f) < 1e-4f);  // the DC filter's step from the cut has decayed
}

static std::vector<float> render(void (*setup)(Rig&)) {
    Rig r;
    setup(r);
    r.note(60, 100);
    std::vector<float> out;
    r.run(1.0f, 0.05f, &out);
    return out;
}

static void testCv() {
    auto plain = render([](Rig&) {});
    auto wheel = render([](Rig& r) { r.cc(1, 127); });
    auto modCv = render([](Rig& r) { r.hold(DexedEngine::I_MOD, 1.0f); });
    auto both = render([](Rig& r) { r.cc(1, 20); r.hold(DexedEngine::I_MOD, 1.0f); });
    CHECK(wheel != plain);  // the wheel adds LFO pitch depth
    CHECK(modCv == wheel);  // 1.0 is the wheel at full
    CHECK(both == wheel);   // the higher of the two

    Rig open, shut;
    shut.hold(DexedEngine::I_CUTOFF, -1.0f);
    open.e.settings.cutoff = shut.e.settings.cutoff = 1.0f;
    open.note(60, 100);
    shut.note(60, 100);
    CHECK(shut.run(0.2f) < 0.5f * open.run(0.2f));
}

static void testProgramVoice() {
    CHECK(programVoice(0) == 0);
    CHECK(programVoice(31) == 31);
    CHECK(programVoice(32) == -1);
    CHECK(programVoice(127) == -1);
    CHECK(programVoice(-1) == -1);
}

static void testApplySettings() {
    uint8_t base[Cartridge::VOICE_BYTES], out[Cartridge::VOICE_BYTES];
    initVoice(base);
    Settings s;
    applySettings(base, s, out);
    CHECK(std::memcmp(base, out, sizeof base) == 0);

    s.opLevel[0] = -50;  // operator 1: the last 21 bytes
    s.opLevel[5] = 120;  // operator 6: the first
    s.algorithm = 32;
    s.feedback = 9;
    s.lfoRate = -99;
    applySettings(base, s, out);
    CHECK(out[5 * 21 + OP_OUTPUT] == 49);
    CHECK(out[OP_OUTPUT] == 99);
    CHECK(out[134] == 31);
    CHECK(out[135] == 7);
    CHECK(out[137] == 0);
}

// DX7 32-voice packing, from the voice data format
static void pack(const uint8_t* v, uint8_t* p) {
    for (int op = 0; op < 6; op++) {
        const uint8_t* o = v + op * 21;
        uint8_t* b = p + op * 17;
        std::memcpy(b, o, 11);
        b[11] = uint8_t(o[11] | (o[12] << 2));
        b[12] = uint8_t(o[13] | (o[20] << 3));
        b[13] = uint8_t(o[14] | (o[15] << 2));
        b[14] = o[16];
        b[15] = uint8_t(o[17] | (o[18] << 1));
        b[16] = o[19];
    }
    std::memcpy(p + 102, v + 126, 8);
    p[110] = v[134];
    p[111] = uint8_t(v[135] | (v[136] << 3));
    std::memcpy(p + 112, v + 137, 4);
    p[116] = uint8_t(v[141] | (v[142] << 1) | (v[143] << 4));
    p[117] = v[144];
    std::memcpy(p + 118, v + 145, 10);
}

static const uint8_t MAXIMA_OP[21] = { 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14 };
static const uint8_t MAXIMA_GLOBAL[19] = { 99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48 };

static std::vector<uint8_t> randomBank(std::vector<std::vector<uint8_t>>& voices, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<uint8_t> sysex = { 0xF0, 0x43, 0x00, 0x09, 0x20, 0x00 };
    voices.assign(32, std::vector<uint8_t>(Cartridge::VOICE_BYTES));
    unsigned sum = 0;
    for (int v = 0; v < 32; v++) {
        auto& u = voices[size_t(v)];
        for (int i = 0; i < 126; i++) u[size_t(i)] = uint8_t(rng() % (MAXIMA_OP[i % 21] + 1));
        for (int i = 0; i < 19; i++) u[size_t(126 + i)] = uint8_t(rng() % (MAXIMA_GLOBAL[i] + 1));
        std::string name = "VOICE " + std::to_string(v + 1);
        name.resize(10, ' ');
        std::memcpy(u.data() + 145, name.data(), 10);
        uint8_t p[128];
        pack(u.data(), p);
        for (uint8_t b : p) {
            sysex.push_back(b);
            sum += b;
        }
    }
    sysex.push_back(uint8_t((0x80 - (sum & 0x7F)) & 0x7F));
    sysex.push_back(0xF7);
    return sysex;
}

static void testCartridge(const std::string& dir) {
    std::vector<std::vector<uint8_t>> voices;
    auto sysex = randomBank(voices, 7);
    CHECK(sysex.size() == size_t(Cartridge::SYSEX_BYTES));

    Cartridge c;
    CHECK(c.parse(sysex.data(), sysex.size()).empty());
    CHECK(c.checksumOk);
    bool same = true;
    for (int v = 0; v < 32; v++) same &= std::memcmp(c.voice[v], voices[size_t(v)].data(), 155) == 0;
    CHECK(same);
    CHECK(c.name(0) == "VOICE 1");
    CHECK(c.name(31) == "VOICE 32");

    // Synth_Dexed's own decoder agrees, but for its 2-bit LFO pitch sensitivity
    struct D : Dexed {
        D() : Dexed(1, 48000) {}
    } d;
    uint8_t dec[156];
    d.decodeVoice(dec, sysex.data() + 6 + 5 * 128);
    dec[143] = c.voice[5][143];
    CHECK(std::memcmp(dec, c.voice[5], 155) == 0);

    // the data alone, and a dump inside a longer file
    Cartridge raw;
    CHECK(raw.parse(sysex.data() + 6, 4096).empty());
    CHECK(std::memcmp(raw.voice, c.voice, sizeof c.voice) == 0);
    std::vector<uint8_t> longer = { 0xF0, 0x7E, 0x00, 0xF7, 0x12 };
    longer.insert(longer.end(), sysex.begin(), sysex.end());
    longer.insert(longer.end(), { 0xF0, 0x43, 0xF7 });
    Cartridge embedded;
    CHECK(embedded.parse(longer.data(), longer.size()).empty());
    CHECK(std::memcmp(embedded.voice, c.voice, sizeof c.voice) == 0);

    // a bad checksum still loads, as in Dexed
    auto bad = sysex;
    bad[4102] ^= 1;
    Cartridge b;
    CHECK(b.parse(bad.data(), bad.size()).empty());
    CHECK(!b.checksumOk);

    // not a cartridge: unchanged
    Cartridge kept = c;
    CHECK(!kept.parse(sysex.data(), 4000).empty());
    auto voice1 = sysex;
    voice1[3] = 0x00;  // a single-voice dump's format
    CHECK(!kept.parse(voice1.data(), voice1.size()).empty());
    CHECK(std::memcmp(kept.voice, c.voice, sizeof c.voice) == 0);

    // out-of-range bytes are clamped: detune 15, a rate of 127, algorithm bits all set
    auto wild = sysex;
    wild[6 + 0] = 0x7F;
    wild[6 + 12] = 0x7F;
    wild[6 + 116] = 0x0E;  // LFO wave 7
    Cartridge w;
    CHECK(w.parse(wild.data(), wild.size()).empty());
    CHECK(w.voice[0][0] == 99);
    CHECK(w.voice[0][20] == 14);
    CHECK(w.voice[0][142] == 5);

    // files
    std::string path = dir + "/bank.syx";
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(sysex.data()), std::streamsize(sysex.size()));
    Cartridge f;
    CHECK(f.load(path).empty());
    CHECK(std::memcmp(f.voice, c.voice, sizeof c.voice) == 0);
    CHECK(!f.load(dir + "/missing.syx").empty());
    std::string big = dir + "/big.syx";
    std::ofstream(big, std::ios::binary) << std::string(70000, 'x');
    CHECK(!f.load(big).empty());
}

// voice 1 has operator 1, the init voice's one carrier, at level 0
static std::unique_ptr<Cartridge> twoVoices() {
    auto c = std::make_unique<Cartridge>();
    for (auto& v : c->voice) initVoice(v);
    c->voice[1][5 * 21 + OP_OUTPUT] = 0;
    return c;
}

static void testVoiceSelection() {
    Rig r;
    r.e.setCartridge(twoVoices());
    r.e.settings.voice = 1;
    r.note(60, 100);
    CHECK(r.run(0.1f) == 0.0f);
    r.e.settings.opLevel[0] = 99;  // an offset reaches the note playing
    CHECK(r.run(0.1f) > 0.01f);
    r.e.settings.opLevel[0] = 0;
    r.e.settings.voice = 0;  // a voice change cuts the note
    r.run(0.01f);
    CHECK(r.e.notesPlaying() == 0);
    r.note(60, 100);
    CHECK(r.run(0.1f) > 0.01f);
    r.e.setCartridge(nullptr);  // the init voice
    r.note(60, 100);
    CHECK(r.run(0.1f) > 0.01f);
}

// MIDI, audio and message threads at once, and a second instance prepared meanwhile
static void testThreads() {
    Rig r(128);
    std::atomic<bool> stop{ false };
    std::thread midi([&] {
        int k = 0;
        while (!stop) {
            r.note(48 + k % 24, 100);
            r.cc(1, k % 128);
            r.e.post({ 0xE0, 0, uint8_t(k % 128) });
            r.note(48 + (k + 12) % 24, 0);
            k++;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    std::thread message([&] {
        for (int i = 0; !stop; i++) {
            r.e.setCartridge(i % 3 ? twoVoices() : nullptr);
            DexedEngine other;
            other.prepare(SR, 128);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
    for (int i = 0; std::chrono::steady_clock::now() < until; i++) {
        r.e.settings.voice = i / 50 % 2;
        r.e.settings.transpose = i / 30 % 5;
        r.run(128 / SR);
    }
    stop = true;
    midi.join();
    message.join();
    CHECK(r.e.eventsDropped() == 0);
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    if (argc > 2 && std::string(argv[2]) == "threads") {
        testThreads();
        return failures ? 1 : 0;
    }
    testNoteAndRelease();
    testBlockSizes();
    testVoiceCap();
    testStealsOldest();
    testTransposeWhileHeld();
    testSustain();
    testControllers();
    testCv();
    testProgramVoice();
    testApplySettings();
    testCartridge(argv[1]);
    testVoiceSelection();
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
