#pragma once

#include "engine/EngineEditor.h"
#include "engine/EngineProcessor.h"
#include "engine/FaustEngine.h"

namespace ssp::faust {

// An EngineProcessor whose buses, parameters and pages come from a Faust kernel's controls.
class FaustProcessor : public engine::EngineProcessor {
public:
    // Takes ownership of `engine`.
    FaustProcessor(FaustEngineBase* engine, juce::Colour colour);

    const juce::String getName() const override { return JucePlugin_Name; }
    juce::AudioProcessorEditor* createEditor() override;

    // the kernel's audio inputs, a CV per control, the kernel's outputs
    static BusesProperties buses(const Spec& spec);

protected:
    void control(const float* const* in, int n) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(const Spec& spec);
    static juce::StringArray ids(const Spec& spec);
    std::vector<engine::ParamPage> pages();

    FaustEngineBase& faust() { return static_cast<FaustEngineBase&>(engine()); }

    juce::Colour colour_;
    std::vector<juce::RangedAudioParameter*> params_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustProcessor)
};

}  // namespace ssp::faust
