#include "PluginProcessor.h"

#include <chrono>
#include <mutex>
#include <set>

#include "ssp/Log.h"
#include "tree-sitter-alda-highlights.h"

extern "C" const TSLanguage* tree_sitter_alda(void);

namespace {
std::mutex portsLock;
std::set<int> portsUsed;  // the port numbers live instances hold
}  // namespace

PluginProcessor::PluginProcessor()
    : ScriptProcessor(std::make_unique<alda::AldaEngine>(), "/media/BOOT/alda",
                      { 7775, "ALDA_KEYBOARD", ".alda", "#", tree_sitter_alda, ALDA_HIGHLIGHTS, false,
                        "C-Enter run  C-. stop  C-L clear  C-R reset  C-S save  C-S-S as  C-O open  Esc" },
                      getBusesProperties()) {
    {
        std::lock_guard<std::mutex> lock(portsLock);
        int k = 1;
        while (portsUsed.count(k)) k++;
        port_ = MidiOutput::createNewDevice(k == 1 ? String("alda") : "alda " + String(k));
        if (port_) {
            portsUsed.insert(k);
            portIndex_ = k;
        } else {
            ssp::log("alda: no ALSA MIDI port");
        }
    }
    applyDefaults();  // the controls' neutral settings; a preset restored later replaces them
    clockFromScore();
    midiTransportInput(true);
    midiThread_ = std::thread([this] { midiLoop(); });
}

PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    static_assert(2 * alda::AldaEngine::VOICES == CHANNELS, "a gate and a CV out per voice");
    BusesProperties props;
    for (int c = 0; c < CHANNELS; c++)
        props.addBus(true, c == 0 ? String("Reset") : c == alda::AldaEngine::CLOCK_INPUT ? String("Clock") : "In " + String(c + 1),
                     AudioChannelSet::mono());
    for (const char* kind : { "Gate ", "CV " })
        for (int k = 1; k <= alda::AldaEngine::VOICES; k++) props.addBus(false, kind + String(k), AudioChannelSet::mono());
    return props;
}

PluginProcessor::~PluginProcessor() {
    midiQuit_ = true;
    midiThread_.join();
    MidiBuffer off;
    for (int ch = 1; ch <= 16; ch++) off.addEvent(MidiMessage::allNotesOff(ch), 0);
    send(off);
    port_.reset();
    std::lock_guard<std::mutex> lock(portsLock);
    portsUsed.erase(portIndex_);
}

void PluginProcessor::customToXml(XmlElement* xml) {
    ScriptProcessor::customToXml(xml);
    xml->setAttribute("controls", 2);
}

void PluginProcessor::customFromXml(XmlElement* xml) {
    ScriptProcessor::customFromXml(xml);
    int version = xml->getIntAttribute("controls", 0);
    if (version == 0) applyDefaults();
    if (version < 2) clockFromScore();
}

void PluginProcessor::clockFromScore() {
    auto& p = param(alda::AldaEngine::CLOCK);
    p.setValueNotifyingHost(script().spec(alda::AldaEngine::CLOCK).unmap(float(alda::AldaEngine::CLOCK_SCORE)));
}

void PluginProcessor::send(const MidiBuffer& buf) {
    if (port_) port_->sendBlockOfMessagesNow(buf);
    sendMidiMessagesNow(buf);
}

void PluginProcessor::midiLoop() {
    MidiBuffer buf;
    alda::AldaEngine::Midi m;
    while (!midiQuit_.load(std::memory_order_relaxed)) {
        buf.clear();
        while (alda().midiOut(m)) buf.addEvent(m.bytes, m.size, 0);
        if (!buf.isEmpty()) send(buf);
        std::this_thread::sleep_for(std::chrono::milliseconds(MIDI_POLL_MS));
    }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
