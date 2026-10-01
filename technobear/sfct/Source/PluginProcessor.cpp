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

// "t1:fb_src"
static String getTrackParamId(unsigned t, StringRef id) {
    return "t" + String(t + 1) + String(ID::separator) + id;
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

    // Polling, rather than freeing after each load, also covers loads a preset restores before the
    // audio thread starts. reclaimStaging() is a no-op unless the load lock is idle.
    housekeeper_ = std::thread([this] {
        while (!quit_.load()) {
            engine_.reclaimStaging();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}

PluginProcessor::~PluginProcessor() {
    quit_ = true;
    housekeeper_.join();
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
      post_rq(*apvt.getParameter(getVoiceParamId(v, ID::post_rq))),
      post_hp(*apvt.getParameter(getVoiceParamId(v, ID::post_hp))),
      post_bp(*apvt.getParameter(getVoiceParamId(v, ID::post_bp))),
      post_br(*apvt.getParameter(getVoiceParamId(v, ID::post_br))),
      post_dry(*apvt.getParameter(getVoiceParamId(v, ID::post_dry))),
      pre_fc(*apvt.getParameter(getVoiceParamId(v, ID::pre_fc))),
      pre_rq(*apvt.getParameter(getVoiceParamId(v, ID::pre_rq))),
      pre_fc_mod(*apvt.getParameter(getVoiceParamId(v, ID::pre_fc_mod))),
      pre_dry(*apvt.getParameter(getVoiceParamId(v, ID::pre_dry))),
      pre_lp(*apvt.getParameter(getVoiceParamId(v, ID::pre_lp))),
      pre_hp(*apvt.getParameter(getVoiceParamId(v, ID::pre_hp))),
      pre_bp(*apvt.getParameter(getVoiceParamId(v, ID::pre_bp))),
      pre_br(*apvt.getParameter(getVoiceParamId(v, ID::pre_br))),
      rec_shape(*apvt.getParameter(getVoiceParamId(v, ID::rec_shape))),
      pre_shape(*apvt.getParameter(getVoiceParamId(v, ID::pre_shape))),
      rec_delay(*apvt.getParameter(getVoiceParamId(v, ID::rec_delay))),
      pre_window(*apvt.getParameter(getVoiceParamId(v, ID::pre_window))),
      rec_offset(*apvt.getParameter(getVoiceParamId(v, ID::rec_offset))),
      link(*apvt.getParameter(getLinkId(v / 2))) {
}

std::vector<RangedAudioParameter*> PluginProcessor::Voice::params() {
    return { &on,        &play,      &rec,     &loop, &cut,  &rate, &start, &end,   &level,
             &rec_level, &pre_level, &in_gain, &pan,  &fade, &slew, &lpf,   &lp_mix,
             &post_rq, &post_hp, &post_bp, &post_br, &post_dry, &pre_fc, &pre_rq, &pre_fc_mod, &pre_dry, &pre_lp, &pre_hp, &pre_bp, &pre_br, &rec_shape, &pre_shape, &rec_delay, &pre_window, &rec_offset };
}

PluginProcessor::Track::Track(AudioProcessorValueTreeState& apvt, unsigned t)
    : fb_src(*apvt.getParameter(getTrackParamId(t, ID::fb_src))),
      fb_amt(*apvt.getParameter(getTrackParamId(t, ID::fb_amt))),
      sync(*apvt.getParameter(getTrackParamId(t, ID::sync))),
      phase_q(*apvt.getParameter(getTrackParamId(t, ID::phase_q))),
      phase_off(*apvt.getParameter(getTrackParamId(t, ID::phase_off))),
      input(*apvt.getParameter(getTrackParamId(t, ID::input))) {
}

PluginProcessor::PluginParams::PluginParams(AudioProcessorValueTreeState& apvt) {
    for (unsigned v = 0; v < VOICES; v++) { voices_.push_back(std::make_unique<Voice>(apvt, v)); }
    for (unsigned t = 0; t < PAIRS; t++) { tracks_.push_back(std::make_unique<Track>(apvt, t)); }
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
        grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(ID::lp_mix), desc + "LP", 0.0f, 1.0f,
                                                                lowpass ? 1.0f : 0.0f));
        // softcut's own defaults, except the output filter's dry level, which complements lp
        auto flt = [&](StringRef pid, const String& name, float lo, float hi, float def) {
            grp->addChild(std::make_unique<ssp::BaseFloatParameter>(id(pid), desc + name, lo, hi, def));
        };
        flt(ID::post_rq, "Q", 0.05f, 4.0f, 4.0f);
        flt(ID::post_hp, "HP", 0.0f, 1.0f, 0.0f);
        flt(ID::post_bp, "BP", 0.0f, 1.0f, 0.0f);
        flt(ID::post_br, "BR", 0.0f, 1.0f, 0.0f);
        flt(ID::post_dry, "Dry", 0.0f, 1.0f, lowpass ? 0.0f : 1.0f);
        flt(ID::pre_fc, "In FC", 20.0f, 20000.0f, 16000.0f);
        flt(ID::pre_rq, "In Q", 0.05f, 4.0f, 4.0f);
        flt(ID::pre_fc_mod, "In Track", 0.0f, 1.0f, 1.0f);
        flt(ID::pre_dry, "In Dry", 0.0f, 1.0f, 0.0f);
        flt(ID::pre_lp, "In LP", 0.0f, 1.0f, 1.0f);
        flt(ID::pre_hp, "In HP", 0.0f, 1.0f, 0.0f);
        flt(ID::pre_bp, "In BP", 0.0f, 1.0f, 0.0f);
        flt(ID::pre_br, "In BR", 0.0f, 1.0f, 0.0f);
        StringArray shapes{ "linear", "sine", "raised" };
        grp->addChild(std::make_unique<ssp::BaseChoiceParameter>(id(ID::rec_shape), desc + "Rec Shape", shapes, 2));
        grp->addChild(std::make_unique<ssp::BaseChoiceParameter>(id(ID::pre_shape), desc + "Pre Shape", shapes, 0));
        flt(ID::rec_delay, "Rec Delay", 0.0f, 0.5f, 1.0f / 128.0f);
        flt(ID::pre_window, "Pre Window", 0.0f, 1.0f, 1.0f / 8.0f);
        flt(ID::rec_offset, "Rec Ofs ms", -10.0f, 10.0f, -8.0f / 48.0f);  // softcut's -8 frames at 48 kHz
        params.add(std::move(grp));
    }
    for (unsigned pair = 0; pair < PAIRS; pair++) {
        String desc = String(pair * 2 + 1) + "+" + String(pair * 2 + 2) + " Link";
        params.add(std::make_unique<ssp::BaseBoolParameter>(getLinkId(pair), desc, true));
    }
    params.add(std::make_unique<ssp::BaseChoiceParameter>(ID::mode, "Mode", StringArray{ "norns", "4 loop" }, 0));
    for (unsigned t = 0; t < PAIRS; t++) {
        String desc = "T" + String(t + 1) + " ";
        params.add(std::make_unique<ssp::BaseChoiceParameter>(getTrackParamId(t, ID::fb_src), desc + "FB Src",
                                                              StringArray{ "off", "T1", "T2", "T3", "T4" }, 0));
        params.add(
            std::make_unique<ssp::BaseFloatParameter>(getTrackParamId(t, ID::fb_amt), desc + "FB Amt", 0.0f, 1.0f, 0.5f));
        StringArray beats{ "off" };
        for (int i = 1; i < int(std::size(SYNC_BEATS)); i++) beats.add(String(SYNC_BEATS[i]));
        params.add(std::make_unique<ssp::BaseChoiceParameter>(getTrackParamId(t, ID::sync), desc + "Sync", beats, 0));
        params.add(std::make_unique<ssp::BaseFloatParameter>(getTrackParamId(t, ID::phase_q), desc + "Phase Q", 0.0f,
                                                             16.0f, 0.0f));
        params.add(std::make_unique<ssp::BaseFloatParameter>(getTrackParamId(t, ID::phase_off), desc + "Phase Ofs",
                                                             0.0f, MAX_SECONDS, 0.0f));
        // index order matches sfct::InputSrc: stereo is each side's own input
        params.add(std::make_unique<ssp::BaseChoiceParameter>(getTrackParamId(t, ID::input), desc + "Input",
                                                              StringArray{ "stereo", "L", "R", "L+R" }, 0));
    }
    return params;
}

const String PluginProcessor::getInputBusName(int channelIndex) {
    if (channelIndex == I_IN_L) return "In L";
    if (channelIndex == I_IN_R) return "In R";
    if (channelIndex < I_MAX) {
        static const char* names[TI_MAX] = { "Rate", "Pos", "Rec", "Cut" };
        int i = channelIndex - I_TRACK_1;
        return "T" + String(i / TI_MAX + 1) + " " + names[i % TI_MAX];
    }
    return "ZZIn-" + String(channelIndex);
}

const String PluginProcessor::getOutputBusName(int channelIndex) {
    if (channelIndex == O_OUT_L) return "Out L";
    if (channelIndex == O_OUT_R) return "Out R";
    if (channelIndex < O_PHASE_1) return "Voice " + String(channelIndex - O_VOICE_1 + 1);
    if (channelIndex < O_MAX) return "T" + String(channelIndex - O_PHASE_1 + 1) + " Phase";
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

void PluginProcessor::recOnce(unsigned v) {
    unsigned mask = 1u << v;
    if (getVoice(v).link.getValue() > 0.5f) mask |= 1u << sfct::partnerOf(v);
    recOnceMask_.fetch_or(mask, std::memory_order_release);
}

// MIDI input thread
void PluginProcessor::onMidiClock(double ts) {
    tempo_.tick(ts);
    spb_.store(tempo_.secondsPerBeat(), std::memory_order_relaxed);
}

void PluginProcessor::onMidiStart(double ts) {
    syncStart_.store(true, std::memory_order_release);
}

void PluginProcessor::clearAll() {
    engine_.requestClearAll();
    {
        std::lock_guard<std::mutex> lock(fileLock_);
        for (unsigned b = 0; b < sfct::BUFFERS; b++) {
            bufferFile_[b] = String();
            bufferChannel_[b] = 0;
        }
    }
    // the defaults already agree between linked voices, so mirroring is not needed; mode still
    // reaches the engine, as parameterChanged handles it before this guard
    restoring_ = true;
    for (auto* p : getParameters()) {
        if (auto* rp = dynamic_cast<RangedAudioParameter*>(p)) {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost(rp->getDefaultValue());
            rp->endChangeGesture();
        }
    }
    restoring_ = false;
    cutMask_.fetch_or((1u << VOICES) - 1, std::memory_order_release);  // playheads back to loop start
    setStatus("cleared all");
}

bool PluginProcessor::clearLoop(unsigned v) {
    float s = std::min(playedStart(v), playedEnd(v)), e = std::max(playedStart(v), playedEnd(v));
    unsigned left = v & ~1u;
    unsigned mask = getVoice(v).link.getValue() > 0.5f ? (1u << bufferOf(left)) | (1u << bufferOf(left + 1))
                                                       : 1u << bufferOf(v);
    String what = "T" + String(v / 2 + 1) + (getVoice(v).link.getValue() > 0.5f ? String() : (v % 2 ? " R" : " L"));
    if (!engine_.requestClear(mask, unsigned(s * sampleRate_), unsigned(e * sampleRate_),
                              unsigned(0.005 * sampleRate_))) {
        setStatus("clear busy");
        return false;
    }
    setStatus("cleared " + what + String::formatted(" %.2f-%.2f s", s, e));
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
    // silent voices' filters decay through denormals, which are slow on VFP
    ScopedNoDenormals noDenormals;
    BaseProcessor::processBlock(buffer, midiMessages);
    auto t0 = std::chrono::steady_clock::now();
    unsigned n = buffer.getNumSamples();
    if (n == 0) return;

    inRms_[0].process(buffer, I_IN_L);
    inRms_[1].process(buffer, I_IN_R);

    // read the CV inputs first: the outputs overwrite their channels
    struct {
        float rate, pos;
        bool gate, trig;
    } cv[PAIRS];
    for (unsigned t = 0; t < PAIRS; t++) {
        int ch = I_TRACK_1 + int(t * TI_MAX);
        cv[t].rate = buffer.getSample(ch + TI_RATE, int(n) - 1);
        cv[t].pos = buffer.getSample(ch + TI_POS, int(n) - 1);
        cv[t].gate = buffer.getSample(ch + TI_REC, int(n) - 1) > 0.5f;
        cv[t].trig = false;
        const float* trig = buffer.getReadPointer(ch + TI_CUT);
        bool high = trigHigh_[t];
        for (unsigned i = 0; i < n; i++) {
            bool h = trig[i] > 0.5f;
            cv[t].trig = cv[t].trig || (h && !high);
            high = h;
        }
        trigHigh_[t] = high;
    }

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
        p[v].postRq = normValue(vp.post_rq);
        p[v].postHp = normValue(vp.post_hp);
        p[v].postBp = normValue(vp.post_bp);
        p[v].postBr = normValue(vp.post_br);
        p[v].postDry = normValue(vp.post_dry);
        p[v].preFc = normValue(vp.pre_fc);
        p[v].preRq = normValue(vp.pre_rq);
        p[v].preFcMod = normValue(vp.pre_fc_mod);
        p[v].preDry = normValue(vp.pre_dry);
        p[v].preLp = normValue(vp.pre_lp);
        p[v].preHp = normValue(vp.pre_hp);
        p[v].preBp = normValue(vp.pre_bp);
        p[v].preBr = normValue(vp.pre_br);
        p[v].recShape = int(normValue(vp.rec_shape));
        p[v].preShape = int(normValue(vp.pre_shape));
        p[v].recDelay = normValue(vp.rec_delay);
        p[v].preWindow = normValue(vp.pre_window);
        p[v].recOffset = normValue(vp.rec_offset) * 0.001f;
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

    // Track settings apply to both voices, after linking, so a linked track stays locked: sync sets
    // the loop length, then CV moves it.
    double spb = spb_.load(std::memory_order_relaxed);
    bool syncStart = syncStart_.exchange(false, std::memory_order_acquire);
    float fb[VOICES][VOICES] = {};
    for (unsigned t = 0; t < PAIRS; t++) {
        auto& tp = getTrack(t);
        int beats = SYNC_BEATS[std::clamp(int(normValue(tp.sync)), 0, int(std::size(SYNC_BEATS)) - 1)];
        for (unsigned v = t * 2; v < t * 2 + 2; v++) {
            if (beats > 0 && spb > 0.0) {
                p[v].loopStart = std::min(p[v].loopStart, p[v].loopEnd);
                p[v].loopEnd = p[v].loopStart + float(beats * spb);
                cut[v] = cut[v] || syncStart;
            }
            p[v].input = int(normValue(tp.input));
            p[v].phaseQuant = normValue(tp.phase_q);
            p[v].phaseOffset = normValue(tp.phase_off);
            p[v] = sfct::withCv(p[v], cv[t].rate, cv[t].pos, cv[t].gate, engine_.bufferSeconds());
            cut[v] = cut[v] || cv[t].trig;
        }
        int src = int(normValue(tp.fb_src)) - 1;
        if (src < 0) continue;
        float amt = normValue(tp.fb_amt);
        fb[src * 2][t * 2] = amt;
        fb[src * 2 + 1][t * 2 + 1] = amt;
    }
    engine_.setFeedback(fb);

    for (unsigned v = 0; v < VOICES; v++) {
        played_[v].start.store(p[v].loopStart, std::memory_order_relaxed);
        played_[v].end.store(p[v].loopEnd, std::memory_order_relaxed);
        played_[v].rate.store(p[v].rate, std::memory_order_relaxed);
        engine_.set(v, p[v]);
        if (((cutMask & (1u << v)) || cut[v]) && (p[v].play || p[v].rec))
            engine_.cut(v, std::min(p[v].loopStart, p[v].loopEnd));
    }
    unsigned recOnce = recOnceMask_.exchange(0, std::memory_order_acquire);
    for (unsigned v = 0; v < VOICES; v++)
        if (recOnce & (1u << v)) engine_.recOnce(v);

    const float* in[sfct::INPUTS] = { buffer.getReadPointer(I_IN_L), buffer.getReadPointer(I_IN_R) };
    float* mix[2] = { buffer.getWritePointer(O_OUT_L), buffer.getWritePointer(O_OUT_R) };
    float* vout[VOICES];
    for (unsigned v = 0; v < VOICES; v++) vout[v] = buffer.getWritePointer(O_VOICE_1 + v);
    engine_.process(in, mix, vout, n);

    for (unsigned t = 0; t < PAIRS; t++) {
        unsigned v = t * 2;
        double q = engine_.voice(v).getQuantPhase();
        bool pulse = p[v].on && (p[v].play || p[v].rec) && p[v].phaseQuant > 0.0f && q != lastPhase_[t];
        lastPhase_[t] = q;
        float* out = buffer.getWritePointer(O_PHASE_1 + int(t));
        std::fill(out, out + n, pulse ? 1.0f : 0.0f);
    }

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
