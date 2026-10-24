#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

#include "Launchpad.h"

namespace ssp::launchpad {

// The Launchpad's MIDI interface, in and out, found by name (isPort), for the first model in MODELS listed. One instance in the process holds
// it at a time: the others wait in poll() until it is released. Programmer mode while held; Live mode on
// release, so the device works on its own again.
class Device : private juce::MidiInputCallback {
public:
    using Handler = std::function<void(const Event&)>;  // the MIDI input thread

    explicit Device(Handler h) : handler_(std::move(h)) {}
    ~Device() override { close(); }

    // One thread, not audio, about once a second: opens the device when it is listed and free, and
    // closes it when it is gone. True when it has just opened: what it shows is then unknown.
    bool poll();
    bool isOpen() const { return out_ != nullptr; }
    const Model& model() const { return *model_; }  // poll()'s thread: the open device's, or the last
    // poll()'s thread: a whole message, SysEx included
    void send(const std::vector<uint8_t>& bytes);
    void close();

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override;

    Handler handler_;
    const Model* model_ = &MODELS[0];
    std::unique_ptr<juce::MidiInput> in_;
    std::unique_ptr<juce::MidiOutput> out_;
    juce::String inId_, outId_;
};

}  // namespace ssp::launchpad
