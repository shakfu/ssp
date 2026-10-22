#pragma once

#include "SapfEngine.h"
#include "engine/ScriptProcessor.h"
#include "ssp/controls/RmsTrack.h"

using namespace juce;

// sapf's key port, and the variable whose value 0 turns its USB keyboard off (see ssp::KeyInput)
constexpr uint16_t SAPF_KEY_PORT = 7771;
constexpr const char* SAPF_KEYBOARD_ENV = "SAPF_KEYBOARD";

class PluginProcessor : public ssp::engine::ScriptProcessor {
public:
    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    void processBlock(AudioSampleBuffer&, MidiBuffer&) override;
    AudioProcessorEditor* createEditor() override;
    // Mouse X and Y, on encoders 1 and 2; sapf code reads the inputs with adc
    std::vector<ssp::engine::ParamPage> pages() override;

    const sapf::SapfEngine& sapf() { return static_cast<const sapf::SapfEngine&>(script()); }
    // SAPF_FILES, else /media/BOOT/sapf: the code, the prelude, the examples, and what >sf and >sfo write
    static String filesDir();

    // inputs and outputs 1-2, the ones code uses first (Synthor shows all eight): each one's peak,
    // averaged over 100 blocks; audio and CV alike
    static constexpr int METERED = 2;
    float inLevel(int c) { return inLevel_[c].lvl(); }
    float outLevel(int c) { return outLevel_[c].lvl(); }

protected:
    void control(const float* const* in, int n) override;
    void customFromXml(juce::XmlElement*) override;

private:
    ssp::RmsTrack inLevel_[METERED], outLevel_[METERED];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
