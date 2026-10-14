#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "FilePicker.h"
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

    // Ctrl-S: the code to the current file; Ctrl-O: a file from files() as the code and the current
    // file. A line for the output.
    String save();
    String open(const String& name);
    StringArray files() const;  // the .sapf and .txt files in filesDir(), by name
    const String& currentFile() const { return current_; }
    sapf::FilePicker& picker() { return picker_; }
    // SAPF_FILES, else /media/BOOT/sapf: the code, the examples, and what >sd and >nrt write
    static String filesDir();

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
    sapf::FilePicker picker_;
    String current_ = "live.sapf";  // in filesDir()
    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    AudioSampleBuffer in_, spare_;  // inputs, copied before outputs overwrite their channels
    const float* inPtrs_[CHANNELS] = {};
    float* outPtrs_[CHANNELS] = {};
    std::atomic<float> loadAvg_{ 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
