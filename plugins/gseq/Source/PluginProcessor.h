#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "GridView.h"
#include "GseqEngine.h"
#include "LaunchpadView.h"
#include "engine/EngineEditor.h"
#include "engine/EngineProcessor.h"
#include "launchpad/LaunchpadDevice.h"
#if SSP_MONOME
#include "monome/GridDevice.h"
#endif

using namespace juce;

// SSPApi.h requires the class name PluginProcessor
class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    static constexpr int MIDI_POLL_MS = 1;
    static constexpr int PAD_POLL_MS = 1000, PAD_FRAME_MS = 16;                  // looks for the Launchpad; redraws it
    static constexpr int BLINK_MS = 250;  // the grid's cursor: lit, then not, for this long each
    static constexpr const char* CLOCK_NAMES[3] = { "free", "analog", "midi" };  // Clock::Source order
    static constexpr const char* MODE_NAMES[2] = { "grid", "roll" };

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
    RangedAudioParameter& mode() { return mode_; }
    bool roll() const;
    // UI thread: grid or roll mode, the parameter and the engine together (docs/dev/gseq-roll.md)
    void setRoll(bool on);
    // the Launchpad's window, for the screen; null while no Launchpad is open
    const gseq::LaunchpadView* padView() const { return padOpen_.load(std::memory_order_relaxed) ? &padView_ : nullptr; }

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
    // MIDI out, if one is chosen. Without either the messages are dropped, so the queue never fills. Also
    // opens the Launchpad when it appears and redraws it, and redraws the grid.
    void midiLoop();
    void send(const MidiBuffer& buf);  // MIDI thread, or after it ends

    RangedAudioParameter& tempo_;
    RangedAudioParameter& swing_;
    RangedAudioParameter& clock_;
    RangedAudioParameter& ppqn_;
    RangedAudioParameter& mode_;  // last in the layout: MIDI automation keys parameters by index

    // An ALSA output port that other modules choose as MIDI in: "gseq", then "gseq 2" and so on for further
    // instances. Null if ALSA refused it.
    std::unique_ptr<MidiOutput> port_;
    int portIndex_ = 0;  // 1-based; 0 without a port

    // the Launchpad: its input reaches the view on the MIDI input thread; midiLoop draws it
    gseq::LaunchpadView padView_{ gseq() };
    ssp::launchpad::Device pad_{ [this](const ssp::launchpad::Event& e) { padView_.press(e); } };

    std::atomic<bool> padOpen_{ false };  // pad_.isOpen(), for other threads

    // a monome grid: its own thread finds it and passes its keys to the view; midiLoop draws it
    gseq::GridView gridView_{ gseq() };
#if SSP_MONOME
    ssp::monome::GridDevice grid_{ [this](const ssp::monome::Key& k) { gridView_.press(k); } };
#endif
    std::atomic<bool> midiQuit_{ false };
    std::thread midiThread_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
