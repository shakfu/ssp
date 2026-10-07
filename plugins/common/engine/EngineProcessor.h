#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "engine/Engine.h"
#include "ssp/BaseProcessor.h"

namespace ssp::engine {

// Hosts an Engine: copies the inputs, runs the engine in blocks of at most the prepared size, runs
// Engine::idle() on a worker thread, and measures DSP load. Plugins derive from it and map their
// parameters and CV inputs onto the engine in control().
class EngineProcessor : public ssp::BaseProcessor {
public:
    static constexpr int IDLE_MS = 10;

    EngineProcessor(const BusesProperties& ioLayouts, juce::AudioProcessorValueTreeState::ParameterLayout layout,
                    std::unique_ptr<Engine> engine);
    ~EngineProcessor() override;

    void prepareToPlay(double newSampleRate, int estimatedSamplesPerBlock) override;
    void processBlock(juce::AudioSampleBuffer&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }

    // processBlock time as a fraction of the block's duration, on one core
    float loadAverage() const { return loadAvg_.load(std::memory_order_relaxed); }
    float loadPeak() const { return loadPeak_.load(std::memory_order_relaxed); }

protected:
    // Audio thread, before each Engine::process call: `in` is the copy the engine will see.
    virtual void control(const float* const* in, int n) {}

    Engine& engine() { return *engine_; }
    double sampleRate() const { return sampleRate_; }

    // Runs `fn` with idle() stopped, for message-thread work that touches the engine's idle state.
    template <typename Fn>
    void withIdleStopped(Fn&& fn) {
        std::lock_guard<std::mutex> lock(idleLock_);
        fn();
    }

private:
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    void idleLoop();

    std::unique_ptr<Engine> engine_;
    std::mutex idleLock_;  // held around idle() and prepare()
    std::thread worker_;
    std::atomic<bool> quit_{ false };

    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    juce::AudioBuffer<float> in_;
    juce::AudioBuffer<float> spare_;  // outputs the host's buffer lacks
    std::vector<const float*> inPtrs_;
    std::vector<float*> outPtrs_;

    std::atomic<float> loadAvg_{ 0.0f };
    std::atomic<float> loadPeak_{ 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineProcessor)
};

}  // namespace ssp::engine
