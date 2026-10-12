#pragma once

#include <memory>

#include "Teletype.h"
#include "ssp/BaseProcessor.h"

using namespace juce;

namespace ID {
constexpr const char* param{ "param" };
constexpr const char* in_range{ "in_range" };
// cvN_range, N 1-4
inline juce::String cvRange(unsigned o) { return "cv" + juce::String(o + 1) + "_range"; }
}  // namespace ID

class PluginProcessor : public ssp::BaseProcessor {
public:
    explicit PluginProcessor();
    explicit PluginProcessor(const AudioProcessor::BusesProperties& ioLayouts,
                             AudioProcessorValueTreeState::ParameterLayout layout);
    ~PluginProcessor() override;

    const String getName() const override { return JucePlugin_Name; }

    void prepareToPlay(double newSampleRate, int estimatedSamplesPerBlock) override;
    void processBlock(AudioSampleBuffer&, MidiBuffer&) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    enum { I_TRIG_1, I_IN = I_TRIG_1 + ttyp::Teletype::TRIGS, I_MAX };
    enum { O_TR_1, O_CV_1 = O_TR_1 + ttyp::Teletype::OUTS, O_MAX = O_CV_1 + ttyp::Teletype::OUTS };
    // range, per CV output and for IN: how teletype's 0-10 V maps onto the SSP's +/-5 V
    enum { R_VOLTS, R_SHIFT };

    // null if the firmware failed to load; error() says why
    ttyp::Teletype* teletype() { return teletype_ && teletype_->ok() ? teletype_.get() : nullptr; }
    String error() const { return teletype_ ? String(teletype_->error()) : String("no firmware"); }

    // Teletype's USB stick, with sceneDir() as the stick: every scene to ttNNs.txt, then each ttNN.txt
    // into its slot. Blocks for the file I/O. Returns a line for the status panel.
    String syncScenes();
    // TTYP_SCENES, else /media/BOOT/teletype
    static String sceneDir();

    static BusesProperties getBusesProperties() {
        BusesProperties props;
        for (auto i = 0; i < I_MAX; i++) { props.addBus(true, getInputBusName(i), AudioChannelSet::mono()); }
        for (auto i = 0; i < O_MAX; i++) { props.addBus(false, getOutputBusName(i), AudioChannelSet::mono()); }
        return props;
    }

protected:
    void customFromXml(juce::XmlElement*) override;
    void customToXml(juce::XmlElement*) override;
    AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    static const String getInputBusName(int channelIndex);
    static const String getOutputBusName(int channelIndex);

    std::unique_ptr<ttyp::Teletype> teletype_;
    juce::RangedAudioParameter& param_;
    juce::RangedAudioParameter& inRange_;
    juce::RangedAudioParameter* cvRange_[ttyp::Teletype::OUTS] = {};
    bool trigHigh_[ttyp::Teletype::TRIGS] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
