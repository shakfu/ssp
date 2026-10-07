#pragma once

#include "RadioEngine.h"
#include "engine/EngineProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    using Engine = radio::RadioEngine;
    static constexpr int DECKS = Engine::DECKS;

    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;

    struct Deck {
        using Parameter = RangedAudioParameter;
        Deck(AudioProcessorValueTreeState& apvts, int d);
        Parameter& station;
        Parameter& start;
        Parameter& speed;
        Parameter& noise;
        Parameter& level;
        Parameter& bank;
        Parameter& reset;
    };
    Deck& deck(int d) { return *decks_[size_t(d)]; }
    RangedAudioParameter& crossfade() { return crossfade_; }
    RangedAudioParameter& route() { return route_; }
    RangedAudioParameter& rawRate() { return rawRate_; }
    RangedAudioParameter& fade() { return fade_; }
    RangedAudioParameter& startPotImmediate() { return startPotImm_; }
    RangedAudioParameter& startCvImmediate() { return startCvImm_; }

    static BusesProperties getBusesProperties();

    Engine& radio() { return static_cast<Engine&>(engine()); }
    void setRoot(const String& dir) { radio().setRoot(dir.toStdString()); }
    // A root chosen by the user: also applies its SETTINGS.TXT to the parameters. A preset's root
    // does not, so the preset's values stand.
    void chooseRoot(const String& dir);
    String root() { return radio().root(); }

protected:
    void control(const float* const* in, int n) override;
    void customFromXml(XmlElement*) override;
    void customToXml(XmlElement*) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::vector<std::unique_ptr<Deck>> decks_;
    RangedAudioParameter& crossfade_;
    RangedAudioParameter& route_;
    RangedAudioParameter& rawRate_;
    RangedAudioParameter& fade_;
    RangedAudioParameter& startPotImm_;
    RangedAudioParameter& startCvImm_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
