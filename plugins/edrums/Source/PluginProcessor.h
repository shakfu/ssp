#pragma once

#include "EdrumsEngine.h"
#include "engine/EngineProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    using Engine = edrums::EdrumsEngine;
    static constexpr int DRUMS = Engine::DRUMS;

    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;

    struct Drum {
        using Parameter = RangedAudioParameter;
        Drum(AudioProcessorValueTreeState& apvts, int d);
        Parameter& model;
        Parameter& hits;
        Parameter& steps;
        Parameter& rotate;
        Parameter& rate;
        Parameter& chance;
        Parameter& swing;
        Parameter& pitch;
        Parameter& decay;
        Parameter& level;
        Parameter& drive;
        Parameter& sweep;
        Parameter& tone;
        Parameter& bright;
        Parameter& mute;
        Parameter& voice;
        Parameter& trig;
    };
    Drum& drum(int d) { return *drums_[size_t(d)]; }
    RangedAudioParameter& route() { return route_; }

    static BusesProperties getBusesProperties();
    Engine& edrums() { return static_cast<Engine&>(engine()); }

protected:
    void control(const float* const* in, int n) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::vector<std::unique_ptr<Drum>> drums_;
    RangedAudioParameter& route_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
