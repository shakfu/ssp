#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "KeyQueue.h"
#include "Session.h"
#include "TextBuffer.h"
#include "ssp/BaseProcessor.h"

using namespace juce;

// sapf's key port, and the variable whose value 0 turns its USB keyboard off (see ssp::KeyInput)
constexpr uint16_t SAPF_KEY_PORT = 7771;
constexpr const char* SAPF_KEYBOARD_ENV = "SAPF_KEYBOARD";

class PluginProcessor : public ssp::BaseProcessor {
public:
    static constexpr int CHANNELS = sapf::Session::CHANNELS;

    explicit PluginProcessor();
    ~PluginProcessor() override;

    const String getName() const override { return JucePlugin_Name; }
    void prepareToPlay(double newSampleRate, int estimatedSamplesPerBlock) override;
    void processBlock(AudioSampleBuffer&, MidiBuffer&) override;
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    sapf::Session& session() { return *session_; }
    sapf::KeyQueue& keys() { return keys_; }
    // the code; message thread
    sapf::TextBuffer& text() { return text_; }

    // Ctrl-S and Ctrl-O: the code to and from file(), a line for the output
    String save();
    String open();
    // SAPF_FILES, else /media/BOOT/sapf: the code, and what >sd and >nrt write
    static String filesDir();
    static String file() { return filesDir() + "/live.sapf"; }

    float load() const { return loadAvg_.load(std::memory_order_relaxed); }  // of the audio callback's time

    static BusesProperties getBusesProperties() {
        BusesProperties props;
        for (int c = 0; c < CHANNELS; c++) props.addBus(true, "In " + String(c + 1), AudioChannelSet::mono());
        for (int c = 0; c < CHANNELS; c++) props.addBus(false, "Out " + String(c + 1), AudioChannelSet::mono());
        return props;
    }

protected:
    void customFromXml(juce::XmlElement*) override;
    void customToXml(juce::XmlElement*) override;
    AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    std::unique_ptr<sapf::Session> session_;
    sapf::KeyQueue keys_;
    sapf::TextBuffer text_;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    AudioSampleBuffer in_, spare_;  // inputs, copied before outputs overwrite their channels
    const float* inPtrs_[CHANNELS] = {};
    float* outPtrs_[CHANNELS] = {};
    std::atomic<float> loadAvg_{ 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
