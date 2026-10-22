#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "GseqEngine.h"
#include "engine/EngineEditor.h"
#include "engine/EngineProcessor.h"

using namespace juce;

// SSPApi.h requires the class name PluginProcessor
class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    static constexpr int MIDI_POLL_MS = 1;
    static constexpr const char* CLOCK_NAMES[3] = { "free", "analog", "midi" };  // Clock::Source order

    PluginProcessor();
    // Sends All Notes Off on every channel before the devices go.
    ~PluginProcessor() override;

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;
    // Inputs Reset, Clock; outputs Gate 1-4, CV 1-4, Vel 1-4
    static BusesProperties getBusesProperties();

    gseq::GseqEngine& gseq() { return static_cast<gseq::GseqEngine&>(engine()); }
    // Tempo, Swing, Clock and PPQN, the global page and the compact view's
    std::vector<ssp::engine::ParamPage> pages();
    RangedAudioParameter& tempo() { return tempo_; }
    RangedAudioParameter& swing() { return swing_; }
    RangedAudioParameter& clock() { return clock_; }
    RangedAudioParameter& ppqn() { return ppqn_; }

protected:
    void control(const float* const* in, int n) override;
    // the pattern: `row1`..`row8`, each the row's word then its steps', in hex (GseqEngine::Row, Step)
    void customToXml(XmlElement* xml) override;
    void customFromXml(XmlElement* xml) override;

    // MIDI input thread: clock and transport from the general panel's MIDI in, for Clock `midi`
    void onMidiClock(double ts) override { gseq().midiClock(ts); }
    void onMidiStart(double) override { gseq().midiStart(); }
    void onMidiStop(double) override { gseq().midiStop(); }
    void onMidiContinue(double) override { gseq().midiContinue(); }
    // notes to record; the engine ignores them unless Rec or Step is on
    void midiNoteInput(unsigned note, unsigned velocity) override { gseq().noteIn(int(note), int(velocity)); }
    // A patch stores note input, and gseq's first patches stored it off: recording needs it on.
    void midiFromXml(XmlElement* xml) override {
        EngineProcessor::midiFromXml(xml);
        noteInput(true);
    }

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    // Sends the engine's MIDI, every MIDI_POLL_MS, to the plugin's own ALSA port and to the general panel's
    // MIDI out, if one is chosen. Without either the messages are dropped, so the queue never fills.
    void midiLoop();
    void send(const MidiBuffer& buf);  // MIDI thread, or after it ends

    RangedAudioParameter& tempo_;
    RangedAudioParameter& swing_;
    RangedAudioParameter& clock_;
    RangedAudioParameter& ppqn_;

    // An ALSA output port that other modules choose as MIDI in: "gseq", then "gseq 2" and so on for further
    // instances. Null if ALSA refused it.
    std::unique_ptr<MidiOutput> port_;
    int portIndex_ = 0;  // 1-based; 0 without a port

    std::atomic<bool> midiQuit_{ false };
    std::thread midiThread_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
