#pragma once


#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "SSP.h"
#include "controls/BaseParameter.h"

namespace ssp {

class BaseProcessor : public juce::AudioProcessor,
                      public juce::MidiInputCallback,
                      //                      public juce::AudioProcessorListener {
                      public juce::AudioProcessorListener,
                      private juce::ValueTree::Listener {
public:
    explicit BaseProcessor(const AudioProcessor::BusesProperties& ioLayouts,
                           juce::AudioProcessorValueTreeState::ParameterLayout pl);
    ~BaseProcessor() override;

    virtual void init();

    bool acceptsMidi() const override { return false; }

    bool producesMidi() const override { return false; }

    bool silenceInProducesSilenceOut() const override { return false; }

    double getTailLengthSeconds() const override { return 0.0f; }

    int getNumPrograms() override { return 1; }

    int getCurrentProgram() override { return 0; }

    void setCurrentProgram(int index) override {}

    const juce::String getProgramName(int index) override { return ""; }

    void changeProgramName(int index, const juce::String& newName) override {}

    void prepareToPlay(double newSampleRate, int estimatedSamplesPerBlock) override;
    void processBlock(juce::AudioSampleBuffer&, juce::MidiBuffer&) override;
    void releaseResources() override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;


    juce::RangedAudioParameter* getParameter(juce::StringRef n) { return apvts.getParameter(n); }

    juce::AudioProcessorValueTreeState& vts() { return apvts; }

    virtual void onInputChanged(unsigned i, bool b);
    virtual void onOutputChanged(unsigned i, bool b);

    bool isOutputEnabled(unsigned i) { return i < numOut && outputEnabled[i]; }

    bool isInputEnabled(unsigned i) { return i < numIn && inputEnabled[i]; }

    void setMidiInDevice(const std::string& id);
    void setMidiOutDevice(const std::string& id);

    bool isActiveMidiIn(const std::string& id) { return midiInDeviceId_ == id; }
    bool isConnectedMidiIn(const std::string& id) { return midiInDevice_ != nullptr && isActiveMidiIn(id); }
    std::string getMidiInId() { return midiInDeviceId_; }

    bool isActiveMidiOut(const std::string& id) { return midiOutDeviceId_ == id; }
    bool isConnectedMidiOut(const std::string& id) { return midiOutDevice_ != nullptr && isActiveMidiOut(id); }
    std::string getMidiOutId() { return midiOutDeviceId_; }

    void midiLearn(bool b);

    virtual void midiNoteInput(unsigned note, unsigned velocity) { ; }

    void midiChannel(unsigned ch) { midiChannel_ = ch; }
    int midiChannel() const { return midiChannel_; }

    void noteInput(bool b) { noteInput_ = b; }
    bool noteInput() const { return noteInput_; }
    void midiClockInput(bool b) { midiClockInput_ = b; }
    bool midiClockInput() const { return midiClockInput_; }
    void midiTransportInput(bool b) { midiTransportInput_ = b; }
    bool midiTransportInput() const { return midiTransportInput_; }

    void useCompactUI(bool b) { compactEditor_ = b; }
    bool useCompactUI() const { return compactEditor_; }

    void onAsyncThread();

    struct MidiAutomation {
        int paramIdx_ = -1;
        float scale_ = 1.0f;
        float offset_ = 0.0f;
        struct Midi {
            int channel_ = 0;
            int num_ = 0;  // note num, cc , if applicable
            enum Type { T_CC, T_PRESSURE, T_NOTE, T_MAX } type_ = T_MAX;
        } midi_;

        void reset() {
            paramIdx_ = -1;
            scale_ = 1.0f;
            offset_ = 0.0f;
            midi_.channel_ = 0;
            midi_.num_ = 0;
            midi_.type_ = Midi::T_MAX;
        }

        bool valid() const { return paramIdx_ >= 0 && midi_.type_ != Midi::T_MAX; }

        void store(juce::XmlElement*);
        void recall(juce::XmlElement*);
    };

    void sendMidiMessagesNow(const juce::MidiBuffer& midimsgs);


protected:

    bool isMidiInputDeviceIdValid(const std::string& id);
    bool isMidiOutputDeviceIdValid(const std::string& id);

    friend class BaseEditor;

    friend class SystemEditor;

    // FIXME : or at least clean up!
    static constexpr unsigned numIn = 24;
    static constexpr unsigned numOut = 24;

    juce::AudioProcessorValueTreeState apvts;
    bool inputEnabled[numIn];
    bool outputEnabled[numOut];

    virtual void midiFromXml(juce::XmlElement*);
    virtual void midiToXml(juce::XmlElement*);
    virtual void customFromXml(juce::XmlElement*);
    virtual void customToXml(juce::XmlElement*);


#if __APPLE__
    virtual void testFromXml(juce::XmlElement*);
    virtual void testToXml(juce::XmlElement*);
#endif

    void addBaseParameters(juce::AudioProcessorValueTreeState::ParameterLayout&);


    // midi automation
    // AudioProcessorListener
    void audioProcessorParameterChanged(juce::AudioProcessor* p, int parameterIndex, float newValue) override;

    void audioProcessorChanged(juce::AudioProcessor* processor,
                               const AudioProcessorListener::ChangeDetails& details) override {
        ;
    }

    // MidiInputCallback
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    void handleMidi(const juce::MidiMessage& message);

    void automateParam(int idx, const MidiAutomation& a, const juce::MidiMessage& msg);
    void midiOutStatusChange(bool connected) { ; }
    void midiInStatusChange(bool connected) { ; }
    void checkMidiDevices();

    virtual void onMidiStart(double ts) { ; }
    virtual void onMidiContinue(double ts) { ; }
    virtual void onMidiStop(double ts) { ; }
    virtual void onMidiClock(double ts) { ; }

    std::map<int, MidiAutomation> midiAutomation_;


public:
    std::map<int, MidiAutomation>& midiAutomation() { return midiAutomation_; }

private:
    void connectMidiInDevice(const std::string& id);
    void connectMidiOutDevice(const std::string& id);

    std::string getMidiInputDeviceName(const std::string& id);
    std::string getMidiOutputDeviceName(const std::string& id);
    void createAsyncThreadIfNeeded();


    std::string midiInDeviceId_;
    std::string midiOutDeviceId_;
    std::unique_ptr<juce::MidiInput> midiInDevice_;
    std::unique_ptr<juce::MidiOutput> midiOutDevice_;
    int midiCheckCounter_ = 0;
    int midiChannel_ = 0;
    bool midiLearn_ = false;
    bool noteInput_ = false;
    bool midiClockInput_ = true;
    bool midiTransportInput_ = false;

    std::unique_ptr<std::thread> asyncThread_;
    bool asyncActive_ = true;

    MidiAutomation lastLearn_;

    bool compactEditor_ = false;

    std::atomic_flag midiDeviceChangeLock = ATOMIC_FLAG_INIT;

    //     struct MidiMsg {
    //     };
    //    moodycamel::ReaderWriterQueue<MidiMsg> messageQueue_;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BaseProcessor)
};

}  // namespace ssp
