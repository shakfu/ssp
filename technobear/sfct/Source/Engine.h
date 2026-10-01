#pragma once

// Multi-voice softcut host, free of JUCE so it can be tested on the build machine.
// Eight mono voices form four tracks of L/R pairs (voices 2k, 2k+1). Voice v records input
// channel v % 2. In SHARED mode (norns) every voice uses buffer v % 2; in PER_TRACK mode each
// voice has its own buffer v, so each track is an independent stereo loop.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "softcut/Voice.h"

namespace sfct {

static constexpr unsigned VOICES = 8;
static constexpr unsigned TRACKS = VOICES / 2;
static constexpr unsigned INPUTS = 2;
static constexpr unsigned BUFFERS = VOICES;     // PER_TRACK uses all; SHARED uses the first two
static constexpr unsigned SHARED_BUFFERS = 2;
static constexpr unsigned LANES = 2;            // waveform display: one track's L and R buffers
// softcut requires a power of two; 2^21 frames is 43.7 s at 48 kHz, 8 MB per buffer
static constexpr unsigned BUFFER_FRAMES = 1u << 21;
static constexpr unsigned CHUNK = 64;
// waveform display: one bin per pixel; frames scanned per block
static constexpr unsigned PEAK_BINS = 580;
// frames per buffer copied out per block while saving: 256 KB, a full buffer in 64 blocks
static constexpr unsigned SNAPSHOT_BUDGET = 32768;
static constexpr unsigned SCAN_BUDGET = 4096;  // a 4 s view refreshes in 0.25 s at 128-frame blocks

enum Mode : int { SHARED = 0, PER_TRACK = 1 };

inline unsigned bufferFor(Mode m, unsigned v) {
    return m == SHARED ? v % SHARED_BUFFERS : v;
}

struct VoiceParams {
    bool on = true;  // off: not processed, silent, records nothing
    bool play = true;
    bool rec = false;
    bool loop = true;
    float rate = 1.0f;
    float loopStart = 0.0f;
    float loopEnd = 4.0f;
    float fadeTime = 0.05f;
    float rateSlew = 0.1f;
    float recLevel = 1.0f;
    float preLevel = 0.5f;
    float level = 0.8f;
    float pan = 0.0f;
    float inputGain = 0.0f;
    float postFc = 8000.0f;
    float postLp = 0.0f;  // lowpass mix; dry is 1 - postLp
};

// Voices 2k and 2k+1 form a stereo pair, L and R.
inline unsigned partnerOf(unsigned v) {
    return v ^ 1u;
}

// The follower of a linked pair: the leader's settings with pan mirrored.
inline VoiceParams linkedTo(const VoiceParams& leader) {
    VoiceParams p = leader;
    p.pan = -leader.pan;
    return p;
}

class Engine {
public:
    // Only the shared buffers are allocated up front; PER_TRACK allocates the rest on first use.
    Engine() : buffers_(BUFFERS) {
        for (unsigned b = 0; b < SHARED_BUFFERS; b++) {
            buffers_[b].assign(BUFFER_FRAMES, 0.0f);
            allocated_[b] = true;
        }
        for (unsigned v = 0; v < VOICES; v++) voices_[v].setBuffer(buffers_[bufferFor(SHARED, v)].data(), BUFFER_FRAMES);
        setSampleRate(48000.0f);
    }

    void setSampleRate(float sr) {
        sampleRate_ = sr;
        for (unsigned v = 0; v < VOICES; v++) {
            voices_[v].setSampleRate(sr);
            applied_[v] = false;
        }
    }

    float bufferSeconds() const { return float(BUFFER_FRAMES) / sampleRate_; }

    // Applies only the fields that differ from the last call; softcut setters restart ramps.
    void set(unsigned vi, const VoiceParams& p) {
        auto& v = voices_[vi];
        auto& o = params_[vi];
        bool all = !applied_[vi];
        float maxT = bufferSeconds();
        float start = std::clamp(std::min(p.loopStart, p.loopEnd), 0.0f, maxT);
        float end = std::clamp(std::max(p.loopStart, p.loopEnd), 0.0f, maxT);

        if (all || p.rate != o.rate) v.setRate(p.rate);
        if (all || p.loopStart != o.loopStart || p.loopEnd != o.loopEnd) {
            v.setLoopStart(start);
            v.setLoopEnd(end);
        }
        if (all || p.loop != o.loop) v.setLoopFlag(p.loop);
        if (all || p.fadeTime != o.fadeTime) v.setFadeTime(p.fadeTime);
        if (all || p.rateSlew != o.rateSlew) v.setRateSlewTime(p.rateSlew);
        if (all || p.recLevel != o.recLevel) v.setRecLevel(p.recLevel);
        if (all || p.preLevel != o.preLevel) v.setPreLevel(p.preLevel);
        if (all || p.postFc != o.postFc) v.setPostFilterFc(p.postFc);
        if (all || p.postLp != o.postLp) {
            v.setPostFilterLp(p.postLp);
            v.setPostFilterDry(1.0f - p.postLp);
        }
        bool wasRunning = !all && (o.play || o.rec);
        if (all || p.play != o.play) v.setPlayFlag(p.play);
        if (all || p.rec != o.rec) v.setRecFlag(p.rec);
        // a subhead stays silent and ignores loop points until a cut activates it
        if ((p.play || p.rec) && !wasRunning) v.cutToPos(start);
        o = p;
        applied_[vi] = true;
    }

    void cut(unsigned vi, float sec) { voices_[vi].cutToPos(sec); }

    // Buffer replacement, for one control thread. Fill staging(b) for the buffers in `mask` between
    // beginLoad() and commitLoad(mask); the next process() swaps them in at once. Swapping into an
    // unallocated buffer allocates it. Staging is allocated on first use; after a swap it holds the
    // old contents.
    void beginLoad() {
        for (;;) {
            int s = loadState_.load(std::memory_order_acquire);
            // a committed load not yet swapped in is reclaimed, and the buffers it staged stay in the mask
            if ((s == L_IDLE || s == L_READY) && loadState_.compare_exchange_weak(s, L_WRITING)) break;
            std::this_thread::yield();  // L_SWAPPING lasts a few pointer swaps
        }
    }
    float* staging(unsigned b) {
        staging_[b].resize(BUFFER_FRAMES);
        allocated_[b] = true;
        return staging_[b].data();
    }
    void commitLoad(unsigned mask) {
        loadMask_ |= mask;
        loadState_.store(L_READY, std::memory_order_release);
    }

    // Control thread. Takes effect with the next process(), after any buffers it needs are in place.
    void requestMode(Mode m) {
        beginLoad();
        unsigned mask = 0;
        if (m == PER_TRACK) {
            for (unsigned b = SHARED_BUFFERS; b < BUFFERS; b++) {
                if (allocated_[b]) continue;
                float* d = staging(b);
                std::fill(d, d + BUFFER_FRAMES, 0.0f);
                mask |= 1u << b;
            }
        }
        pendingMode_ = m;
        commitLoad(mask);
    }
    Mode mode() const { return Mode(mode_.load(std::memory_order_relaxed)); }

    // Snapshot for saving, for one control thread. The audio thread copies the first `frames` of the
    // buffers in `mask`, SNAPSHOT_BUDGET frames per block, so a voice recording meanwhile leaves
    // seams up to 64 blocks apart. Returns false while a snapshot is in progress.
    bool requestSnapshot(unsigned mask, unsigned frames) {
        if (snapState_.load(std::memory_order_acquire) != S_IDLE) return false;
        for (unsigned b = 0; b < BUFFERS; b++)
            if ((mask & (1u << b)) && !allocated_[b]) return false;
        snapFrames_ = std::min(frames, BUFFER_FRAMES);
        snapPos_ = 0;
        snapMask_ = mask;
        for (unsigned b = 0; b < BUFFERS; b++)
            if (mask & (1u << b)) snap_[b].resize(snapFrames_);
        snapState_.store(S_COPYING, std::memory_order_release);
        return true;
    }
    bool snapshotReady() const { return snapState_.load(std::memory_order_acquire) == S_DONE; }
    unsigned snapshotFrames() const { return snapFrames_; }
    const float* snapshot(unsigned b) const { return snap_[b].data(); }
    // Ends a finished snapshot, or cancels one in progress, and frees its memory.
    void releaseSnapshot() {
        for (;;) {
            int s = snapState_.load(std::memory_order_acquire);
            if (s == S_IDLE) break;
            if (s != S_BUSY && snapState_.compare_exchange_weak(s, S_IDLE)) break;
            std::this_thread::yield();  // S_BUSY lasts one slice
        }
        for (auto& v : snap_) std::vector<float>().swap(v);
    }

    // Audio thread, after process(): refreshes the next slice of peak bins over the first
    // viewFrames of buffers lanes[0] and lanes[1]. A changed view restarts the sweep at bin 0.
    void scanPeaks(unsigned viewFrames, const unsigned (&lanes)[LANES]) {
        unsigned len = std::clamp(viewFrames, PEAK_BINS, BUFFER_FRAMES);
        if (len != viewFrames_.load(std::memory_order_relaxed) || lanes[0] != scanLanes_[0] ||
            lanes[1] != scanLanes_[1]) {
            viewFrames_.store(len, std::memory_order_relaxed);
            scanLanes_[0] = lanes[0];
            scanLanes_[1] = lanes[1];
            scanBin_ = 0;
        }
        unsigned bins = std::max(1u, SCAN_BUDGET * PEAK_BINS / (len * LANES));
        for (unsigned n = 0; n < bins; n++) {
            unsigned b0 = scanBin_ * len / PEAK_BINS, b1 = (scanBin_ + 1) * len / PEAK_BINS;
            for (unsigned l = 0; l < LANES; l++) {
                const auto& buf = buffers_[lanes[l]];
                float pk = buf.empty() ? 0.0f : absPeak(buf.data() + b0, b1 - b0);
                peaks_[l][scanBin_].store(pk, std::memory_order_relaxed);
            }
            scanBin_ = (scanBin_ + 1) % PEAK_BINS;
        }
    }
    // any thread
    float peak(unsigned lane, unsigned bin) const { return peaks_[lane][bin].load(std::memory_order_relaxed); }
    unsigned viewFrames() const { return viewFrames_.load(std::memory_order_relaxed); }

    const VoiceParams& params(unsigned vi) const { return params_[vi]; }
    softcut::Voice& voice(unsigned vi) { return voices_[vi]; }
    const std::vector<float>& buffer(unsigned b) const { return buffers_[b]; }

    // in: INPUTS channels; mix: 2 channels (L, R); voiceOut: VOICES channels, post level, pre pan.
    // voiceOut entries may be null. Outputs may alias inputs, as JUCE's in-place buffers do.
    void process(const float* const* in, float* const* mix, float* const* voiceOut, unsigned n) {
        int ready = L_READY;
        if (loadState_.compare_exchange_strong(ready, L_SWAPPING, std::memory_order_acquire)) {
            for (unsigned b = 0; b < BUFFERS; b++)
                if (loadMask_ & (1u << b)) buffers_[b].swap(staging_[b]);
            loadMask_ = 0;
            if (pendingMode_ >= 0) {
                mode_.store(pendingMode_, std::memory_order_relaxed);
                pendingMode_ = -1;
            }
            for (unsigned v = 0; v < VOICES; v++)
                voices_[v].setBuffer(buffers_[bufferFor(mode(), v)].data(), BUFFER_FRAMES);
            loadState_.store(L_IDLE, std::memory_order_release);
        }
        for (unsigned off = 0; off < n; off += CHUNK) {
            unsigned len = std::min(CHUNK, n - off);
            for (unsigned c = 0; c < INPUTS; c++)
                for (unsigned i = 0; i < len; i++) chunkIn_[c][i] = in[c][off + i];
            for (unsigned i = 0; i < len; i++) mix[0][off + i] = mix[1][off + i] = 0.0f;
            for (unsigned vi = 0; vi < VOICES; vi++) {
                const auto& p = params_[vi];
                float* vo = voiceOut[vi] ? voiceOut[vi] + off : nullptr;
                if (!p.on) {
                    if (vo) std::fill(vo, vo + len, 0.0f);
                    continue;
                }
                const float* src = chunkIn_[vi % INPUTS];
                for (unsigned i = 0; i < len; i++) scratchIn_[i] = src[i] * p.inputGain;
                voices_[vi].processBlockMono(scratchIn_, scratchOut_, int(len));

                // equal-power pan: -1..1 -> 0..pi/2
                float theta = (p.pan * 0.5f + 0.5f) * float(M_PI_2);
                float gl = p.level * std::cos(theta);
                float gr = p.level * std::sin(theta);
                for (unsigned i = 0; i < len; i++) {
                    float y = scratchOut_[i];
                    mix[0][off + i] += y * gl;
                    mix[1][off + i] += y * gr;
                    if (vo) vo[i] = y * p.level;
                }
            }
        }

        int copying = S_COPYING;
        if (snapState_.compare_exchange_strong(copying, S_BUSY, std::memory_order_acquire)) {
            unsigned end = std::min(snapPos_ + SNAPSHOT_BUDGET, snapFrames_);
            for (unsigned b = 0; b < BUFFERS; b++) {
                if (!(snapMask_ & (1u << b))) continue;
                // staged but not yet swapped in: nothing recorded there yet
                if (buffers_[b].empty()) std::fill(snap_[b].begin() + snapPos_, snap_[b].begin() + end, 0.0f);
                else std::copy(buffers_[b].begin() + snapPos_, buffers_[b].begin() + end, snap_[b].begin() + snapPos_);
            }
            snapPos_ = end;
            snapState_.store(end == snapFrames_ ? S_DONE : S_COPYING, std::memory_order_release);
        }
    }

private:
    // Non-negative float bit patterns order like their values, so an integer max over the
    // sign-cleared bits is the float peak. Unlike a float max, it vectorises.
    static float absPeak(const float* x, unsigned n) {
        uint32_t m = 0;
        for (unsigned i = 0; i < n; i++) {
            uint32_t u;
            std::memcpy(&u, x + i, sizeof u);
            u &= 0x7fffffffu;
            m = u > m ? u : m;
        }
        float pk;
        std::memcpy(&pk, &m, sizeof pk);
        return pk;
    }

    enum { L_IDLE, L_WRITING, L_READY, L_SWAPPING };
    enum { S_IDLE, S_COPYING, S_BUSY, S_DONE };

    std::vector<std::vector<float>> buffers_;
    std::vector<float> staging_[BUFFERS];
    bool allocated_[BUFFERS] = {};  // control thread: allocated, or staged to be
    std::atomic<int> loadState_{ L_IDLE };
    // written by the holder of L_WRITING, read under L_SWAPPING
    unsigned loadMask_ = 0;
    int pendingMode_ = -1;
    std::atomic<int> mode_{ SHARED };

    std::atomic<float> peaks_[LANES][PEAK_BINS] = {};
    std::atomic<unsigned> viewFrames_{ PEAK_BINS };
    unsigned scanLanes_[LANES] = { 0, 1 };
    unsigned scanBin_ = 0;

    std::vector<float> snap_[BUFFERS];
    std::atomic<int> snapState_{ S_IDLE };
    unsigned snapMask_ = 0, snapFrames_ = 0, snapPos_ = 0;  // set while S_IDLE, then audio-thread owned

    // fixQuirks: upstream records polarity-inverted, which cancels against the dry signal on SSP patches
    softcut::Voice voices_[VOICES] = { softcut::Voice(true), softcut::Voice(true), softcut::Voice(true),
                                       softcut::Voice(true), softcut::Voice(true), softcut::Voice(true),
                                       softcut::Voice(true), softcut::Voice(true) };
    VoiceParams params_[VOICES];
    bool applied_[VOICES] = {};
    float sampleRate_ = 48000.0f;
    float chunkIn_[INPUTS][CHUNK];
    float scratchIn_[CHUNK];
    float scratchOut_[CHUNK];
};

}  // namespace sfct
