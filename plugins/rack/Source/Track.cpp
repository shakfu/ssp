#include "Track.h"

#include <algorithm>
#include <cmath>

#include "GraphOrder.h"
#include "JsonPreset.h"

#include "ssp/Log.h"

Track::Track() {
    trackIn_ = std::make_shared<InputModule::PluginInterface>();
    modules_[M_IN].alloc("IN", trackIn_.get(), trackIn_->createDescriptor(), nullptr);
    trackOut_ = std::make_shared<OutputModule::PluginInterface>();
    modules_[M_OUT].alloc("OUT", trackOut_.get(), trackOut_->createDescriptor(), nullptr);
}

Track::~Track() {
}

std::vector<Matrix::Wire> Track::connections() {
    return matrix_.connections_;
}

bool Track::requestModuleChange(unsigned midx, const std::string& mn) {
    auto& m = modules_[midx];

    // track lock before module lock, the order requestClearTrack and process use
    if (lock_.test_and_set()) return false;
    if (m.lock_.test_and_set()) {
        lock_.clear();
        return false;
    }
    // process reads the wires, so drop them under the track lock; loading needs only the module lock
    clearModuleConnections(midx);
    lock_.clear();

    if (m.loadModule(mn)) {
        resetModuleConnections(midx);
        m.prepare(sampleRate_, blockSize_);
    }
    m.lock_.clear();
    return true;
}

bool Track::requestClearTrack() {
    if (!lock_.test_and_set()) {
        for (int midx = 0; midx < M_MAX; midx++) {
            if (midx == M_IN || midx == M_OUT) continue;
            auto& m = modules_[midx];
            while (!m.lock_.test_and_set()) {};
            clearModuleConnections(midx);
            modules_[midx].free();
            m.lock_.clear();
        }
        lock_.clear();
        return true;
    }
    return false;
}


bool Track::requestMatrixConnect(const Matrix::Jack& src, const Matrix::Jack& dest, float gain, float offset) {
    if (lock_.test_and_set()) return false;
    connectLocked(src, dest, gain, offset);
    lock_.clear();
    return true;
}


bool Track::requestMatrixDisconnect(const Matrix::Jack& src, const Matrix::Jack& dest) {
    if (lock_.test_and_set()) return false;
    disconnectLocked(src, dest);
    lock_.clear();
    return true;
}


// one lock for the check and the edit, so the result does not depend on a wire changing in between
bool Track::requestMatrixToggle(const Matrix::Jack& src, const Matrix::Jack& dest) {
    if (lock_.test_and_set()) return false;
    unsigned n = 0;
    for (auto& w : matrix_.connections_) {
        if (w.src_ == src && w.dest_ == dest) n++;
    }
    if (n == 0) connectLocked(src, dest, 1.0f, 0.0f);
    while (n-- > 0) disconnectLocked(src, dest);
    lock_.clear();
    return true;
}


bool Track::requestMatrixGain(const Matrix::Jack& src, const Matrix::Jack& dest, float delta) {
    if (lock_.test_and_set()) return false;
    Matrix::Wire* wire = nullptr;
    for (auto& w : matrix_.connections_) {
        if (w.src_ == src && w.dest_ == dest) {
            wire = &w;
            break;
        }
    }
    // rounded to 0.01 so repeated steps land exactly on 0 and 1
    float gain = std::round(std::clamp((wire ? wire->gain_ : 0.0f) + delta, 0.0f, 1.0f) * 100.0f) / 100.0f;
    if (wire == nullptr) {
        if (gain > 0.0f) connectLocked(src, dest, gain, 0.0f);
    } else if (gain > 0.0f) {
        wire->gain_ = gain;
    } else {
        disconnectLocked(src, dest);
    }
    lock_.clear();
    return true;
}


void Track::connectLocked(const Matrix::Jack& src, const Matrix::Jack& dest, float gain, float offset) {
    int srcCount = 0;
    int destCount = 0;
    for (auto& w : matrix_.connections_) {
        if (w.src_ == src) srcCount++;
        if (w.dest_ == dest) destCount++;
    }

    auto& srcMod = modules_[src.modIdx_];
    auto& destMod = modules_[dest.modIdx_];

    if (srcMod.descriptor_ && src.chIdx_ < srcMod.descriptor_->outputChannelNames.size() && destMod.descriptor_ &&
        dest.chIdx_ < destMod.descriptor_->inputChannelNames.size()) {
        matrix_.connect(src, dest);
        matrix_.connections_.back().applyGainOffset(gain, offset);
        if (srcCount == 0 && srcMod.plugin_) srcMod.plugin_->outputEnabled(src.chIdx_, true);
        if (destCount == 0 && destMod.plugin_) destMod.plugin_->inputEnabled(dest.chIdx_, true);
    }
}


void Track::disconnectLocked(const Matrix::Jack& src, const Matrix::Jack& dest) {
    int srcCount = 0;
    int destCount = 0;
    for (auto& w : matrix_.connections_) {
        if (w.src_ == src) srcCount++;
        if (w.dest_ == dest) destCount++;
    }

    auto& srcMod = modules_[src.modIdx_];
    auto& destMod = modules_[dest.modIdx_];
    matrix_.disconnect(src, dest);
    if (srcCount == 1 && srcMod.plugin_) srcMod.plugin_->outputEnabled(src.chIdx_, false);
    if (destCount == 1 && destMod.plugin_) destMod.plugin_->inputEnabled(dest.chIdx_, false);
}


bool Track::requestMatrixAttenuate(const Matrix::Jack& src, const Matrix::Jack& dest, bool isOffset, float delta) {
    if (!lock_.test_and_set()) {
        for (auto& w : matrix_.connections_) {
            if (w.src_ == src && w.dest_ == dest) {
                if (isOffset) {
                    w.offset_ += delta;
                } else {
                    w.gain_ += delta;
                }
                lock_.clear();
                return true;
            }
        }
        lock_.clear();
        return true;
    }
    return false;
}


void Track::prepare(int sampleRate, int blockSize) {
    blockSize_ = blockSize;
    sampleRate_ = sampleRate;

    for (auto& m : modules_) { m.prepare(sampleRate_, blockSize_); }
}

void Track::process(juce::AudioSampleBuffer& ioBuffer) {
    if (!lock_.test_and_set()) {
        size_t n = blockSize_;
        auto& inMod = modules_[M_IN];
        for (int c = 0; c < MAX_IO_IN; c++) { inMod.audioBuffer_.copyFrom(c, 0, ioBuffer, c, 0, n); }
        auto& outMod = modules_[M_OUT];
        outMod.audioBuffer_.clear();

        // routing decides the order, not slot position; only wires on a cycle read the previous block
        bool adj[M_MAX][M_MAX] = {};
        for (auto& w : matrix_.connections_) {
            if (w.src_.modIdx_ < M_MAX && w.dest_.modIdx_ < M_MAX) adj[w.src_.modIdx_][w.dest_.modIdx_] = true;
        }

        for (unsigned modIdx : rack::executionOrder(adj)) {
            auto& m = modules_[modIdx];
            // a module being loaded may resize its buffer, so leave it untouched; it has no wires
            if (m.lock_.test_and_set()) continue;
            auto& moduleBuf = m.audioBuffer_;
            if (modIdx != M_IN) moduleBuf.clear();
            for (auto& route : matrix_.connections_) {
                if (route.dest_.modIdx_ == modIdx) {
                    auto& srcBuf = modules_[route.src_.modIdx_].audioBuffer_;
                    float gain = route.gain_;
                    float offset = route.offset_;
                    moduleBuf.addFrom(route.dest_.chIdx_, 0, srcBuf, route.src_.chIdx_, 0, n, gain);
                    if (offset != 0.0f) {
                        auto buf = moduleBuf.getWritePointer(route.dest_.chIdx_, 0);
                        juce::FloatVectorOperations::add(buf, offset, n);
                    }
                }
            }

            m.process(moduleBuf);
            m.lock_.clear();
        }

        if (!mute()) {
            for (int c = 0; c < MAX_IO_OUT; c++) { ioBuffer.copyFrom(c, 0, outMod.audioBuffer_, c, 0, n); }
        } else {
            // muted, so don't process
            ioBuffer.applyGain(0.0f);
        }
        lock_.clear();
    }
}

// form juce_AudioProcessor.cpp
const juce::uint32 magicXmlNumber = 0x21324356;
void copyXmlToBinary(const juce::XmlElement& xml, juce::MemoryBlock& destData) {
    {
        juce::MemoryOutputStream out(destData, false);
        out.writeInt(magicXmlNumber);
        out.writeInt(0);
        xml.writeTo(out, juce::XmlElement::TextFormat().singleLine());
        out.writeByte(0);
    }

    // go back and write the string length..
    static_cast<juce::uint32*>(destData.getData())[1] =
        juce::ByteOrder::swapIfBigEndian((juce::uint32)destData.getSize() - 9);
}

std::unique_ptr<juce::XmlElement> getXmlFromBinary(const void* data, const int sizeInBytes) {
    if (sizeInBytes > 8 && juce::ByteOrder::littleEndianInt(data) == magicXmlNumber) {
        auto stringLength = (int)juce::ByteOrder::littleEndianInt(juce::addBytesToPointer(data, 4));

        if (stringLength > 0)
            return parseXML(juce::String::fromUTF8(static_cast<const char*>(data) + 8,
                                                   juce::jmin((sizeInBytes - 8), stringLength)));
    }
    return {};
}


void Track::getStateInformation(juce::XmlElement& outStream) {
    outStream.setAttribute("mute", mute());
    outStream.setAttribute("level", level());

    std::unique_ptr<juce::XmlElement> xmlModules = std::make_unique<juce::XmlElement>("Modules");

    for (auto& m : modules_) {
        std::unique_ptr<juce::XmlElement> xmlModule = std::make_unique<juce::XmlElement>("Module");

        auto& plugin = m.plugin_;
        if (!plugin) {
            xmlModule->setAttribute("pluginName", "");
            xmlModule->setAttribute("dataSz", (int)0);
        } else {
            void* data;
            size_t dataSz;
            plugin->getState(&data, &dataSz);
            xmlModule->setAttribute("pluginName", m.pluginName_.c_str());
            xmlModule->setAttribute("dataSz", (int)dataSz);

            if (dataSz > 0 && data) {
                std::unique_ptr<juce::XmlElement> xmlPlugData = std::make_unique<juce::XmlElement>("data");
                auto pluginData = getXmlFromBinary(data, dataSz);
                if (pluginData) {
                    xmlPlugData->addChildElement(pluginData.release());
                    xmlModule->addChildElement(xmlPlugData.release());
                }
            }
        }

        xmlModules->addChildElement(xmlModule.release());
    }
    outStream.addChildElement(xmlModules.release());

    std::unique_ptr<juce::XmlElement> xmlMatrix = std::make_unique<juce::XmlElement>("Matrix");
    matrix_.getStateInformation(*xmlMatrix);
    outStream.addChildElement(xmlMatrix.release());
}

void Track::setStateInformation(juce::XmlElement& inStream) {
    mute_ = inStream.getBoolAttribute("mute", false);
    level_ = inStream.getDoubleAttribute("level", 1.0f);

    auto xmlModules = inStream.getChildByName("Modules");
    if (xmlModules) {
        int midx = 0;
        for (auto xmlModule : xmlModules->getChildIterator()) {
            juce::String pluginName = xmlModule->getStringAttribute("pluginName");
            int size = xmlModule->getIntAttribute("dataSz");

            if (!pluginName.isEmpty() && size > 0) {
                while (!requestModuleChange(midx, pluginName.toStdString())) {}

                auto& plugin = modules_[midx].plugin_;
                if (plugin) {
                    auto xmlPlugData = xmlModule->getChildByName("data");
                    if (xmlPlugData) {
                        juce::MemoryBlock moduleData;
                        auto pluginData = xmlPlugData->getFirstChildElement();
                        if (pluginData) {
                            copyXmlToBinary(*pluginData, moduleData);
                            plugin->setState(moduleData.getData(), moduleData.getSize());
                        }
                    }
                }
            }
            midx++;
            if (midx >= M_MAX) break;
        }
    } else {
        ssp::log("setStateInformation : no Modules tag");
    }

    auto xmlMatrix = inStream.getChildByName("Matrix");
    if (xmlMatrix) {
        // parse aside: process reads matrix_, which changes only under the track lock
        Matrix restored;
        restored.setStateInformation(*xmlMatrix);
        while (lock_.test_and_set()) {}
        matrix_.clear();
        lock_.clear();

        // we need to use requestMatrixConnect, as this will update the plugin connections
        for (auto& w : restored.connections_) {
            while (!requestMatrixConnect(w.src_, w.dest_, w.gain_, w.offset_));
        }

    } else {
        ssp::log("setStateInformation : no Matrix tag");
    }

    // channel enables are set under the track lock elsewhere (requestMatrixConnect), so here too
    while (lock_.test_and_set()) {}
    for (int midx = 0; midx < M_MAX; midx++) { resetModuleConnections(midx); }
    lock_.clear();
}


void Track::alloc(int sampleRate, int blockSize) {
    prepare(sampleRate, blockSize);
}


void Track::free() {
}

void Track::resetModuleConnections(int midx) {
    auto& m = modules_[midx];
    if (m.descriptor_ && m.plugin_) {
        int inSz = m.descriptor_->inputChannelNames.size();
        for (int c = 0; c < inSz; c++) { m.plugin_->inputEnabled(c, false); }
        int outSz = m.descriptor_->outputChannelNames.size();
        for (int c = 0; c < outSz; c++) { m.plugin_->outputEnabled(c, false); }

        for (auto& w : matrix_.connections_) {
            if (w.dest_.modIdx_ == midx && w.dest_.chIdx_ < inSz) m.plugin_->inputEnabled(w.dest_.chIdx_, true);
            if (w.src_.modIdx_ == midx && w.src_.chIdx_ < outSz) m.plugin_->outputEnabled(w.src_.chIdx_, true);
        }
    }
}


void Track::clearModuleConnections(int midx) {
    auto& wires = matrix_.connections_;
    wires.erase(
        std::remove_if(wires.begin(), wires.end(),
                       [&](const Matrix::Wire& w) { return w.src_.modIdx_ == midx || w.dest_.modIdx_ == midx; }),
        wires.end());
}

// --- JSON presets ------------------------------------------------------------
// The XML path above restores a preset rack wrote, so it can trust it. A JSON preset is
// hand-written, so every lookup here reports what it rejected rather than dropping it.

static bool setParameter(SSPExtendedApi::PluginInterface* plugin, const juce::String& key, float value,
                         const juce::String& where) {
    unsigned count = plugin->numberOfParameters();
    int byId = -1, byLowerCaseName = -1;

    for (unsigned idx = 0; idx < count; idx++) {
        SSPExtendedApi::PluginInterface::ParameterDesc desc;
        if (!plugin->parameterDesc(idx, desc)) continue;

        if (key == desc.name_.c_str()) {
            plugin->parameterValue(idx, value);
            return true;
        }
        if (byId < 0 && key == desc.id_.c_str()) byId = (int)idx;
        if (byLowerCaseName < 0 && key.equalsIgnoreCase(desc.name_.c_str())) byLowerCaseName = (int)idx;
    }

    int idx = byId >= 0 ? byId : byLowerCaseName;
    if (idx < 0) {
        jsonpreset::logError(where + " : no parameter named " + key.quoted());
        return false;
    }
    plugin->parameterValue((unsigned)idx, value);
    return true;
}

void Track::setStateInformation(const juce::var& track, int trackIdx) {
    juce::String where = "track " + juce::String(trackIdx + 1);

    auto* object = track.getDynamicObject();
    if (object == nullptr) {
        jsonpreset::logError(where + " : expected an object");
        return;
    }

    mute_ = (bool)object->getProperty("mute");
    level_ = object->hasProperty("level") ? (float)(double)object->getProperty("level") : 1.0f;

    // Modules load first: both the matrix and the parameters need the plugin instance.
    auto moduleList = object->getProperty("modules");
    if (auto* modules = moduleList.getDynamicObject()) {
        for (const auto& entry : modules->getProperties()) {
            auto key = entry.name.toString();
            int midx = jsonpreset::slotIndex(key);
            if (midx == jsonpreset::BAD_INDEX || midx == M_IN || midx == M_OUT) {
                jsonpreset::logError(where + " : cannot load a module into slot " + key.quoted());
                continue;
            }
            auto name = entry.value.toString();
            while (!requestModuleChange(midx, name.toStdString())) {}
            if (modules_[midx].plugin_ == nullptr) {
                jsonpreset::logError(where + " slot " + key + " : cannot load module " + name.quoted());
            }
        }
    }

    // Applied before the named parameters, which override it. Written by the JSON save for
    // settings that are not parameters, such as MIDI assignments.
    auto stateList = object->getProperty("state");
    if (auto* states = stateList.getDynamicObject()) {
        for (const auto& entry : states->getProperties()) {
            auto key = entry.name.toString();
            int midx = jsonpreset::slotIndex(key);
            auto* plugin = (midx == jsonpreset::BAD_INDEX) ? nullptr : modules_[midx].plugin_;
            if (plugin == nullptr) {
                jsonpreset::logError(where + " : state for slot " + key.quoted() + " with no module loaded");
                continue;
            }
            juce::MemoryBlock blob;
            juce::MemoryOutputStream stream(blob, false);
            if (!juce::Base64::convertFromBase64(stream, entry.value.toString())) {
                jsonpreset::logError(where + " slot " + key + " : state is not valid base64");
                continue;
            }
            stream.flush();
            plugin->setState(blob.getData(), blob.getSize());
        }
    }

    auto paramList = object->getProperty("params");
    if (auto* params = paramList.getDynamicObject()) {
        for (const auto& entry : params->getProperties()) {
            auto key = entry.name.toString();
            int midx = jsonpreset::slotIndex(key);
            auto* plugin = (midx == jsonpreset::BAD_INDEX) ? nullptr : modules_[midx].plugin_;
            if (plugin == nullptr) {
                jsonpreset::logError(where + " : parameters for slot " + key.quoted() + " with no module loaded");
                continue;
            }
            auto* values = entry.value.getDynamicObject();
            if (values == nullptr) {
                jsonpreset::logError(where + " slot " + key + " : params must be an object");
                continue;
            }
            for (const auto& param : values->getProperties()) {
                setParameter(plugin, param.name.toString(), (float)(double)param.value,
                             where + " slot " + key);
            }
        }
    }

    // requestMatrixConnect drops an out-of-range channel without a word, so resolve and report
    // here. A channel is an index or one of the module's channel names.
    auto connect = [&](const jsonpreset::Wire& wire, const juce::String& label) {
        auto& src = modules_[wire.src.slot];
        auto& dest = modules_[wire.dest.slot];
        if (src.descriptor_ == nullptr || dest.descriptor_ == nullptr) {
            jsonpreset::logError(where + " : wire " + label.quoted() + " touches an empty slot");
            return;
        }

        int srcCh = jsonpreset::channelIndex(wire.src.channel, src.descriptor_->outputChannelNames);
        int destCh = jsonpreset::channelIndex(wire.dest.channel, dest.descriptor_->inputChannelNames);
        if (srcCh == jsonpreset::BAD_INDEX) {
            jsonpreset::logError(where + " : slot " + jsonpreset::slotName(wire.src.slot) + " has no output " +
                                 wire.src.channel.quoted());
            return;
        }
        if (destCh == jsonpreset::BAD_INDEX) {
            jsonpreset::logError(where + " : slot " + jsonpreset::slotName(wire.dest.slot) + " has no input " +
                                 wire.dest.channel.quoted());
            return;
        }

        while (!requestMatrixConnect(Matrix::Jack(wire.src.slot, srcCh), Matrix::Jack(wire.dest.slot, destCh),
                                     wire.gain, wire.offset)) {}
    };

    auto wires = object->getProperty("wires");
    if (auto* list = wires.getArray()) {
        for (const auto& item : *list) {
            auto wire = jsonpreset::parseWire(item);
            if (!wire.valid()) {
                jsonpreset::logError(where + " : cannot read wire " + item.toString().quoted());
                continue;
            }
            connect(wire, item.toString());
        }
    } else if (!wires.isVoid()) {
        jsonpreset::logError(where + " : wires must be an array");
    }

    auto matrix = object->getProperty("matrix");
    if (!matrix.isVoid()) {
        juce::String error;
        auto matrixWires = jsonpreset::parseMatrix(matrix, error);
        if (error.isNotEmpty()) jsonpreset::logError(where + " " + error);
        for (auto& wire : matrixWires) {
            connect(wire, jsonpreset::slotName(wire.src.slot) + ":" + wire.src.channel + " -> " +
                              jsonpreset::slotName(wire.dest.slot) + ":" + wire.dest.channel);
        }
    }

    while (lock_.test_and_set()) {}
    for (int midx = 0; midx < M_MAX; midx++) { resetModuleConnections(midx); }
    lock_.clear();
}


// Anything the module holds that is not a parameter: MIDI assignments, and whatever a module
// writes through customToXml. The parameters are dropped, because the preset carries those by
// name; what is left is opaque, so it goes in as base64. Empty when there is nothing to keep.
static juce::String nonParameterState(SSPExtendedApi::PluginInterface* plugin) {
    void* data = nullptr;
    size_t size = 0;
    plugin->getState(&data, &size);
    if (data == nullptr || size == 0) return {};

    auto xml = getXmlFromBinary(data, (int)size);
    delete[] (char*)data;
    if (xml == nullptr) return {};

    xml->deleteAllChildElementsWithTagName("state");

    bool empty = true;
    for (auto* child : xml->getChildIterator()) {
        if (child->getNumAttributes() + child->getNumChildElements() > 0) empty = false;
    }
    if (empty) return {};

    juce::MemoryBlock blob;
    copyXmlToBinary(*xml, blob);
    return juce::Base64::toBase64(blob.getData(), blob.getSize());
}

void Track::getStateInformation(juce::var& out) {
    auto object = new juce::DynamicObject();
    object->setProperty("level", level());
    if (mute()) object->setProperty("mute", true);

    auto modules = new juce::DynamicObject();
    auto params = new juce::DynamicObject();
    // held in a var from the start: it is only attached to the track when it has content
    juce::var states(new juce::DynamicObject());

    for (int midx = M_SLOT_1; midx < M_OUT; midx++) {
        auto& module = modules_[midx];
        auto* plugin = module.plugin_;
        if (plugin == nullptr) continue;

        auto key = jsonpreset::slotName(midx);
        modules->setProperty(key, juce::String(module.pluginName_));

        auto values = new juce::DynamicObject();
        unsigned count = plugin->numberOfParameters();
        for (unsigned idx = 0; idx < count; idx++) {
            SSPExtendedApi::PluginInterface::ParameterDesc desc;
            if (!plugin->parameterDesc(idx, desc)) continue;
            auto name = desc.name_.empty() ? desc.id_ : desc.name_;
            values->setProperty(juce::String(name), plugin->parameterValue(idx));
        }
        params->setProperty(key, juce::var(values));

        auto state = nonParameterState(plugin);
        if (state.isNotEmpty()) states.getDynamicObject()->setProperty(key, state);
    }

    object->setProperty("modules", juce::var(modules));

    // routing is saved as a matrix; jacks are channel indices, which stay exact if names repeat
    std::vector<jsonpreset::Wire> wires;
    for (auto& w : matrix_.connections_) {
        wires.push_back({ { (int)w.src_.modIdx_, juce::String(w.src_.chIdx_) },
                          { (int)w.dest_.modIdx_, juce::String(w.dest_.chIdx_) },
                          w.gain_,
                          w.offset_ });
    }
    auto matrix = jsonpreset::formatMatrix(wires);
    if (matrix.isObject()) object->setProperty("matrix", matrix);
    object->setProperty("params", juce::var(params));
    if (states.getDynamicObject()->getProperties().size() > 0) object->setProperty("state", states);

    out = juce::var(object);
}
