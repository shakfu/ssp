#include "EngineProcessor.h"

#include <chrono>

#ifndef __APPLE__
#include <pthread.h>
#endif

namespace ssp::engine {

EngineProcessor::EngineProcessor(const BusesProperties& ioLayouts,
                                 juce::AudioProcessorValueTreeState::ParameterLayout layout,
                                 std::unique_ptr<Engine> engine)
    : BaseProcessor(ioLayouts, std::move(layout)), engine_(std::move(engine)) {
}

EngineProcessor::~EngineProcessor() {
    quit_ = true;
    if (worker_.joinable()) worker_.join();
}

void EngineProcessor::prepareToPlay(double newSampleRate, int estimatedSamplesPerBlock) {
    BaseProcessor::prepareToPlay(newSampleRate, estimatedSamplesPerBlock);
    sampleRate_ = newSampleRate;
    maxBlock_ = std::max(1, estimatedSamplesPerBlock);
    in_.setSize(std::max(1, getTotalNumInputChannels()), maxBlock_);
    spare_.setSize(std::max(1, getTotalNumOutputChannels()), maxBlock_);
    inPtrs_.assign(size_t(getTotalNumInputChannels()), nullptr);
    outPtrs_.assign(size_t(getTotalNumOutputChannels()), nullptr);
    {
        std::lock_guard<std::mutex> lock(idleLock_);
        engine_->prepare(float(newSampleRate), maxBlock_);
    }
    if (!worker_.joinable()) worker_ = std::thread([this] { idleLoop(); });
}

void EngineProcessor::idleLoop() {
#ifndef __APPLE__
    // keep file I/O and compiles on the UI core, as BaseProcessor does for its async thread
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
    while (!quit_.load()) {
        {
            std::lock_guard<std::mutex> lock(idleLock_);
            engine_->idle();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(IDLE_MS));
    }
}

void EngineProcessor::processBlock(juce::AudioSampleBuffer& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    BaseProcessor::processBlock(buffer, midi);
    auto t0 = std::chrono::steady_clock::now();
    int n = buffer.getNumSamples();
    if (n == 0 || maxBlock_ == 0) return;

    int chans = buffer.getNumChannels();
    int nIn = std::min(int(inPtrs_.size()), chans), nOut = std::min(int(outPtrs_.size()), chans);
    for (int c = 0; c < int(inPtrs_.size()); c++) inPtrs_[size_t(c)] = in_.getReadPointer(c);
    if (nIn < int(inPtrs_.size())) in_.clear();  // a host with fewer channels leaves the rest silent

    // the host may exceed the block size it announced
    for (int pos = 0; pos < n; pos += maxBlock_) {
        int len = std::min(maxBlock_, n - pos);
        for (int c = 0; c < nIn; c++) in_.copyFrom(c, 0, buffer, c, pos, len);
        for (int c = 0; c < int(outPtrs_.size()); c++)
            outPtrs_[size_t(c)] = c < nOut ? buffer.getWritePointer(c, pos) : spare_.getWritePointer(c);
        control(inPtrs_.data(), len);
        engine_->process(inPtrs_.data(), outPtrs_.data(), len);
    }

    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    float load = float(dt * sampleRate_ / n);
    float avg = loadAvg_.load(std::memory_order_relaxed);
    loadAvg_.store(avg + 0.01f * (load - avg), std::memory_order_relaxed);
    float peak = loadPeak_.load(std::memory_order_relaxed);
    loadPeak_.store(std::max(load, peak * 0.999f), std::memory_order_relaxed);
}

}  // namespace ssp::engine
