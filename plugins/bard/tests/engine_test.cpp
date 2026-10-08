// Native test of bard::BardEngine and its text formats; built and run by test_bard.py.
// argv[1] is an empty scratch directory.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "BardEngine.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)
#define NEAR(a, b, tol)                                                                                     \
    do {                                                                                                    \
        double a_ = double(a), b_ = double(b);                                                              \
        if (std::fabs(a_ - b_) > (tol)) {                                                                   \
            std::fprintf(stderr, "%s:%d: NEAR(%s, %s): %g vs %g\n", __FILE__, __LINE__, #a, #b, a_, b_); \
            failures++;                                                                                     \
        }                                                                                                   \
    } while (0)

using namespace bard;
using E = BardEngine;

static constexpr float SR = 48000.0f;
static constexpr int BLOCK = 128;
static constexpr float PI = 3.14159265f;

static void put16(FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }
static void put32(FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }

static void writeWav(const std::string& path, float secs, std::function<float(int)> value, int rate = 48000) {
    int frames = int(secs * float(rate));
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite("RIFF", 1, 4, f);
    put32(f, uint32_t(36 + frames * 2));
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16), put16(f, 1), put16(f, 1), put32(f, uint32_t(rate)), put32(f, uint32_t(rate * 2)), put16(f, 2),
        put16(f, 16);
    std::fwrite("data", 1, 4, f);
    put32(f, uint32_t(frames * 2));
    for (int i = 0; i < frames; i++) put16(f, uint16_t(int16_t(std::lround(value(i) * 32767.0f))));
    std::fclose(f);
}

static void writeText(const std::string& path, const std::string& text) {
    std::ofstream(path) << text;
}

static std::string readText(const std::string& path) {
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static std::function<float(int)> sine(float hz) {
    return [hz](int i) { return 0.5f * std::sin(2.0f * PI * hz * float(i) / SR); };
}

static void settle() {
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
}

struct Rig {
    E e;
    std::vector<float> in[E::I_MAX], out[E::O_MAX];
    const float* ip[E::I_MAX];
    float* op[E::O_MAX];
    bool worker = true;  // run idle() between blocks, on this thread

    explicit Rig(const std::string& root) {
        for (int c = 0; c < E::I_MAX; c++) in[c].assign(BLOCK, 0.0f), ip[c] = in[c].data();
        for (int c = 0; c < E::O_MAX; c++) out[c].assign(BLOCK, 0.0f), op[c] = out[c].data();
        e.prepare(SR, BLOCK);
        e.setRoot(root);
        e.controls(1).volume = 0.0f;
    }
    std::vector<float> run(float secs, int ch = E::O_A) {
        std::vector<float> rec;
        for (int b = 0; b < int(secs * SR / BLOCK); b++) {
            if (worker) e.idle();
            e.process(ip, op, BLOCK);
            rec.insert(rec.end(), out[ch].begin(), out[ch].end());
        }
        return rec;
    }
    // a button press: high for a block, then low
    void press(bool E::Controls::*button, int d = 0) {
        e.controls(d).*button = true;
        run(float(BLOCK) / SR);
        e.controls(d).*button = false;
        run(float(BLOCK) / SR);
    }
};

static double rms(const std::vector<float>& v, size_t from = 0) {
    double a = 0.0;
    for (size_t i = from; i < v.size(); i++) a += double(v[i]) * v[i];
    return v.size() > from ? std::sqrt(a / double(v.size() - from)) : 0.0;
}

static int crossings(const std::vector<float>& v, size_t from) {
    int n = 0;
    for (size_t i = from + 1; i < v.size(); i++) n += (v[i - 1] < 0.0f) != (v[i] < 0.0f);
    return n;
}

static int pulses(const std::vector<float>& v) {
    int n = 0;
    for (size_t i = 1; i < v.size(); i++) n += v[i] > 0.5f && v[i - 1] < 0.5f;
    return n;
}

static void testFormats() {
    MarkList l;
    int n = parse_sidecar("#!bard order=time loop=segment\n# a comment\n0:10 Chapter 2\n0:00 - Prologue\n"
                          "1:00:00\nnot a time\n0:20-0:25 a passage\n",
                          1000, 4000000, l);
    CHECK(n == 4);
    CHECK(l.ordering == MarkOrder::Time && l.loop == LoopMode::Segment && l.loop_set);
    CHECK(l.mark[0].start == 10000 && l.mark[1].start == 0 && l.mark[2].start == 3600000);
    CHECK(l.mark[3].start == 20000 && l.mark[3].end == 25000);
    resolve(l, 4000000, 1);
    CHECK(l.mark[1].end == 10000);   // runs to the next later mark, in any line
    CHECK(l.mark[0].end == 20000);
    CHECK(l.mark[2].end == 4000000);  // the last runs to the end
    CHECK(l.order[0] == 1 && l.order[1] == 0 && l.order[2] == 3 && l.order[3] == 2);
    char text[512];
    serialize_marks(l, 1000, 4000000, text, sizeof(text));
    MarkList r;
    parse_sidecar(text, 1000, 4000000, r);
    resolve(r, 4000000, 1);
    CHECK(r.count == l.count);
    for (int i = 0; i < l.count; i++) CHECK(r.mark[i].start == l.mark[i].start && r.mark[i].end == l.mark[i].end);

    // resume keys may hold spaces and long names; the frame is after the last space
    ResumeTable t;
    std::string longName(200, 'x');
    t.parse(("0/my book.wav 1234\n# c\n1/" + longName + ".wav 99\nbroken\n3/a.wav  7 \n").c_str());
    uint32_t f = 0;
    CHECK(t.count == 3);
    CHECK(t.get("0/my book.wav", f) && f == 1234);
    CHECK(t.get(("1/" + longName + ".wav").c_str(), f) && f == 99);
    CHECK(t.get("3/a.wav", f) && f == 7);
    std::vector<char> buf(ResumeTable::kTextMax);
    t.serialize(buf.data(), int(buf.size()));
    ResumeTable u;
    u.parse(buf.data());
    CHECK(u.count == 3 && u.get("0/my book.wav", f) && f == 1234);
}

// root/0/a.wav (30 s, 440 Hz) with a sidecar, root/0/b.wav (20 s, no sidecar), root/1/c.wav (5 s, 220 Hz)
static std::string library(const std::string& dir, const char* name) {
    std::string root = dir + "/" + name;
    mkdir(root.c_str(), 0755);
    mkdir((root + "/0").c_str(), 0755);
    mkdir((root + "/1").c_str(), 0755);
    writeWav(root + "/0/a.wav", 30.0f, sine(440.0f));
    writeText(root + "/0/a.txt", "0:00 start\n0:05\n0:10 - 0:12 a passage\n");
    writeWav(root + "/0/b.wav", 20.0f, sine(330.0f));
    writeWav(root + "/1/c.wav", 5.0f, sine(220.0f));
    return root;
}

static void testPlay(const std::string& dir) {
    std::string root = library(dir, "play");
    writeText(root + "/bard.cfg", "resume=off\n");
    Rig r(root);
    auto v = r.run(1.0f);
    auto i = r.e.info(0);
    CHECK(i.shelves == 2 && i.shelf == 0 && i.shelfName == "0");
    CHECK(i.books == 2 && i.book == 0 && i.bookName == "a.wav");
    CHECK(i.marks == 3 && !i.autoMarks && !i.paused);
    NEAR(i.length, 30.0, 1e-3);
    NEAR(i.seconds, 1.0, 0.02);
    NEAR(rms(v, size_t(0.5f * SR)), 0.354, 0.02);

    // pause holds the playhead and silences the deck
    r.press(&E::Controls::play);
    double held = r.e.info(0).seconds;
    v = r.run(0.5f);
    CHECK(r.e.info(0).paused);
    NEAR(r.e.info(0).seconds, held, 0.01);
    CHECK(rms(v, size_t(0.1f * SR)) == 0.0);
    r.press(&E::Controls::play);
    r.run(4.0f);
    // back jumps 15 s, but not before the start
    r.press(&E::Controls::back);
    r.run(0.1f);
    NEAR(r.e.info(0).seconds, 0.1, 0.05);
    r.run(19.0f);
    r.press(&E::Controls::back);
    r.run(0.1f);
    NEAR(r.e.info(0).seconds, 19.2 - 15.0, 0.05);
}

static void testRateAndKeep(const std::string& dir) {
    std::string root = library(dir, "rate");
    writeText(root + "/bard.cfg", "resume=off\n");
    auto measure = [&](float rate, float keep, double& secs) {
        Rig r(root);
        r.e.controls(0).rate = rate;
        r.e.controls(0).keep = keep;
        auto v = r.run(2.0f);
        secs = r.e.info(0).seconds;
        return crossings(v, size_t(1.0f * SR));  // over the last second
    };
    double s1, s25, s05, sk;
    int c1 = measure(0.5f, 0.0f, s1), c25 = measure(1.0f, 0.0f, s25), c05 = measure(0.0f, 0.0f, s05);
    int ck = measure(1.0f, 1.0f, sk);
    NEAR(s1, 2.0, 0.03);
    NEAR(s25, 5.0, 0.1);
    NEAR(s05, 1.0, 0.03);
    NEAR(sk, 5.0, 0.3);                   // pitch kept: still ~2.5x through the book (WSOLA's search
                                          // drifts a few percent; sk-engines accepts 2.2x..2.8x)...
    NEAR(c1, 880, 10);                    // 440 Hz
    NEAR(double(c25) / c1, 2.5, 0.05);    // varispeed: pitch follows speed
    NEAR(double(c05) / c1, 0.5, 0.02);
    NEAR(double(ck) / c1, 1.0, 0.05);     // ...at the original pitch
}

static void testMarks(const std::string& dir) {
    std::string root = library(dir, "marks");
    writeText(root + "/bard.cfg", "resume=off\n");
    Rig r(root);
    r.run(0.5f);
    // the bookmark selector jumps once it settles; in Read the book plays on past the mark
    r.e.controls(0).mark = 1.0f;
    r.run(0.05f);
    settle();
    r.run(0.5f);
    auto i = r.e.info(0);
    CHECK(i.mark == 2);
    NEAR(i.seconds, 10.5, 0.05);
    r.run(2.0f);
    NEAR(r.e.info(0).seconds, 12.5, 0.05);
    CHECK(!r.e.info(0).paused);

    // Recite holds at the segment end
    r.e.controls(0).seq = Seq::Recite;
    r.run(0.1f);
    r.press(&E::Controls::next);  // segment 2 is last: holds, as loop is off
    CHECK(r.e.info(0).paused);
    r.e.controls(0).mark = 0.5f;  // segment 1: 5..10 s
    r.run(0.05f);
    settle();
    auto v = r.run(6.0f);
    i = r.e.info(0);
    CHECK(i.mark == 1 && i.paused);
    NEAR(i.seconds, 10.0, 0.01);
    CHECK(rms(v, v.size() - size_t(0.5f * SR)) == 0.0);

    // loop restarts a held segment and then repeats it, a gate pulse each time
    r.e.controls(0).loop = LoopPolicy::Loop;
    auto gate = r.run(11.0f, E::O_GATE_A);
    CHECK(!r.e.info(0).paused);
    CHECK(r.e.info(0).seconds >= 5.0 && r.e.info(0).seconds < 10.0);
    CHECK(pulses(gate) >= 2);

    // Next walks the play order and wraps under loop; the gate input does the same
    r.e.controls(0).seq = Seq::Read;
    r.press(&E::Controls::next);
    r.run(0.1f);
    CHECK(r.e.info(0).mark == 2);
    r.in[E::I_GATE].assign(BLOCK, 1.0f);
    r.run(0.1f);
    r.in[E::I_GATE].assign(BLOCK, 0.0f);
    r.run(0.1f);
    CHECK(r.e.info(0).mark == 0);
    NEAR(r.e.info(0).seconds, 0.2, 0.05);

    // Wander moves on at each segment end
    r.e.controls(0).seq = Seq::Wander;
    r.run(5.5f);
    CHECK(r.e.info(0).mark == 1);
}

static void testAutoMarksAndShelves(const std::string& dir) {
    std::string root = library(dir, "auto");
    writeText(root + "/bard.cfg", "resume=off\n");
    Rig r(root);
    r.e.controls(0).book = 1.0f;  // b.wav: no sidecar
    r.run(0.1f);
    settle();
    r.run(0.1f);
    auto i = r.e.info(0);
    CHECK(i.bookName == "b.wav" && i.autoMarks && i.marks == 4);
    r.e.controls(0).mark = 0.34f;  // the second mark
    r.run(0.05f);
    settle();
    r.run(0.05f);
    double a = r.e.info(0).seconds;
    CHECK(r.e.info(0).mark == 1);
    // a reroll moves the auto-marks; the same reroll puts them back
    r.e.controls(0).reroll = 1;
    r.e.controls(0).mark = 0.0f;
    r.run(0.05f);
    settle();
    r.run(0.05f);
    r.e.controls(0).mark = 0.34f;
    r.run(0.05f);
    settle();
    r.run(0.05f);
    double b = r.e.info(0).seconds;
    CHECK(std::fabs(a - b) > 0.2);

    r.e.controls(0).shelf = 0.9f;
    r.run(0.2f);
    i = r.e.info(0);
    CHECK(i.shelf == 1 && i.books == 1 && i.bookName == "c.wav");
    // the end of a book pauses it in Read
    r.run(5.0f);
    CHECK(r.e.info(0).paused);
}

// A paused position is saved and restored by the next instance; the raw rate comes from bard.cfg.
static void testResumeAndConfig(const std::string& dir) {
    std::string root = library(dir, "resume");
    {
        Rig r(root);
        r.run(3.0f);
        r.press(&E::Controls::play);  // pause: saves now
        r.run(0.1f);
    }
    std::string saved = readText(root + "/resume.txt");
    CHECK(saved.rfind("0/a.wav ", 0) == 0);
    {
        Rig r(root);
        r.run(0.1f);
        NEAR(r.e.info(0).seconds, 3.1, 0.05);
    }
    writeText(root + "/bard.cfg", "resume=off\nrate=24000\n");
    mkdir((root + "/2").c_str(), 0755);
    {
        FILE* f = std::fopen((root + "/2/r.raw").c_str(), "wb");
        for (int i = 0; i < 24000 * 4; i++) put16(f, uint16_t(int16_t(8000)));
        std::fclose(f);
    }
    Rig r(root);
    r.e.controls(0).shelf = 0.99f;
    r.run(1.0f);
    auto i = r.e.info(0);
    CHECK(i.bookName == "r.raw");
    NEAR(i.length, 4.0, 1e-3);
    NEAR(i.seconds, 1.0, 0.02);
}

// Deck A's voice ducks deck B.
static void testDuck(const std::string& dir) {
    std::string root = library(dir, "duck");
    writeText(root + "/bard.cfg", "resume=off\n");
    auto level = [&](float duck) {
        Rig r(root);
        r.e.controls(1).volume = 1.0f;
        r.e.controls(0).duck = duck;
        return rms(r.run(1.0f, E::O_B), size_t(0.5f * SR));
    };
    double open = level(0.0f), ducked = level(1.0f);
    CHECK(open > 0.3);
    CHECK(ducked < 0.1 * open);
}

static void testThreads(const std::string& dir) {
    std::string root = library(dir, "threads");
    Rig r(root);
    r.worker = false;
    std::atomic<bool> done{ false };
    std::thread worker([&] {
        while (!done.load()) {
            r.e.idle();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    std::thread ui([&] {
        size_t sum = 0;
        while (!done.load())
            for (int d = 0; d < E::DECKS; d++) sum += r.e.info(d).bookName.size() + size_t(r.e.info(d).mark);
        (void)sum;
    });
    for (int i = 0; i < 80; i++) {
        auto& c = r.e.controls(i & 1);
        c.book = float(i % 3) / 2.0f;
        c.mark = float(i % 4) / 3.0f;
        c.position = float(i % 5) / 4.0f;
        c.seq = Seq(i % 3);
        c.loop = LoopPolicy(i % 3);
        c.play = i % 6 == 0;
        c.back = i % 7 == 0;
        c.next = i % 5 == 0;
        c.shelf = i % 20 < 10 ? 0.0f : 0.9f;
        if (i == 40) r.e.setRoot(root);
        r.run(0.03f);
    }
    done = true;
    worker.join();
    ui.join();
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string dir = argv[1];
    if (argc > 2 && std::strcmp(argv[2], "threads") == 0) {
        testThreads(dir);
    } else {
        testFormats();
        testPlay(dir);
        testRateAndKeep(dir);
        testMarks(dir);
        testAutoMarksAndShelves(dir);
        testResumeAndConfig(dir);
        testDuck(dir);
    }
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
