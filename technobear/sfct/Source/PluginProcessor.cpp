#include "PluginProcessor.h"

#include <chrono>

#include "PluginEditor.h"
#include "PluginMiniEditor.h"
#include "ssp/EditorHost.h"

// loop points range over the buffer at 48 kHz (43.7 s); the engine clamps at other rates
static constexpr float MAX_SECONDS = 43.0f;

static String getVoicePid(unsigned v) {
    return "v" + String(v + 1);
}

static String getVoiceParamId(unsigned v, StringRef id) {
    return getVoicePid(v) + String(ID::separator) + id;
}

// "link12" .. "link78"
static String getLinkId(unsigned pair) {
    return String(ID::link) + String(pair * 2 + 1) + String(pair * 2 + 2);
}

PluginProcessor::PluginProcessor() : PluginProcessor(getBusesProperties(), createParameterLayout()) {
}

PluginProcessor::PluginProcessor(const AudioProcessor::BusesProperties& ioLayouts,
                                 AudioProcessorValueTreeState::ParameterLayout layout)
    : BaseProcessor(ioLayouts, std::move(layout)), params_(vts()), modeParam_(*vts().getParameter(ID::mode)) {
    init();
    formatManager_.registerBasicFormats();
    for (unsigned v = 0; v < VOICES; v++)
        for (auto* p : getVoice(v).params()) vts().addParameterListener(p->paramID, this);
    for (unsigned pair = 0; pair < PAIRS; pair++) vts().addParameterListener(getLinkId(pair), this);
    vts().addParameterListener(ID::mode, this);
}

PluginProcessor::~PluginProcessor() {
    if (saveThread_.joinable()) saveThread_.join();  // finishes within its 5 s timeout
    for (unsigned v = 0; v < VOICES; v++)
        for (auto* p : getVoice(v).params()) vts().removeParameterListener(p->paramID, this);
    for (unsigned pair = 0; pair < PAIRS; pair++) vts().removeParameterListener(getLinkId(pair), this);
    vts().removeParameterListener(ID::mode, this);
}

PluginProcessor::Voice::Voice(AudioProcessorValueTreeState& apvt, unsigned v)
    : on(*apvt.getParameter(getVoiceParamId(v, ID::on))),
      play(*apvt.getParameter(getVoiceParamId(v, ID::play))),
      rec(*apvt.getParameter(getVoiceParamId(v, ID::rec))),
      loop(*apvt.getParameter(getVoiceParamId(v, ID::loop))),
      cut(*apvt.getParameter(getVoiceParamId(v, ID::cut))),
      rate(*apvt.getParameter(getVoiceParamId(v, ID::rate))),
      start(*apvt.getParameter(getVoiceParamId(v, ID::start))),
      end(*apvt.getParameter(getVoiceParamId(v, ID::end))),
      level(*apvt.getParameter(getVoiceParamId(v, ID::level))),
      rec_level(*apvt.getParameter(getVoiceParamId(v, ID::rec_level))),
      pre_level(*apvt.getParameter(getVoiceParamId(v, ID::pre_level))),
      in_gain(*apvt.getParameter(getVoiceParamId(v, ID::in_gain))),
      pan(*apvt.getParameter(getVoiceParamId(v, ID::pan))),
      fade(*apvt.getParameter(getVoiceParamId(v, ID::fade))),
      slew(*apvt.getParameter(getVoiceParamId(v, ID::slew))),
      lpf(*apvt.getParameter(getVoiceParamId(v, ID::lpf))),
      lp_mix(*apvt.getParameter(getVoiceParamId(v, ID::lp_mix))),
      link(*apvt.getParameter(getLinkId(v / 2))) {
}

std::vector<RangedAudioParameter*> PluginProcessor::Voice::params() {
    return { &on,        &play,      &rec,     &loop, &cut,  &rate, &start, &end,   &level,
             &rec_level, &pre_level, &in_gain, &pan,  &fade, &slew, &lpf,   &lp_mix };
}

PluginProcessor::PluginParams::PluginParams(AudioProcessorValueTreeState& apvt) {
    for (unsigned v = 0; v < VOICES; v++) { voices_.push_back(std::make_unique<Voice>(apvt, v)); }
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    BaseProcessor::addBaseParameters(params);

    // Track 1 records the input at rate 1 and track 2 replays it at -0.5 through a lowpass, as in
    // the softcut demo. Tracks 3 and 4 start off, to keep the default CPU load at two tracks.
    static constexpr float rates[sfct::TRACKS] = { 1.0f, -0.5f, 2.0f, 0.5f };
    for (unsigned v = 0; v < VOICES; v++) {
        unsigned t = v / 2;
        bool left = v % 2 == 0;
        bool front = t == 0;
        bool lowpass = t == 1;
        auto id = [v](StringRef s) { return getVoiceParamId(v, s); };
        String desc = String(v + 1) + " ";
        auto grp = std::make_unique<AudioProcessorParameterGroup>(getVoicePid(v), getVoicePid(v), ID::separator);
        grp->addChild(std::make_unique<ssp::BaseBoolParameter>(id(ID::on), desc + "On", t < 2));
        grp->addChild(std::make_unique<ssp::BaseBoolParameter>(id(ID::play), desc + "Play", true));
        grp->addChild(std::make_unique<ssp::BaseBoolParameter>(id(ID::rec), desc + "Rec", false));
        grp->addChild(std::make_unique<ssp::BaseBoolParameter>(id(ID::loop), desc + "Loop", true));
        grp->addChild(std::make_unique<ssp::BaseBoolParameter>(id(ID::cut), desc + "Cut", false));
        grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(ID::rate), desc + "Rate", -4.0f, 4.0f, rates[t]));
        grp->addChild(
            std::make_unique<ssp::BaseFloatParameter>(id(ID::start), desc + "Start", 0.0f, MAX_SECONDS, 0.0f));
        grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(ID::end), desc + "End", 0.0f, MAX_SECONDS, 4.0f));
        grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(ID::level), desc + "Level", 0.0f, 1.0f,
                                                                front ? 0.8f : 0.6f));
        grp->addChild(
            std::make_unique<ssp::BaseFloatParameter>(id(ID::rec_level), desc + "Rec Lvl", 0.0f, 1.0f, 1.0f));
        grp->addChild(
            std::make_unique<ssp::BaseFloatParameter>(id(ID::pre_level), desc + "Pre Lvl", 0.0f, 1.0f, 0.5f));
        grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(ID::in_gain), desc + "In Gain", 0.0f, 1.0f,
                                                                front ? 1.0f : 0.0f));
        grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(ID::pan), desc + "Pan", -1.0f, 1.0f,
                                                                left ? -1.0f : 1.0f));
        grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(ID::fade), desc + "Fade", 0.001f, 1.0f, 0.05f));
        grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(ID::slew), desc + "Slew", 0.0f, 4.0f, 0.1f));
        grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(ID::lpf), desc + "LPF", 20.0f, 20000.0f,
                                                                lowpass ? 2000.0f : 8000.0f));
        grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(ID::lp_mix), desc + "LP Mix", 0.0f, 1.0f,
                                                                lowpass ? 1.0f : 0.0f));
        params.add(std::move(grp));
    }
    for (unsigned pair = 0; pair < PAIRS; pair++) {
        String desc = String(pair * 2 + 1) + "+" + String(pair * 2 + 2) + " Link";
        params.add(std::make_unique<ssp::BaseBoolParameter>(getLinkId(pair), desc, true));
    }
    params.add(std::make_unique<ssp::BaseChoiceParameter>(ID::mode, "Mode", StringArray{ "norns", "4 loop" }, 0));
    return params;
}

const String PluginProcessor::getInputBusName(int channelIndex) {
    static String inBusName[I_MAX] = { "In L", "In R" };
    if (channelIndex < I_MAX) { return inBusName[channelIndex]; }
    return "ZZIn-" + String(channelIndex);
}

const String PluginProcessor::getOutputBusName(int channelIndex) {
    if (channelIndex == O_OUT_L) return "Out L";
    if (channelIndex == O_OUT_R) return "Out R";
    if (channelIndex < O_MAX) return "Voice " + String(channelIndex - O_VOICE_1 + 1);
    return "ZZOut-" + String(channelIndex);
}

sfct::Mode PluginProcessor::mode() {
    return sfct::Mode(int(normValue(modeParam_)));
}

void PluginProcessor::prepareToPlay(double newSampleRate, int estimatedSamplesPerBlock) {
    BaseProcessor::prepareToPlay(newSampleRate, estimatedSamplesPerBlock);
    sampleRate_ = newSampleRate;
    engine_.setSampleRate(float(newSampleRate));
    if (fileLoadRate_ != 0.0 && fileLoadRate_ != sampleRate_) reloadFiles();
}

static void setParam(RangedAudioParameter& p, float v) {
    p.beginChangeGesture();
    p.setValueNotifyingHost(p.convertTo0to1(v));
    p.endChangeGesture();
}

int PluginProcessor::loadChannels(const String& path, const int (&channelFor)[sfct::BUFFERS]) {
    File f(path);
    if (!f.existsAsFile()) return -1;
    std::unique_ptr<AudioFormatReader> reader(formatManager_.createReaderFor(f));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0) return -1;
    for (int ch : channelFor)
        if (ch >= int(reader->numChannels)) return -1;

    double ratio = reader->sampleRate / sampleRate_;  // input frames per output frame
    int outFrames = int(std::min<double>(sfct::BUFFER_FRAMES, double(reader->lengthInSamples) / ratio));
    int inFrames = int(std::min<double>(double(reader->lengthInSamples), std::ceil(outFrames * ratio)));
    unsigned nch = std::min(2u, reader->numChannels);
    AudioBuffer<float> in(int(nch), inFrames);
    if (!reader->read(&in, 0, inFrames, 0, true, nch > 1)) return -1;

    unsigned mask = 0;
    engine_.beginLoad();
    for (unsigned b = 0; b < sfct::BUFFERS; b++) {
        if (channelFor[b] < 0) continue;
        const float* src = in.getReadPointer(channelFor[b]);
        float* dst = engine_.staging(b);
        if (ratio == 1.0) {
            std::copy(src, src + outFrames, dst);
        } else {
            // Lagrange does not band-limit, so downsampling (e.g. 96k to 48k) can alias
            LagrangeInterpolator interp;
            interp.process(ratio, src, dst, outFrames, inFrames, 0);
        }
        std::fill(dst + outFrames, dst + sfct::BUFFER_FRAMES, 0.0f);
        mask |= 1u << b;
    }
    {
        std::lock_guard<std::mutex> lock(fileLock_);
        for (unsigned b = 0; b < sfct::BUFFERS; b++) {
            if (!(mask & (1u << b))) continue;
            bufferFile_[b] = path;
            bufferChannel_[b] = channelFor[b];
        }
    }
    engine_.commitLoad(mask);
    fileLoadRate_ = sampleRate_;
    return outFrames;
}

bool PluginProcessor::loadFile(const String& path, unsigned voice, bool fitLoops) {
    std::unique_ptr<AudioFormatReader> reader(formatManager_.createReaderFor(File(path)));
    if (reader == nullptr) return false;
    int channelFor[sfct::BUFFERS];
    std::fill(std::begin(channelFor), std::end(channelFor), -1);
    unsigned left = voice & ~1u;
    if (reader->numChannels >= 2) {
        channelFor[bufferOf(left)] = 0;
        channelFor[bufferOf(left + 1)] = 1;
    } else {
        channelFor[bufferOf(voice)] = 0;
    }
    reader.reset();

    int frames = loadChannels(path, channelFor);
    if (frames < 0) return false;
    if (fitLoops) {
        // the voices that play the loaded buffers: all of them when shared, else this track's
        unsigned mask = 0;
        for (unsigned v = 0; v < VOICES; v++)
            if (channelFor[bufferOf(v)] >= 0) mask |= 1u << v;
        float seconds = std::min(MAX_SECONDS, float(frames / sampleRate_));
        for (unsigned v = 0; v < VOICES; v++) {
            if (!(mask & (1u << v))) continue;
            auto& vp = getVoice(v);
            setParam(vp.rec, 0.0f);
            setParam(vp.start, 0.0f);
            setParam(vp.end, seconds);
        }
        cutMask_.fetch_or(mask, std::memory_order_release);
    }
    return true;
}

bool PluginProcessor::saveBuffers(const String& path, unsigned mask) {
    File f(path);
    if (saving_.load()) return false;
    if (saveThread_.joinable()) saveThread_.join();
    if (f.exists()) {
        setStatus(f.getFileName() + " exists");
        return false;
    }
    if (!engine_.requestSnapshot(mask, engine_.viewFrames())) return false;
    saving_ = true;
    setStatus("saving " + f.getFileName());

    double sr = sampleRate_;
    saveThread_ = std::thread([this, f, mask, sr] {
        // the copy takes at most 64 blocks; give up if the audio thread is not running
        for (int i = 0; i < 1000 && !engine_.snapshotReady(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        bool ok = false;
        int nch = 0;
        if (engine_.snapshotReady()) {
            const float* chans[sfct::BUFFERS];
            for (unsigned b = 0; b < sfct::BUFFERS; b++)
                if (mask & (1u << b)) chans[nch++] = engine_.snapshot(b);
            f.getParentDirectory().createDirectory();
            f.deleteFile();
            std::unique_ptr<OutputStream> out = f.createOutputStream();
            WavAudioFormat wav;
            std::unique_ptr<AudioFormatWriter> writer;
            auto opts = AudioFormatWriterOptions{}.withSampleRate(sr).withNumChannels(nch).withBitsPerSample(24);
            if (out != nullptr) writer = wav.createWriterFor(out, opts);
            if (writer != nullptr) ok = writer->writeFromFloatArrays(chans, nch, int(engine_.snapshotFrames()));
            writer.reset();  // flushes the header
        }
        engine_.releaseSnapshot();

        std::lock_guard<std::mutex> lock(fileLock_);
        if (ok) {
            int ch = 0;
            for (unsigned b = 0; b < sfct::BUFFERS; b++) {
                if (!(mask & (1u << b))) continue;
                bufferFile_[b] = f.getFullPathName();
                bufferChannel_[b] = ch++;
            }
        }
        saveStatus_ = (ok ? "saved " : "save failed: ") + f.getFileName();
        saving_ = false;
    });
    return true;
}

void PluginProcessor::reloadFiles() {
    String files[sfct::BUFFERS];
    int chans[sfct::BUFFERS];
    {
        std::lock_guard<std::mutex> lock(fileLock_);
        for (unsigned b = 0; b < sfct::BUFFERS; b++) {
            files[b] = bufferFile_[b];
            chans[b] = bufferChannel_[b];
        }
    }
    // one decode per file, for every buffer that references it
    bool done[sfct::BUFFERS] = {};
    for (unsigned b = 0; b < sfct::BUFFERS; b++) {
        if (done[b] || files[b].isEmpty()) continue;
        int channelFor[sfct::BUFFERS];
        std::fill(std::begin(channelFor), std::end(channelFor), -1);
        for (unsigned o = b; o < sfct::BUFFERS; o++) {
            if (files[o] != files[b]) continue;
            channelFor[o] = chans[o];
            done[o] = true;
        }
        loadChannels(files[b], channelFor);
    }
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
    restoring_ = true;
    BaseProcessor::setStateInformation(data, sizeInBytes);
    restoring_ = false;
}

static String bufferTag(const char* what, unsigned b) {
    return String("BUFFER_") + what + "_" + String(b);
}

void PluginProcessor::customFromXml(juce::XmlElement* xml) {
    {
        std::lock_guard<std::mutex> lock(fileLock_);
        for (unsigned b = 0; b < sfct::BUFFERS; b++) {
            bufferFile_[b] = xml->getStringAttribute(bufferTag("FILE", b), "");
            bufferChannel_[b] = xml->getIntAttribute(bufferTag("CHANNEL", b), 0);
        }
    }
    reloadFiles();
}

void PluginProcessor::customToXml(juce::XmlElement* xml) {
    std::lock_guard<std::mutex> lock(fileLock_);
    for (unsigned b = 0; b < sfct::BUFFERS; b++) {
        xml->setAttribute(bufferTag("FILE", b), bufferFile_[b]);
        xml->setAttribute(bufferTag("CHANNEL", b), bufferChannel_[b]);
    }
}

// Linked pairs: an edit to either voice is copied to its partner, pan mirrored, so the UI and
// presets show what plays. processBlock also derives the follower from the leader, so both
// change in the same block and the playheads stay locked.
void PluginProcessor::parameterChanged(const String& id, float newValue) {
    // a restored mode must still reach the engine
    if (id == ID::mode) {
        engine_.requestMode(sfct::Mode(int(newValue)));
        return;
    }
    if (restoring_) return;
    for (unsigned pair = 0; pair < PAIRS; pair++) {
        if (id != getLinkId(pair)) continue;
        if (newValue > 0.5f) {
            // relink: the follower takes the leader's settings
            for (auto* p : getVoice(pair * 2).params()) parameterChanged(p->paramID, p->convertFrom0to1(p->getValue()));
            relink_[pair].store(true, std::memory_order_release);
        }
        return;
    }

    if (!id.startsWithChar('v')) return;
    unsigned v = unsigned(id.substring(1).getIntValue()) - 1;
    if (v >= VOICES || getVoice(v).link.getValue() < 0.5f) return;
    String name = id.fromFirstOccurrenceOf(ID::separator, false, false);
    float target = name == ID::pan ? -newValue : newValue;
    auto* p = getParameter(getVoiceParamId(sfct::partnerOf(v), name));
    // the partner's own callback sees its target already met, which ends the recursion
    if (p != nullptr && std::fabs(p->getValue() - p->convertTo0to1(target)) > 1e-6f) setParam(*p, target);
}

void PluginProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages) {
    BaseProcessor::processBlock(buffer, midiMessages);
    auto t0 = std::chrono::steady_clock::now();
    unsigned n = buffer.getNumSamples();

    inRms_[0].process(buffer, I_IN_L);
    inRms_[1].process(buffer, I_IN_R);

    sfct::VoiceParams p[VOICES];
    bool cut[VOICES];
    for (unsigned v = 0; v < VOICES; v++) {
        auto& vp = getVoice(v);
        p[v].on = vp.on.getValue() > 0.5f;
        p[v].play = vp.play.getValue() > 0.5f;
        p[v].rec = vp.rec.getValue() > 0.5f;
        p[v].loop = vp.loop.getValue() > 0.5f;
        p[v].rate = normValue(vp.rate);
        p[v].loopStart = normValue(vp.start);
        p[v].loopEnd = normValue(vp.end);
        p[v].fadeTime = normValue(vp.fade);
        p[v].rateSlew = normValue(vp.slew);
        p[v].recLevel = normValue(vp.rec_level);
        p[v].preLevel = normValue(vp.pre_level);
        p[v].level = normValue(vp.level);
        p[v].pan = normValue(vp.pan);
        p[v].inputGain = normValue(vp.in_gain);
        p[v].postFc = normValue(vp.lpf);
        p[v].postLp = normValue(vp.lp_mix);
        bool held = vp.cut.getValue() > 0.5f;
        cut[v] = held && !cutHeld_[v];
        cutHeld_[v] = held;
    }

    unsigned cutMask = cutMask_.exchange(0, std::memory_order_acquire);
    for (unsigned pair = 0; pair < PAIRS; pair++) {
        unsigned lead = pair * 2, follow = lead + 1;
        if (relink_[pair].exchange(false, std::memory_order_acquire)) cut[lead] = true;
        if (getVoice(lead).link.getValue() < 0.5f) continue;
        p[follow] = sfct::linkedTo(p[lead]);
        cut[lead] = cut[follow] = cut[lead] || cut[follow];
    }

    for (unsigned v = 0; v < VOICES; v++) {
        engine_.set(v, p[v]);
        if (((cutMask & (1u << v)) || cut[v]) && (p[v].play || p[v].rec))
            engine_.cut(v, std::min(p[v].loopStart, p[v].loopEnd));
    }

    const float* in[sfct::INPUTS] = { buffer.getReadPointer(I_IN_L), buffer.getReadPointer(I_IN_R) };
    float* mix[2] = { buffer.getWritePointer(O_OUT_L), buffer.getWritePointer(O_OUT_R) };
    float* vout[VOICES];
    for (unsigned v = 0; v < VOICES; v++) vout[v] = buffer.getWritePointer(O_VOICE_1 + v);
    engine_.process(in, mix, vout, n);

    // the displayed track's buffers, over the furthest loop end among enabled voices that use them
    auto m = engine_.mode();
    unsigned left = displayTrack_.load(std::memory_order_relaxed) * 2;
    unsigned lanes[sfct::LANES] = { sfct::bufferFor(m, left), sfct::bufferFor(m, left + 1) };
    float view = 0.5f;  // seconds; a floor keeps a short loop readable
    for (unsigned v = 0; v < VOICES; v++) {
        unsigned b = sfct::bufferFor(m, v);
        if (p[v].on && (b == lanes[0] || b == lanes[1]))
            view = std::max(view, std::max(p[v].loopStart, p[v].loopEnd));
    }
    engine_.scanPeaks(unsigned(view * sampleRate_), lanes);

    outRms_[0].process(buffer, O_OUT_L);
    outRms_[1].process(buffer, O_OUT_R);

    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    float load = float(dt * sampleRate_ / std::max(1u, n));
    float avg = loadAvg_.load(std::memory_order_relaxed);
    loadAvg_.store(avg + 0.01f * (load - avg), std::memory_order_relaxed);  // ~0.3 s at 128 frames
    float peak = loadPeak_.load(std::memory_order_relaxed);
    loadPeak_.store(std::max(load, peak * 0.999f), std::memory_order_relaxed);
}

AudioProcessorEditor* PluginProcessor::createEditor() {
#ifdef FORCE_COMPACT_UI
    return new ssp::EditorHost(this, new PluginMiniEditor(*this), true);
#else
    if (useCompactUI()) {
        return new ssp::EditorHost(this, new PluginMiniEditor(*this), useCompactUI());
    } else {
        return new ssp::EditorHost(this, new PluginEditor(*this), useCompactUI());
    }
#endif
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
