#pragma once

#include "DexedEngine.h"
#include "engine/EngineEditor.h"
#include "engine/EngineProcessor.h"

using namespace juce;

// SSPApi.h requires the class name PluginProcessor
class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    static constexpr const char* CART_DIR = "/media/BOOT/dexed";

    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;
    static BusesProperties getBusesProperties();

    // message thread
    void loadCartridge(const String& path);
    String cartridgePath() const { return path_; }
    String cartridgeStatus() const { return status_; }  // empty, or why the file did not load as it is
    String voiceName(int v) const;                      // v in 0..31
    int voice() const;                                  // 0..31, from the Voice parameter
    void stepVoice(int dir);                            // wraps from 32 to 1
    std::vector<ssp::engine::ParamPage> pages();

    dexed::DexedEngine& dexed() { return static_cast<dexed::DexedEngine&>(engine()); }

protected:
    void control(const float* const* in, int n) override;
    void midiMessageInput(const MidiMessage& m) override;
    void customToXml(XmlElement*) override;
    void customFromXml(XmlElement*) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void useCartridge(const dexed::Cartridge& c);

    RangedAudioParameter& voice_;
    RangedAudioParameter& voices_;
    RangedAudioParameter& level_;
    RangedAudioParameter& transpose_;
    RangedAudioParameter& cutoff_;
    RangedAudioParameter& resonance_;
    RangedAudioParameter& algorithm_;
    RangedAudioParameter& feedback_;
    RangedAudioParameter* opLevel_[6];
    RangedAudioParameter& lfoRate_;
    RangedAudioParameter& lfoDepth_;
    RangedAudioParameter& bendRange_;
    RangedAudioParameter& portamento_;
    RangedAudioParameter& mono_;
    RangedAudioParameter& engineType_;

    // message thread: a copy of what the engine plays, for names and the preset's fallback voice
    dexed::Cartridge cart_;
    bool hasCart_ = false;
    String path_, status_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
