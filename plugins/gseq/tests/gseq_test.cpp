// GseqEngine natively: the pattern's words, CV and MIDI from steps, durations, retrigger, the minimum gate,
// polymeter, mute and reset. argv[1] == "threads" runs the threaded test only (ThreadSanitizer). Built and
// run by test_gseq.py.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "GseqEngine.h"

using gseq::GseqEngine;
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
#define NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

// 120 bpm, 16ths: a step is 6000 samples at 48 kHz
static constexpr int RATE = 48000, BLOCK = 128, STEP16 = 6000, OUTS = 3 * GseqEngine::VOICES;

// An engine and every output sample, recorded
struct Rig {
    GseqEngine e;
    float in[2][BLOCK] = {}, out[OUTS][BLOCK];
    const float* ip[2] = { in[0], in[1] };
    float* op[OUTS];
    std::vector<float> rec[OUTS];
    long frame = 0;

    Rig() {
        for (int c = 0; c < OUTS; c++) op[c] = out[c];
        e.prepare(float(RATE), BLOCK);
        e.setBpm(120.0);
    }
    void runTo(long end) {
        while (frame < end) {
            int n = int(std::min<long>(BLOCK, end - frame));
            e.process(ip, op, n);
            for (int c = 0; c < OUTS; c++) rec[c].insert(rec[c].end(), out[c], out[c] + n);
            frame += n;
        }
    }
    float at(int channel, long f) const { return rec[size_t(channel)][size_t(f)]; }
    // rising edges of a gate in [from, to)
    int onsets(int channel, long from, long to) const {
        int count = 0;
        for (long f = std::max(1L, from); f < to; f++) count += at(channel, f) > 0.5f && at(channel, f - 1) < 0.5f;
        return count + (from == 0 && at(channel, 0) > 0.5f);
    }
    std::vector<GseqEngine::Midi> midi() {
        std::vector<GseqEngine::Midi> all;
        GseqEngine::Midi m;
        while (e.midiOut(m)) all.push_back(m);
        return all;
    }
};

static Step note(int n, int duration, int velocity = 100) {
    Step s;
    s.on = true, s.note = uint8_t(n), s.duration = uint8_t(duration), s.velocity = uint8_t(velocity);
    return s;
}

static Row cvRow(int voice, int length) {
    Row r;
    r.voice = uint8_t(voice), r.length = uint8_t(length), r.velocityCv = true;
    return r;
}

static void silence(GseqEngine& e) {  // the default rows play MIDI on channels 1-8: none here
    for (int r = 0; r < GseqEngine::ROWS; r++) e.setRow(r, Row());
}

static void testWords() {
    Step s = note(127, 15, 127);
    Step t = Step::unpack(s.pack());
    CHECK(t.on && t.note == 127 && t.velocity == 127 && t.duration == 15);
    CHECK(Step::unpack(0).velocity == 1);  // velocity 0 would be a note-off
    Row r;
    r.length = 32, r.division = 5, r.channel = 16, r.voice = 4, r.velocityCv = true, r.mute = true;
    r.velocityGated = true;
    Row u = Row::unpack(r.pack());
    CHECK(u.length == 32 && u.division == 5 && u.channel == 16 && u.voice == 4 && u.velocityCv && u.mute);
    CHECK(u.velocityGated && !Row::unpack(Row().pack()).velocityGated);  // a word before bit 18: held
    r.mono = true;
    CHECK(Row::unpack(r.pack()).mono && !Row::unpack(Row().pack()).mono);
    Row w = Row::unpack(0xFFFFFFFFu);  // any word is valid
    CHECK(w.length == 32 && w.division == GseqEngine::DIVISIONS - 1 && w.channel == 16 && w.voice == 4);
    GseqEngine e;
    // new rows: MIDI on their own channel; rows 1-4 on voices 1-4, with velocity CV; rows 5-8 MIDI only
    CHECK(e.row(0).channel == 1 && e.row(0).voice == 1 && e.row(0).velocityCv);
    CHECK(e.row(3).channel == 4 && e.row(3).voice == 4 && e.row(3).velocityCv);
    CHECK(e.row(4).channel == 5 && e.row(4).voice == 0 && e.row(7).channel == 8 && e.row(7).voice == 0);
    silence(e);
    CHECK(e.setRow(0, cvRow(1, 16)));
    CHECK(!e.setRow(1, cvRow(1, 16)) && e.row(1).voice == 0 && e.row(1).velocityCv);  // voice 1 is row 0's
    CHECK(e.setRow(1, cvRow(2, 16)));
}

static void testCv() {
    Rig r;
    silence(r.e);
    r.e.setRow(0, cvRow(1, 4));
    r.e.setStep(0, 0, note(72, 3, 127));  // half a step
    r.e.setStep(0, 2, note(48, 7, 64));   // one step
    r.runTo(4 * STEP16 + 100);
    CHECK(r.at(0, 10) == 1.0f && r.at(0, STEP16 / 2 - 10) == 1.0f && r.at(0, STEP16 / 2 + 10) == 0.0f);
    NEAR(r.at(4, 10), 0.2, 1e-6);  // C5: 1 V
    NEAR(r.at(8, 10), 1.0, 1e-6);  // velocity 127: 5 V
    CHECK(r.at(0, STEP16 + 10) == 0.0f);  // step 2 is off
    NEAR(r.at(4, STEP16 + 10), 0.2, 1e-6);  // the pitch holds
    CHECK(r.at(0, 2 * STEP16 + 10) == 1.0f && r.at(0, 3 * STEP16 - 10) == 1.0f && r.at(0, 3 * STEP16 + 10) == 0.0f);
    NEAR(r.at(4, 2 * STEP16 + 10), -0.2, 1e-6);
    NEAR(r.at(8, 2 * STEP16 + 10), 64.0 / 127.0, 1e-6);
    CHECK(r.at(0, 4 * STEP16 + 10) == 1.0f);  // the row's 4 steps again
    CHECK(r.e.playing(0) == 0);
    CHECK(r.midi().empty());
}

// gated velocity CV: the velocity while the gate is open, 0 V when it closes and in a retrigger's gap
static void testVelocityGated() {
    Rig r;
    silence(r.e);
    Row row = cvRow(1, 4);
    row.velocityGated = true;
    r.e.setRow(0, row);
    r.e.setStep(0, 0, note(60, 9, 127));  // 2 steps: step 2 retriggers it
    r.e.setStep(0, 1, note(62, 3, 64));   // half a step
    r.runTo(3 * STEP16);
    NEAR(r.at(8, 10), 1.0, 1e-6);
    NEAR(r.at(8, STEP16 - 10), 1.0, 1e-6);
    CHECK(r.at(0, STEP16 + 20) == 0.0f && r.at(8, STEP16 + 20) == 0.0f);  // the gap
    NEAR(r.at(8, STEP16 + 60), 64.0 / 127.0, 1e-6);
    CHECK(r.at(0, STEP16 + STEP16 / 2 + 10) == 0.0f && r.at(8, STEP16 + STEP16 / 2 + 10) == 0.0f);  // closed
    row.velocityGated = false;  // held: stays after the gate closes, as before
    r.e.setRow(0, row);
    r.runTo(6 * STEP16);
    CHECK(r.at(0, 5 * STEP16 + STEP16 / 2 + 10) == 0.0f);
    NEAR(r.at(8, 5 * STEP16 + STEP16 / 2 + 10), 64.0 / 127.0, 1e-6);
}

static void testRetrigger() {
    Rig r;
    silence(r.e);
    r.e.setRow(0, cvRow(1, 4));
    r.e.setStep(0, 0, note(60, 9));  // 2 steps, so step 2 starts while it sounds
    r.e.setStep(0, 1, note(62, 3));
    r.runTo(2 * STEP16);
    CHECK(r.at(0, STEP16 - 10) == 1.0f);
    CHECK(r.at(0, STEP16 + 20) == 0.0f);  // closed for 1 ms, 48 samples
    CHECK(r.at(0, STEP16 + 60) == 1.0f);
    NEAR(r.at(4, STEP16 + 20), 2.0 / 12 * 0.2, 1e-6);
    CHECK(r.at(0, STEP16 + STEP16 / 2 + 10) == 0.0f);  // the second note's half step, not the first's two
}

static void testMinimumGate() {
    Rig r;
    silence(r.e);
    r.e.setBpm(1000.0);  // 32nds: 360 samples, an eighth of one 45
    Row row = cvRow(1, 1);
    row.division = 5;
    r.e.setRow(0, row);
    r.e.setStep(0, 0, note(60, 0));
    r.runTo(300);
    CHECK(r.at(0, 80) == 1.0f && r.at(0, 110) == 0.0f);  // 2 ms, 96 samples
}

static void testMidi() {
    Rig r;
    silence(r.e);
    Row row;
    row.channel = 10, row.length = 4;
    r.e.setRow(0, row);
    r.e.setStep(0, 0, note(36, 3, 90));
    r.e.setStep(0, 1, note(38, 9, 80));  // 2 steps: step 3 retriggers it
    r.e.setStep(0, 2, note(38, 3, 70));
    r.runTo(4 * STEP16 - 100);
    auto m = r.midi();
    const uint8_t want[][3] = { { 0x99, 36, 90 }, { 0x89, 36, 0 }, { 0x99, 38, 80 },
                                { 0x89, 38, 0 },  { 0x99, 38, 70 }, { 0x89, 38, 0 } };
    CHECK(m.size() == 6);
    for (size_t i = 0; i < std::min<size_t>(m.size(), 6); i++)
        CHECK(m[i].size == 3 && m[i].bytes[0] == want[i][0] && m[i].bytes[1] == want[i][1] &&
              m[i].bytes[2] == want[i][2]);
    CHECK(r.at(0, 10) == 0.0f);  // no CV voice
}

static void testBoth() {
    Rig r;
    silence(r.e);
    Row row = cvRow(2, 2);
    row.channel = 1;
    r.e.setRow(3, row);
    r.e.setStep(3, 0, note(64, 3));
    r.runTo(100);
    CHECK(r.at(1, 10) == 1.0f);
    auto m = r.midi();
    CHECK(m.size() == 1 && m[0].bytes[0] == 0x90 && m[0].bytes[1] == 64);
}

static void testPolymeter() {
    Rig r;
    silence(r.e);
    r.e.setRow(0, cvRow(1, 3));
    r.e.setRow(1, cvRow(2, 4));
    r.e.setStep(0, 0, note(60, 3));
    r.e.setStep(1, 0, note(60, 3));
    r.runTo(12 * STEP16 + 100);
    CHECK(r.onsets(0, 0, 12 * STEP16 - 100) == 4);  // steps 0, 3, 6, 9
    CHECK(r.onsets(1, 0, 12 * STEP16 - 100) == 3);  // 0, 4, 8
    CHECK(r.e.playing(0) == 0 && r.e.playing(1) == 0);  // together again after 12
    CHECK(r.at(0, 12 * STEP16 + 10) == 1.0f && r.at(1, 12 * STEP16 + 10) == 1.0f);
}

static void testDivision() {
    Rig r;
    silence(r.e);
    Row row = cvRow(1, 1);
    row.division = 2;  // 3 a quarter: 8000 samples
    r.e.setRow(0, row);
    r.e.setStep(0, 0, note(60, 3));
    r.runTo(3 * 8000 + 100);
    CHECK(r.onsets(0, 0, 3 * 8000 - 100) == 3);
    CHECK(r.at(0, 8000 + 10) == 1.0f && r.at(0, 8000 - 10) == 0.0f);
}

static void testMute() {
    Rig r;
    silence(r.e);
    Row row = cvRow(1, 1);
    row.mute = true;
    r.e.setRow(0, row);
    r.e.setStep(0, 0, note(60, 7));
    r.runTo(2 * STEP16);
    CHECK(r.onsets(0, 0, 2 * STEP16) == 0);
}

static void testReset() {
    Rig r;
    silence(r.e);
    r.e.setRow(0, cvRow(1, 4));
    r.e.setStep(0, 0, note(72, 3));
    r.e.setStep(0, 2, note(48, 7));
    r.runTo(2 * STEP16 + 1000);  // in step 3
    std::fill(r.in[0], r.in[0] + BLOCK, 1.0f);  // 5 V on input 1
    r.runTo(2 * STEP16 + 1000 + BLOCK);
    std::fill(r.in[0], r.in[0] + BLOCK, 0.0f);
    r.runTo(2 * STEP16 + 2000);
    // the gap closes the gate, then step 1 plays again
    NEAR(r.at(4, 2 * STEP16 + 1000 + 100), 0.2, 1e-6);
    CHECK(r.at(0, 2 * STEP16 + 1000 + 100) == 1.0f);
    CHECK(r.e.playing(0) == 0);
}

static Row lengthRow(int length) {
    Row r;
    r.length = uint8_t(length);
    return r;
}

// Rec on row 1: the next whole pass replaces the row; notes land on the nearest step, hold for their
// duration, echo on the row's MIDI channel and CV voice, and are not played back in the pass that took them
static void testRecord() {
    Rig r;
    silence(r.e);
    Row row = cvRow(1, 4);
    row.channel = 1;
    r.e.setRow(0, row);
    r.e.setStep(0, 0, note(60, 3));
    r.e.setStep(0, 1, note(62, 3));
    r.e.setStep(0, 3, note(65, 3));
    r.e.setCursor(0, 0);
    r.runTo(1000);
    r.e.toggleRecord();
    r.runTo(1128);
    CHECK(r.e.record() == GseqEngine::ARMED && r.e.recordRow() == 0);
    r.runTo(4 * STEP16 + 200);  // the take: steps 5-8, the row's next pass
    CHECK(r.e.record() == GseqEngine::RECORDING);
    r.midi();  // the pass before the take played as usual
    r.e.noteIn(72, 100);  // just after step 5: its step
    r.runTo(4 * STEP16 + 200 + 3000);
    CHECK(r.at(0, 4 * STEP16 + 300) == 1.0f);  // echo on CV voice 1
    NEAR(r.at(4, 4 * STEP16 + 300), 0.2, 1e-6);
    r.e.noteIn(72, 0);  // held half a step
    r.runTo(6 * STEP16 - 600);
    r.e.noteIn(74, 90);  // just before step 7: its step
    r.runTo(7 * STEP16 - 600);
    r.e.noteIn(74, 0);  // held one step
    r.runTo(8 * STEP16 - 100);
    auto m = r.midi();  // the echo only: the take's steps did not play, the old ones were cleared
    const uint8_t want[][3] = { { 0x90, 72, 100 }, { 0x80, 72, 0 }, { 0x90, 74, 90 }, { 0x80, 74, 0 } };
    CHECK(m.size() == 4);
    for (size_t i = 0; i < std::min<size_t>(m.size(), 4); i++)
        CHECK(m[i].bytes[0] == want[i][0] && m[i].bytes[1] == want[i][1] && m[i].bytes[2] == want[i][2]);
    Step s0 = r.e.step(0, 0), s2 = r.e.step(0, 2);
    CHECK(s0.on && s0.note == 72 && s0.velocity == 100 && s0.duration == 3);
    CHECK(s2.on && s2.note == 74 && s2.velocity == 90 && s2.duration == 7);
    CHECK(!r.e.step(0, 1).on && !r.e.step(0, 3).on);
    CHECK(r.e.step(0, 1).note == 62);  // cleared, not reset
    r.runTo(8 * STEP16 + 100);  // one pass, then the take stops and the row plays it
    CHECK(r.e.record() == GseqEngine::IDLE);
    m = r.midi();
    CHECK(m.size() == 1 && m[0].bytes[0] == 0x90 && m[0].bytes[1] == 72);
    r.e.noteIn(80, 100);  // not recording: ignored
    r.runTo(8 * STEP16 + 1000);
    CHECK(r.midi().empty());
}

// a chord's notes, lowest first, from the cursor's row down, wrapping to row 1; only that row is replaced
static void testRecordChord() {
    Rig r;
    silence(r.e);
    for (int row : { 6, 7, 0 }) r.e.setRow(row, lengthRow(4));
    r.e.setStep(6, 2, note(50, 3));
    r.e.setStep(7, 2, note(51, 3));
    r.e.setStep(0, 2, note(52, 3));
    r.e.setCursor(6, 0);
    r.e.toggleRecord();
    r.runTo(4 * STEP16 + 200);
    for (int n : { 64, 60, 67 }) r.e.noteIn(n, 100);
    r.runTo(4 * STEP16 + 1200);
    for (int n : { 64, 60, 67 }) r.e.noteIn(n, 0);
    r.runTo(8 * STEP16 + 100);
    CHECK(r.e.step(6, 0).on && r.e.step(6, 0).note == 60);
    CHECK(r.e.step(7, 0).on && r.e.step(7, 0).note == 64);
    CHECK(r.e.step(0, 0).on && r.e.step(0, 0).note == 67);
    CHECK(!r.e.step(6, 2).on);                         // the cursor's row: replaced
    CHECK(r.e.step(7, 2).on && r.e.step(0, 2).on);     // the rows it spread to: kept
}

// step entry: each note at the cursor, which moves on when every key is up; a chord across rows
static void testStepEntry() {
    Rig r;
    silence(r.e);
    r.e.setRow(1, lengthRow(4));
    r.e.setStepEntry(true);
    r.e.setCursor(1, 0);
    r.e.toggleRecord();  // Rec does nothing during step entry
    r.e.noteIn(60, 100);
    r.runTo(BLOCK);
    CHECK(r.e.record() == GseqEngine::IDLE);
    r.e.noteIn(60, 0);
    r.runTo(2 * BLOCK);
    CHECK(r.e.step(1, 0).on && r.e.step(1, 0).note == 60 && r.e.cursorCol() == 1);
    r.e.noteIn(65, 80);
    r.e.noteIn(62, 90);
    r.runTo(3 * BLOCK);
    r.e.noteIn(65, 0);
    r.runTo(4 * BLOCK);
    CHECK(r.e.cursorCol() == 1);  // a key is still down
    r.e.noteIn(62, 0);
    r.runTo(5 * BLOCK);
    CHECK(r.e.step(1, 1).note == 62 && r.e.step(2, 1).note == 65 && r.e.step(2, 1).velocity == 80);
    CHECK(r.e.cursorCol() == 2);
    r.e.setCursor(1, 3);
    r.e.noteIn(67, 100);
    r.e.noteIn(67, 0);
    r.runTo(6 * BLOCK);
    CHECK(r.e.cursorCol() == 0);  // past the row's last step, its first
}

static void testRecordCancel() {
    Rig r;
    silence(r.e);
    r.e.setRow(0, lengthRow(4));
    r.e.setStep(0, 1, note(62, 3));
    r.e.toggleRecord();
    r.runTo(BLOCK);
    r.e.toggleRecord();  // pressed again while armed
    r.runTo(2 * BLOCK);
    CHECK(r.e.record() == GseqEngine::IDLE);
    r.runTo(8 * STEP16);
    CHECK(r.e.step(0, 1).on);
}

// Rec moves with the cursor while armed; once recording, the take stays on its row
static void testRecordRearm() {
    Rig r;
    silence(r.e);
    r.e.setRow(0, lengthRow(4));
    r.e.setRow(2, lengthRow(8));
    r.e.setCursor(0, 0);
    r.e.toggleRecord();
    r.runTo(BLOCK);
    CHECK(r.e.record() == GseqEngine::ARMED && r.e.recordRow() == 0);
    r.e.setCursor(2, 5);
    r.runTo(2 * BLOCK);
    CHECK(r.e.record() == GseqEngine::ARMED && r.e.recordRow() == 2);
    r.runTo(4 * STEP16 + 200);  // row 1's next pass would start here; row 3's starts at step 9
    CHECK(r.e.record() == GseqEngine::ARMED);
    r.runTo(8 * STEP16 + 200);
    CHECK(r.e.record() == GseqEngine::RECORDING && r.e.recordRow() == 2);
    r.e.setCursor(0, 0);
    r.e.noteIn(70, 100);
    r.runTo(8 * STEP16 + 1200);
    r.e.noteIn(70, 0);
    r.runTo(8 * STEP16 + 2200);
    CHECK(r.e.recordRow() == 2);
    CHECK(r.e.step(2, 0).on && r.e.step(2, 0).note == 70);
    CHECK(!r.e.step(0, 0).on);
}

// playIn: sounds from the cursor's row down in any mode, writes no step, and does not move the cursor
static void testPlay() {
    Rig r;
    silence(r.e);
    Row a = cvRow(1, 4), b = cvRow(2, 4);
    a.channel = 1, b.channel = 2;
    r.e.setRow(3, a);
    r.e.setRow(4, b);
    r.e.setCursor(3, 1);
    r.e.playIn(64, 90);
    r.e.playIn(60, 100);
    r.runTo(BLOCK);
    CHECK(r.at(0, BLOCK - 1) == 1.0f && r.at(1, BLOCK - 1) == 1.0f);  // voices 1 and 2
    auto m = r.midi();
    CHECK(m.size() == 2 && m[0].bytes[0] == 0x90 && m[0].bytes[1] == 64 && m[1].bytes[0] == 0x91);
    r.runTo(4 * STEP16);  // held past the step: still sounding
    CHECK(r.at(0, 4 * STEP16 - 1) == 1.0f);
    r.e.playIn(64, 0);
    r.e.playIn(60, 0);
    r.runTo(4 * STEP16 + 2 * BLOCK);
    CHECK(r.at(0, 4 * STEP16 + 2 * BLOCK - 1) == 0.0f && r.at(1, 4 * STEP16 + 2 * BLOCK - 1) == 0.0f);
    for (int row = 0; row < GseqEngine::ROWS; row++)
        for (int st = 0; st < 4; st++) CHECK(!r.e.step(row, st).on);
    r.e.setStepEntry(true);  // step entry: noteIn writes at the cursor, playIn does not
    r.e.playIn(67, 100);
    r.e.playIn(67, 0);
    r.runTo(4 * STEP16 + 3 * BLOCK);
    CHECK(!r.e.step(3, 1).on && r.e.cursorCol() == 1);
}

// Roll on: every row takes row 1's channel, length, division and velocity CV, voices 1-4, no mute
static void testRollMode() {
    GseqEngine e;
    Row first = e.row(0);
    first.length = 12, first.division = 2, first.channel = 3, first.velocityGated = true, first.mute = true;
    e.setRow(0, first);
    e.setRoll(true);
    CHECK(e.roll());
    for (int r = 0; r < GseqEngine::ROWS; r++) {
        const Row x = e.row(r);
        CHECK(x.length == 12 && x.division == 2 && x.channel == 3 && x.velocityCv && x.velocityGated && !x.mute);
        CHECK(x.voice == (r < GseqEngine::VOICES ? r + 1 : 0));
    }
    e.setRoll(false);  // back to grid: the rows stay
    CHECK(!e.roll() && e.row(5).channel == 3);
    Row noCv = e.row(0);
    noCv.voice = 0;
    e.setRow(0, noCv);
    e.setRoll(true);  // row 1 without a voice: CV off for the track
    for (int r = 0; r < GseqEngine::ROWS; r++) CHECK(e.row(r).voice == 0);
}

// the placement rules of docs/dev/gseq-roll.md; durations: index 3 half a step, 7 one, 9 two, 10 three, 11 four
static void testRollPlace() {
    GseqEngine e;
    e.setRoll(true);
    CHECK(e.addNote(0, note(60, 7)) == 0 && e.addNote(0, note(64, 7)) == 1 && e.addNote(0, note(67, 7)) == 2);
    CHECK(e.addNote(0, note(64, 7, 30)) == 1 && e.step(1, 0).velocity == 30);  // the same cell: updated
    CHECK(e.findNote(0, 67) == 2 && e.findNote(0, 65) == -1);
    CHECK(e.addNote(4, note(50, 11)) == 0);  // steps 4-7 on row 1
    CHECK(e.addNote(6, note(52, 3)) == 1);   // row 1 sounds there
    CHECK(e.addNote(7, note(50, 3)) == 0);   // rule 1: the same pitch, sounding: that row
    CHECK(e.addNote(4, note(55, 10)) == 2);  // rule 2: on row 2 it would run into step 6's note
    CHECK(e.removeNote(0, 60) == 0 && e.findNote(0, 60) == -1 && e.removeNote(0, 60) == -1);

    GseqEngine full;  // rule 3, and refusal: 8 notes at step 2 of 4, then a long one at step 1
    Row four = full.row(0);
    four.length = 4;
    full.setRow(0, four);
    full.setRoll(true);
    for (int i = 0; i < GseqEngine::ROWS; i++) CHECK(full.addNote(2, note(40 + i, 7)) == i);
    CHECK(full.addNote(2, note(90, 7)) == -1);
    CHECK(full.addNote(1, note(30, 9)) == 0);  // no row free for 2 steps: the first free at the step

    GseqEngine wrap;  // a note at the last step covers the first
    Row eight = wrap.row(0);
    eight.length = 8;
    wrap.setRow(0, eight);
    wrap.setRoll(true);
    CHECK(wrap.addNote(7, note(60, 9)) == 0 && wrap.addNote(0, note(62, 7)) == 1);

    GseqEngine d;  // a longer duration moves a note to a free row; one that retriggers its own pitch stays
    d.setRoll(true);
    CHECK(d.addNote(0, note(60, 7)) == 0 && d.addNote(2, note(62, 7)) == 0);
    CHECK(d.setNoteDuration(0, 60, 10) == 1);
    CHECK(d.step(1, 0).on && d.step(1, 0).duration == 10 && !d.step(0, 0).on);
    CHECK(d.setNoteDuration(0, 60, 7) == 1);  // fits where it is
    CHECK(d.addNote(4, note(70, 11)) == 0 && d.addNote(6, note(70, 3)) == 0);
    CHECK(d.setNoteDuration(6, 70, 15) == 0);
    CHECK(d.setNoteDuration(9, 99, 7) == -1);
}

// Rec in roll: the whole track's next pass; a chord and overlapping (legato) notes each get a row; the
// notes no key landed on are cleared from every row
static void testRollRecord() {
    Rig r;
    silence(r.e);
    Row track = lengthRow(4);
    track.channel = 1;
    r.e.setRow(0, track);
    r.e.setRoll(true);
    r.e.setStep(5, 1, note(40, 3));
    r.e.setStep(0, 3, note(41, 3));
    r.e.setCursor(3, 0);  // any row: a roll take is the whole track
    r.e.toggleRecord();
    r.runTo(4 * STEP16 + 200);
    CHECK(r.e.record() == GseqEngine::RECORDING && r.e.recordRow() == 0);
    r.e.noteIn(60, 100);
    r.e.noteIn(64, 90);
    r.runTo(4 * STEP16 + 1200);
    r.e.noteIn(60, 0);
    r.e.noteIn(64, 0);
    r.runTo(5 * STEP16 - 300);
    r.e.noteIn(67, 100);  // step 6 of the pass
    r.runTo(6 * STEP16 - 300);
    r.e.noteIn(69, 100);  // step 7, while 67 is held
    r.runTo(6 * STEP16 + 2700);
    r.e.noteIn(67, 0);  // held 1.5 steps
    r.runTo(7 * STEP16 - 600);
    r.e.noteIn(69, 0);
    r.runTo(8 * STEP16 + 100);
    CHECK(r.e.record() == GseqEngine::IDLE);
    const int a = r.e.findNote(0, 60), b = r.e.findNote(0, 64);
    CHECK(a >= 0 && b >= 0 && a != b);
    const int c = r.e.findNote(1, 67), d = r.e.findNote(2, 69);
    CHECK(c >= 0 && d >= 0 && c != d);
    CHECK(c < 0 || r.e.step(c, 1).duration == 8);  // 1.5 steps
    CHECK(r.e.findNote(1, 40) == -1 && r.e.findNote(3, 41) == -1);
    r.midi();
    r.runTo(12 * STEP16 + 100);  // the next pass plays the take
    int ons = 0;
    for (auto& m : r.midi()) ons += m.bytes[0] == 0x90;
    CHECK(ons == 4);
}

// step entry in roll: a chord at the cursor's step, each note its own row; the cursor moves on
static void testRollStepEntry() {
    Rig r;
    silence(r.e);
    Row track = lengthRow(4);
    track.channel = 1;
    r.e.setRow(0, track);
    r.e.setRoll(true);
    r.e.setStepEntry(true);
    r.e.setCursor(0, 1);
    r.e.noteIn(60, 100);
    r.e.noteIn(64, 100);
    r.runTo(BLOCK);
    r.e.noteIn(60, 0);
    r.e.noteIn(64, 0);
    r.runTo(2 * BLOCK);
    const int a = r.e.findNote(1, 60), b = r.e.findNote(1, 64);
    CHECK(a >= 0 && b >= 0 && a != b && r.e.cursorCol() == 2);
}

// keys in roll sound on the first silent rows, not from the cursor's row down: a chord gets CV voices 1, 2
static void testRollEcho() {
    Rig r;
    silence(r.e);
    Row track = cvRow(1, 4);
    track.channel = 1;
    r.e.setRow(0, track);
    r.e.setRoll(true);
    r.e.setCursor(5, 0);
    r.e.playIn(60, 100);
    r.e.playIn(64, 100);
    r.runTo(BLOCK);
    CHECK(r.at(0, BLOCK - 1) == 1.0f && r.at(1, BLOCK - 1) == 1.0f);
    r.e.playIn(60, 0);
    r.e.playIn(64, 0);
    r.runTo(2 * BLOCK);
    auto m = r.midi();
    CHECK(m.size() == 4 && m[0].bytes[0] == 0x90 && m[1].bytes[0] == 0x90 && m[1].bytes[1] == 64);
    CHECK(m.size() == 4 && m[2].bytes[0] == 0x80 && m[3].bytes[0] == 0x80);
}

// mono roll: one note per step on row 1; a new note replaces the step's; switching on keeps one note per
// step; keys and recording follow
static void testRollMono() {
    Rig r;
    silence(r.e);
    Row track = cvRow(1, 4);
    track.channel = 1;
    r.e.setRow(0, track);
    r.e.setRoll(true);
    CHECK(r.e.addNote(0, note(60, 7)) == 0 && r.e.addNote(0, note(64, 7)) == 1);  // poly: a chord
    CHECK(r.e.addNote(2, note(67, 7)) == 0);
    r.e.addNote(3, note(50, 7));
    r.e.removeNote(3, 50);
    CHECK(r.e.addNote(3, note(52, 7)) == 0);
    r.e.setStep(2, 1, note(55, 7));  // a step whose only note is on row 3
    r.e.setMono(true);
    CHECK(r.e.mono() && r.e.row(5).mono);
    CHECK(r.e.findNote(0, 60) == 0 && r.e.findNote(0, 64) == -1);  // row 1's kept
    CHECK(r.e.findNote(1, 55) == 0);                                 // the lowest row's, moved to row 1
    CHECK(r.e.addNote(0, note(72, 3)) == 0);  // replaces
    CHECK(r.e.findNote(0, 72) == 0 && r.e.findNote(0, 60) == -1);
    CHECK(r.e.setNoteDuration(0, 72, 11) == 0);  // overlaps step 2's note: stays, cut by it
    for (int row = 1; row < GseqEngine::ROWS; row++)
        for (int st = 0; st < 4; st++) CHECK(!r.e.step(row, st).on);
    // keys sound on voice 1, one at a time (the track muted, so its steps do not take the voice)
    Row muted = r.e.row(0);
    muted.mute = true;
    r.e.setRow(0, muted);
    r.e.playIn(60, 100);
    r.e.playIn(64, 100);
    r.runTo(BLOCK);
    CHECK(r.at(0, BLOCK - 1) == 1.0f && r.at(1, BLOCK - 1) == 0.0f);
    NEAR(r.at(4, BLOCK - 1), 4.0 / 12 * 0.2, 1e-6);  // E4: the later key
    r.e.playIn(60, 0);
    r.e.playIn(64, 0);
    r.runTo(2 * BLOCK);
    muted.mute = false;
    r.e.setRow(0, muted);
    // Rec: a chord at a step leaves its last note
    r.e.toggleRecord();
    r.runTo(4 * STEP16 + 200);
    for (int n : { 60, 64, 67 }) r.e.noteIn(n, 100);
    r.runTo(4 * STEP16 + 1200);
    for (int n : { 60, 64, 67 }) r.e.noteIn(n, 0);
    r.runTo(8 * STEP16 + 100);
    CHECK(r.e.findNote(0, 67) == 0 && r.e.findNote(0, 60) == -1 && r.e.findNote(0, 64) == -1);
    r.e.setMono(false);
    CHECK(!r.e.mono() && r.e.addNote(0, note(48, 7)) == 1);  // poly again: stacks
}

// Every note-on gets its note-off and every gate closes: random patterns, edits, keys (noteIn and
// playIn) and modes, then every step off and every key up
static void testNoteOffs() {
    for (int seed = 1; seed <= 20; seed++) {
        Rig r;
        uint32_t x = uint32_t(seed) * 2654435761u;
        auto rnd = [&](int n) {
            x ^= x << 13, x ^= x >> 17, x ^= x << 5;
            return int(x % uint32_t(n));
        };
        int count[16][128] = {};
        auto drain = [&] {
            for (auto& m : r.midi()) {
                const int ch = m.bytes[0] & 15, n = m.bytes[1];
                if ((m.bytes[0] & 0xF0) == 0x90 && m.bytes[2]) count[ch][n]++;
                else if ((m.bytes[0] & 0xF0) == 0x80 || (m.bytes[0] & 0xF0) == 0x90) count[ch][n]--;
            }
        };
        bool down[128] = {};
        for (int t = 0; t < 3000; t++) {
            const int what = rnd(20);
            if (what < 6) {
                Step st;
                st.on = rnd(3) > 0, st.note = uint8_t(36 + rnd(48)), st.velocity = uint8_t(1 + rnd(127));
                st.duration = uint8_t(rnd(GseqEngine::DURATIONS));
                r.e.setStep(rnd(8), rnd(32), st);
            } else if (what < 8) {
                Row row = r.e.row(rnd(8));
                row.length = uint8_t(1 + rnd(32)), row.division = uint8_t(rnd(GseqEngine::DIVISIONS));
                row.channel = uint8_t(rnd(17)), row.voice = uint8_t(rnd(5)), row.mute = rnd(4) == 0;
                r.e.setRow(rnd(8), row);
            } else if (what < 12) {
                const int n = 48 + rnd(24);
                if (rnd(2)) r.e.noteIn(n, down[n] ? 0 : 100);
                else r.e.playIn(n, down[n] ? 0 : 100);
                down[n] = !down[n];
            } else if (what == 12) {
                r.e.toggleRecord();
            } else if (what == 13) {
                r.e.setStepEntry(rnd(2));
            } else if (what == 14) {
                r.e.setCursor(rnd(8), rnd(32));
            } else if (what == 15) {
                r.e.setRoll(rnd(3) > 0);
            } else if (what < 18) {
                r.e.addNote(rnd(32), note(36 + rnd(48), rnd(GseqEngine::DURATIONS), 1 + rnd(127)));
            } else if (what == 18) {
                r.e.removeNote(rnd(32), 36 + rnd(48));
            } else if (what == 19) {
                r.e.setMono(rnd(2));
            }
            r.runTo(r.frame + BLOCK * (1 + rnd(40)));
            drain();
        }
        for (int row = 0; row < GseqEngine::ROWS; row++)
            for (int st = 0; st < GseqEngine::STEPS; st++) r.e.setStep(row, st, Step());
        for (int n = 0; n < 128; n++)
            if (down[n]) r.e.playIn(n, 0);
        r.runTo(r.frame + 20 * RATE);  // longer than any note: 16 steps of 1/4 at 120 bpm is 8 s
        drain();
        int open = 0;
        for (auto& ch : count)
            for (int c : ch) open += c != 0;
        CHECK(open == 0);
        for (int v = 0; v < GseqEngine::VOICES; v++) CHECK(r.at(v, r.frame - 1) == 0.0f);
        if (open) std::printf("seed %d: %d notes left open\n", seed, open);
    }
}

static void testThreads() {
    Rig r;
    std::atomic<bool> stop{ false };
    std::thread ui([&] {
        for (int i = 0; !stop.load(); i++) {
            r.e.setStep(i % 8, i % 32, note(40 + i % 40, i % 16));
            Row row = cvRow(i % 5, 1 + i % 32);
            row.channel = uint8_t(i % 17);
            r.e.setRow((i / 3) % 8, row);
            if (i % 50 == 0) r.e.setRoll(i % 100 == 0);
            if (i % 70 == 0) r.e.setMono(i % 140 == 0);
            r.e.addNote(i % 32, note(40 + i % 40, i % 16));
            r.e.removeNote((i + 7) % 32, 40 + (i + 3) % 40);
            (void)r.e.playing(i % 8);
            (void)r.e.bpm();
        }
    });
    std::thread clock([&] {  // as the MIDI input thread
        for (int i = 0; !stop.load(); i++) {
            r.e.midiClock(i * 0.02);
            if (i % 50 == 0) r.e.midiStart();
            if (i % 50 == 25) r.e.midiStop();
            r.e.noteIn(40 + i % 30, i % 3 ? 100 : 0);
            r.e.playIn(70 + i % 20, i % 4 ? 90 : 0);  // the Launchpad's keys, on the same MIDI input thread
        }
    });
    std::thread keys([&] {  // as the UI thread's Rec, Step and cursor
        for (int i = 0; !stop.load(); i++) {
            if (i % 97 == 0) r.e.toggleRecord();
            r.e.setStepEntry(i % 1000 < 300);
            r.e.setCursor(i % 8, i % 32);
            (void)r.e.record();
        }
    });
    std::thread midi([&] {
        GseqEngine::Midi m;
        while (!stop.load())
            while (r.e.midiOut(m)) {
            }
    });
    for (int b = 0; b < 4000; b++) {
        r.e.setClock(b % 2000 < 1000 ? ssp::engine::Clock::FREE : ssp::engine::Clock::MIDI, 4);
        r.e.process(r.ip, r.op, BLOCK);
    }
    stop = true;
    ui.join();
    clock.join();
    keys.join();
    midi.join();
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "threads") {
        testThreads();
        std::printf("%s\n", failures ? "FAILED" : "all passed");
        return failures ? 1 : 0;
    }
    testWords();
    testCv();
    testVelocityGated();
    testRetrigger();
    testMinimumGate();
    testMidi();
    testBoth();
    testPolymeter();
    testDivision();
    testMute();
    testReset();
    testRecord();
    testRecordChord();
    testStepEntry();
    testRecordCancel();
    testRecordRearm();
    testPlay();
    testRollMode();
    testRollPlace();
    testRollRecord();
    testRollStepEntry();
    testRollEcho();
    testRollMono();
    testNoteOffs();
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
