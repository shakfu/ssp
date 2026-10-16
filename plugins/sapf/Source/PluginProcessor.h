#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "FilePicker.h"
#include "keys/KeyQueue.h"
#include "Session.h"
#include "keys/TextBuffer.h"
#include "ssp/BaseProcessor.h"
#include "ssp/controls/RmsTrack.h"

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
    ssp::KeyQueue& keys() { return keys_; }
    // the code; message thread
    ssp::TextBuffer& text() { return text_; }

    // Ctrl-S: the code to the current file; Ctrl-O: a file from files() as the code and the current
    // file. A line for the output.
    String save();
    String open(const String& name);
    StringArray files() const;  // the .sapf and .txt files in filesDir(), by name
    const String& currentFile() const { return current_; }
    sapf::FilePicker& picker() { return picker_; }
    // SAPF_FILES, else /media/BOOT/sapf: the code, the examples, and what >sf and >sfo write
    static String filesDir();

    double sampleRate() const { return sampleRate_; }
    // inputs and outputs 1-2, the ones code uses first (Synthor shows all eight): each one's peak,
    // averaged over 100 blocks; audio and CV alike
    static constexpr int METERED = 2;
    float inLevel(int c) { return inLevel_[c].lvl(); }
    float outLevel(int c) { return outLevel_[c].lvl(); }

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
    ssp::KeyQueue keys_;
    ssp::TextBuffer text_;
    sapf::FilePicker picker_;
    String current_ = "live.sapf";  // in filesDir()
    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    AudioSampleBuffer in_, spare_;  // inputs, copied before outputs overwrite their channels
    const float* inPtrs_[CHANNELS] = {};
    float* outPtrs_[CHANNELS] = {};
    ssp::RmsTrack inLevel_[METERED], outLevel_[METERED];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
