// SPDX-License-Identifier: GPL-3.0-only
//
// GPLv3 as a combined work with GlitchVoice.h; see ../NOTICE.md.
#pragma once

#include "GlitchEngine.h"
#include "engine/EngineProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    using Engine = glitch::GlitchEngine;
    static constexpr int DECKS = Engine::DECKS;

    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;

    struct Deck {
        using Parameter = RangedAudioParameter;
        Deck(AudioProcessorValueTreeState& apvts, int d);
        Parameter& algo;
        Parameter& p1;
        Parameter& p2;
        Parameter& pitch;
        Parameter& tone;
        Parameter& level;
        Parameter& regen;
    };
    Deck& deck(int d) { return *decks_[size_t(d)]; }
    RangedAudioParameter& crossfade() { return crossfade_; }
    RangedAudioParameter& route() { return route_; }

    static BusesProperties getBusesProperties();
    Engine& glitch() { return static_cast<Engine&>(engine()); }

protected:
    void control(const float* const* in, int n) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::vector<std::unique_ptr<Deck>> decks_;
    RangedAudioParameter& crossfade_;
    RangedAudioParameter& route_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
