#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "AldaEngine.h"
#include "engine/ScriptProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::engine::ScriptProcessor {
public:
    PluginProcessor();
    // Sends All Notes Off on every channel before the devices go.
    ~PluginProcessor() override;
    const String getName() const override { return JucePlugin_Name; }
    // Inputs Reset, Clock, In 3-8; outputs Gate k and CV k, voice k's pair.
    static BusesProperties getBusesProperties();

    static constexpr int MIDI_POLL_MS = 1;

protected:
    // A patch saved before alda had controls stores p1-p16 at 0: Velocity x0, Tempo x0.25. Such a patch,
    // without the `controls` attribute, gets the neutral settings instead.
    void customToXml(XmlElement* xml) override;
    void customFromXml(XmlElement* xml) override;

    // MIDI input thread: clock and transport from the general panel's MIDI in, for `# @clock midi`
    void onMidiClock(double ts) override { alda().midiClock(ts); }
    void onMidiStart(double) override { alda().midiStart(); }
    void onMidiStop(double) override { alda().midiStop(); }
    void onMidiContinue(double) override { alda().midiContinue(); }

private:
    alda::AldaEngine& alda() { return static_cast<alda::AldaEngine&>(script()); }
    // Sends the engine's MIDI, every MIDI_POLL_MS, to the plugin's own ALSA port and to the general panel's
    // MIDI out, if one is chosen. Without either the messages are dropped, so the queue never fills.
    void midiLoop();
    void send(const MidiBuffer& buf);  // MIDI thread, or after it ends

    // An ALSA output port that other modules choose as MIDI in: "alda", then "alda 2" and so on for further
    // instances. Null if ALSA refused it.
    std::unique_ptr<MidiOutput> port_;
    int portIndex_ = 0;  // 1-based; 0 without a port

    std::atomic<bool> midiQuit_{ false };
    std::thread midiThread_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
