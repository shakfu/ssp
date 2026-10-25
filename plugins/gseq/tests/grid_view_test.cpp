// GridView against a GseqEngine: keys toggling steps, the note a step takes, paging, roll mode, the frame's
// levels, and the grid Surface's quadrant diff. argv[1] == "threads" runs the threaded test only
// (ThreadSanitizer). Built and run by test_gseq.py.

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "GridView.h"
#include "GseqEngine.h"

using gseq::GridView;
using gseq::GseqEngine;
using ssp::monome::Frame;
using ssp::monome::Key;
using ssp::monome::Quad;
using ssp::monome::Surface;
using Step = GseqEngine::Step;
using Row = GseqEngine::Row;

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond); \
            failures++;                                           \
        }                                                         \
    } while (0)

static constexpr int RATE = 48000, BLOCK = 128, OUTS = 3 * GseqEngine::VOICES;

struct Rig {
    GseqEngine e;
    GridView v{ e };
    float in[2][BLOCK] = {}, out[OUTS][BLOCK];
    const float* ip[2] = { in[0], in[1] };
    float* op[OUTS];

    Rig() {
        for (int c = 0; c < OUTS; c++) op[c] = out[c];
        e.prepare(float(RATE), BLOCK);
        e.setBpm(120.0);
        for (int r = 0; r < GseqEngine::ROWS; r++) {  // MIDI only, row r on channel r + 1
            Row row;
            row.channel = uint8_t(r + 1);
            e.setRow(r, row);
        }
    }
    void block(int n = 1) {
        for (int i = 0; i < n; i++) e.process(ip, op, BLOCK);
    }
    void tap(int x, int y) {
        v.press({ x, y, true });
        v.press({ x, y, false });
    }
    Frame frame(bool blink = false) {
        Frame f;
        v.render(f, blink);
        return f;
    }
};

static void testToggle() {
    Rig r;
    r.tap(3, 2);  // row 3, step 4
    CHECK(r.e.cursorRow() == 2 && r.e.cursorCol() == 3);
    auto s = r.e.step(2, 3);
    CHECK(s.on && s.note == 60 && s.velocity == 100 && s.duration == Step().duration);  // no note played yet: C4
    r.tap(3, 2);
    CHECK(!r.e.step(2, 3).on);
    CHECK(r.e.step(2, 3).note == 60);  // off keeps its note
    // releases do nothing
    r.v.press({ 5, 0, false });
    CHECK(!r.e.step(0, 5).on);
}

static void testLastNote() {
    Rig r;
    CHECK(r.e.lastNote() == -1);
    r.e.playIn(67, 90);  // a Launchpad key or a played note
    r.e.playIn(67, 0);   // its note-off leaves it
    CHECK(r.e.lastNote() == 67);
    r.tap(0, 0);
    CHECK(r.e.step(0, 0).note == 67 && r.e.step(0, 0).velocity == 100);
    r.e.noteIn(48, 100);  // a MIDI keyboard, recording or not
    r.tap(1, 0);
    CHECK(r.e.step(0, 1).note == 48);
    // a step switched off and on again takes the new last note
    r.tap(0, 0);
    r.tap(0, 0);
    CHECK(r.e.step(0, 0).note == 48);
}

static void testLength() {
    Rig r;
    Row row = r.e.row(4);
    row.length = 6;
    r.e.setRow(4, row);
    r.tap(6, 4);  // past the row's length
    CHECK(!r.e.step(4, 6).on);
    CHECK(r.e.cursorRow() == 0 && r.e.cursorCol() == 0);  // the cursor stays
    const Frame f = r.frame();
    CHECK(f.at(5, 4) == GridView::OFF);
    CHECK(f.at(6, 4) == 0);
}

static void testPaging() {
    Rig r;
    for (int k = 0; k < GseqEngine::ROWS; k++) {
        Row row = r.e.row(k);
        row.length = 32;
        r.e.setRow(k, row);
    }
    CHECK(r.v.left() == 0);
    r.e.setCursor(1, 20);  // the SSP's arrows
    r.frame();
    CHECK(r.v.left() == 16);
    r.tap(2, 1);  // step 19
    CHECK(r.e.step(1, 18).on && !r.e.step(1, 2).on);
    r.e.setCursor(1, 15);
    r.frame();
    CHECK(r.v.left() == 0);
    // an unchanged cursor leaves the page alone: the frame is the same
    const Frame a = r.frame(), b = r.frame();
    CHECK(a.level == b.level);
}

static void testLevels() {
    Rig r;
    Step s;
    s.on = true;
    s.velocity = 127;
    r.e.setStep(0, 2, s);
    s.velocity = 1;
    r.e.setStep(0, 3, s);
    r.e.setStep(1, 2, s);
    Row muted = r.e.row(1);
    muted.mute = true;
    r.e.setRow(1, muted);
    Row silent = r.e.row(2);
    silent.channel = 0;
    r.e.setRow(2, silent);
    r.e.setStep(2, 2, s);
    r.e.setCursor(7, 15);
    Frame f = r.frame();
    CHECK(f.at(2, 0) == GridView::ON_HIGH);
    CHECK(f.at(3, 0) == GridView::ON_LOW);
    CHECK(f.at(4, 0) == GridView::OFF);
    CHECK(f.at(2, 1) == GridView::SILENT);  // muted
    CHECK(f.at(2, 2) == GridView::SILENT);  // no output
    CHECK(f.at(15, 7) == GridView::OFF);    // the cursor, unlit phase
    f = r.frame(true);
    CHECK(f.at(15, 7) == 15);
    r.e.setStep(7, 15, s);
    f = r.frame(true);
    CHECK(f.at(15, 7) == 15);  // an ON_LOW step: lit
    s.velocity = 127;
    r.e.setStep(7, 15, s);
    f = r.frame(true);
    CHECK(f.at(15, 7) == 0);  // near full: dark
}

static void testPlayhead() {
    Rig r;
    Step s;
    s.on = true;
    r.e.setStep(0, 0, s);
    r.e.setCursor(7, 15);
    r.block();  // step 1 plays
    CHECK(r.e.playing(0) == 0 && r.e.playing(1) == 0);
    const Frame f = r.frame();
    CHECK(f.at(0, 0) == GridView::PLAY_ON);
    CHECK(f.at(0, 1) == GridView::PLAY_OFF);
}

static void testRoll() {
    Rig r;
    r.e.setRoll(true);
    r.tap(4, 7);  // the bottom row: pitch 60, step 5
    CHECK(r.e.findNote(4, 60) >= 0);
    CHECK(r.e.cursorPitch() == 60 && r.e.cursorCol() == 4);
    r.tap(4, 0);  // the top row: pitch 67
    CHECK(r.e.findNote(4, 67) >= 0);
    Frame f = r.frame();
    CHECK(f.at(4, 7) == GridView::onLevel(100) && f.at(4, 0) == GridView::onLevel(100));
    CHECK(f.at(5, 7) == GridView::C_ROW);  // a half-step note holds no further step
    CHECK(f.at(5, 6) == GridView::PITCH_ROW);
    r.tap(4, 7);  // removes it
    CHECK(r.e.findNote(4, 60) < 0);
    // the pitches follow the cursor
    r.e.setCursorPitch(80);
    r.frame();
    CHECK(r.v.pitchLow() == 73);
    r.e.setCursorPitch(127);
    r.frame();
    CHECK(r.v.pitchLow() == GridView::HIGHEST_PITCH_LOW);
    r.e.setCursorPitch(10);
    r.frame();
    CHECK(r.v.pitchLow() == 10);
}

static void testSurface() {
    Surface s;
    Frame f;
    std::vector<Quad> q;
    s.update(f, q);
    CHECK(q.size() == 2);  // unknown: both quadrants
    q.clear();
    s.update(f, q);
    CHECK(q.empty());
    f.at(9, 3) = 7;
    s.update(f, q);
    CHECK(q.size() == 1 && q[0].x == 8 && q[0].y == 0 && q[0].level[3 * 8 + 1] == 7);
    q.clear();
    f.at(0, 0) = 200;  // clamped to 15
    s.update(f, q);
    CHECK(q.size() == 1 && q[0].x == 0 && q[0].level[0] == 15);
    q.clear();
    s.forget();
    s.update(f, q);
    CHECK(q.size() == 2);
}

static void testThreads() {
    Rig r;
    std::atomic<bool> stop{ false };
    std::thread keys([&] {  // as the grid's thread
        for (int i = 0; !stop.load(); i++) {
            r.v.press({ i % 16, (i / 16) % 8, i % 2 == 0 });
            if (i % 500 == 0) r.e.setRoll(i % 1000 == 0);
        }
    });
    std::thread draw([&] {  // as midiLoop
        Frame f;
        GseqEngine::Midi m;
        for (int i = 0; !stop.load(); i++) {
            r.v.render(f, i % 2);
            while (r.e.midiOut(m)) {}
        }
    });
    std::thread notes([&] {  // as the MIDI input thread
        for (int i = 0; !stop.load(); i++) r.e.playIn(40 + i % 40, i % 2 ? 100 : 0);
    });
    r.block(4000);
    stop = true;
    keys.join();
    draw.join();
    notes.join();
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "threads") {
        testThreads();
        std::printf("%s\n", failures ? "FAILED" : "all passed");
        return failures ? 1 : 0;
    }
    testToggle();
    testLastNote();
    testLength();
    testPaging();
    testLevels();
    testPlayhead();
    testRoll();
    testSurface();
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
