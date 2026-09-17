
#include "PluginProcessor.h"

#include "JsonPreset.h"
#include "Matrix.h"
#include "PluginEditor.h"
#include "ssp/EditorHost.h"
#include "ssp/Log.h"

PluginProcessor::PluginProcessor() : PluginProcessor(getBusesProperties(), createParameterLayout()) {
}

PluginProcessor::PluginProcessor(const AudioProcessor::BusesProperties& ioLayouts,
                                 AudioProcessorValueTreeState::ParameterLayout layout)
    : BaseProcessor(ioLayouts, std::move(layout)) {
    init();

    int trackIdx = 0;
    for (auto& track : tracks_) {
        // create thread for each track, place into trackThreads_
        trackThreads_.emplace_back([this, trackIdx] {
            bool running = true;
            ssp::log("trax thread create : " + std::to_string(trackIdx));
            while (running) {
                std::unique_lock<std::mutex> lk(tracks_[trackIdx].mutex_);
                tracks_[trackIdx].cv_.wait(lk, [this, trackIdx] { return tracks_[trackIdx].ready_; });
                tracks_[trackIdx].ready_ = false;
                if (!shouldExit()) {
                    processTrack(tracks_[trackIdx], threadBuffers_[trackIdx]);
                } else {
                    running = false;
                }
                tracks_[trackIdx].processed_ = true;
                tracks_[trackIdx].exited_ = !running;
                lk.unlock();
                tracks_[trackIdx].cv_.notify_one();
            }
            ssp::log("trax thread exit : " + std::to_string(trackIdx));
        });
        trackIdx++;
    }
#ifndef __APPLE__
    for (int i = 0; i < trackThreads_.size(); i++) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        int cpu = i % 2 == 0 ? 2 : 3;
        CPU_SET(cpu, &cpuset);
        int rc = pthread_setaffinity_np(trackThreads_[i].native_handle(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0) {
            ssp::log("Trax : Error calling pthread_setaffinity_np: " + std::to_string(rc));
        } else {
            ssp::log("Trax : Set affinity for dsp thread " + std::to_string(i) + " to CPU " + std::to_string(cpu));
        }
    }
#endif

    for (auto& t : trackThreads_) { t.detach(); }
}

bool PluginProcessor::shouldExit() {
    return shouldExit_;
}

PluginProcessor::~PluginProcessor() {
    shouldExit_ = true;

    int trackCnt = 0;
    for (auto& track : tracks_) {
        std::lock_guard lk(track.mutex_);
        track.ready_ = true;
        track.cv_.notify_one();
        // ssp::log("ssp thread :" + std::to_string(trackCnt++));
    }

    for (auto& track : tracks_) {
        std::unique_lock<std::mutex> lk(track.mutex_);
        track.cv_.wait(lk, [&track] { return track.exited_; });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // ssp::log("ssp thread :" + std::to_string(--trackCnt) + " : " + std::to_string(track.processed_) + "," +
        // std::to_string(track.ready_));
    }


    // clear tracks etc
    initPreset();
}


bool PluginProcessor::requestModuleChange(unsigned t, unsigned m, const std::string& mn) {
    if (t >= MAX_TRACKS || m >= Track::M_MAX) return false;
    auto& track = tracks_[t];
    bool ret = track.requestModuleChange(m, mn);
    if (ret) { while (!removePerformanceParam(t, m)); }
    return ret;
}


void PluginProcessor::loadSupportedModules(bool force) {
    if (!force) {
        if (supportedModules_.empty()) { Module::loadSupportedModules(supportedModules_); }
    }

    if (force || supportedModules_.empty()) {
        ssp::log("plugin scan - start");
        Module::scanPlugins(supportedModules_);
        for (auto m : supportedModules_) { ssp::log("module: " + m.name); }
        ssp::log("plugin scan - send");
    }
}


AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    BaseProcessor::addBaseParameters(params);
    return params;
}

const String PluginProcessor::getInputBusName(int channelIndex) {
    return String("In ") + String(channelIndex + 1);
}


const String PluginProcessor::getOutputBusName(int channelIndex) {
    return String("Out ") + String(channelIndex + 1);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    BaseProcessor::prepareToPlay(sampleRate, samplesPerBlock);

    for (int i = 0; i < MAX_TRACKS; i++) { threadBuffers_[i].setSize(IO_MAX, samplesPerBlock); }
    for (auto& track : tracks_) { prepareTrack(track, sampleRate, samplesPerBlock); }
}

void PluginProcessor::prepareTrack(Track& track, double sampleRate, int samplesPerBlock) {
    track.prepare(sampleRate, samplesPerBlock);
}


void PluginProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages) {
    BaseProcessor::processBlock(buffer, midiMessages);
    if (shouldExit_) return;

    auto n = buffer.getNumSamples();

    // process thread
    int trackIdx = 0;
    for (auto& track : tracks_) {
        auto& tBuf = threadBuffers_[trackIdx];
        for (int c = 0; c < I_MAX; c++) { tBuf.copyFrom(c, 0, buffer, c, 0, n); }

        std::lock_guard lk(track.mutex_);
        track.processed_ = false;
        track.ready_ = true;
        track.cv_.notify_one();
        trackIdx++;
    }

    for (auto& track : tracks_) {
        std::unique_lock<std::mutex> lk(track.mutex_);
        track.cv_.wait(lk, [&track] { return track.processed_ || track.exited_; });
        if (track.exited_) return;
    }


    // pluginprocessr thread
    // once all process done... sum outputs for mix
    buffer.applyGain(0.0f);  // zero it out
    // static constexpr float MIX_CHANNEL_GAIN = 1.0f / float(MAX_TRACKS);
    static constexpr float MIX_CHANNEL_GAIN = 1.0f;
    trackIdx = 0;
    for (auto& track : tracks_) {
        auto& tBuf = threadBuffers_[trackIdx];
        float gain = track.level();

        for (int i = 0; i < O_MAX; i++) {
            tBuf.applyGainRamp(i, 0, n, lastGain_[trackIdx], gain);
            rmsData_[trackIdx][i].process(tBuf, i);
            buffer.addFrom(i, 0, tBuf, i, 0, n, MIX_CHANNEL_GAIN);
        }
        lastGain_[trackIdx] = gain;
        trackIdx++;
    }
}

void PluginProcessor::processTrack(Track& track, AudioSampleBuffer& ioBuffer) {
    track.process(ioBuffer);
}

void PluginProcessor::onInputChanged(unsigned i, bool b) {
    BaseProcessor::onInputChanged(i, b);
}

void PluginProcessor::onOutputChanged(unsigned i, bool b) {
    BaseProcessor::onOutputChanged(i, b);
}


static constexpr int checkBytes = 0x1FF1;
static constexpr int protoVersion = 0x0001;

static const char* TRAX_XML_TAG = "TRAX";
static const char* TRACKS_XML_TAG = "Tracks";
static const char* TRACK_XML_TAG = "Track";
static const char* PERF_XML_TAG = "PerformParams";
static const char* PERF_PARAM_XML_TAG = "Param";


void PluginProcessor::getStateInformation(MemoryBlock& destData) {
    std::unique_ptr<juce::XmlElement> xmlTrax = std::make_unique<juce::XmlElement>(TRAX_XML_TAG);

    std::unique_ptr<juce::XmlElement> xmlTracks = std::make_unique<juce::XmlElement>(TRACKS_XML_TAG);
    for (auto& track : tracks_) {
        std::unique_ptr<juce::XmlElement> xmlTrack = std::make_unique<juce::XmlElement>(TRACK_XML_TAG);
        track.getStateInformation(*xmlTrack);
        xmlTracks->addChildElement(xmlTrack.release());
    }
    xmlTrax->addChildElement(xmlTracks.release());

    std::unique_ptr<juce::XmlElement> xmlPerf = std::make_unique<juce::XmlElement>(PERF_XML_TAG);
    for (auto& param : performanceParams_) {
        std::unique_ptr<juce::XmlElement> xmlParam = std::make_unique<juce::XmlElement>(PERF_PARAM_XML_TAG);
        xmlParam->setAttribute("track", (int)param.trackIdx());
        xmlParam->setAttribute("module", (int)param.moduleIdx());
        xmlParam->setAttribute("param", (int)param.paramIdx());
        xmlPerf->addChildElement(xmlParam.release());
    }
    xmlTrax->addChildElement(xmlPerf.release());

    // ssp::log("getStateInformation : " + xmlTrax->toString().toStdString());
    copyXmlToBinary(*xmlTrax, destData);
}


void PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
    initPreset();

    loadSupportedModules();

    std::unique_ptr<juce::XmlElement> xmlTrax(getXmlFromBinary(data, sizeInBytes));

    if (xmlTrax != nullptr && xmlTrax->hasTagName(TRAX_XML_TAG)) {
        // ssp::log("setStateInformation : " + xmlTrax->toString().toStdString());
        auto xmlTracks = xmlTrax->getChildByName(TRACKS_XML_TAG);
        if (xmlTracks != nullptr) {
            int trackIdx = 0;
            for (auto xmlTrack : xmlTracks->getChildIterator()) {
                tracks_[trackIdx].setStateInformation(*xmlTrack);
                trackIdx++;
                if (trackIdx >= MAX_TRACKS) break;
            }
        } else {
            ssp::log("setStateInformation : no TRACKS_XML_TAG tag");
        }

        auto xmlPerf = xmlTrax->getChildByName(PERF_XML_TAG);
        if (xmlPerf != nullptr) {
            performanceParams_.clear();
            for (auto xmlParam : xmlPerf->getChildIterator()) {
                unsigned t = xmlParam->getIntAttribute("track");
                unsigned m = xmlParam->getIntAttribute("module");
                unsigned p = xmlParam->getIntAttribute("param");

                addPerformanceParam(t, m, p, "setStateInformation");
            }
        } else {
            ssp::log("setStateInformation : no PERF_XML_TAG tag");
        }
    } else {
        ssp::log("setStateInformation : no TRAX_XML_TAG tag");
    }
}
void PluginProcessor::initPreset() {
    int trackIdx = 0;
    for (auto& track : tracks_) {
        while (!track.requestClearTrack()) {}
        while (!removePerformanceParam(trackIdx)) {}
        track.mute(false);
        track.level(1.f);
        trackIdx++;
    }
}

void PluginProcessor::savePreset() {
    ssp::log("savePreset : " + requestedPreset_.toStdString());
    presetName_ = requestedPreset_;
    requestPresetSave_ = false;

    File f(presetName_);

    if (jsonpreset::isJsonName(presetName_)) {
        saveJsonPreset(f);
        return;
    }

    MemoryBlock destData;
    getStateInformation(destData);

    f.replaceWithData(destData.getData(), destData.getSize());
}


// Chosen by naming the preset with a .json suffix. The binary format stays the default
// because it is what trax has always written, and an older build cannot read JSON.
void PluginProcessor::saveJsonPreset(const File& f) {
    auto doc = new DynamicObject();
    doc->setProperty("trax", 1);

    Array<var> tracks;
    for (auto& track : tracks_) {
        var entry;
        track.getStateInformation(entry);
        tracks.add(entry);
    }
    doc->setProperty("tracks", tracks);

    Array<var> performance;
    for (auto& param : performanceParams_) {
        auto entry = new DynamicObject();
        entry->setProperty("track", (int)param.trackIdx() + 1);
        entry->setProperty("slot", jsonpreset::slotName(param.moduleIdx()));
        entry->setProperty("param", (int)param.paramIdx());
        performance.add(var(entry));
    }
    if (!performance.isEmpty()) doc->setProperty("performance", performance);

    if (!f.replaceWithText(JSON::toString(var(doc)) + "\n")) {
        jsonpreset::logError("cannot write " + f.getFileName());
    }
}

void PluginProcessor::loadPreset() {
    ssp::log("loadPreset : " + requestedPreset_.toStdString());
    presetName_ = requestedPreset_;
    requestPresetLoad_ = false;

    File f(presetName_);
    if (!f.exists()) return;

    if (jsonpreset::isJsonFile(f)) {
        auto doc = JSON::parse(f);
        if (doc.isVoid()) {
            jsonpreset::logError("cannot parse " + f.getFileName());
            return;
        }
        loadJsonPreset(doc);
        return;
    }

    MemoryMappedFile mmf(f, MemoryMappedFile::AccessMode::readOnly, false);
    setStateInformation(mmf.getData(), mmf.getSize());
}


// Hand-written presets, described in ../README.md. Unlike the XML above this is
// not a preset trax wrote, so each track reports what it could not apply rather than
// failing the whole load.
void PluginProcessor::loadJsonPreset(const juce::var& doc) {
    initPreset();
    loadSupportedModules();

    auto tracks = doc["tracks"];
    if (auto* list = tracks.getArray()) {
        for (int trackIdx = 0; trackIdx < list->size() && trackIdx < (int)MAX_TRACKS; trackIdx++) {
            tracks_[trackIdx].setStateInformation((*list)[trackIdx], trackIdx);
        }
        if (list->size() > (int)MAX_TRACKS) {
            jsonpreset::logError(String(list->size()) + " tracks : trax has " + String((int)MAX_TRACKS));
        }
    } else if (auto* object = tracks.getDynamicObject()) {
        for (const auto& entry : object->getProperties()) {
            auto key = entry.name.toString();
            int trackIdx = key.getIntValue() - 1;
            if (!key.containsOnly("0123456789") || trackIdx < 0 || trackIdx >= (int)MAX_TRACKS) {
                jsonpreset::logError("no track " + key.quoted());
                continue;
            }
            tracks_[trackIdx].setStateInformation(entry.value, trackIdx);
        }
    } else if (!tracks.isVoid()) {
        jsonpreset::logError("tracks must be an array or an object");
    }

    auto performance = doc["performance"];
    if (auto* list = performance.getArray()) {
        for (const auto& item : *list) {
            auto* entry = item.getDynamicObject();
            if (entry == nullptr) {
                jsonpreset::logError("performance : expected an object");
                continue;
            }
            int t = (int)entry->getProperty("track") - 1;
            int m = jsonpreset::slotIndex(entry->getProperty("slot").toString());
            int p = (int)entry->getProperty("param");
            if (t < 0 || t >= (int)MAX_TRACKS || m == jsonpreset::BAD_INDEX || p < 0) {
                jsonpreset::logError("performance : bad track, slot or param");
                continue;
            }
            addPerformanceParam(t, m, p, "json preset");
        }
    } else if (!performance.isVoid()) {
        jsonpreset::logError("performance must be an array");
    }
}


bool PluginProcessor::addPerformanceParam(const PerformanceParam& p) {
    performanceParams_.push_back(p);
    return true;
}

bool PluginProcessor::addPerformanceParam(unsigned t, unsigned m, unsigned p, const juce::String& where) {
    auto plugin = getPlugin(t, m);
    if (!plugin) {
        ssp::log(where.toStdString() + " : cannot add performance param, no plugin for track " + std::to_string(t) +
                 " module " + std::to_string(m));
        return false;
    }
    if (p >= plugin->numberOfParameters()) {
        ssp::log(where.toStdString() + " : cannot add performance param, param index out of range for track " +
                 std::to_string(t) + " module " + std::to_string(m));
        return false;
    }

    SSPExtendedApi::PluginInterface::ParameterDesc desc;
    if (!plugin->parameterDesc(p, desc)) return false;

    addPerformanceParam(PerformanceParam(t, m, p, getLoadedPlugin(t, m), desc.name_, desc.min_, desc.max_, desc.def_,
                                         desc.numSteps_, desc.isDescrete_));
    return true;
}
bool PluginProcessor::removePerformanceParam(const PerformanceParam& p) {
    // remove from vector if track, module and param ids match
    performanceParams_.erase(std::remove_if(performanceParams_.begin(), performanceParams_.end(),
                                            [&p](const PerformanceParam& pp) {
                                                return pp.trackIdx() == p.trackIdx() &&
                                                       pp.moduleIdx() == p.moduleIdx() && pp.paramIdx() == p.paramIdx();
                                            }),
                             performanceParams_.end());
    return true;
}

bool PluginProcessor::removePerformanceParam(unsigned t, unsigned m) {
    // remove from vector if track, module ids match
    performanceParams_.erase(
        std::remove_if(performanceParams_.begin(), performanceParams_.end(),
                       [t, m](const PerformanceParam& pp) { return pp.trackIdx() == t && pp.moduleIdx() == m; }),
        performanceParams_.end());
    return true;
}
bool PluginProcessor::removePerformanceParam(unsigned t) {
    // remove from vector if track id matches
    performanceParams_.erase(std::remove_if(performanceParams_.begin(), performanceParams_.end(),
                                            [t](const PerformanceParam& pp) { return pp.trackIdx() == t; }),
                             performanceParams_.end());
    return true;
}


AudioProcessorEditor* PluginProcessor::createEditor() {
    static constexpr bool useSysEditor = false, defaultDraw = false;
#ifdef FORCE_COMPACT_UI
    return new ssp::EditorHost(this, new PluginEditor(*this), true, useSysEditor, defaultDraw);
#else
    return new ssp::EditorHost(this, new PluginEditor(*this), useCompactUI(), useSysEditor, defaultDraw);
#endif
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
