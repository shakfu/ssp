#pragma once

#include "PstretchEngine.h"
#include "engine/EngineProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    using Engine = pstretch::PstretchEngine;
    static constexpr int DECKS = Engine::DECKS;

    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;

    struct Deck {
        using Parameter = RangedAudioParameter;
        Deck(AudioProcessorValueTreeState& apvts, int d);
        Parameter& stretch;
        Parameter& diffuse;
        Parameter& pitch;
        Parameter& tone;
        Parameter& mix;
        Parameter& source;
        Parameter& clip;
        Parameter& position;
        Parameter& modRate;
        Parameter& modDepth;
        Parameter& modShape;
        Parameter& modTarget;
        Parameter& freeze;
        Parameter& grab;
    };
    Deck& deck(int d) { return *decks_[size_t(d)]; }
    RangedAudioParameter& crossfade() { return crossfade_; }
    RangedAudioParameter& route() { return route_; }
    RangedAudioParameter& window() { return window_; }

    static BusesProperties getBusesProperties();

    Engine& pstretch() { return static_cast<Engine&>(engine()); }
    void setRoot(const String& dir) { pstretch().setRoot(dir.toStdString()); }
    String root() { return pstretch().root(); }

protected:
    void control(const float* const* in, int n) override;
    void customFromXml(XmlElement*) override;
    void customToXml(XmlElement*) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::vector<std::unique_ptr<Deck>> decks_;
    RangedAudioParameter& crossfade_;
    RangedAudioParameter& route_;
    RangedAudioParameter& window_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
