#pragma once

// DSP core of an engine plugin, hosted by EngineProcessor. Engines do not include JUCE, so their
// tests build natively with the host compiler.

namespace ssp::engine {

// SSP CV inputs read 0.2 per volt (1.0 = 5 V).
static constexpr float CV_PER_VOLT = 0.2f;

class Engine {
public:
    virtual ~Engine() = default;

    // Message thread, audio and idle() stopped. Called again on a rate or block-size change.
    virtual void prepare(float sampleRate, int maxBlock) = 0;

    // Audio thread. `in` holds a copy of the inputs, so writing `out` does not disturb them.
    virtual void process(const float* const* in, float* const* out, int n) = 0;

    // Worker thread, every EngineProcessor::IDLE_MS: file I/O, compiles, anything that blocks.
    virtual void idle() {}
};

}  // namespace ssp::engine
