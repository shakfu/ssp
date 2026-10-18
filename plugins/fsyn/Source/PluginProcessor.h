#pragma once

#include "FluidEngine.h"
#include "engine/EngineEditor.h"
#include "engine/EngineProcessor.h"

using namespace juce;

// SSPApi.h requires the class name PluginProcessor
class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    static constexpr const char* FONT_DIR = "/media/BOOT/fsyn";
    static constexpr const char* DEFAULT_FONT = "/media/BOOT/fsyn/FluidR3_GM.sf2";  // a new instance's

    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;
    static BusesProperties getBusesProperties();

    // message thread: queues the load; the engine reports its result
    void loadFont(const String& path);
    std::vector<ssp::engine::ParamPage> pages();

    fsyn::FluidEngine& fluid() { return static_cast<fsyn::FluidEngine&>(engine()); }

protected:
    void control(const float* const* in, int n) override;
    void midiMessageInput(const MidiMessage& m) override;
    void customToXml(XmlElement*) override;
    void customFromXml(XmlElement*) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    RangedAudioParameter& gain_;
    RangedAudioParameter& reverb_;
    RangedAudioParameter& chorus_;
    RangedAudioParameter& voices_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
