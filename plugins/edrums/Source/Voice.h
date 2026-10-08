#pragma once

#include <cstdint>

#include "engine/Dsp.h"

namespace edrums {

enum class Model { Kick, Snare, Clap, Hat, Tom, Count };
static constexpr int MODELS = int(Model::Count);

// One synthesized drum: a pitched sine body with a downward pitch sweep, a second partial, and
// band- or high-passed noise, each with its own exponential decay. From sk-engines edrums (MIT,
// Synthux Academy); the DSP is unchanged.
class Voice {
public:
    void init(float sampleRate, Model model, uint32_t seed);
    void setModel(Model m);
    void setPitch(float norm);  // 0..1: body frequency and noise colour
    void setDecay(float norm);  // 0..1: body and noise decay
    // 0..1, 0.5 = the model as voiced
    void setDrive(float v) { driveMacro_ = clamp01(v); }
    void setSweep(float v) { sweepMacro_ = clamp01(v); }
    void setTone(float v) { toneMacro_ = clamp01(v); }  // body <-> noise balance
    void setBright(float v);                             // noise filter cutoff, +/-2 octaves

    void trigger();
    float process();

private:
    // 128-point linearly interpolated sine table (sk-engines LUTSinOsc)
    struct SinOsc {
        float k = 0, phase = 0, inc = 0;
        void init(float sr);
        void setFreq(float hz) { inc = hz * k; }
        void reset() { phase = 0; }
        float process();
    };

    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    void setNoiseFilter();
    void recomputePitch();
    void recomputeDecay();

    SinOsc osc_, osc2_;
    ssp::engine::Biquad noiseFilt_;
    float sr_ = 48000.0f;
    float baseHz_ = 60.0f, pitchNorm_ = 0.5f, decayNorm_ = 0.5f;
    // model character
    float tone_ = 0, sweep_ = 3, baseScale_ = 1, decayScale_ = 1, noiseDecay_ = 1, noiseMult_ = 8, noiseQ_ = 1.2f;
    bool noiseHp_ = false;
    float partialRatio_ = 0, partialMix_ = 0, drive_ = 0, clickLevel_ = 0;
    float driveMacro_ = 0.5f, sweepMacro_ = 0.5f, toneMacro_ = 0.5f, brightMacro_ = 0.5f;
    // envelopes
    float amp_ = 0, ampCoef_ = 0, namp_ = 0, nampCoef_ = 0, pitchEnv_ = 0, pitchCoef_ = 0, clickAmp_ = 0, clickCoef_ = 0;
    uint32_t rng_ = 0x1234567u;
    // the clap's extra bursts
    int burstCount_ = 0, burstLeft_ = 0;
    float burstGap_ = 0, burstTimer_ = 0;
};

}  // namespace edrums
