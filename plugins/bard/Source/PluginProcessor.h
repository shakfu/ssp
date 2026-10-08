#pragma once

#include "BardEngine.h"
#include "engine/EngineProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    using Engine = bard::BardEngine;
    static constexpr int DECKS = Engine::DECKS;

    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;

    struct Deck {
        using Parameter = RangedAudioParameter;
        Deck(AudioProcessorValueTreeState& apvts, int d);
        Parameter& book;
        Parameter& mark;
        Parameter& rate;
        Parameter& keep;
        Parameter& volume;
        Parameter& shelf;
        Parameter& position;
        Parameter& seq;
        Parameter& loop;
        Parameter& reroll;
        Parameter& seam;
        Parameter& duck;
        Parameter& release;
        Parameter& colour;
        Parameter& colourMix;
        Parameter& room;
        Parameter& roomMix;
        Parameter& character;
        Parameter& play;
        Parameter& back;
        Parameter& next;
    };
    Deck& deck(int d) { return *decks_[size_t(d)]; }
    RangedAudioParameter& crossfade() { return crossfade_; }
    RangedAudioParameter& route() { return route_; }

    static BusesProperties getBusesProperties();

    Engine& bard() { return static_cast<Engine&>(engine()); }
    void setRoot(const String& dir) { bard().setRoot(dir.toStdString()); }
    String root() { return bard().root(); }

protected:
    void control(const float* const* in, int n) override;
    void customFromXml(XmlElement*) override;
    void customToXml(XmlElement*) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::vector<std::unique_ptr<Deck>> decks_;
    RangedAudioParameter& crossfade_;
    RangedAudioParameter& route_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
