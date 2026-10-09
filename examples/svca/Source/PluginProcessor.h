#pragma once

#include "SvcaEngine.h"
#include "engine/EngineEditor.h"
#include "engine/EngineProcessor.h"

using namespace juce;

// SSPApi.h requires the class name PluginProcessor
class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;
    static BusesProperties getBusesProperties();

protected:
    void control(const float* const* in, int n) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    std::vector<ssp::engine::ParamPage> pages();
    svca::SvcaEngine& vca() { return static_cast<svca::SvcaEngine&>(engine()); }

    RangedAudioParameter& level_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
