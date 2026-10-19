#pragma once

#include "BreaksEngine.h"
#include "engine/EngineEditor.h"
#include "engine/EngineProcessor.h"

using namespace juce;

// SSPApi.h requires the class name PluginProcessor
class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    static constexpr const char* CARD_DIR = "/media/BOOT/brks";               // where Load opens
    static constexpr const char* DEFAULT_CARD = "/media/BOOT/brks/default";  // a new instance's
    // Clock Out's choices, in the firmware's division order
    static constexpr const char* CLOCK_NAMES[7] = { "8/beat", "4/beat", "2/beat", "1/beat", "1/2 beats", "1/bar",
                                                    "1/2 bars" };

    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;
    static BusesProperties getBusesProperties();

    // message thread: queues the card folder at `path`, or the one holding it
    void loadCard(const String& path);
    std::vector<ssp::engine::ParamPage> pages();
    std::vector<RangedAudioParameter*> buttons();

    brks::BreaksEngine& brks() { return static_cast<brks::BreaksEngine&>(engine()); }
    RangedAudioParameter& tempo() { return tempo_; }

protected:
    void control(const float* const* in, int n) override;
    void customToXml(XmlElement*) override;
    void customFromXml(XmlElement*) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    RangedAudioParameter* knobs_[brks::Panel::K_MAX];
    RangedAudioParameter& volume_;
    RangedAudioParameter& filter_;
    RangedAudioParameter& gating_;
    RangedAudioParameter& tempo_;
    RangedAudioParameter& clockOut_;
    RangedAudioParameter& mode_;
    RangedAudioParameter& tap_;
    RangedAudioParameter& play_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
