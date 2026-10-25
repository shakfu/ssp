#pragma once

#include "UseqEngine.h"
#include "engine/ScriptProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::engine::ScriptProcessor {
public:
    PluginProcessor();
    const String getName() const override { return JucePlugin_Name; }
    // Inputs In 1-8; outputs d1-d4 then a1-a4, as the program names them.
    static BusesProperties getBusesProperties();

protected:
    // MIDI input thread: clock and transport from the general panel's MIDI in, for `; @clock midi`
    void onMidiClock(double ts) override { useq().midiClock(ts); }
    void onMidiStart(double) override { useq().midiStart(); }
    void onMidiStop(double) override { useq().midiStop(); }
    void onMidiContinue(double) override { useq().midiContinue(); }

private:
    mlsp::UseqEngine& useq() { return static_cast<mlsp::UseqEngine&>(script()); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
