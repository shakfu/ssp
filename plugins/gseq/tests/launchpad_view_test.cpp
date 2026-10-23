// LaunchpadView against a GseqEngine: the keyboard's notes, cursor pads in each mode, side keys, the top
// row, and the frames it draws. argv[1] == "threads" runs the threaded test only (ThreadSanitizer). Built
// and run by test_gseq.py.

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "GseqEngine.h"
#include "LaunchpadView.h"

using gseq::GseqEngine;
using gseq::LaunchpadView;
using ssp::launchpad::Event;
using ssp::launchpad::Frame;
using ssp::launchpad::Led;
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
    LaunchpadView v{ e };
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
    std::vector<GseqEngine::Midi> midi() {
        std::vector<GseqEngine::Midi> all;
        GseqEngine::Midi m;
        while (e.midiOut(m)) all.push_back(m);
        return all;
    }
};

// a window pad's y for the window's w-th row
static int padY(int w) {
    return 7 - w;
}

static void testKeyNotes() {
    const int white[8] = { 60, 62, 64, 65, 67, 69, 71, 72 }, black[8] = { -1, 61, 63, -1, 66, 68, 70, -1 };
    for (int x = 0; x < 8; x++) {
        CHECK(LaunchpadView::keyNote(x, 0, 60) == white[x]);
        CHECK(LaunchpadView::keyNote(x, 1, 60) == black[x]);
    }
    CHECK(LaunchpadView::keyNote(0, 0, 0) == 0 && LaunchpadView::keyNote(7, 0, 108) == 120);
    CHECK(LaunchpadView::keyNote(0, 2, 60) == -1);
}

// neither Rec nor Step: a cursor pad moves the cursor only; past the row's length it does nothing
static void testCursorPad() {
    Rig r;
    Row row = r.e.row(2);
    row.length = 4;
    r.e.setRow(2, row);
    r.tap(3, padY(2));
    CHECK(r.e.cursorRow() == 2 && r.e.cursorCol() == 3);
    CHECK(!r.e.step(2, 3).on);
    r.tap(5, padY(2));  // step 6 of a 4-step row
    CHECK(r.e.cursorCol() == 3);
}

// Step: no key held toggles, on with the last key's note; held keys write their notes, a chord down the rows
static void testStepPads() {
    Rig r;
    r.tap(5, 8);  // Step
    CHECK(r.e.stepEntry());
    r.tap(0, padY(0));
    Step s = r.e.step(0, 0);
    CHECK(s.on && s.note == 60 && s.velocity == 100 && s.duration == Step().duration);
    r.tap(0, padY(0));
    CHECK(!r.e.step(0, 0).on);
    r.tap(4, 0);  // G4: plays, and becomes the default note
    r.tap(1, padY(0));
    CHECK(r.e.step(0, 1).on && r.e.step(0, 1).note == 67);
    // a held key over a step that is on: stays on, takes the note, keeps velocity and duration
    Step old;
    old.on = true, old.note = 50, old.velocity = 30, old.duration = 9;
    r.e.setStep(1, 2, old);
    r.v.press({ 2, 1, true });  // D#4 held
    r.tap(2, padY(1));
    s = r.e.step(1, 2);
    CHECK(s.on && s.note == 63 && s.velocity == 30 && s.duration == 9);
    // a chord: lowest on the pad's row, the next on the row below; a row too short is skipped
    Row shortRow = r.e.row(3);
    shortRow.length = 2;
    r.e.setRow(3, shortRow);
    r.v.press({ 0, 0, true });  // C4, with D#4 still held
    r.v.press({ 2, 0, true });  // E4
    r.tap(5, padY(1));
    CHECK(r.e.step(1, 5).note == 60 && r.e.step(2, 5).note == 63 && !r.e.step(3, 5).on);
    CHECK(r.e.cursorRow() == 1 && r.e.cursorCol() == 5);  // the cursor stays: no step entry advance
    for (int x : { 0, 2 }) r.v.press({ x, 0, false });
    r.v.press({ 2, 1, false });
    r.block(2);
    CHECK(r.e.cursorRow() == 1 && r.e.cursorCol() == 5);
}

// key pads sound through the cursor's row outside Rec; in Rec they record; a release goes the way its press went
static void testKeyPads() {
    Rig r;
    r.e.setCursor(2, 0);
    r.v.press({ 0, 0, true });
    r.block();
    auto m = r.midi();
    CHECK(m.size() == 1 && m[0].bytes[0] == 0x92 && m[0].bytes[1] == 60 && m[0].bytes[2] == 100);
    r.tap(8, 1);  // octave up: the held key still releases its own note
    r.tap(4, 8);  // Rec
    r.block();
    CHECK(r.e.record() == GseqEngine::ARMED);
    r.v.press({ 0, 0, false });
    r.block();
    m = r.midi();
    CHECK(m.size() == 1 && m[0].bytes[0] == 0x82 && m[0].bytes[1] == 60);
    CHECK(r.v.octave() == 72);
    // recording: the next pass of row 3 (16 steps of 6000 samples)
    r.block(16 * 6000 / BLOCK + 2);
    CHECK(r.e.record() == GseqEngine::RECORDING);
    r.v.press({ 0, 0, true });  // C5
    r.block(10);
    r.v.press({ 0, 0, false });
    r.block(2);
    bool written = false;
    for (int st = 0; st < 16; st++) written |= r.e.step(2, st).on && r.e.step(2, st).note == 72;
    CHECK(written);
    for (int i = 0; i < 20; i++) r.tap(8, 1);
    CHECK(r.v.octave() == LaunchpadView::HIGHEST);
    for (int i = 0; i < 20; i++) r.tap(8, 0);
    CHECK(r.v.octave() == LaunchpadView::LOWEST);
}

static void testSideAndTop() {
    Rig r;
    r.tap(8, padY(1));  // mute the window's 2nd row
    CHECK(r.e.row(1).mute);
    r.tap(1, 8);  // down
    r.tap(1, 8);
    r.tap(1, 8);
    CHECK(r.v.top() == 2);
    r.tap(8, padY(1));  // now row 4
    CHECK(r.e.row(3).mute);
    r.tap(0, 8);  // up
    CHECK(r.v.top() == 1);
    for (int i = 0; i < 5; i++) r.tap(3, 8);  // right
    CHECK(r.v.left() == 24);
    r.tap(2, 8);  // left
    CHECK(r.v.left() == 16);
    r.tap(6, 8);
    CHECK(r.v.follow());
    r.tap(5, 8);
    r.tap(4, 8);  // Rec turns step entry off
    r.block();
    CHECK(!r.e.stepEntry() && r.e.record() == GseqEngine::ARMED);
}

static void testRender() {
    Rig r;
    Frame f;
    Row row = r.e.row(0);
    row.length = 4;
    row.mute = true;
    r.e.setRow(0, row);
    Step loud;
    loud.on = true, loud.velocity = 127;
    r.e.setStep(0, 1, loud);
    Step quiet = loud;
    quiet.velocity = 1;
    r.e.setStep(0, 2, quiet);
    Row silent = r.e.row(1);
    silent.channel = 0;
    r.e.setRow(1, silent);
    r.e.setStep(1, 0, loud);
    r.e.setCursor(0, 3);
    r.v.render(f);
    CHECK(f.at(4, 7) == Led::off());                                        // past the row's length
    CHECK(f.at(1, 7).type == Led::RGB && f.at(1, 7).a > f.at(2, 7).a);      // brighter for higher velocity
    CHECK(f.at(3, 7).type == Led::FLASH);                                   // the cursor
    CHECK(f.at(0, 7).type == Led::RGB && f.at(0, 7).a < 10);                // off: dim grey
    CHECK(f.at(0, 6) == Led::rgb(24, 24, 24));                              // a row with no output: grey
    CHECK(f.at(8, 7) == Led::palette(5) && f.at(8, 6) != Led::palette(5));  // muted row: red side key
    CHECK(f.at(0, 1) == Led::off() && f.at(3, 1) == Led::off() && f.at(7, 1) == Led::off());  // black gaps
    r.v.press({ 1, 0, true });
    r.v.render(f);
    CHECK(f.at(1, 0) == Led::rgb(127, 127, 127));                 // a held key
    CHECK(f.at(0, 8) == Led::off() && f.at(1, 8) != Led::off());  // up: at the top; down: possible
    r.block(2);
    r.v.render(f);
    CHECK(f.at(0, 7) == Led::rgb(127, 127, 127));  // the playhead, step 1
    // a cursor moved elsewhere brings the window to it; the arrows can then move the window away
    r.e.setCursor(7, 20);
    r.v.render(f);
    CHECK(r.v.top() == 2 && r.v.left() == 16);
    r.tap(0, 8);
    r.tap(2, 8);
    r.v.render(f);
    CHECK(r.v.top() == 1 && r.v.left() == 8);
    // Follow pages to the cursor row's playhead
    r.tap(6, 8);
    Row longRow = r.e.row(7);
    longRow.length = 32;
    r.e.setRow(7, longRow);
    r.block(9 * 6000 / BLOCK);
    r.v.render(f);
    CHECK(r.e.playing(7) >= 8 && r.v.left() == r.e.playing(7) / 8 * 8);
}

static void testThreads() {
    Rig r;
    std::atomic<bool> stop{ false };
    std::thread input([&] {  // as the MIDI input thread
        for (int i = 0; !stop.load(); i++) {
            const int x = i % 9, y = (i / 9) % 9;
            r.v.press({ x, y, i % 2 == 0 });
        }
    });
    std::thread draw([&] {  // as midiLoop
        Frame f;
        GseqEngine::Midi m;
        while (!stop.load()) {
            r.v.render(f);
            while (r.e.midiOut(m)) {}
        }
    });
    r.block(4000);
    stop = true;
    input.join();
    draw.join();
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "threads") {
        testThreads();
        std::printf("%s\n", failures ? "FAILED" : "all passed");
        return failures ? 1 : 0;
    }
    testKeyNotes();
    testCursorPad();
    testStepPads();
    testKeyPads();
    testSideAndTop();
    testRender();
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
