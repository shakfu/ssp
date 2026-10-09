#pragma once


#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>

#include "Matrix.h"
#include "Module.h"
#include "modules/InputModule.h"
#include "modules/OutputModule.h"

class Track {
public:
    explicit Track();
    ~Track();

    std::vector<Matrix::Wire> connections();

    void prepare(int sampleRate, int blockSize);
    void process(juce::AudioSampleBuffer& ioBuffer);
    static void limitInputs(juce::AudioSampleBuffer& buf, uint64_t wired, int n);

    void getStateInformation(juce::XmlElement& outStream);
    void getStateInformation(juce::var& out);
    void setStateInformation(juce::XmlElement& inStream);
    void setStateInformation(const juce::var& track, int trackIdx);

    bool requestModuleChange(unsigned midx, const std::string& mn);
    bool requestMatrixConnect(const Matrix::Jack& src, const Matrix::Jack& dest, float gain = 1.0f,
                              float offset = 0.0f);
    bool requestMatrixDisconnect(const Matrix::Jack& src, const Matrix::Jack& dest);
    // adds a wire at gain 1, or removes every wire between the two jacks
    bool requestMatrixToggle(const Matrix::Jack& src, const Matrix::Jack& dest);
    // steps a wire's gain within 0 to 1; stepping up from no wire adds one, reaching 0 removes it
    bool requestMatrixGain(const Matrix::Jack& src, const Matrix::Jack& dest, float delta);
    bool requestMatrixAttenuate(const Matrix::Jack& src, const Matrix::Jack& dest, bool isOffset, float delta);
    bool requestClearTrack();

    enum ModuleIdx {
        M_IN,
        M_SLOT_1,
        M_SLOT_2,
        M_SLOT_3,
        M_SLOT_4,
        M_SLOT_5,
        M_SLOT_6,
        M_SLOT_7,
        M_SLOT_8,
        M_OUT,
        M_MAX
    };

    Module modules_[M_MAX];

    void mute(bool m) { mute_ = m; }
    bool mute() const { return mute_; }

    void level(float l) { level_ = l; }
    float level() const { return level_; }

    std::mutex mutex_;
    std::condition_variable cv_;
    bool ready_ = false;
    bool processed_ = false;
    bool exited_ = false;
    static constexpr unsigned MAX_MODULES = Track::M_MAX;
    static constexpr unsigned MAX_USER_MODULES = M_OUT - M_IN - 1;

private:
    static constexpr int MAX_IO_IN = 8;
    static constexpr int MAX_IO_OUT = 2;
    static constexpr int MAX_IO = std::max(MAX_IO_IN, MAX_IO_OUT);
    // a module input's limit: +-10 V at 0.2 a volt; see limitInputs
    static constexpr float IN_LIMIT = 2.0f;

    bool loadModule(std::string, Module& m);
    void alloc(int sampleRate, int blockSize);
    void free();

    // callers hold lock_
    void connectLocked(const Matrix::Jack& src, const Matrix::Jack& dest, float gain, float offset);
    void disconnectLocked(const Matrix::Jack& src, const Matrix::Jack& dest);

    void resetModuleConnections(int midx);
    void clearModuleConnections(int midx);
    int sampleRate_ = 48000;
    int blockSize_ = 128;
    // set from the UI or a preset load, read on the audio thread
    std::atomic<bool> mute_{ false };
    std::atomic<float> level_{ 1.0f };

    std::shared_ptr<InputModule::PluginInterface> trackIn_;
    std::shared_ptr<OutputModule::PluginInterface> trackOut_;
    Matrix matrix_;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Track)
};
