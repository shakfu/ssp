#include "PluginProcessor.h"

#include <chrono>
#include <mutex>
#include <set>

#include "PluginEditor.h"
#include "ssp/EditorHost.h"
#include "ssp/Log.h"
#include "ssp/controls/BaseParameter.h"

using gseq::GseqEngine;

namespace {
std::mutex portsLock;
std::set<int> portsUsed;  // the port numbers live instances hold

float value(const RangedAudioParameter& p) {
    return p.convertFrom0to1(p.getValue());
}
}  // namespace

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<GseqEngine>()),
      tempo_(*vts().getParameter("tempo")),
      swing_(*vts().getParameter("swing")),
      clock_(*vts().getParameter("clock")),
      ppqn_(*vts().getParameter("ppqn")) {
    init();
    {
        std::lock_guard<std::mutex> lock(portsLock);
        int k = 1;
        while (portsUsed.count(k)) k++;
        port_ = MidiOutput::createNewDevice(k == 1 ? String("gseq") : "gseq " + String(k));
        if (port_) {
            portsUsed.insert(k);
            portIndex_ = k;
        } else {
            ssp::log("gseq: no ALSA MIDI port");
        }
    }
    midiTransportInput(true);
    noteInput(true);
    midiThread_ = std::thread([this] { midiLoop(); });
}

PluginProcessor::~PluginProcessor() {
    midiQuit_ = true;
    midiThread_.join();
    pad_.close();
    MidiBuffer off;
    for (int ch = 1; ch <= 16; ch++) off.addEvent(MidiMessage::allNotesOff(ch), 0);
    send(off);
    port_.reset();
    std::lock_guard<std::mutex> lock(portsLock);
    portsUsed.erase(portIndex_);
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    params.add(std::make_unique<ssp::BaseFloatParameter>("tempo", "Tempo", 20.0f, 300.0f, 120.0f, 1.0f));
    params.add(std::make_unique<ssp::BaseFloatParameter>("swing", "Swing", 50.0f, 75.0f, 50.0f, 1.0f));
    params.add(std::make_unique<ssp::BaseChoiceParameter>("clock", "Clock", StringArray(CLOCK_NAMES, 3), 0));
    params.add(std::make_unique<ssp::BaseFloatParameter>("ppqn", "PPQN", 1.0f, 24.0f, 4.0f, 1.0f));
    return params;
}

PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    BusesProperties props;
    for (auto* name : { "Reset", "Clock" }) props.addBus(true, name, AudioChannelSet::mono());
    for (const char* kind : { "Gate ", "CV ", "Vel " })
        for (int k = 1; k <= GseqEngine::VOICES; k++) props.addBus(false, kind + String(k), AudioChannelSet::mono());
    return props;
}

void PluginProcessor::control(const float* const*, int) {
    auto source = ssp::engine::Clock::Source(jlimit(0, 2, int(std::lround(value(clock_)))));
    gseq().setClock(source, int(std::lround(value(ppqn_))));
    gseq().setBpm(value(tempo_));
    gseq().setSwing((value(swing_) - 50.0) / 50.0);
}

void PluginProcessor::customToXml(XmlElement* xml) {
    xml->setAttribute("pattern", 1);
    for (int r = 0; r < GseqEngine::ROWS; r++) {
        String words = String::toHexString(int(gseq().row(r).pack()));
        for (int s = 0; s < GseqEngine::STEPS; s++) words << " " << String::toHexString(int(gseq().step(r, s).pack()));
        xml->setAttribute("row" + String(r + 1), words);
    }
}

// Any word unpacks to a valid row or step; a missing one keeps its current value. Voices are cleared
// before the rows are set in order, so a voice moving between rows is not refused as taken; a voice two
// rows claim stays with the first.
void PluginProcessor::customFromXml(XmlElement* xml) {
    auto& g = gseq();
    GseqEngine::Row rows[GseqEngine::ROWS];
    for (int r = 0; r < GseqEngine::ROWS; r++) {
        rows[r] = g.row(r);
        String attr = "row" + String(r + 1);
        auto words = StringArray::fromTokens(xml->getStringAttribute(attr), " ", "");
        if (words.isEmpty()) continue;
        rows[r] = GseqEngine::Row::unpack(uint32_t(words[0].getHexValue32()));
        for (int s = 0; s < GseqEngine::STEPS && s + 1 < words.size(); s++)
            g.setStep(r, s, GseqEngine::Step::unpack(uint32_t(words[s + 1].getHexValue32())));
    }
    for (int r = 0; r < GseqEngine::ROWS; r++) {
        auto row = g.row(r);
        row.voice = 0;
        g.setRow(r, row);
    }
    for (int r = 0; r < GseqEngine::ROWS; r++) g.setRow(r, rows[r]);
}

std::vector<ssp::engine::ParamPage> PluginProcessor::pages() {
    using C = ssp::engine::ParamPage::Control;
    return { { "clock", Colours::cyan, { C{ &tempo_, 5.0f, 1.0f }, C{ &swing_, 5.0f, 1.0f }, C{ &clock_, 1.0f, 1.0f },
                                         C{ &ppqn_, 1.0f, 1.0f } } } };
}

void PluginProcessor::send(const MidiBuffer& buf) {
    if (port_) port_->sendBlockOfMessagesNow(buf);
    sendMidiMessagesNow(buf);
}

void PluginProcessor::midiLoop() {
    MidiBuffer buf;
    GseqEngine::Midi m;
    ssp::launchpad::Surface surface;
    ssp::launchpad::Frame frame;
    std::vector<uint8_t> lights;
    // ms counts loops, wrapping where both periods divide it
    for (int ms = 0; !midiQuit_.load(std::memory_order_relaxed);
         ms = (ms + MIDI_POLL_MS) % (PAD_POLL_MS * PAD_FRAME_MS)) {
        buf.clear();
        while (gseq().midiOut(m)) buf.addEvent(m.bytes, m.size, 0);
        if (!buf.isEmpty()) send(buf);
        if (ms % PAD_POLL_MS == 0 && pad_.poll()) surface.invalidate();
        if (ms % PAD_FRAME_MS == 0 && pad_.isOpen()) {
            padView_.render(frame);
            lights.clear();
            surface.update(frame, lights);
            pad_.send(lights);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(MIDI_POLL_MS));
    }
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI()) return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, pages(), {}), true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
