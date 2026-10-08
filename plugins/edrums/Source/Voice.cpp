#include "Voice.h"

#include "engine/Dsp.h"

#include <algorithm>
#include <cmath>

namespace edrums {

static constexpr float PI = 3.14159265358979f;

namespace {
struct ModelSpec {
    float tone, sweep, pitchMs, baseScale, decayScale, noiseDecay, noiseMult, noiseQ;
    bool noiseHp;
    float partialRatio, partialMix, drive, click;
    int bursts;
    float burstMs;
};
// clang-format off
const ModelSpec specs[MODELS] = {
    // tone  sweep p_ms  base   dscl  ndcy  nmult  nq    hp     prat  pmix  drive click  burst
    { 0.00f, 3.0f, 55.f, 0.50f, 1.30f, 1.0f,  8.f, 1.2f, false, 0.0f, 0.00f, 0.35f, 0.5f, 0,  0.f },  // kick
    { 0.50f, 0.8f, 18.f, 1.30f, 0.55f, 1.6f, 18.f, 0.8f, false, 1.6f, 0.50f, 0.10f, 0.0f, 0,  0.f },  // snare
    { 0.92f, 0.0f, 10.f, 1.00f, 0.40f, 1.5f,  9.f, 1.0f, false, 0.0f, 0.00f, 0.00f, 0.0f, 3, 11.f },  // clap
    { 1.00f, 0.0f,  5.f, 1.00f, 0.22f, 1.0f, 30.f, 0.9f, true,  0.0f, 0.00f, 0.00f, 0.0f, 0,  0.f },  // hat
    { 0.18f, 1.8f, 40.f, 0.90f, 1.00f, 0.8f,  6.f, 1.2f, false, 1.5f, 0.35f, 0.15f, 0.2f, 0,  0.f },  // tom
};
// clang-format on

constexpr int SIN_SIZE = 128;
struct SinTable {
    float v[SIN_SIZE];
    SinTable() {
        for (int i = 0; i < SIN_SIZE; i++) v[i] = std::sin(2.0f * PI * float(i) / float(SIN_SIZE));
    }
};
const SinTable sinTable;
}  // namespace

void Voice::SinOsc::init(float sr) {
    k = float(SIN_SIZE) / sr;
}

float Voice::SinOsc::process() {
    int i = int(phase);
    float frac = phase - float(i);
    int j = i + 1 < SIN_SIZE ? i + 1 : 0;
    float s = sinTable.v[i] + frac * (sinTable.v[j] - sinTable.v[i]);
    phase += inc;
    while (phase >= float(SIN_SIZE)) phase -= float(SIN_SIZE);
    return s;
}

void Voice::init(float sampleRate, Model model, uint32_t seed) {
    sr_ = sampleRate;
    osc_.init(sr_);
    osc2_.init(sr_);
    noiseFilt_ = ssp::engine::Biquad();
    rng_ = seed;
    setModel(model);
    amp_ = namp_ = pitchEnv_ = clickAmp_ = 0.0f;
    burstLeft_ = 0;
}

void Voice::setModel(Model model) {
    const ModelSpec& m = specs[std::clamp(int(model), 0, MODELS - 1)];
    tone_ = m.tone;
    sweep_ = m.sweep;
    baseScale_ = m.baseScale;
    decayScale_ = m.decayScale;
    noiseDecay_ = m.noiseDecay;
    noiseMult_ = m.noiseMult;
    noiseQ_ = m.noiseQ;
    noiseHp_ = m.noiseHp;
    partialRatio_ = m.partialRatio;
    partialMix_ = m.partialMix;
    drive_ = m.drive;
    clickLevel_ = m.click;
    burstCount_ = m.bursts;
    burstGap_ = m.burstMs * 0.001f * sr_;
    pitchCoef_ = std::exp(-1.0f / (std::max(1.0f, m.pitchMs) * 0.001f * sr_));
    clickCoef_ = std::exp(-1.0f / (0.002f * sr_));  // ~2 ms attack click
    recomputeDecay();
    recomputePitch();
}

void Voice::setNoiseFilter() {
    const float fc = baseHz_ * noiseMult_ * std::exp2((brightMacro_ - 0.5f) * 4.0f);
    noiseFilt_.set(noiseHp_ ? ssp::engine::Biquad::HighPass : ssp::engine::Biquad::BandPass, sr_,
                   std::clamp(fc, 300.0f, sr_ * 0.45f), noiseQ_);
}

void Voice::recomputePitch() {
    baseHz_ = 30.0f * std::exp2(pitchNorm_ * 4.0f) * baseScale_;  // ~30..480 Hz times the model's offset
    setNoiseFilter();
}

void Voice::recomputeDecay() {
    const float T = (0.03f + decayNorm_ * decayNorm_ * 1.2f) * decayScale_;  // body: 30 ms .. ~1.2 s
    ampCoef_ = std::exp(-1.0f / (T * sr_));
    const float Tn = std::max(0.005f, T * noiseDecay_);
    nampCoef_ = std::exp(-1.0f / (Tn * sr_));
}

void Voice::setPitch(float norm) {
    pitchNorm_ = clamp01(norm);
    recomputePitch();
}

void Voice::setDecay(float norm) {
    decayNorm_ = clamp01(norm);
    recomputeDecay();
}

void Voice::setBright(float v) {
    brightMacro_ = clamp01(v);
    setNoiseFilter();
}

void Voice::trigger() {
    amp_ = namp_ = pitchEnv_ = 1.0f;
    clickAmp_ = clickLevel_;
    osc_.reset();
    osc2_.reset();
    burstLeft_ = burstCount_;
    burstTimer_ = burstGap_;
}

float Voice::process() {
    if (burstLeft_ > 0) {
        burstTimer_ -= 1.0f;
        if (burstTimer_ <= 0.0f) {
            amp_ = namp_ = 1.0f;
            burstLeft_--;
            burstTimer_ += burstGap_;
        }
    }
    if (amp_ < 1.0e-4f && namp_ < 1.0e-4f && clickAmp_ < 1.0e-4f) {
        amp_ = namp_ = clickAmp_ = 0.0f;
        return 0.0f;
    }

    const float effSweep = sweep_ * std::exp2((sweepMacro_ - 0.5f) * 4.0f);
    const float effDrive = std::clamp(drive_ + (driveMacro_ - 0.5f) * 2.0f, 0.0f, 1.5f);

    pitchEnv_ *= pitchCoef_;
    const float f = baseHz_ * (1.0f + pitchEnv_ * effSweep);
    osc_.setFreq(f);
    float body = osc_.process();
    if (partialMix_ > 0.0f) {
        osc2_.setFreq(f * partialRatio_);
        body += osc2_.process() * partialMix_;
    }
    if (effDrive > 0.0f) body = ssp::engine::softLimit(body * (1.0f + effDrive * 4.0f));
    body *= amp_;

    rng_ = rng_ * 1664525u + 1013904223u;
    const float nRaw = float(rng_ >> 8) * (1.0f / 8388608.0f) - 1.0f;
    const float n = noiseFilt_.process(nRaw) * 2.0f * namp_;  // the filter attenuates: compensate

    float clk = 0.0f;
    if (clickAmp_ > 1.0e-4f) {
        clk = nRaw * clickAmp_;
        clickAmp_ *= clickCoef_;
    } else {
        clickAmp_ = 0.0f;
    }

    amp_ *= ampCoef_;
    namp_ *= nampCoef_;
    const float effTone = std::clamp(tone_ + (toneMacro_ - 0.5f), 0.0f, 1.0f);
    return body * (1.0f - effTone) + n * effTone + clk;
}

}  // namespace edrums
