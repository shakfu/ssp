#include "LaunchpadDevice.h"

#include <atomic>

#include "ssp/Log.h"

namespace ssp::launchpad {

namespace {
std::atomic<bool> held{ false };  // an instance holds the device

template <typename List>
juce::String find(const List& devices, const Model& m) {
    for (const auto& d : devices)
        if (isPort(d.name.toStdString(), m)) return d.identifier;
    return {};
}

template <typename List>
bool listed(const List& devices, const juce::String& id) {
    for (const auto& d : devices)
        if (d.identifier == id) return true;
    return false;
}
}  // namespace

bool Device::poll() {
    if (isOpen()) {
        if (!listed(juce::MidiInput::getAvailableDevices(), inId_) ||
            !listed(juce::MidiOutput::getAvailableDevices(), outId_)) {
            ssp::log("launchpad: gone");
            close();
        }
        return false;
    }
    if (held.exchange(true)) return false;
    const auto ins = juce::MidiInput::getAvailableDevices();
    const auto outs = juce::MidiOutput::getAvailableDevices();
    for (const Model& m : MODELS) {
        inId_ = find(ins, m);
        outId_ = find(outs, m);
        if (inId_.isEmpty() || outId_.isEmpty()) continue;
        model_ = &m;
        in_ = juce::MidiInput::openDevice(inId_, this);
        out_ = juce::MidiOutput::openDevice(outId_);
        break;
    }
    if (!in_ || !out_) {
        in_.reset();
        out_.reset();
        held = false;
        return false;
    }
    send(programmerMode(*model_, true));
    in_->start();
    ssp::log(std::string("launchpad: open ") + model_->port);
    return true;
}

void Device::send(const std::vector<uint8_t>& bytes) {
    if (out_ && !bytes.empty()) out_->sendMessageNow(juce::MidiMessage(bytes.data(), int(bytes.size())));
}

void Device::close() {
    if (!out_) return;
    if (in_) in_->stop();
    send(programmerMode(*model_, false));
    in_.reset();
    out_.reset();
    held = false;
}

void Device::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) {
    Event e;
    if (decode(*model_, m.getRawData(), m.getRawDataSize(), e)) handler_(e);
}

}  // namespace ssp::launchpad
