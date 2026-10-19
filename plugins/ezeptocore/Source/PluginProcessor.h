#pragma once

#include "EzepEngine.h"
#include "engine/EngineEditor.h"
#include "engine/EngineProcessor.h"

using namespace juce;

// SSPApi.h requires the class name PluginProcessor
class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    static constexpr const char* CARD_DIR = "/media/BOOT/ezep";               // where Load opens
    static constexpr const char* DEFAULT_CARD = "/media/BOOT/ezep/default";  // a new instance's

    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;
    static BusesProperties getBusesProperties();

    // message thread: queues the card folder at `path`, or the one holding it
    void loadCard(const String& path);
    std::vector<ssp::engine::ParamPage> pages();
    std::vector<RangedAudioParameter*> buttons();

    ezep::EzepEngine& ezep() { return static_cast<ezep::EzepEngine&>(engine()); }

protected:
    void control(const float* const* in, int n) override;
    void customToXml(XmlElement*) override;
    void customFromXml(XmlElement*) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    RangedAudioParameter* knobs_[ezep::Panel::K_MAX];
    RangedAudioParameter& volume_;
    RangedAudioParameter& filter_;
    RangedAudioParameter& gating_;
    RangedAudioParameter* buttons_[ezep::Panel::B_MAX];
    RangedAudioParameter& play_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
