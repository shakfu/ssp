// SPDX-License-Identifier: GPL-3.0-only
//
// GPLv3 as a combined work with GlitchVoice.h; see ../NOTICE.md.
#include "GlitchEngine.h"

#include "engine/Dsp.h"

#include <algorithm>
#include <cmath>

namespace glitch {

static constexpr float CENTRE = 0.70710678f;
static constexpr float OCTAVES = 4.0f;  // the pitch range across 0..1

void GlitchEngine::prepare(float sampleRate, int) {
    static constexpr uint32_t seed[DECKS] = { 0x12345678u, 0x2545F491u };  // decorrelates the decks
    for (int d = 0; d < DECKS; d++) {
        dk_[d].voice.init(sampleRate, seed[d]);
        dk_[d].algo = -1;  // re-apply everything on the next block
        dk_[d].p1 = dk_[d].p2 = dk_[d].pitch = -1.0f;
    }
}

void GlitchEngine::setCrossfade(float x) {
    gA_ = x <= 0.5f ? 1.0f : 2.0f * (1.0f - x);
    gB_ = x >= 0.5f ? 1.0f : 2.0f * x;
}

void GlitchEngine::setRoute(Route r) {
    if (r == route_) return;
    route_ = r;
    if (r != Route::Random) return;
    for (auto& k : dk_) {
        rng_ = rng_ * 1664525u + 1013904223u;
        float p = float(rng_ >> 8) * (1.0f / 16777216.0f);
        k.panL = std::cos(p * 1.5707963f);
        k.panR = std::sin(p * 1.5707963f);
    }
}

// Pushes changed controls into the voice; each setter recomputes the algorithm's coefficients.
void GlitchEngine::apply(int d, const float* const* cv, int n) {
    auto& k = dk_[d];
    auto& c = ctl_[d];
    int algo = std::clamp(c.algo, 0, kAlgoCount - 1);
    if (algo != k.algo) {
        k.voice.set_algo(Algo(algo));
        k.algo = algo;
        shown_[d].store(algo, std::memory_order_relaxed);
    }
    float p1 = std::clamp(c.p1 + cv[I_P1][n - 1], 0.0f, 1.0f);
    float p2 = std::clamp(c.p2 + cv[I_P2][n - 1], 0.0f, 1.0f);
    float pitch = std::clamp(c.pitch + cv[I_PITCH][n - 1] / ssp::engine::CV_PER_VOLT / OCTAVES, 0.0f, 1.0f);
    if (p1 != k.p1) k.voice.set_p1(k.p1 = p1);
    if (p2 != k.p2) k.voice.set_p2(k.p2 = p2);
    if (pitch != k.pitch) k.voice.set_pitch(k.pitch = pitch);
    if (c.regen && !k.regenHigh) k.voice.regen();
    k.regenHigh = c.regen;
}

void GlitchEngine::process(const float* const* in, float* const* out, int n) {
    for (int d = 0; d < DECKS; d++) apply(d, in + d * I_PER_DECK, n);

    float pLa, pRa, pLb, pRb;
    switch (route_) {
        case Route::Split: pLa = 1.0f, pRa = 0.0f, pLb = 0.0f, pRb = 1.0f; break;
        case Route::Random: pLa = dk_[0].panL, pRa = dk_[0].panR, pLb = dk_[1].panL, pRb = dk_[1].panR; break;
        default: pLa = pRa = pLb = pRb = CENTRE;
    }
    auto& a = dk_[0];
    auto& b = dk_[1];
    float ta = ctl_[0].tone * ctl_[0].tone, tb = ctl_[1].tone * ctl_[1].tone;  // as sk-engines: 0 dark, 1 open
    float la = ctl_[0].level, lb = ctl_[1].level;
    for (int i = 0; i < n; i++) {
        a.lp += ta * (a.voice.process() - a.lp);
        b.lp += tb * (b.voice.process() - b.lp);
        float A = a.lp * la, B = b.lp * lb;
        out[O_A][i] = A;
        out[O_B][i] = B;
        out[O_L][i] = ssp::engine::softLimit(A * gA_ * pLa + B * gB_ * pLb);
        out[O_R][i] = ssp::engine::softLimit(A * gA_ * pRa + B * gB_ * pRb);
    }
}

}  // namespace glitch
