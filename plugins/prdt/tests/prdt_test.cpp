// Native test of prdt::PdEngine against the host build of libpd; built and run by test_prdt.py, and on
// the SSP by run_on_ssp.sh. argv[1] is an empty scratch directory; argv[2] == "threads" runs the threaded
// test only, "bench" prints the DSP time per block, "load FILE [DIR...]" loads FILE, with the DIRs as the search
// path, and checks it created. PRDT_EXAMPLES, PRDT_EXTRA and PRDT_ELSE override the examples, Pd extra/ and ELSE
// abstraction folders, and PRDT_PIN=1 pins each audio thread to its own core from 1 up (the SSP's isolated cores).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>
#include <pthread.h>
#include <dirent.h>
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

// [clone] makes abstractions after the load, which a reload frees before the array: Pd left #A pointing at the
// last one, and the array's free read it (external/libpd/PATCHES.md); a use after free under AddressSanitizer
static void testCloneArray(const std::string& dir) {
    write(dir, "one~.pd", "#N canvas 0 0 300 200 12;\n#X obj 10 10 inlet~;\n#X obj 10 40 outlet~;\n#X connect 0 0 1 0;\n");
    Rig r;
    r.load(write(dir, "clonearray.pd", patch({ "obj 10 10 clone one~ 1", "obj 10 40 loadbang", "msg 10 70 resize 3",
                                               "obj 10 100 array define arr 4" },
                                             { "1 0 2 0", "2 0 0 0" })));
    CHECK(r.e.error().empty());
    r.load(write(dir, "empty.pd", patch({}, {})));
    CHECK(r.e.error().empty());
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

static void writeWav(const std::string& path, int frames);

// Two modules' audio threads run while a third thread reloads both: each output is silence or a
// patch's constant.
static void testThreads(const std::string& dir) {
    // cyclone and ELSE objects with shared state, created on each load and run on the audio thread; output 2 only
    auto busy = [&](const char* name, const char* value) {
        std::string sig = std::string("obj 10 10 sig~ ") + value;
        return write(dir, name, patch({ sig.c_str(), "obj 10 40 dac~ 1", "obj 100 10 loadbang", "obj 100 40 metro 1",
                                        "obj 100 70 drunk 100 5", "obj 100 100 tosymbol", "obj 100 130 fromsymbol",
                                        "obj 100 160 zl.len", "obj 100 190 urn 16", "obj 200 10 cycle~ 440",
                                        "obj 200 40 rand~ 100", "obj 200 70 +~", "obj 200 100 *~ 0", "obj 200 130 dac~ 2",
                                        "obj 300 10 coll", "obj 300 40 pv shared", "obj 400 10 osc.format",
                                        "obj 400 40 osc.parse", "obj 400 70 rand.f", "obj 400 100 pluck~ 440",
                                        "obj 400 130 messcoll", "obj 400 160 knob", "obj 500 10 play.file~ -loop two.wav 1",
                                        "obj 500 40 sfload -t sa", "obj 500 70 array define 0-sa", "msg 500 100 load two.wav" },
                                      { "0 0 1 0", "2 0 3 0", "3 0 4 0", "4 0 5 0", "5 0 6 0", "6 0 7 0", "4 0 8 0",
                                        "9 0 11 0", "10 0 11 1", "11 0 12 0", "12 0 13 0", "4 0 16 0", "16 0 17 0",
                                        "3 0 18 0", "3 0 19 0", "19 0 12 0", "22 0 12 0", "2 0 25 0", "25 0 23 0" }));
    };
    writeWav(dir + "/two.wav", 1000);
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

    Rig jacks;
    jacks.load(dir + "/jacks.pd");
    CHECK(jacks.e.error().empty());
    jacks.e.setParam(0, 0.5f);  // gain 0.5
    jacks.e.setParam(1, 0.4f);  // 2 V
    jacks.input(0, 0.5f);
    CHECK(std::fabs(jacks.run(8, 0) - 0.25f) < 1e-5f);
    CHECK(std::fabs(jacks.run(1, 1) - 0.4f) < 1e-5f);
    CHECK(jacks.run(1, 2) == 0.0f);
    jacks.input(2, 1.0f);  // a gate on In 3 opens Out 3 and fires one 5 ms pulse on Out 4
    int pulse = 0;
    for (int b = 0; b < 40; b++) {
        jacks.e.process(jacks.ip, jacks.op, BLOCK);
        for (float v : jacks.out[3]) pulse += v > 0.5f;
    }
    CHECK(pulse == 240);
    CHECK(jacks.out[2][BLOCK - 1] == 1.0f);
    jacks.input(2, 0.0f);
    CHECK(jacks.run(8, 2) == 0.0f);

    Rig midicv;
    midicv.load(dir + "/midicv.pd");
    CHECK(midicv.e.error().empty());
    midicv.e.midi(0x90, 72, 100);  // C5: 1 V, gate open
    midicv.run(4);
    CHECK(std::fabs(midicv.out[0][BLOCK - 1] - 0.2f) < 1e-5f);
    CHECK(midicv.out[1][BLOCK - 1] == 1.0f);
    midicv.e.midi(0x80, 72, 0);  // the gate closes; the pitch holds
    midicv.run(4);
    CHECK(std::fabs(midicv.out[0][BLOCK - 1] - 0.2f) < 1e-5f);
    CHECK(midicv.out[1][BLOCK - 1] == 0.0f);
    midicv.e.midi(0x90, 60, 100);  // C4: 0 V
    midicv.run(4);
    CHECK(std::fabs(midicv.out[0][BLOCK - 1]) < 1e-5f);

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

// The ELSE abstractions, flat in dir/else as install-prdt.sh copies them: from PRDT_ELSE, or the repo's folders
// (dirent, as the SSP's libstdc++ keeps std::filesystem in a separate library)
static void copyPd(const std::string& from, const std::string& to) {
    DIR* d = opendir(from.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name, path = from + "/" + name;
        struct stat st;
        if (name[0] == '.' || stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode))
            copyPd(path, to);
        else if (name.size() > 3 && name.compare(name.size() - 3, 3, ".pd") == 0)
            std::ofstream(to + "/" + name, std::ios::binary) << std::ifstream(path, std::ios::binary).rdbuf();
    }
    closedir(d);
}

static void copyElse(const std::string& dir) {
    const char* src = std::getenv("PRDT_ELSE");
    mkdir((dir + "/else").c_str(), 0755);
    copyPd(src ? src : ELSE_ABSTRACTIONS, dir + "/else");
}

// ELSE, compiled in: objects by their bare and else/ names, an alias (s2f~), a name cyclone also has, and an
// abstraction by both names from a patch in another folder, as BOOT/prdt and BOOT/prdt/else are on the path
static void testElse(const std::string& dir) {
    copyElse(dir);
    std::string proj = dir + "/elseproj";
    mkdir(proj.c_str(), 0755);
    Rig r({ dir, dir + "/else" });
    r.load(write(proj, "else.pd", patch({ "obj 10 10 knob", "obj 10 30 else/knob", "obj 10 50 pink~", "obj 10 70 else/pink~",
                                          "obj 10 90 osc.format", "obj 10 110 else/osc.parse", "obj 10 130 pluck~",
                                          "obj 10 150 messcoll", "obj 10 170 f2s~", "obj 10 190 else/f2s~",
                                          "obj 10 210 else/cartopol", "obj 10 230 s2f~", "obj 10 250 bd~",
                                          "obj 10 270 else/rand.f" },
                                        {})));
    CHECK(r.e.error().empty());
    if (!r.e.error().empty()) std::fprintf(stderr, "%s\n", r.e.error().c_str());

    // [messcoll -k] restores the data saved in the patch under #C, a symbol of the module's instance
    write(dir, "messcoll.pd", "#N canvas 0 50 450 300 12;\n#X obj 10 10 messcoll -k;\n#C store 1 42;\n#C keep 1;\n"
                              "#C restore;\n#X obj 10 40 loadbang;\n#X msg 10 70 1;\n#X obj 10 100 print got;\n"
                              "#X connect 1 0 2 0;\n#X connect 2 0 0 0;\n#X connect 0 0 3 0;\n");
    r.load(dir + "/messcoll.pd");
    CHECK(r.e.error().empty());
    CHECK(r.e.status().find("got: 42") != std::string::npos);

    // [osc.format] to [osc.parse]: the address and arguments back
    r.load(write(dir, "osc.pd", patch({ "obj 10 10 loadbang", "msg 10 40 /x 1 2", "obj 10 70 osc.format", "obj 10 100 osc.parse",
                                        "obj 10 130 print osc" },
                                      { "0 0 1 0", "1 0 2 0", "2 0 3 0", "3 0 4 0" })));
    CHECK(r.e.error().empty());
    CHECK(r.e.status().find("osc: /x 1 2") != std::string::npos);

    // the example: plucks through [echo.rev~] on both outputs, and a reload frees the array after the
    // abstractions [echo.rev~] made at loadbang (external/libpd/PATCHES.md)
    for (int k = 0; k < 2; k++) {
        r.load(examples() + "/pluck.pd");
        CHECK(r.e.error().empty());
        if (!r.e.error().empty()) std::fprintf(stderr, "%s\n", r.e.error().c_str());
        CHECK(r.e.spec(0).label == "rate" && r.e.spec(1).label == "decay" && r.e.spec(2).label == "size");
        CHECK(r.level(200, 0) > 0.005);  // 0.5 s: the first note is at the load
        CHECK(r.level(1, 1) > 0.005);
    }
}

// A 16-bit stereo WAV at 48 kHz: channel 0 a ramp from 0 by 1/1024 a frame, channel 1 at -0.5
static void writeWav(const std::string& path, int frames) {
    auto u32 = [](std::ofstream& f, uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [](std::ofstream& f, uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    u32(f, 36 + frames * 4);
    f.write("WAVEfmt ", 8);
    u32(f, 16);
    u16(f, 1);  // PCM
    u16(f, 2);
    u32(f, 48000);
    u32(f, 48000 * 4);
    u16(f, 4);
    u16(f, 16);
    f.write("data", 4);
    u32(f, frames * 4);
    for (int i = 0; i < frames; i++) {
        u16(f, uint16_t(int16_t((i % 1024) * 32)));  // (i % 1024) / 1024 of full scale
        u16(f, uint16_t(int16_t(-16384)));
    }
}

// runs r until its console has `text`, for up to a second
static bool waitFor(Rig& r, const char* text) {
    for (int k = 0; k < 100 && r.e.status().find(text) == std::string::npos; k++) {
        r.run(4);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return r.e.status().find(text) != std::string::npos;
}

// ELSE's [sfload], [sfinfo] and [play.file~] on Pd's sound-file reader (pd/else/): the file's samples into arrays,
// on Pd's thread and on [sfload]'s, files Pd's [soundfiler] wrote as 24-bit AIFF and float WAV, the channel count,
// and playback to its end, and round again with -loop
static void testSoundFiles(const std::string& dir) {
    writeWav(dir + "/two.wav", 1000);
    for (const char* sfload : { "sfload a", "sfload -t a" }) {
        Rig r;
        std::string load = std::string("obj 10 70 ") + sfload;
        r.load(write(dir, "sfload.pd", patch({ "obj 10 10 loadbang", "msg 10 40 load two.wav", load.c_str(),
                                               "obj 10 100 print info", "obj 200 10 array define 0-a",
                                               "obj 200 40 array define 1-a", "obj 200 100 t b b",
                                               "obj 200 130 array get 0-a 10 1", "obj 200 160 print ch0",
                                               "obj 300 130 array get 1-a 500 1", "obj 300 160 print ch1" },
                                             { "0 0 1 0", "1 0 2 0", "2 0 3 0", "2 0 6 0", "6 0 7 0", "7 0 8 0",
                                               "6 1 9 0", "9 0 10 0" })));
        CHECK(r.e.error().empty());
        CHECK(waitFor(r, "info: 1000 48000 2 16"));
        CHECK(r.e.status().find("ch0: 0.00976562") != std::string::npos);  // frame 10: 10 / 1024
        CHECK(r.e.status().find("ch1: -0.5") != std::string::npos);
    }
    // vanilla's writer, then [sfload]: 24-bit big-endian and 32-bit float
    for (const char* fmt : { "-aiff -bytes 3 w.aif", "-wave -bytes 4 w.wav" }) {
        Rig r;
        std::string write_ = std::string("msg 10 70 write ") + fmt + " src";
        std::string load = std::string("msg 10 130 load ") + (std::strstr(fmt, "aif") ? "w.aif" : "w.wav");
        r.load(write(dir, "round.pd", patch({ "obj 10 10 loadbang", "msg 10 40 0 0.25 -0.5 0.75", write_.c_str(),
                                              "obj 10 100 soundfiler", load.c_str(), "obj 10 160 sfload dst",
                                              "obj 10 190 t b", "obj 10 220 array get dst 0 4", "obj 10 250 print got",
                                              "obj 200 10 array define src 4", "obj 200 40 array define dst",
                                              "obj 200 70 array set src", "obj 10 300 t b b b" },
                                            { "0 0 12 0", "12 2 1 0", "1 0 11 0", "12 1 2 0", "2 0 3 0", "12 0 4 0",
                                              "4 0 5 0", "5 0 6 0", "6 0 7 0", "7 0 8 0" })));
        CHECK(r.e.error().empty());
        CHECK(r.e.status().find("got: 0 0.25 -0.5 0.75") != std::string::npos);
    }
    {
        Rig r;
        r.load(write(dir, "sfinfo.pd", patch({ "obj 10 10 loadbang", "msg 10 40 read two.wav \\, channels", "obj 10 70 sfinfo",
                                               "obj 10 100 print sfinfo" },
                                             { "0 0 1 0", "1 0 2 0", "2 0 3 0" })));
        CHECK(r.e.error().empty());
        CHECK(r.e.status().find("sfinfo: channels 2") != std::string::npos);
    }
    for (const char* play : { "play.file~ two.wav 1", "play.file~ -loop two.wav 1" }) {
        Rig r;
        std::string obj = std::string("obj 10 10 ") + play;
        r.load(write(dir, "play.pd", patch({ obj.c_str(), "obj 10 40 dac~ 1 2", "obj 10 70 print end" },
                                           { "0 0 1 0", "0 1 1 1", "0 2 2 0" })));
        CHECK(r.e.error().empty());
        float c0 = 0, c1 = 0;
        int blocks = 0;
        for (; blocks < 200 && c1 == 0; blocks++) {  // until the reader has filled the ring
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            r.run(1);
            c1 = r.out[1][BLOCK - 1], c0 = r.out[0][BLOCK - 1];
        }
        NEAR(c1, -0.5, 1e-6);
        CHECK(c0 > 0 && c0 < 1);
        bool loop = std::strstr(play, "-loop");
        CHECK(waitFor(r, "end: bang"));
        if (loop) {  // round again: another end, and still sound
            std::string first = r.e.status();
            for (int k = 0; k < 100 && r.e.status() == first; k++) r.run(4);
            CHECK(r.e.status() != first);
            NEAR(r.run(1, 1), -0.5, 1e-6);
        } else {
            r.run(4);
            NEAR(r.out[1][BLOCK - 1], 0.0, 1e-6);  // stopped
        }
    }
}

// -loop loses, repeats and zeroes no sample across its loop points, in a file shorter than the reader's chunk and in
// one longer. The ramp goes on from each frame to the next: 2048 frames wrap as the ramp does, at 1024.
static void testLoopSeam(const std::string& dir) {
    for (int frames : { 1000, 2048 }) {
        const int wrap = frames < 1024 ? frames : 1024;
        writeWav(dir + "/seam.wav", frames);
        Rig r;
        r.load(write(dir, "seam.pd", patch({ "obj 10 10 play.file~ -loop seam.wav 1", "obj 10 40 dac~ 1 2" },
                                           { "0 0 1 0", "0 1 1 1" })));
        CHECK(r.e.error().empty());
        for (int k = 0; k < 200 && r.run(1, 1) == 0; k++) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        float prev = r.out[0][BLOCK - 1];
        int breaks = 0;
        for (int f = 0; f < 5 * frames; f += BLOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));  // about real time, so the reader keeps up
            r.run(1);
            for (int i = 0; i < BLOCK; i++) {
                const int want = (int(std::lround(prev * 1024)) + 1) % wrap;
                if (std::fabs(r.out[0][i] - want / 1024.0f) > 1e-5 || r.out[1][i] != -0.5f) breaks++;
                prev = r.out[0][i];
            }
        }
        CHECK(breaks == 0);
    }
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
    if (argc > 3 && std::string(argv[2]) == "load") {  // argv[3] loads without error, then runs; argv[4..] are the path
        Rig r(std::vector<std::string>(argv + 4, argv + argc));
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
        testCloneArray(dir);
        testCyclone(dir);
        testElse(dir);
        testSoundFiles(dir);
        testLoopSeam(dir);
        testRate(dir);
        testThreads(dir);
        testExamples();
    }
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
