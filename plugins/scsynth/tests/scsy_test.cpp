// Native test of scsy::ScsynthEngine; built and run by test_scsynth.py.
// usage: scsy_test <core UGen dir> <defs dir> <empty scratch dir>
//        scsy_test example <UGen dir> <.scsyndef>   (an example sounds, with a signal on inputs 1, 2
//                                                    and a held middle C)

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ScsynthEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

using scsy::ScsynthEngine;
static constexpr int CH = ScsynthEngine::CHANNELS;
static constexpr int BLOCK = 128;
static std::string defs, scratch;

struct Rig {
    ScsynthEngine e;
    float sr;
    std::vector<float> in[CH], out[CH];
    const float* ip[CH];
    float* op[CH];

    Rig(const std::string& ugens, float rate = 48000.0f): e(ugens), sr(rate) {
        for (int c = 0; c < CH; c++) {
            in[c].assign(BLOCK, 0.0f);
            out[c].assign(BLOCK, 0.0f);
            ip[c] = in[c].data();
            op[c] = out[c].data();
        }
        e.prepare(sr, BLOCK);  // no audio thread yet: the built-in loads by pumping
        defaults();
    }
    // as ScriptProcessor::applyDefaults
    void defaults() {
        for (int i = 0; i < ScsynthEngine::PARAMS; i++)
            if (e.spec(i).def >= 0.0f) e.setParam(i, e.spec(i).def);
    }
    std::vector<float> run(int blocks, int c = 0) {
        std::vector<float> got;
        for (int b = 0; b < blocks; b++) {
            e.process(ip, op, BLOCK);
            got.insert(got.end(), out[c].begin(), out[c].end());
        }
        return got;
    }
    // load() and idle() on this thread while another runs process(), as in the plugin
    std::string load(const std::string& path) {
        std::atomic<bool> running{ true };
        std::thread audio([&] {
            while (running) {
                run(1);
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        });
        unsigned gen = e.specsGeneration();
        e.load(path);
        e.idle();
        running = false;
        audio.join();
        // as ScriptEditor: defaults only for new specs, so not after a failed load
        if (e.specsGeneration() != gen) defaults();
        run(2);  // performs them
        return e.error();
    }
};

static float peak(const std::vector<float>& x) {
    float p = 0.0f;
    for (float v : x) p = std::max(p, std::fabs(v));
    return p;
}

// frequency from rising zero crossings: about +-4 Hz over 100 blocks at 48 kHz
static double frequency(const std::vector<float>& x, double sr) {
    long first = -1, last = -1, count = 0;
    for (size_t i = 1; i < x.size(); i++)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (first < 0) first = long(i);
            last = long(i);
            count++;
        }
    return count > 1 ? (count - 1) * sr / double(last - first) : 0.0;
}
#define HZ(x, hz) NEAR(frequency(x, r.sr), hz, (hz) * 0.01)

static void write(const std::string& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

static std::string copyDef(const std::string& name) {
    std::ifstream f(defs + "/" + name + ".scsyndef", std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string path = scratch + "/" + name + ".scsyndef";
    write(path, bytes);
    return path;
}

static int example(const std::string& ugens, const std::string& path) {
    Rig r(ugens);
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < BLOCK; i++) r.in[c][i] = 0.5f * std::sin(2.0 * M_PI * 220.0 * i / r.sr);
    std::string error = r.load(path);
    if (!error.empty()) std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
    CHECK(error.empty());
    r.e.note(60, 100);  // a voice def sounds only with a note; others ignore it
    auto x = r.run(100);
    bool finite = true;
    for (float v : x) finite = finite && std::isfinite(v);
    CHECK(finite);
    std::printf("peak %g\n", peak(x));
    CHECK(peak(x) > 1e-3f);
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "example") return example(argv[2], argv[3]);
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <core UGen dir> <defs dir> <scratch dir>\n", argv[0]);
        return 2;
    }
    std::string ugens = argv[1];
    defs = argv[2];
    scratch = argv[3];

    {  // the built-in, loaded in prepare() with no audio thread
        Rig r(ugens);
        CHECK(r.e.error().empty());
        CHECK(r.e.spec(0).label == "pitch" && r.e.spec(0).log && r.e.spec(0).unit == "Hz");
        CHECK(r.e.spec(1).label == "level" && r.e.spec(2).label == "cutoff");
        NEAR(r.e.value(0), 110.0, 1e-3);
        r.run(4);
        CHECK(peak(r.run(20, 0)) > 0.02f && peak(r.run(20, 1)) > 0.02f);
    }

    Rig r(ugens);
    {  // a def without a sidecar: every control, in the def's order, ranges from the defaults
        CHECK(r.load(defs + "/sine.scsyndef").empty());
        CHECK(r.e.spec(0).label == "amp" && r.e.spec(1).label == "freq");  // nanosynth sorts controls
        NEAR(r.e.spec(0).max, 1.0, 1e-6);
        NEAR(r.e.spec(1).max, 880.0, 1e-3);
        NEAR(r.e.value(1), 440.0, 1e-2);
        HZ(r.run(100), 440.0);
        r.e.setParam(1, 0.25f);  // 220 Hz
        r.run(2);
        auto x = r.run(100);
        HZ(x, 220.0);
        NEAR(peak(x), 0.5, 1e-3);
    }
    {  // a sidecar chooses, orders and scales the controls
        std::string path = copyDef("sine");
        write(scratch + "/sine.txt", "// @p1 amp\n// @p2 freq 100 1000 Hz log\n");
        CHECK(r.load(path).empty());
        CHECK(r.e.spec(0).label == "amp" && r.e.spec(1).label == "freq" && r.e.spec(1).log);
        CHECK(r.e.spec(2).label.empty());
        NEAR(r.e.value(1), 440.0, 1e-1);
        r.e.setParam(1, r.e.spec(1).unmap(500.0f));
        r.e.setParam(0, 0.25f);
        r.run(2);
        auto x = r.run(100);
        HZ(x, 500.0);
        NEAR(peak(x), 0.25, 1e-3);
    }
    {  // `cv 3` in the sidecar: input 3 moves freq an octave per volt (0.2 per volt on the SSP)
        std::string path = copyDef("sine");
        write(scratch + "/sine.txt", "// @p1 amp\n// @p2 freq 100 1000 Hz log cv 3\n");
        CHECK(r.load(path).empty());
        CHECK(r.e.spec(1).cv == 2);
        r.e.setParam(1, r.e.spec(1).unmap(250.0f));
        std::fill(r.in[2].begin(), r.in[2].end(), 0.2f);  // 1 V
        r.run(2);
        HZ(r.run(100), 500.0);
        std::fill(r.in[2].begin(), r.in[2].end(), -0.2f);  // -1 V
        r.run(2);
        HZ(r.run(100), 125.0);
        std::fill(r.in[2].begin(), r.in[2].end(), 0.0f);
        r.e.setParam(1, r.e.spec(1).unmap(500.0f));
        r.run(2);
    }
    {  // a control with CV follows it at audio rate, sample by sample, by the display's formula
        std::string path = copyDef("follow");
        write(scratch + "/follow.txt", "// @p1 x -10 10 cv 3\n");
        CHECK(r.load(path).empty());
        r.e.setParam(0, 0.5f);  // 0
        for (int i = 0; i < BLOCK; i++) r.in[2][size_t(i)] = 0.2f * float(i) / BLOCK;  // 0 to 1 V in a block
        r.run(2);
        auto x = r.run(1);
        bool follows = true;
        for (int i = 0; i < BLOCK; i++) {
            float want = r.e.spec(0).modulated(0.5f, float(i) / BLOCK);  // 2 per volt
            follows = follows && std::fabs(x[size_t(i)] - want) < 1e-5f;
        }
        CHECK(follows);
        NEAR(x[BLOCK - 1], 2.0 * (BLOCK - 1) / BLOCK, 1e-5);
        std::fill(r.in[2].begin(), r.in[2].end(), 0.0f);
    }
    {  // an audio-rate control read only at the start gets the def's default, as a control-rate one
        std::string path = copyDef("hold");
        write(scratch + "/hold.txt", "// @p1 x 0 1 cv 3\n");
        CHECK(r.load(path).empty());
        NEAR(r.run(10).back(), 0.55078125, 1e-6);
    }
    {  // the sine again, for the tests below
        std::string path = copyDef("sine");
        write(scratch + "/sine.txt", "// @p1 amp\n// @p2 freq 100 1000 Hz log\n");
        CHECK(r.load(path).empty());
        r.e.setParam(1, r.e.spec(1).unmap(500.0f));
        r.run(2);
    }
    {  // a sidecar naming no control of the def: an error, and the running synth goes on
        std::string path = copyDef("thru");
        write(scratch + "/thru.txt", "// @p1 nope\n");
        std::string error = r.load(path);
        std::fprintf(stderr, "bad sidecar: %s\n", error.c_str());
        CHECK(error.find("names nope") != std::string::npos);
        HZ(r.run(100), 500.0);
    }
    {  // a UGen this World lacks, and a file that is not a def
        std::string error = r.load(defs + "/dfm1.scsyndef");
        CHECK(error.find("UGen 'DFM1' not installed") != std::string::npos);
        error = r.load(defs + "/defs.py");
        CHECK(error.find("not a SynthDef file") != std::string::npos);
        HZ(r.run(100), 500.0);
    }
    {  // binary: a 0x0d byte in the file survives
        CHECK(r.load(defs + "/cr.scsyndef").empty());
        NEAR(r.e.value(0), 0.55078125, 1e-6);
        auto x = r.run(10);
        NEAR(x.back(), 0.55078125, 1e-6);
    }
    {  // MIDI: a def with a gate control is a voice per note
        CHECK(r.load(defs + "/voice.scsyndef").empty());
        CHECK(r.e.spec(0).label == "level" && r.e.spec(1).label.empty());  // notes set freq, velocity, gate
        auto voices = [&](int c) {
            r.run(4);  // the notes, then the envelopes
            return r.run(1, c).back();
        };
        NEAR(voices(0), 0.0, 1e-6);  // no synth until a note
        r.e.note(69, 127);
        NEAR(voices(0), 1.0, 1e-4);   // velocity 127 is 1
        NEAR(voices(1), 0.44, 1e-4);  // A4 is 440 Hz
        r.e.note(72, 64);
        NEAR(voices(0), 1.0 + 64.0 / 127.0, 1e-4);
        r.e.note(69, 0);
        r.run(40);  // the 10 ms release, then the synth frees itself
        NEAR(voices(0), 64.0 / 127.0, 1e-4);
        NEAR(voices(1), 0.440 * std::pow(2.0, 3.0 / 12.0), 1e-4);  // C5 alone
        r.e.note(72, 0);
        r.run(40);
        NEAR(voices(0), 0.0, 1e-6);

        for (int n = 40; n < 60; n++) r.e.note(n, 127);  // 20 held: the first 4 are released
        r.run(40);
        NEAR(voices(0), double(scsy::ScWorld::VOICES), 1e-3);
        CHECK(r.e.status() == "MIDI notes: 22, held: " + std::to_string(scsy::ScWorld::VOICES));  // 2 + 20
        r.e.setParam(0, r.e.spec(0).unmap(0.5f));  // level drives every voice
        NEAR(voices(0), 0.5 * scsy::ScWorld::VOICES, 1e-3);
        for (int n = 40; n < 60; n++) r.e.note(n, 0);
        r.run(40);
        NEAR(voices(0), 0.0, 1e-6);

        r.e.note(60, 127);
        NEAR(voices(1), 0.440 * std::pow(2.0, -9.0 / 12.0), 1e-4);
        CHECK(r.load(defs + "/thru.scsyndef").empty());  // another def frees the voices
        std::fill(r.in[1].begin(), r.in[1].end(), 0.0f);
        NEAR(voices(1), 0.0, 1e-6);

        std::string path = copyDef("voice");
        write(scratch + "/voice.txt", "// @p1 gate\n");
        CHECK(r.load(path).find("which MIDI notes set") != std::string::npos);
    }
    {  // a def without gate: notes set freq and velocity when no control is mapped to them
        std::string path = copyDef("sine");
        write(scratch + "/sine.txt", "// @p1 amp\n");
        CHECK(r.load(path).empty());
        r.e.note(57, 100);  // A3, 220 Hz
        r.run(2);
        HZ(r.run(100), 220.0);
    }
    {  // notes from a thread of their own while loads and the audio run, for ThreadSanitizer
        std::atomic<bool> stop{ false };
        std::thread midi([&] {
            for (int k = 0; !stop; k++) {
                r.e.note(48 + k % 24, k % 3 ? 100 : 0);
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
        });
        CHECK(r.load(defs + "/voice.scsyndef").empty());
        CHECK(r.load(defs + "/sine.scsyndef").empty());
        CHECK(r.load(defs + "/voice.scsyndef").empty());
        stop = true;
        midi.join();
        for (int n = 0; n < 128; n++) r.e.note(n, 0);
        r.run(40);
    }
    {  // a new rate reopens the World and reloads the program, with no audio thread
        CHECK(r.load(defs + "/sine.scsyndef").empty());
        r.sr = 44100.0f;
        r.e.prepare(r.sr, BLOCK);
        r.defaults();
        r.run(2);
        CHECK(r.e.error().empty());
        HZ(r.run(100), 440.0);
    }
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
