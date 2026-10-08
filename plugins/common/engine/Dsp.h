#pragma once

#include <cmath>

// Small DSP pieces the engine plugins share. No JUCE, like Engine.h.

namespace ssp::engine {

// DaisySP's SoftLimit: unity near 0, approaching +/-1 smoothly up to |x| = 3.
inline float softLimit(float x) {
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// One biquad section in transposed direct form II, with infrasonic's coefficients (as sk-engines).
struct Biquad {
    enum Type { LowPass, HighPass, BandPass };

    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;

    void set(Type type, float sr, float fc, float q) {
        const float K = std::tan(3.14159265358979f * fc / sr), Ksq = K * K;
        const float norm = 1.0f / (1.0f + K / q + Ksq);
        switch (type) {
            case HighPass: b0 = norm, b1 = -2.0f * b0, b2 = b0; break;
            case BandPass: b0 = K / q * norm, b1 = 0.0f, b2 = -b0; break;
            default: b0 = Ksq * norm, b1 = 2.0f * b0, b2 = b0;
        }
        a1 = 2.0f * (Ksq - 1.0f) * norm;
        a2 = (1.0f - K / q + Ksq) * norm;
    }
    float process(float in) {
        float y = b0 * in + s1;
        s1 = s2 + in * b1 - a1 * y;
        s2 = b2 * in - a2 * y;
        return y;
    }
};

}  // namespace ssp::engine
