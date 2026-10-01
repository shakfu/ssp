// Native test of sfct::Engine; built and run by test_engine.py.
// Prints the host realtime factor for 4 recording voices as its last line.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "Engine.h"
#include "FadeZones.h"
#include "Tempo.h"

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

using namespace sfct;

static constexpr float SR = 48000.0f;
static constexpr unsigned BLOCK = 128;
static constexpr unsigned LANES01[LANES] = { 0, 1 };

struct Rig {
    Engine e;
    std::vector<float> in[INPUTS], mix[2], vout[VOICES];
    VoiceParams p[VOICES];
    double maxDiff01 = 0.0;  // max |voice 1 out - voice 2 out| over the last run

    Rig() {
        for (auto& b : in) b.assign(BLOCK, 0.0f);
        for (auto& b : mix) b.assign(BLOCK, 0.0f);
        for (auto& b : vout) b.assign(BLOCK, 0.0f);
        e.setSampleRate(SR);
        for (auto& v : p) v.play = false;
    }

    // Runs `seconds` with input channels held at inL/inR; returns the mean of |x| over the
    // last half of the run for mix L, mix R and each voice out.
    std::vector<double> run(float seconds, float inL = 0.0f, float inR = 0.0f) {
        unsigned blocks = unsigned(seconds * SR / BLOCK);
        std::vector<double> acc(2 + VOICES, 0.0);
        unsigned counted = 0;
        maxDiff01 = 0.0;
        for (unsigned b = 0; b < blocks; b++) {
            for (unsigned v = 0; v < VOICES; v++) e.set(v, p[v]);
            std::fill(in[0].begin(), in[0].end(), inL);
            std::fill(in[1].begin(), in[1].end(), inR);
            const float* ip[INPUTS] = { in[0].data(), in[1].data() };
            float* mp[2] = { mix[0].data(), mix[1].data() };
            float* vp[VOICES];
            for (unsigned v = 0; v < VOICES; v++) vp[v] = vout[v].data();
            e.process(ip, mp, vp, BLOCK);
            e.scanPeaks(BUFFER_FRAMES, LANES01);  // as the plugin does every block
            if (b < blocks / 2) continue;
            for (unsigned i = 0; i < BLOCK; i++) {
                maxDiff01 = std::max(maxDiff01, double(std::fabs(vout[0][i] - vout[1][i])));
                acc[0] += mix[0][i];
                acc[1] += mix[1][i];
                for (unsigned v = 0; v < VOICES; v++) acc[2 + v] += vout[v][i];
                counted++;
            }
        }
        for (auto& a : acc) a /= counted;
        return acc;
    }
};

static bool allFinite(const std::vector<double>& xs) {
    for (double x : xs)
        if (!std::isfinite(x)) return false;
    return true;
}

static void testSilence() {
    Rig r;
    for (auto& v : r.p) v.play = true;
    auto m = r.run(0.5f);
    CHECK(allFinite(m));
    for (double x : m) CHECK(std::fabs(x) < 1e-6);
}

static void testRecordThenPlay() {
    Rig r;
    auto& v = r.p[0];
    v.play = v.rec = true;
    v.inputGain = 1.0f;
    v.recLevel = 1.0f;
    v.preLevel = 0.0f;
    v.loopStart = 0.0f;
    v.loopEnd = 0.1f;
    v.fadeTime = 0.005f;
    v.level = 1.0f;
    v.pan = -1.0f;
    r.run(0.3f, 0.5f, 0.0f);  // three laps of the loop

    v.rec = false;
    v.inputGain = 0.0f;
    auto m = r.run(0.4f);
    CHECK(allFinite(m));
    // softcut's record-path SoftClip has gain 1.2 below its 0.68 knee: 0.5 records as 0.6.
    // Positive because fixQuirks undoes upstream's polarity inversion.
    CHECK(m[2] > 0.58 && m[2] < 0.62);
    CHECK(m[0] > 0.58);             // panned hard left
    CHECK(std::fabs(m[1]) < 1e-3);  // nothing right
    // voice 0 owns buffer 0; buffer 1 stays silent
    for (float s : r.e.buffer(1)) {
        if (s != 0.0f) {
            CHECK(s == 0.0f);
            break;
        }
    }
}

static void testSecondChannelToSecondBuffer() {
    Rig r;
    auto& v = r.p[1];
    v.play = v.rec = true;
    v.inputGain = 1.0f;
    v.preLevel = 0.0f;
    v.loopEnd = 0.1f;
    r.run(0.3f, 0.0f, 0.5f);
    double sum0 = 0, sum1 = 0;
    for (float s : r.e.buffer(0)) sum0 += std::fabs(s);
    for (float s : r.e.buffer(1)) sum1 += std::fabs(s);
    CHECK(sum0 == 0.0);
    CHECK(sum1 > 0.0);
}

// JUCE hands over one buffer whose input and output channels share indices.
static void testOutputsAliasInputs() {
    Engine e;
    e.setSampleRate(SR);
    VoiceParams p;
    p.rec = true;
    p.inputGain = 1.0f;
    p.preLevel = 0.0f;
    p.loopEnd = 0.1f;
    p.fadeTime = 0.005f;
    std::vector<float> ch[2] = { std::vector<float>(BLOCK), std::vector<float>(BLOCK) };
    for (unsigned b = 0; b < unsigned(0.08f * SR / BLOCK); b++) {
        e.set(0, p);
        std::fill(ch[0].begin(), ch[0].end(), 0.5f);
        float* io[2] = { ch[0].data(), ch[1].data() };
        float* vo[VOICES] = {};
        e.process(io, io, vo, BLOCK);
    }
    CHECK(e.buffer(0)[3000] > 0.58f);
}

static void stage(Engine& e, float v0, float v1) {
    e.beginLoad();
    std::fill(e.staging(0), e.staging(0) + BUFFER_FRAMES, v0);
    std::fill(e.staging(1), e.staging(1) + BUFFER_FRAMES, v1);
    e.commitLoad(0b11);
}

static void stageOne(Engine& e, unsigned b, float v) {
    e.beginLoad();
    std::fill(e.staging(b), e.staging(b) + BUFFER_FRAMES, v);
    e.commitLoad(1u << b);
}

static void testPartialLoad() {
    Rig r;
    stage(r.e, 0.1f, 0.1f);
    r.run(0.01f);
    stageOne(r.e, 1, 0.7f);
    r.run(0.01f);
    CHECK(r.e.buffer(0)[1000] == 0.1f);  // untouched
    CHECK(r.e.buffer(1)[1000] == 0.7f);
}

// a reclaimed load keeps the buffers the earlier load staged
static void testReclaimKeepsEarlierBuffers() {
    Rig r;
    stageOne(r.e, 0, 0.2f);
    stageOne(r.e, 1, 0.4f);
    r.run(0.01f);
    CHECK(r.e.buffer(0)[1000] == 0.2f);
    CHECK(r.e.buffer(1)[1000] == 0.4f);
}

static void testDisabledVoice() {
    Rig r;
    auto& v = r.p[0];
    v.on = false;
    v.play = v.rec = true;
    v.inputGain = 1.0f;
    v.preLevel = 0.0f;
    v.loopEnd = 0.1f;
    v.fadeTime = 0.005f;
    auto m = r.run(0.2f, 0.5f, 0.0f);
    CHECK(m[2] == 0.0 && m[0] == 0.0);
    double sum = 0;
    for (float s : r.e.buffer(0)) sum += std::fabs(s);
    CHECK(sum == 0.0);
    v.on = true;
    r.run(0.2f, 0.5f, 0.0f);
    CHECK(r.e.buffer(0)[2400] > 0.58f);
}

// a linked pair over identical buffers stays sample-locked through rate and loop changes
static void testLinkedPairLocked() {
    Rig r;
    r.e.beginLoad();
    for (unsigned b = 0; b < SHARED_BUFFERS; b++) {
        float* d = r.e.staging(b);
        for (unsigned i = 0; i < BUFFER_FRAMES; i++) d[i] = std::sin(i * 0.01f);
    }
    r.e.commitLoad(0b11);
    auto& lead = r.p[0];
    lead.play = true;
    lead.level = 1.0f;
    lead.pan = -1.0f;
    lead.loopEnd = 0.3f;
    lead.rate = 0.7f;
    r.p[partnerOf(0)] = linkedTo(lead);
    CHECK(r.p[1].pan == 1.0f);
    r.run(0.5f);
    CHECK(r.maxDiff01 == 0.0);
    lead.rate = -1.3f;
    lead.loopStart = 0.1f;
    r.p[1] = linkedTo(lead);
    r.run(0.5f);
    CHECK(r.maxDiff01 == 0.0);
}

static void testLoadReplacesBuffers() {
    Rig r;
    stage(r.e, 0.25f, -0.25f);
    CHECK(r.e.buffer(0)[1000] == 0.0f);  // not before the next process()
    auto& v = r.p[0];
    v.play = true;
    v.level = 1.0f;
    v.pan = 0.0f;
    v.loopEnd = 0.1f;
    v.fadeTime = 0.005f;  // equal-power crossfades sum above unity; keep them short
    auto m = r.run(0.3f);
    CHECK(r.e.buffer(0)[1000] == 0.25f);
    CHECK(r.e.buffer(1)[1000] == -0.25f);
    // played back without the record path's soft clip
    CHECK(std::fabs(m[2] - 0.25) < 0.01);
}

static void testUnswappedLoadIsReclaimed() {
    Rig r;
    stage(r.e, 0.1f, 0.1f);
    stage(r.e, 0.3f, 0.3f);
    r.run(0.01f);
    CHECK(r.e.buffer(0)[1000] == 0.3f);
    // the old buffers came back as staging, and the next load reuses them
    stage(r.e, 0.5f, 0.5f);
    r.run(0.01f);
    CHECK(r.e.buffer(1)[5] == 0.5f);
}

static void testPeakScan() {
    Engine e;
    e.beginLoad();
    float* d0 = e.staging(0);
    float* d1 = e.staging(1);
    std::fill(d0, d0 + BUFFER_FRAMES, 0.0f);
    std::fill(d1, d1 + BUFFER_FRAMES, 0.0f);
    unsigned second = unsigned(SR);
    for (unsigned i = 0; i < second; i++) {
        d0[i] = (i % 2 ? -0.3f : 0.1f);  // peak is |x|, so the negative half counts
        d1[i] = 0.6f;
    }
    e.commitLoad(0b11);
    float* vo[VOICES] = {};
    std::vector<float> ch[2] = { std::vector<float>(BLOCK), std::vector<float>(BLOCK) };
    float* io[2] = { ch[0].data(), ch[1].data() };
    e.process(io, io, vo, BLOCK);  // swaps the load in

    unsigned view = 2 * second;  // first half loud, second half silent
    unsigned perBlock = SCAN_BUDGET * PEAK_BINS / (view * LANES);
    for (unsigned i = 0; i < PEAK_BINS / perBlock + 1; i++) e.scanPeaks(view, LANES01);
    CHECK(e.viewFrames() == view);
    CHECK(std::fabs(e.peak(0, 0) - 0.3f) < 1e-6f);
    CHECK(std::fabs(e.peak(1, PEAK_BINS / 2 - 1) - 0.6f) < 1e-6f);
    CHECK(e.peak(0, PEAK_BINS / 2 + 1) == 0.0f);
    CHECK(e.peak(1, PEAK_BINS - 1) == 0.0f);

    // a shorter view rescales: the loud region now fills every bin
    for (unsigned i = 0; i < PEAK_BINS; i++) e.scanPeaks(second / 2, LANES01);
    CHECK(e.viewFrames() == second / 2);
    CHECK(std::fabs(e.peak(1, PEAK_BINS - 1) - 0.6f) < 1e-6f);

    // the view is clamped to the buffer
    e.scanPeaks(BUFFER_FRAMES * 2u, LANES01);
    CHECK(e.viewFrames() == BUFFER_FRAMES);
}

static void processBlocks(Engine& e, unsigned blocks) {
    std::vector<float> ch[2] = { std::vector<float>(BLOCK), std::vector<float>(BLOCK) };
    float* io[2] = { ch[0].data(), ch[1].data() };
    float* vo[VOICES] = {};
    for (unsigned i = 0; i < blocks; i++) e.process(io, io, vo, BLOCK);
}

static void testSnapshot() {
    Engine e;
    e.beginLoad();
    float* d1 = e.staging(1);
    for (unsigned i = 0; i < BUFFER_FRAMES; i++) d1[i] = float(i % 1000) * 1e-3f;
    e.commitLoad(0b10);

    unsigned frames = 3 * SNAPSHOT_BUDGET + 17;  // takes four blocks
    CHECK(e.requestSnapshot(0b10, frames));
    CHECK(!e.requestSnapshot(0b01, 100));  // one at a time
    processBlocks(e, 3);
    CHECK(!e.snapshotReady());
    processBlocks(e, 1);
    CHECK(e.snapshotReady());
    CHECK(e.snapshotFrames() == frames);
    bool same = true;
    for (unsigned i = 0; i < frames; i++) same = same && e.snapshot(1)[i] == e.buffer(1)[i];
    CHECK(same);
    CHECK(e.snapshot(1)[999] == 999.0f * 1e-3f);  // the loaded data, not zeros
    e.releaseSnapshot();

    // cancel mid-copy, then a new snapshot starts cleanly
    CHECK(e.requestSnapshot(0b11, BUFFER_FRAMES));
    processBlocks(e, 2);
    e.releaseSnapshot();
    processBlocks(e, 2);
    CHECK(!e.snapshotReady());
    CHECK(e.requestSnapshot(0b01, 10));
    processBlocks(e, 1);
    CHECK(e.snapshotReady());
    e.releaseSnapshot();
}

static double absSum(const std::vector<float>& b) {
    double s = 0;
    for (float x : b) s += std::fabs(x);
    return s;
}

// voice 2 (track 2, L) records the left input: into buffer 0 when shared, its own buffer per track
static void testModes() {
    Rig r;
    CHECK(r.e.mode() == SHARED);
    CHECK(r.e.buffer(2).empty());  // per-track buffers are not allocated until needed
    auto& v = r.p[2];
    v.play = v.rec = true;
    v.inputGain = 1.0f;
    v.preLevel = 0.0f;
    v.loopEnd = 0.1f;
    v.fadeTime = 0.005f;
    r.run(0.2f, 0.5f, 0.0f);
    CHECK(r.e.buffer(0)[2400] > 0.58f);

    r.e.requestMode(PER_TRACK);
    CHECK(r.e.mode() == SHARED);  // not before the next process()
    stageOne(r.e, 0, 0.0f);       // clear buffer 0 in the same swap
    r.run(0.2f, 0.25f, 0.0f);
    CHECK(r.e.mode() == PER_TRACK);
    CHECK(r.e.buffer(7).size() == BUFFER_FRAMES);
    CHECK(r.e.buffer(2)[2400] > 0.29f && r.e.buffer(2)[2400] < 0.31f);
    CHECK(absSum(r.e.buffer(0)) == 0.0);
    CHECK(absSum(r.e.buffer(3)) == 0.0);  // track 2's R buffer: voice 3 is off

    // back to shared: buffer 2 keeps its take, voice 2 records into buffer 0 again
    r.e.requestMode(SHARED);
    r.run(0.2f, 0.5f, 0.0f);
    CHECK(r.e.mode() == SHARED);
    CHECK(r.e.buffer(2)[2400] > 0.29f && r.e.buffer(2)[2400] < 0.31f);
    CHECK(r.e.buffer(0)[2400] > 0.58f);
}

// a snapshot requested straight after a mode switch copies the newly allocated buffer:
// process() swaps before it copies
static void testSnapshotOfPendingBuffer() {
    Engine e;
    e.requestMode(PER_TRACK);
    CHECK(e.buffer(4).empty());
    CHECK(e.requestSnapshot(1u << 4, 1000));
    processBlocks(e, 1);
    CHECK(e.snapshotReady());
    CHECK(e.snapshot(4)[999] == 0.0f);
    e.releaseSnapshot();
}

// loading into a per-track buffer before the mode switch allocates it
static void testLoadAllocates() {
    Rig r;
    stageOne(r.e, 5, 0.4f);
    r.run(0.01f);
    CHECK(r.e.buffer(5).size() == BUFFER_FRAMES && r.e.buffer(5)[100] == 0.4f);
    r.e.requestMode(PER_TRACK);  // must not zero buffer 5
    r.run(0.01f);
    CHECK(r.e.buffer(5)[100] == 0.4f);
    CHECK(r.e.buffer(4).size() == BUFFER_FRAMES);
}

// voice 0 plays a constant from buffer 0; voice 1 hears only feedback and records it into buffer 1
static void testFeedback() {
    Rig r;
    stage(r.e, 0.25f, 0.0f);
    auto& src = r.p[0];
    src.play = true;
    src.level = 0.0f;  // feedback is taken before level
    src.loopEnd = 0.1f;
    src.fadeTime = 0.005f;
    auto& dst = r.p[1];
    dst.rec = true;
    dst.play = false;
    dst.inputGain = 0.0f;
    dst.preLevel = 0.0f;
    dst.loopEnd = 0.1f;
    dst.fadeTime = 0.005f;

    float fb[VOICES][VOICES] = {};
    r.e.setFeedback(fb);
    r.run(0.2f);
    CHECK(absSum(r.e.buffer(1)) == 0.0);  // no route, nothing recorded

    fb[0][1] = 0.5f;
    r.e.setFeedback(fb);
    r.run(0.3f);
    // 0.25 * 0.5, then the record path's 1.2 soft-clip gain
    CHECK(std::fabs(r.e.buffer(1)[2400] - 0.15f) < 0.01f);

    // a disabled source feeds nothing, after one chunk of latency
    src.on = false;
    r.run(0.01f);
    stageOne(r.e, 1, 0.0f);
    r.run(0.3f);
    // filter state decays through denormals rather than to exact zero
    double peak = 0;
    for (float x : r.e.buffer(1)) peak = std::max(peak, double(std::fabs(x)));
    CHECK(peak < 1e-20);
}

static void testCv() {
    VoiceParams p;
    p.rate = -0.5f;
    p.loopStart = 2.0f;
    p.loopEnd = 6.0f;
    float maxT = 40.0f;
    auto q = withCv(p, CV_PER_VOLT, 0.0f, false, maxT);  // +1 V: an octave up, sign kept
    CHECK(std::fabs(q.rate - -1.0f) < 1e-6f);
    q = withCv(p, -2 * CV_PER_VOLT, 0.0f, false, maxT);
    CHECK(std::fabs(q.rate - -0.125f) < 1e-6f);
    q = withCv(p, 0.0f, 3 * CV_PER_VOLT, false, maxT);  // +3 V: window shifted 3 s
    CHECK(std::fabs(q.loopStart - 5.0f) < 1e-5f && std::fabs(q.loopEnd - 9.0f) < 1e-5f);
    q = withCv(p, 0.0f, -1.0f, false, maxT);  // -5 V: stops at 0, length kept
    CHECK(q.loopStart == 0.0f && q.loopEnd == 4.0f);
    q = withCv(p, 0.0f, 1.0f, false, 8.0f);  // +5 V: stops at maxT
    CHECK(q.loopStart == 4.0f && q.loopEnd == 8.0f);
    CHECK(!withCv(p, 0, 0, false, maxT).rec && withCv(p, 0, 0, true, maxT).rec);
}

static void testFadeZones() {
    FadeZone z[2];
    // forward: fade out past the end, fade in from the start
    CHECK(fadeZones(1.0f, 3.0f, 0.5f, true, 0.1f, z) == 2);
    CHECK(z[0].origin == 3.0f && z[0].dir == 1.0f && !z[0].fadingIn);
    CHECK(z[1].origin == 1.0f && z[1].fadingIn);
    CHECK(std::fabs(z[0].time(1.0f, 0.1f) - 3.1f) < 1e-6f && z[0].fade(1.0f) == 0.0f);
    CHECK(z[1].time(0.0f, 0.1f) == 1.0f && z[1].fade(0.0f) == 0.0f);
    // reverse, and rate 0 counts as reverse; swapped loop points are ordered
    for (float rate : { -2.0f, 0.0f }) {
        CHECK(fadeZones(3.0f, 1.0f, rate, true, 0.1f, z) == 2);
        CHECK(z[0].origin == 1.0f && z[0].dir == -1.0f && !z[0].fadingIn);
        CHECK(z[1].origin == 3.0f && z[1].fadingIn);
    }
    // no loop: the head only fades out; no fade time: nothing to draw
    CHECK(fadeZones(1.0f, 3.0f, 1.0f, false, 0.1f, z) == 1);
    CHECK(fadeZones(1.0f, 3.0f, 1.0f, true, 0.0f, z) == 0);
    CHECK(fadeGain(0.0f) == 0.0f && std::fabs(fadeGain(1.0f) - 1.0f) < 1e-6f);
}

static void testReclaimStaging() {
    Rig r;
    stage(r.e, 0.1f, 0.2f);
    CHECK(!r.e.reclaimStaging());  // not swapped in yet
    CHECK(r.e.stagingFrames() == 2u * BUFFER_FRAMES);
    r.run(0.01f);
    CHECK(r.e.stagingFrames() == 2u * BUFFER_FRAMES);  // the swapped-out buffers
    CHECK(r.e.reclaimStaging());
    CHECK(r.e.stagingFrames() == 0);
    CHECK(r.e.buffer(0)[10] == 0.1f && r.e.buffer(1)[10] == 0.2f);  // live buffers untouched
    stage(r.e, 0.3f, 0.3f);  // and loading still works
    r.run(0.01f);
    CHECK(r.e.buffer(1)[10] == 0.3f);
}

static void testClear() {
    Engine e;
    stage(e, 0.5f, 0.5f);
    processBlocks(e, 1);
    unsigned start = 1000, end = start + 3 * CLEAR_BUDGET, fade = 100;
    CHECK(e.requestClear(0b01, start, end, fade));
    CHECK(!e.requestClear(0b10, 0, 10, 1));  // one at a time
    processBlocks(e, 2);
    CHECK(e.clearing());
    CHECK(e.buffer(0)[end - 1] == 0.5f);  // not reached yet
    processBlocks(e, 1);
    CHECK(!e.clearing());
    const auto& b = e.buffer(0);
    CHECK(b[start - 1] == 0.5f && b[end] == 0.5f);  // outside the region
    CHECK(b[start] == 0.5f && b[end - 1] == 0.5f);  // edges keep full level
    CHECK(std::fabs(b[start + fade / 2] - 0.25f) < 1e-6f && std::fabs(b[end - 1 - fade / 2] - 0.25f) < 1e-6f);
    bool silent = true;
    for (unsigned i = start + fade; i < end - fade; i++) silent = silent && b[i] == 0.0f;
    CHECK(silent);
    CHECK(e.buffer(1)[start + 2000] == 0.5f);  // not in the mask
}

// rec once records exactly one pass, then stops recording by itself
static void testRecOnce() {
    Rig r;
    auto& v = r.p[0];
    v.play = true;
    v.inputGain = 1.0f;
    v.preLevel = 0.0f;
    v.loopEnd = 0.1f;
    v.fadeTime = 0.005f;
    r.run(0.05f, 0.5f, 0.0f);
    CHECK(absSum(r.e.buffer(0)) == 0.0);  // playing, not recording
    r.e.recOnce(0);
    r.run(0.35f, 0.5f, 0.0f);
    CHECK(!r.e.voice(0).getSavedRecFlag());
    CHECK(std::fabs(r.e.buffer(0)[2400] - 0.6f) < 0.01f);
    r.run(0.3f, 0.25f, 0.0f);  // new input is not recorded
    CHECK(std::fabs(r.e.buffer(0)[2400] - 0.6f) < 0.01f);
}

static void testFilterMixes() {
    Rig r;
    stage(r.e, 0.25f, 0.0f);
    auto& v = r.p[0];
    v.play = true;
    v.level = 1.0f;
    v.loopEnd = 0.1f;
    v.fadeTime = 0.005f;
    v.postDry = 0.0f;  // every output mix at 0: silence
    auto m = r.run(0.3f);
    CHECK(std::fabs(m[2]) < 1e-6);
    v.postLp = 1.0f;  // a lowpass passes DC
    m = r.run(0.3f);
    CHECK(std::fabs(m[2] - 0.25) < 0.01);

    // input filter: a highpass blocks a DC input, so the voice records silence
    auto& w = r.p[1];
    w.rec = true;
    w.play = false;
    w.inputGain = 1.0f;
    w.preLevel = 0.0f;
    w.loopEnd = 0.1f;
    w.fadeTime = 0.005f;
    w.preLp = 0.0f;
    w.preHp = 1.0f;
    r.run(0.5f, 0.0f, 0.5f);
    CHECK(std::fabs(r.e.buffer(1)[2400]) < 1e-3f);
    w.preHp = 0.0f;
    w.preDry = 1.0f;  // dry only: recorded as is, with the 1.2 soft-clip gain
    r.run(0.3f, 0.0f, 0.5f);
    CHECK(std::fabs(r.e.buffer(1)[2400] - 0.6f) < 0.01f);
}

static void testPhaseQuant() {
    Rig r;
    auto& v = r.p[0];
    v.play = true;
    v.loopEnd = 2.0f;
    v.phaseQuant = 0.25f;
    v.phaseOffset = 0.1f;
    r.run(0.6f);  // position 0.6 s, plus 0.1 s offset: 0.7 s, floored to 0.5
    double q = r.e.voice(0).getQuantPhase();
    CHECK(std::fabs(q - 0.5) < 1e-9);
}

static void testTempo() {
    ClockTempo c;
    const unsigned window = ClockTempo::BEATS * ClockTempo::PPQN;
    double t = 10.0, spb = 0.5;  // 120 bpm
    for (unsigned i = 0; i < window; i++) c.tick(t += spb / ClockTempo::PPQN);
    CHECK(c.secondsPerBeat() == 0.0);  // not a full window yet
    c.tick(t += spb / ClockTempo::PPQN);
    CHECK(std::fabs(c.secondsPerBeat() - 0.5) < 1e-9 && std::fabs(c.bpm() - 120.0) < 1e-6);

    // +-1 ms of jitter per tick moves the estimate by less than the hysteresis, so it holds
    unsigned seed = 7;
    const double held = c.secondsPerBeat();
    for (unsigned i = 0; i < 200; i++) {
        seed = seed * 1664525u + 1013904223u;
        double jitter = (double(seed >> 8) / double(1u << 24) - 0.5) * 0.002;
        c.tick(10.0 + 0.5 * (window + 2 + i) / ClockTempo::PPQN + jitter);
        CHECK(c.secondsPerBeat() == held);
    }

    // a new tempo is taken after a full window of its ticks
    t = 10.0 + 0.5 * (window + 202) / ClockTempo::PPQN;
    for (unsigned i = 0; i <= window; i++) c.tick(t += 0.6 / ClockTempo::PPQN);
    CHECK(std::fabs(c.secondsPerBeat() - 0.6) < 1e-9);

    // a gap restarts the measurement but keeps the last tempo meanwhile
    c.tick(t += 2.0);
    CHECK(std::fabs(c.secondsPerBeat() - 0.6) < 1e-9);
    for (unsigned i = 0; i < window; i++) c.tick(t += 0.4 / ClockTempo::PPQN);
    CHECK(std::fabs(c.secondsPerBeat() - 0.4) < 1e-9);
}

static void testClearAll() {
    Rig r;
    stage(r.e, 0.5f, 0.5f);
    r.e.requestMode(PER_TRACK);
    stageOne(r.e, 6, 0.25f);
    r.run(0.01f);
    CHECK(r.e.mode() == PER_TRACK && r.e.buffer(6)[10] == 0.25f);

    r.e.requestClearAll();
    CHECK(r.e.buffer(0)[10] == 0.5f);  // not before the next process()
    r.run(0.01f);
    CHECK(r.e.mode() == SHARED);
    CHECK(absSum(r.e.buffer(0)) == 0.0 && absSum(r.e.buffer(1)) == 0.0);
    for (unsigned b = SHARED_BUFFERS; b < BUFFERS; b++) CHECK(r.e.buffer(b).empty());
    CHECK(r.e.reclaimStaging());  // the old buffers wait in staging
    CHECK(r.e.stagingFrames() == 0);

    // 4 loop again allocates fresh, silent buffers
    r.e.requestMode(PER_TRACK);
    r.run(0.01f);
    CHECK(r.e.buffer(6).size() == BUFFER_FRAMES && absSum(r.e.buffer(6)) == 0.0);
}

// voice 1 (an R voice) records its own side by default, else the chosen input
static void testInputSrc() {
    struct Case {
        int input;
        float expect;  // recorded level for In L 0.4, In R 0.2, after the 1.2 soft-clip gain
    } cases[] = { { IN_OWN, 0.24f }, { IN_L, 0.48f }, { IN_R, 0.24f }, { IN_MIX, 0.36f } };
    for (auto& c : cases) {
        Rig r;
        auto& v = r.p[1];
        v.rec = true;
        v.play = false;
        v.inputGain = 1.0f;
        v.preLevel = 0.0f;
        v.loopEnd = 0.1f;
        v.fadeTime = 0.005f;
        v.input = c.input;
        r.run(0.3f, 0.4f, 0.2f);
        CHECK(std::fabs(r.e.buffer(1)[2400] - c.expect) < 0.005f);
    }
}

static void testLoopBoundsSanitised() {
    Rig r;
    auto& v = r.p[0];
    v.play = true;
    v.loopStart = 1000.0f;  // past the buffer, and after loopEnd
    v.loopEnd = 2.0f;
    auto m = r.run(0.2f);
    CHECK(allFinite(m));
    float pos = r.e.voice(0).getSavedPosition();
    CHECK(pos >= 0.0f && pos <= r.e.bufferSeconds());
}

static double realtimeFactor() {
    Rig r;
    for (unsigned i = 0; i < VOICES; i++) {
        auto& v = r.p[i];
        v.play = v.rec = true;
        v.inputGain = 1.0f;
        v.rate = i % 2 ? -0.5f : 1.0f;
        v.postLp = 0.5f;
    }
    float seconds = 10.0f;
    auto t0 = std::chrono::steady_clock::now();
    r.run(seconds, 0.1f, 0.1f);
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return seconds / dt;
}

int main() {
    testSilence();
    testRecordThenPlay();
    testSecondChannelToSecondBuffer();
    testOutputsAliasInputs();
    testLoadReplacesBuffers();
    testUnswappedLoadIsReclaimed();
    testPartialLoad();
    testReclaimKeepsEarlierBuffers();
    testDisabledVoice();
    testLinkedPairLocked();
    testPeakScan();
    testSnapshot();
    testModes();
    testLoadAllocates();
    testSnapshotOfPendingBuffer();
    testFeedback();
    testCv();
    testFadeZones();
    testReclaimStaging();
    testClear();
    testRecOnce();
    testFilterMixes();
    testPhaseQuant();
    testTempo();
    testClearAll();
    testInputSrc();
    testLoopBoundsSanitised();
    if (failures) return 1;
    std::printf("realtime factor (%u voices, rec+play): %.1fx\n", VOICES, realtimeFactor());
    return 0;
}
