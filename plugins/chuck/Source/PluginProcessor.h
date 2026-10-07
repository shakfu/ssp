#pragma once

#include "ChuckEngine.h"
#include "engine/ScriptProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::engine::ScriptProcessor {
public:
    PluginProcessor() : ScriptProcessor(std::make_unique<ck::ChuckEngine>(), "/media/BOOT/chuck") { }
    const String getName() const override { return JucePlugin_Name; }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
