// Native test of prdt::PdEngine against the host build of libpd; built and run by test_prdt.py, and on
// the SSP by run_on_ssp.sh. argv[1] is an empty scratch directory; argv[2] == "threads" runs the threaded
// test only, "bench" prints the DSP time per block, "load FILE" loads FILE and checks it created. PRDT_EXAMPLES and PRDT_EXTRA override the examples and Pd extra/ folders, and
// PRDT_PIN=1 pins each audio thread to its own core from 1 up (the SSP's isolated cores).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>

#include "PdEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

using prdt::PdEngine;
static constexpr int CH = PdEngine::CHANNELS;
static constexpr int BLOCK = 128;

struct Rig {
    PdEngine e;
    std::vector<float> in[CH], out[CH];
    const float* ip[CH];
    float* op[CH];

    explicit Rig(std::vector<std::string> searchPath = {}, float sr = 48000.0f): e(std::move(searchPath)) {
        for (int c = 0; c < CH; c++) {
            in[c].assign(BLOCK, 0.0f);
            out[c].assign(BLOCK, 0.0f);
            ip[c] = in[c].data();
            op[c] = out[c].data();
        }
        e.prepare(sr, BLOCK);
        e.idle();
    }
    // runs `blocks` blocks; returns channel c's last sample
    float run(int blocks, int c = 0) {
        for (int b = 0; b < blocks; b++) e.process(ip, op, BLOCK);
        return out[c][BLOCK - 1];
    }
    // mean |x| of channel c over `blocks` blocks
    double level(int blocks, int c = 0) {
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
    void input(int c, float v) { in[c].assign(BLOCK, v); }
};

// A patch of `objects` ("obj 10 10 osc~ 440"), then `connects` ("0 0 1 0"), then comments, which Pd
// also counts as objects.
static std::string patch(std::initializer_list<const char*> objects, std::initializer_list<const char*> connects,
                         std::initializer_list<const char*> comments = {}) {
    std::string s = "#N canvas 0 50 450 300 12;\n";
    for (auto o : objects) s += std::string("#X ") + o + ";\n";
    for (auto c : comments) s += std::string("#X text 10 200 ") + c + ";\n";
    for (auto c : connects) s += std::string("#X connect ") + c + ";\n";
    return s;
}

static std::string write(const std::string& dir, const char* name, const std::string& text) {
    std::string path = dir + "/" + name;
    std::ofstream(path) << text;
    return path;
}

static void testSpecs() {
    // wrapped across lines, an escaped comma, a box width
    auto s = PdEngine::patchSpecs("#N canvas 0 50 450 300 12;\n#X obj 10 10 r cutoff;\n"
                                  "#X text 10 40 @p1 cutoff 20 20000 Hz\nlog cv 3 \\, f 40;\n"
                                  "#X text 10 80 not a control;\n#X text 10 90 @p3 mix;\n");
    CHECK(s[0].label == "cutoff");
    NEAR(s[0].min, 20, 1e-3);
    NEAR(s[0].max, 20000, 1e-3);
    CHECK(s[0].unit == "Hz");
    CHECK(s[0].log);
    CHECK(s[0].cv == 2);
    CHECK(s[1].label.empty());
    CHECK(s[2].label == "mix");
    // an object box is not a comment
    CHECK(PdEngine::patchSpecs("#N canvas 0 0 1 1 12;\n#X obj 1 1 @p1 x;\n")[0].label.empty());
}

static void testEngine(const std::string& dir) {
    Rig r;
    CHECK(r.e.error().empty());
    CHECK(r.level(20, 0) > 0.005);  // the built-in sounds on 1 and 2
    CHECK(r.level(20, 1) > 0.005);
    CHECK(r.e.spec(0).label == "pitch");

    // input 3 to output 5, one tick late
    r.load(write(dir, "pass.pd", patch({ "obj 10 10 adc~ 3", "obj 10 40 dac~ 5" }, { "0 0 1 0" })));
    CHECK(r.e.error().empty());
    r.input(2, 0.5f);
    r.e.process(r.ip, r.op, BLOCK);
    NEAR(r.out[4][PdEngine::TICK - 1], 0.0, 1e-6);
    NEAR(r.out[4][PdEngine::TICK], 0.5, 1e-6);
    NEAR(r.run(1, 4), 0.5, 1e-6);
    NEAR(r.run(1, 0), 0.0, 1e-6);
    r.input(2, 0.0f);

    // a declared control reaches its receiver in its range; an undeclared one reaches [r p2]
    r.load(write(dir, "ctl.pd", patch({ "obj 10 10 r gain", "obj 10 40 sig~", "obj 10 70 dac~ 1", "obj 100 10 r p2",
                                        "obj 100 40 sig~", "obj 100 70 dac~ 2" },
                                      { "0 0 1 0", "1 0 2 0", "3 0 4 0", "4 0 5 0" }, { "@p1 gain 0 10" })));
    CHECK(r.e.error().empty());
    CHECK(r.e.spec(0).label == "gain");
    r.e.setParam(0, 0.5f);
    r.e.setParam(1, 0.25f);
    NEAR(r.run(2, 0), 5.0, 1e-5);
    NEAR(r.run(1, 1), 0.25, 1e-6);
    r.e.setParam(0, 1.0f);
    NEAR(r.run(2, 0), 10.0, 1e-5);

    // a missing object fails the load and keeps the running patch
    r.load(write(dir, "bad.pd", patch({ "obj 10 10 nosuchobject~", "obj 10 40 dac~ 1" }, {})));
    CHECK(r.e.error().find("nosuchobject~") != std::string::npos);
    NEAR(r.run(2, 0), 10.0, 1e-5);
    r.load(write(dir, "text.pd", "not a patch\n"));
    CHECK(r.e.error().find("not a Pd patch") != std::string::npos);
    NEAR(r.run(1, 0), 10.0, 1e-5);

    // MIDI: note, controller and bend
    r.load(write(dir, "midi.pd", patch({ "obj 10 10 notein", "obj 10 40 sig~", "obj 10 70 dac~ 1", "obj 100 10 ctlin 7",
                                         "obj 100 40 sig~", "obj 100 70 dac~ 2", "obj 200 10 bendin", "obj 200 40 sig~",
                                         "obj 200 70 dac~ 3" },
                                       { "0 0 1 0", "1 0 2 0", "3 0 4 0", "4 0 5 0", "6 0 7 0", "7 0 8 0" })));
    CHECK(r.e.error().empty());
    r.e.midi(0x90, 60, 100);
    r.e.midi(0xB3, 7, 99);
    r.e.midi(0xE0, 0, 0x40);
    NEAR(r.run(2, 0), 60, 1e-6);
    NEAR(r.run(1, 1), 99, 1e-6);
    NEAR(r.run(1, 2), 8192, 1e-3);
    CHECK(r.e.status().find("MIDI notes: 1") != std::string::npos);

    // the console: loadbang prints, kept to the last CONSOLE lines
    r.load(write(dir, "print.pd", patch({ "obj 10 10 loadbang", "obj 10 40 print hello" }, { "0 0 1 0" })));
    CHECK(r.e.status().find("hello: bang") != std::string::npos);

    // the extra/ objects, [expr] and [expr~] are built in
    r.load(write(dir, "extra.pd", patch({ "obj 10 10 bob~", "obj 10 30 bonk~", "obj 10 50 choice", "obj 10 70 fiddle~",
                                          "obj 10 90 loop~", "obj 10 110 lrshift~ 1", "obj 10 130 pique",
                                          "obj 10 150 sigmund~", "obj 10 170 stdout", "obj 10 190 expr $f1*2",
                                          "obj 10 210 expr~ $v1*2" },
                                        {})));
    CHECK(r.e.error().empty());
    if (!r.e.error().empty()) std::fprintf(stderr, "%s\n", r.e.error().c_str());
}

// cyclone, compiled in: objects create in a module's instance, and symbol arguments, which cyclone
// compares against symbols it made at setup in the main instance, select their mode
static void testCyclone(const std::string& dir) {
    Rig r;
    r.load(write(dir, "cyclone.pd", patch({ "obj 10 10 counter", "obj 10 30 coll", "obj 10 50 zl.len", "obj 10 70 cycle~ 440",
                                            "obj 10 90 svf~", "obj 10 110 tosymbol", "obj 10 130 fromsymbol", "obj 10 150 pv x",
                                            "obj 10 170 seq", "obj 10 190 table", "obj 10 210 urn 8", "obj 10 230 drunk",
                                            "obj 10 250 pong~", "obj 10 270 curve~", "obj 10 290 funbuff", "obj 10 310 append a",
                                            "obj 10 330 rand~", "obj 10 350 !-~", "obj 10 370 cyclone/clip~ 0 1",
                                            "obj 10 390 delay~", "obj 10 410 capture", "obj 10 430 mtr" },
                                          {})));
    CHECK(r.e.error().empty());
    if (!r.e.error().empty()) std::fprintf(stderr, "%s\n", r.e.error().c_str());

    // [onepole~ 0.5 linear]: a coefficient of sin(0.5 pi / 2), a step's first sample; read as Hz, it is 1e-4.
    // [sig~] starts inside Pd, so its first sample is frame 0.
    r.load(write(dir, "onepole.pd", patch({ "obj 10 10 sig~ 1", "obj 10 40 onepole~ 0.5 linear", "obj 10 70 dac~ 1" },
                                          { "0 0 1 0", "1 0 2 0" })));
    CHECK(r.e.error().empty());
    r.e.process(r.ip, r.op, BLOCK);
    NEAR(r.out[0][0], 0.70710678, 1e-6);
    // [coll @embed 1] restores the data saved in the patch, sent to #C, a symbol of the module's instance
    write(dir, "embed.pd", "#N canvas 0 50 450 300 12;\n#X obj 10 10 coll @embed 1;\n#C store foo 42;\n#C restore;\n"
                           "#X obj 10 40 loadbang;\n#X msg 10 70 foo;\n#X obj 10 100 print got;\n"
                           "#X connect 1 0 2 0;\n#X connect 2 0 0 0;\n#X connect 0 0 3 0;\n");
    r.load(dir + "/embed.pd");
    CHECK(r.e.error().empty());
    CHECK(r.e.status().find("got: 42") != std::string::npos);
    // [zl.len], a zl alias: the length of a list
    r.load(write(dir, "zl.pd", patch({ "obj 10 10 loadbang", "msg 10 40 1 2 3", "obj 10 70 zl.len", "obj 10 100 print len" },
                                     { "0 0 1 0", "1 0 2 0", "2 0 3 0" })));
    CHECK(r.e.status().find("len: 3") != std::string::npos);
}

// abstractions from the patch's folder, then from the search path
static void testAbstractions(const std::string& dir) {
    std::string lib = dir + "/lib", proj = dir + "/proj";
    mkdir(lib.c_str(), 0755);
    mkdir(proj.c_str(), 0755);
    const char* io = "#N canvas 0 0 300 200 12;\n#X obj 10 10 inlet~;\n#X obj 10 40 *~ %s;\n#X obj 10 70 outlet~;\n"
                     "#X connect 0 0 1 0;\n#X connect 1 0 2 0;\n";
    char buf[256];
    std::snprintf(buf, sizeof(buf), io, "2");
    write(proj, "twice~.pd", buf);
    std::snprintf(buf, sizeof(buf), io, "3");
    write(lib, "thrice~.pd", buf);
    Rig r({ lib });
    r.load(write(proj, "main.pd", patch({ "obj 10 10 adc~ 1", "obj 10 40 twice~", "obj 10 70 thrice~", "obj 10 100 dac~ 1" },
                                        { "0 0 1 0", "1 0 2 0", "2 0 3 0" })));
    CHECK(r.e.error().empty());
    r.input(0, 0.125f);
    NEAR(r.run(2, 0), 0.75, 1e-6);
    // without the search path, thrice~ is missing
    Rig bare;
    bare.load(proj + "/main.pd");
    CHECK(bare.e.error().find("thrice~") != std::string::npos);
}

// the host rate reaches the instance
static void testRate(const std::string& dir) {
    Rig r({}, 44100.0f);
    r.load(write(dir, "sr.pd", patch({ "obj 10 10 loadbang", "obj 10 40 samplerate~", "obj 10 70 print sr" },
                                     { "0 0 1 0", "1 0 2 0" })));
    CHECK(r.e.status().find("sr: 44100") != std::string::npos);
}

static void pin() {
    static std::atomic<int> next{ 0 };
    if (!std::getenv("PRDT_PIN")) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(1 + next.fetch_add(1) % 3, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof set, &set) != 0) std::fprintf(stderr, "PRDT_PIN failed\n");
}

static std::string examples() {
    const char* dir = std::getenv("PRDT_EXAMPLES");
    return dir ? dir : EXAMPLES;
}

// Two modules' audio threads run while a third thread reloads both: each output is silence or a
// patch's constant.
static void testThreads(const std::string& dir) {
    // cyclone objects with shared state, created on each load and run on the audio thread; output 2 only
    auto busy = [&](const char* name, const char* value) {
        std::string sig = std::string("obj 10 10 sig~ ") + value;
        return write(dir, name, patch({ sig.c_str(), "obj 10 40 dac~ 1", "obj 100 10 loadbang", "obj 100 40 metro 1",
                                        "obj 100 70 drunk 100 5", "obj 100 100 tosymbol", "obj 100 130 fromsymbol",
                                        "obj 100 160 zl.len", "obj 100 190 urn 16", "obj 200 10 cycle~ 440",
                                        "obj 200 40 rand~ 100", "obj 200 70 +~", "obj 200 100 *~ 0", "obj 200 130 dac~ 2",
                                        "obj 300 10 coll", "obj 300 40 pv shared" },
                                      { "0 0 1 0", "2 0 3 0", "3 0 4 0", "4 0 5 0", "5 0 6 0", "6 0 7 0", "4 0 8 0",
                                        "9 0 11 0", "10 0 11 1", "11 0 12 0", "12 0 13 0" }));
    };
    std::string a = busy("a.pd", "0.25");
    std::string b = busy("b.pd", "0.5");
    Rig r1, r2;
    r1.load(a);
    r2.load(b);
    std::atomic<bool> stop{ false };
    std::atomic<int> wrong{ 0 };
    auto audio = [&](Rig* r) {
        pin();
        while (!stop.load()) {
            r->e.process(r->ip, r->op, BLOCK);
            for (float v : r->out[0])
                if (v != 0.0f && v != 0.25f && v != 0.5f) wrong++;
            (void)r->e.status();
        }
    };
    std::thread t1(audio, &r1), t2(audio, &r2);
    for (int k = 0; k < 40; k++) {
        r1.load(k % 2 ? a : b);
        r2.load(k % 2 ? b : a);
        r1.e.midi(0x90, 60, 1);
    }
    stop = true;
    t1.join();
    t2.join();
    CHECK(wrong.load() == 0);
    CHECK(r1.e.error().empty());
    CHECK(r2.e.error().empty());
}

// the shipped examples load and do what their comments say
static void testExamples() {
    const std::string dir = examples();
    Rig synth;
    synth.load(dir + "/synth.pd");
    CHECK(synth.e.error().empty());
    CHECK(synth.e.spec(0).label == "cutoff" && synth.e.spec(1).label == "release");
    CHECK(synth.level(4, 0) < 1e-6);
    synth.e.midi(0x90, 57, 100);
    CHECK(synth.level(20, 0) > 0.01);
    CHECK(synth.level(1, 1) > 0.01);
    synth.e.midi(0x80, 57, 0);
    synth.level(200, 0);  // 0.5 s; the release is 20 ms at the knob's 0
    CHECK(synth.level(4, 0) < 1e-6);

    Rig cv;
    cv.load(dir + "/cv.pd");
    CHECK(cv.e.error().empty());
    cv.e.setParam(0, 0.5f);  // base 200 Hz; input 1 at 0.2 is one volt: 400 Hz
    cv.input(0, 0.2f);
    cv.run(4);
    int crossings = 0;
    float last = cv.out[0][0];
    for (int b = 0; b < 75; b++) {  // 0.2 s
        cv.e.process(cv.ip, cv.op, BLOCK);
        for (float v : cv.out[0]) {
            if (last < 0.0f && v >= 0.0f) crossings++;
            last = v;
        }
    }
    CHECK(crossings >= 79 && crossings <= 81);

    // the notes come from [coll]'s embedded data: silence would mean they did not restore
    Rig arp;
    arp.load(dir + "/arp.pd");
    CHECK(arp.e.error().empty());
    CHECK(arp.e.spec(0).label == "rate" && arp.e.spec(1).label == "cutoff");
    CHECK(arp.level(40, 0) > 0.02);
    CHECK(arp.level(1, 1) > 0.02);

    Rig delay;
    delay.load(dir + "/delay.pd");
    CHECK(delay.e.error().empty());
    delay.e.setParam(2, 1.0f);  // all wet, time at its 50 ms minimum
    delay.input(0, 1.0f);
    delay.run(1);
    delay.input(0, 0.0f);
    delay.run(12);  // 35 ms in, before the echo at 50 ms
    NEAR(delay.out[0][BLOCK - 1], 0.0, 1e-6);
    double echo = 0.0;
    for (int b = 0; b < 10; b++) {
        delay.e.process(delay.ip, delay.op, BLOCK);
        for (float v : delay.out[0]) echo = std::max(echo, double(v));
    }
    NEAR(echo, 1.0, 1e-6);
}

// test_prdt.py builds dir/hello.pd_linux: an external cannot find Pd's functions, so it fails to load
static void testExternal(const std::string& dir) {
    Rig r;
    r.load(write(dir, "ext.pd", patch({ "obj 10 10 hello" }, {})));
    CHECK(r.e.error().find("hello") != std::string::npos);
    CHECK(r.level(4, 0) > 0.005);  // the built-in still plays
}

// microseconds per 128-frame block, the mean over 2000 blocks (5.3 s of audio at 48 kHz)
static double bench(Rig& r) {
    pin();
    r.run(100);
    auto t0 = std::chrono::steady_clock::now();
    r.run(2000);
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 2000;
}

static void testBench() {
    const double budget = 1e6 * BLOCK / 48000.0;
    auto report = [&](const char* what, double us) {
        std::printf("%-24s %7.1f us/block  %5.1f%% of %.0f us\n", what, us, 100.0 * us / budget, budget);
    };
    Rig builtin;
    report("built-in", bench(builtin));
    Rig synth;
    synth.load(examples() + "/synth.pd");
    synth.e.midi(0x90, 57, 100);
    report("synth.pd, a held note", bench(synth));
    Rig delay;
    delay.load(examples() + "/delay.pd");
    delay.e.setParam(1, 0.5f);
    delay.e.setParam(2, 0.5f);
    delay.input(0, 0.1f);
    report("delay.pd", bench(delay));
    const char* extra = std::getenv("PRDT_EXTRA");
    Rig rev({ extra ? extra : EXTRA });
    rev.load(write("/tmp", "prdt-rev3.pd", patch({ "obj 10 10 adc~ 1", "obj 10 40 rev3~ 100 90 3000 20", "obj 10 70 dac~ 1 2" },
                                                   { "0 0 1 0", "1 0 2 0", "1 1 2 1" })));
    CHECK(rev.e.error().empty());
    rev.input(0, 0.1f);
    report("rev3~ (extra/)", bench(rev));
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string dir = argv[1];
    if (argc > 3 && std::string(argv[2]) == "load") {  // argv[3] loads without error, then runs
        Rig r;
        r.load(argv[3]);
        if (!r.e.error().empty()) std::fprintf(stderr, "%s\n", r.e.error().c_str());
        CHECK(r.e.error().empty());
        r.run(40);
    } else if (argc > 2 && std::string(argv[2]) == "bench") {
        testBench();
    } else if (argc > 2 && std::string(argv[2]) == "external") {
        testExternal(dir);
    } else if (argc > 2 && std::string(argv[2]) == "threads") {
        testThreads(dir);
        testExamples();
    } else {
        testSpecs();
        testEngine(dir);
        testAbstractions(dir);
        testCyclone(dir);
        testRate(dir);
        testThreads(dir);
        testExamples();
    }
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
