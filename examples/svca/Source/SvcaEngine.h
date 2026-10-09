#pragma once

// A stereo VCA: In L and In R scaled by Level plus the Level CV. No JUCE, so it tests natively.

#include <algorithm>
#include <cmath>

#include "engine/Engine.h"

namespace svca {

class SvcaEngine : public ssp::engine::Engine {
public:
    enum { I_L, I_R, I_CV, I_MAX };  // the order of PluginProcessor's input buses
    enum { O_L, O_R, O_MAX };        // and of its output buses

    // audio thread: set by PluginProcessor::control before each process()
    float level = 1.0f;

    void prepare(float sampleRate, int) override {
        smooth_ = 1.0f - std::exp(-1.0f / (0.005f * sampleRate));  // 5 ms
    }

    void process(const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; i++) {
            // the CV adds to Level: 1.0 (5 V) is one unit of gain
            float target = std::clamp(level + in[I_CV][i], 0.0f, 2.0f);
            gain_ += (target - gain_) * smooth_;
            out[O_L][i] = in[I_L][i] * gain_;
            out[O_R][i] = in[I_R][i] * gain_;
        }
    }

private:
    float gain_ = 0.0f, smooth_ = 1.0f;
};

}  // namespace svca
