// Native test of the teletype engine against the host-built firmware.
// usage: engine_test path/to/ttyp_firmware.so
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "KeyInput.h"
#include "Teletype.h"

using ttyp::Teletype;

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                  \
        }                                                                \
    } while (0)

static std::vector<char> image;

static std::unique_ptr<Teletype> boot() {
    auto t = std::make_unique<Teletype>(image.data(), image.size());
    if (!t->ok()) {
        std::fprintf(stderr, "load: %s\n", t->error().c_str());
        std::exit(1);
    }
    for (int i = 0; i < 3000; i++) t->tick();  // past the splash screen
    return t;
}

static void run(Teletype& t, int ms) {
    for (int i = 0; i < ms; i++) t.tick();
}

// HID usage and shift for the characters the tests type
static uint8_t usage(char c, bool& shift) {
    shift = false;
    if (c >= 'a' && c <= 'z') return uint8_t(0x04 + c - 'a');
    if (c >= '1' && c <= '9') return uint8_t(0x1E + c - '1');
    switch (c) {
        case '0': return 0x27;
        case '\n': return 0x28;
        case '\t': return 0x2B;
        case ' ': return 0x2C;
        case '.': return 0x37;
        case ':': shift = true; return 0x33;
        default: return 0;
    }
}

// through hidMessage, as typed text
static void tap(Teletype& t, const char* s) {
    for (; *s; s++) {
        bool shift;
        uint8_t u = usage(*s, shift);
        t.keyTap(u, shift ? 0x02 : 0);
        run(t, 2);
    }
}

// through boot reports, as a USB keyboard: held long enough for the firmware's 47 ms HID poll
static void type(Teletype& t, const char* s) {
    for (; *s; s++) {
        bool shift;
        uint8_t u = usage(*s, shift);
        if (shift) t.keyDown(0xE1);
        t.keyDown(u);
        run(t, 60);
        t.keyUp(u);
        if (shift) t.keyUp(0xE1);
        run(t, 60);
    }
}

static bool near(float a, float b) {
    return std::fabs(a - b) < 0.01f;
}

static void testIsolation() {
    auto a = boot(), b = boot();
    tap(*a, "cv 1 v 3\n");
    tap(*b, "cv 1 v 7\ntr 1 1\n");
    run(*a, 20);
    run(*b, 20);
    std::printf("isolation: A cv1 %.3f, B cv1 %.3f tr1 %d\n", a->cv(0), b->cv(0), b->tr(0));
    CHECK(near(a->cv(0), 3.0f));
    CHECK(!a->tr(0));
    CHECK(near(b->cv(0), 7.0f));
    CHECK(b->tr(0));
}

static void testReports() {
    auto t = boot();
    type(*t, "tr 2 1\n");
    run(*t, 20);
    std::printf("reports: tr2 %d\n", t->tr(1));
    CHECK(t->tr(1));
}

static void testInputs() {
    auto t = boot();
    t->setIn(5.0f);
    t->setParam(0.25f);
    tap(*t, "cv 3 in\ncv 4 param\n");
    run(*t, 20);
    std::printf("inputs: cv3 %.3f cv4 %.3f\n", t->cv(2), t->cv(3));
    CHECK(std::fabs(t->cv(2) - 5.0f) < 0.02f);
    CHECK(std::fabs(t->cv(3) - 2.5f) < 0.02f);
}

// Tab enters edit mode on script 1; a rise on trigger input 1 runs it
static std::unique_ptr<Teletype> withScript1() {
    auto t = boot();
    tap(*t, "\tcv 2 v 4\n\t\t");  // Tab: live, edit, pattern, live
    run(*t, 20);
    return t;
}

static void trigger(Teletype& t, unsigned i) {
    t.setTrigger(i, true);
    t.setTrigger(i, false);  // a pulse shorter than a tick
    run(t, 20);
}

static void testTrigger() {
    auto t = withScript1();
    CHECK(near(t->cv(1), 0.0f));
    trigger(*t, 0);
    std::printf("trigger: cv2 %.3f\n", t->cv(1));
    CHECK(near(t->cv(1), 4.0f));
}

static void testState() {
    auto a = withScript1();
    auto state = a->saveState();
    std::printf("state: %zu bytes\n", state.size());
    CHECK(state.size() > 12);

    auto b = boot();
    CHECK(b->loadState(state.data(), state.size()));
    run(*b, 20);
    trigger(*b, 0);
    CHECK(near(b->cv(1), 4.0f));

    auto c = boot();
    CHECK(!c->loadState(state.data(), state.size() - 1));
    CHECK(!c->loadState(state.data(), 8));
    auto bad = state;
    bad[0] = 'X';
    CHECK(!c->loadState(bad.data(), bad.size()));
    bad = state;
    bad[4] ^= 1;  // NVRAM size
    CHECK(!c->loadState(bad.data(), bad.size()));
    trigger(*c, 0);
    CHECK(near(c->cv(1), 0.0f));
}

static void testScreen() {
    auto t = boot();
    std::vector<uint8_t> px(Teletype::SCREEN_W * Teletype::SCREEN_H);
    uint32_t s0 = t->screen(px.data());
    tap(*t, "x 1");
    run(*t, 40);
    uint32_t s1 = t->screen(px.data());
    int lit = 0;
    for (auto p : px) lit += p > 0;
    std::printf("screen: seq %u -> %u, %d lit\n", s0, s1, lit);
    CHECK(s1 != s0);
    CHECK(lit > 0);
}

static bool contains(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}

static void testScenes() {
    auto t = boot();
    CHECK(t->importScene(3, "a test scene\n\n#1\nCV 2 V 4\n\n#I\nCV 3 V 2\n"));
    CHECK(!t->importScene(32, "#1\nX 1\n"));
    std::string text = t->exportScene(3);
    std::printf("scenes: slot 3 exports %zu bytes\n", text.size());
    CHECK(contains(text, "A TEST SCENE"));
    CHECK(contains(text, "CV 2 V 4"));
    CHECK(near(t->cv(2), 0.0f));  // the live scene is unchanged until a scene loads
    tap(*t, "scene 3\n");
    run(*t, 20);
    CHECK(near(t->cv(2), 0.0f));  // the SCENE op does not run I
    t->keyTap(0x43, 0);           // F10 runs I
    run(*t, 20);
    CHECK(near(t->cv(2), 2.0f));
    trigger(*t, 0);
    CHECK(near(t->cv(1), 4.0f));
}

static std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static void testDisk() {
    auto t = boot();
    char tmpl[] = "/tmp/ttyp_disk_XXXXXX";
    std::string dir = mkdtemp(tmpl);
    std::ofstream(dir + "/tt05.txt") << "#1\nTR 3 1\n";
    std::ofstream(dir + "/tt06.txt") << std::string(70000, 'x');
    auto r = t->syncDisk(dir);
    std::printf("disk: written %u read %u error '%s'\n", r.written, r.read, r.error.c_str());
    CHECK(r.written == 32);
    CHECK(r.read == 1);
    CHECK(contains(r.error, "tt06.txt"));
    CHECK(contains(readAll(dir + "/tt00s.txt"), "#1"));
    CHECK(!contains(readAll(dir + "/tt05s.txt"), "TR 3 1"));  // written before the read, as on the hardware
    CHECK(contains(t->exportScene(5), "TR 3 1"));
    CHECK(std::ifstream(dir + "/tt31s.txt").good());
    CHECK(!std::ifstream(dir + "/tt00s.txt.tmp").good());
    std::string cleanup = "rm -rf " + dir;
    CHECK(std::system(cleanup.c_str()) == 0);
    auto bad = t->syncDisk("/proc/ttyp_no_such_dir");
    CHECK(!bad.error.empty());
}

static void sendRecords(int fd, const char* s) {
    for (; *s; s++) {
        bool shift;
        uint8_t u = usage(*s, shift);
        uint8_t rec[4] = { 2, u, uint8_t(shift ? 0x02 : 0), 0 };
        CHECK(::send(fd, rec, 4, 0) == 4);
    }
}

// true if the key port is free; on the SSP a running ttyp in Synthor holds it
static bool portFree() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(ttyp::KeyInput::PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool ok = ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
    ::close(fd);
    return ok;
}

static void testTcp() {
    // connecting to another process's ttyp would type into it
    if (!portFree()) {
        std::printf("tcp: skipped, port %u in use\n", ttyp::KeyInput::PORT);
        return;
    }
    auto t = boot();
    ttyp::KeyInput::setKeyboardScan(false);
    ttyp::KeyInput::acquire();
    ttyp::KeyInput::setTarget(t.get());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(ttyp::KeyInput::PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool connected = ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
    CHECK(connected);
    if (connected) {
        sendRecords(fd, "cv 1 v 6\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        run(*t, 20);
    }
    ::close(fd);
    ttyp::KeyInput::clearTarget(t.get());
    ttyp::KeyInput::release();
    std::printf("tcp: cv1 %.3f\n", t->cv(0));
    CHECK(near(t->cv(0), 6.0f));
}

static void testThread() {
    auto t = boot();
    t->start();
    t->keyTap(0x17, 0);  // t
    t->keyTap(0x15, 0);  // r
    t->keyTap(0x2C, 0);
    t->keyTap(0x21, 0);  // 4
    t->keyTap(0x2C, 0);
    t->keyTap(0x1E, 0);  // 1
    t->keyTap(0x28, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    bool tr4 = t->tr(3);
    t->stop();
    std::printf("thread: tr4 %d\n", tr4);
    CHECK(tr4);
}

// examples/ttNN.txt: each imports whole, and runs as its description says
static void loadExample(Teletype& t, const std::string& dir, unsigned slot) {
    char name[16];
    std::snprintf(name, sizeof name, "/tt%02u.txt", slot);
    std::string text = readAll(dir + name);
    CHECK(!text.empty());
    CHECK(t.importScene(slot, text));
    // every script line survives the import: an op teletype rejects would be dropped
    std::string out = t.exportScene(slot);
    size_t scripts = text.find("\n#");
    std::string upper;
    for (char c : text.substr(scripts)) upper += char(std::toupper(static_cast<unsigned char>(c)));
    size_t at = 0;
    while (at < upper.size()) {
        size_t end = upper.find('\n', at);
        std::string line = upper.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (!line.empty() && line[0] != '#') CHECK(contains(out, line.c_str()));
        if (end == std::string::npos) break;
        at = end + 1;
    }
    char cmd[16];
    std::snprintf(cmd, sizeof cmd, "scene %u\n", slot);
    tap(t, cmd);
    t.keyTap(0x43, 0);  // F10: run I, as loading from the scene menu does
    run(t, 20);
}

static void testExamples(const std::string& dir) {
    auto t = boot();
    loadExample(*t, dir, 0);  // clock divider: TR 1 on every 2nd clock
    trigger(*t, 0);
    bool first = t->tr(0);
    t->setTrigger(0, true);
    t->setTrigger(0, false);
    run(*t, 5);
    bool second = t->tr(0);
    std::printf("examples: tt00 TR 1 after clocks 1, 2: %d %d\n", first, second);
    CHECK(!first && second);

    t = boot();
    loadExample(*t, dir, 1);  // sample and hold: CV 1 takes In, in semitones
    t->setIn(5.0f);
    trigger(*t, 0);
    std::printf("examples: tt01 CV 1 %.3f with In at 5 V\n", t->cv(0));
    CHECK(near(t->cv(0), 5.0f));

    t = boot();
    loadExample(*t, dir, 2);  // random gates: the metro runs from I, with no input
    std::vector<int> at;  // the ms of each pulse, on any TR output
    bool was[Teletype::OUTS] = {};
    for (int i = 0; i < 2000; i++) {
        t->tick();
        for (unsigned o = 0; o < Teletype::OUTS; o++) {
            if (t->tr(o) && !was[o]) at.push_back(i);
            was[o] = t->tr(o);
        }
    }
    std::printf("examples: tt02 %zu pulses in 2 s, first at %d ms\n", at.size(), at.empty() ? -1 : at[0]);
    CHECK(at.size() >= 9);  // M.RESET starts the 200 ms metro at once
    for (size_t i = 1; i < at.size(); i++) CHECK(std::abs(at[i] - at[i - 1] - 200) <= 2);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s ttyp_firmware.so [examples dir]\n", argv[0]);
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    image.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (image.empty()) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    testIsolation();
    testReports();
    testInputs();
    testTrigger();
    testState();
    testScreen();
    testScenes();
    testDisk();
    testTcp();
    testThread();
    if (argc > 2) testExamples(argv[2]);
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
