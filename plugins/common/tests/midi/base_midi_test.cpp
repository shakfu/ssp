// Host test of BaseProcessor::handleMidi's dispatch to midiMessageInput and midiNoteInput;
// built and run by ../test_base_midi.py.

#include <cstdio>
#include <cstring>
#include <vector>

#include "ssp/BaseProcessor.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

using juce::MidiMessage;

struct Probe : ssp::BaseProcessor {
    Probe() : BaseProcessor(BusesProperties(), {}) {}

    const juce::String getName() const override { return "probe"; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    void midiMessageInput(const MidiMessage& m) override { messages.push_back(m); }
    void midiNoteInput(unsigned note, unsigned velocity) override { notes.push_back({ note, velocity }); }

    void send(const MidiMessage& m) {
        messages.clear();
        notes.clear();
        handleIncomingMidiMessage(nullptr, m);
    }

    std::vector<MidiMessage> messages;
    std::vector<std::pair<unsigned, unsigned>> notes;
};

static bool passed(Probe& p, const MidiMessage& m) {
    p.send(m);
    return p.messages.size() == 1 && p.messages[0].getRawDataSize() == m.getRawDataSize() &&
           std::memcmp(p.messages[0].getRawData(), m.getRawData(), size_t(m.getRawDataSize())) == 0;
}

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    Probe p;

    // omni: every channel message type except notes, which wait for noteInput
    CHECK(passed(p, MidiMessage::pitchWheel(1, 0x2345)));
    CHECK(passed(p, MidiMessage::controllerEvent(1, 1, 64)));
    CHECK(passed(p, MidiMessage::controllerEvent(16, 64, 127)));
    CHECK(passed(p, MidiMessage::channelPressureChange(3, 90)));
    CHECK(passed(p, MidiMessage::aftertouchChange(3, 60, 90)));
    CHECK(passed(p, MidiMessage::programChange(5, 31)));

    p.send(MidiMessage::noteOn(1, 60, juce::uint8(100)));
    CHECK(p.messages.empty());
    CHECK(p.notes.empty());

    // noteInput: notes reach both hooks
    p.noteInput(true);
    CHECK(passed(p, MidiMessage::noteOn(1, 60, juce::uint8(100))));
    CHECK(p.notes.size() == 1 && p.notes[0].first == 60 && p.notes[0].second == 100);
    CHECK(passed(p, MidiMessage::noteOff(1, 60)));
    CHECK(p.notes.size() == 1 && p.notes[0].second == 0);

    // system messages carry no channel
    p.send(MidiMessage::midiClock());
    CHECK(p.messages.empty());
    p.send(MidiMessage::midiStart());
    CHECK(p.messages.empty());
    const juce::uint8 sysex[] = { 0x43, 0x00, 0x09 };
    p.send(MidiMessage::createSysExMessage(sysex, int(sizeof sysex)));
    CHECK(p.messages.empty());

    // a channel: others are dropped, for both hooks
    p.midiChannel(2);
    CHECK(passed(p, MidiMessage::pitchWheel(2, 0)));
    p.send(MidiMessage::pitchWheel(3, 0));
    CHECK(p.messages.empty());
    p.send(MidiMessage::noteOn(3, 60, juce::uint8(100)));
    CHECK(p.messages.empty());
    CHECK(p.notes.empty());
    CHECK(passed(p, MidiMessage::noteOn(2, 60, juce::uint8(100))));

    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
