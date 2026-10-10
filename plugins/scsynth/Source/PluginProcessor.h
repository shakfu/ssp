#pragma once

#include "ScsynthEngine.h"
#include "engine/ScriptProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::engine::ScriptProcessor {
public:
    PluginProcessor();
    const String getName() const override { return JucePlugin_Name; }

protected:
    // MIDI input thread
    void midiNoteInput(unsigned note, unsigned velocity) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
