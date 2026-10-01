#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "Engine.h"
#include "Tempo.h"
#include "ssp/BaseProcessor.h"
#include "ssp/controls/RmsTrack.h"

using namespace juce;

namespace ID {
#define PARAMETER_ID(str) constexpr const char* str{ #str };
constexpr const char* separator{ ":" };

PARAMETER_ID(on)
PARAMETER_ID(play)
PARAMETER_ID(rec)
PARAMETER_ID(loop)
PARAMETER_ID(cut)
PARAMETER_ID(rate)
PARAMETER_ID(start)
PARAMETER_ID(end)
PARAMETER_ID(level)
PARAMETER_ID(rec_level)
PARAMETER_ID(pre_level)
PARAMETER_ID(in_gain)
PARAMETER_ID(pan)
PARAMETER_ID(fade)
PARAMETER_ID(slew)
PARAMETER_ID(lpf)
PARAMETER_ID(lp_mix)
PARAMETER_ID(link)
PARAMETER_ID(mode)
PARAMETER_ID(fb_src)
PARAMETER_ID(fb_amt)
PARAMETER_ID(post_rq)
PARAMETER_ID(post_hp)
PARAMETER_ID(post_bp)
PARAMETER_ID(post_br)
PARAMETER_ID(post_dry)
PARAMETER_ID(pre_fc)
PARAMETER_ID(pre_rq)
PARAMETER_ID(pre_fc_mod)
PARAMETER_ID(pre_dry)
PARAMETER_ID(pre_lp)
PARAMETER_ID(pre_hp)
PARAMETER_ID(pre_bp)
PARAMETER_ID(pre_br)
PARAMETER_ID(rec_shape)
PARAMETER_ID(pre_shape)
PARAMETER_ID(rec_delay)
PARAMETER_ID(pre_window)
PARAMETER_ID(rec_offset)
PARAMETER_ID(sync)
PARAMETER_ID(phase_q)
PARAMETER_ID(phase_off)
PARAMETER_ID(input)

#undef PARAMETER_ID
}  // namespace ID


class PluginProcessor : public ssp::BaseProcessor, private AudioProcessorValueTreeState::Listener {
public:
    explicit PluginProcessor();
    explicit PluginProcessor(const AudioProcessor::BusesProperties& ioLayouts,
                             AudioProcessorValueTreeState::ParameterLayout layout);
    ~PluginProcessor() override;

    const String getName() const override { return JucePlugin_Name; }

    void prepareToPlay(double newSampleRate, int estimatedSamplesPerBlock) override;
    void processBlock(AudioSampleBuffer&, MidiBuffer&) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    static constexpr unsigned VOICES = sfct::VOICES;
    static constexpr unsigned PAIRS = sfct::TRACKS;

    // per track: rate (V/oct), pos (1 s/V), rec gate, cut trigger
    enum { I_IN_L, I_IN_R, I_TRACK_1, I_MAX = I_TRACK_1 + 4 * PAIRS };
    enum { TI_RATE, TI_POS, TI_REC, TI_CUT, TI_MAX };
    // Tn Phase: a block-long pulse whenever the track's L voice crosses a phase_q boundary
    enum { O_OUT_L, O_OUT_R, O_VOICE_1, O_PHASE_1 = O_VOICE_1 + VOICES, O_MAX = O_PHASE_1 + PAIRS };

    struct Voice {
        using Parameter = juce::RangedAudioParameter;
        Voice(AudioProcessorValueTreeState& apvt, unsigned v);
        // every per-voice parameter, which linking mirrors; excludes link
        std::vector<Parameter*> params();
        Parameter& on;
        Parameter& play;
        Parameter& rec;
        Parameter& loop;
        Parameter& cut;
        Parameter& rate;
        Parameter& start;
        Parameter& end;
        Parameter& level;
        Parameter& rec_level;
        Parameter& pre_level;
        Parameter& in_gain;
        Parameter& pan;
        Parameter& fade;
        Parameter& slew;
        Parameter& lpf;
        Parameter& lp_mix;  // output filter lowpass level
        Parameter& post_rq;
        Parameter& post_hp;
        Parameter& post_bp;
        Parameter& post_br;
        Parameter& post_dry;
        Parameter& pre_fc;
        Parameter& pre_rq;
        Parameter& pre_fc_mod;
        Parameter& pre_dry;
        Parameter& pre_lp;
        Parameter& pre_hp;
        Parameter& pre_bp;
        Parameter& pre_br;
        Parameter& rec_shape;
        Parameter& pre_shape;
        Parameter& rec_delay;
        Parameter& pre_window;
        Parameter& rec_offset;
        Parameter& link;  // shared by both voices of the pair
    };

    // feedback into the track, L to L and R to R, from source track fb_src (0 off, else 1-4)
    struct Track {
        using Parameter = juce::RangedAudioParameter;
        Track(AudioProcessorValueTreeState& apvt, unsigned t);
        Parameter& fb_src;
        Parameter& fb_amt;
        Parameter& sync;       // 0 off, else an index into SYNC_BEATS
        Parameter& phase_q;    // seconds; 0 off
        Parameter& phase_off;  // seconds
        Parameter& input;      // what the track records: stereo, L, R or L+R (see sfct::InputSrc)
    };
    static constexpr int SYNC_BEATS[] = { 0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };

    struct PluginParams {
        explicit PluginParams(juce::AudioProcessorValueTreeState&);
        std::vector<std::unique_ptr<Voice>> voices_;
        std::vector<std::unique_ptr<Track>> tracks_;
    } params_;
    Track& getTrack(unsigned t) { return *params_.tracks_[t]; }
    RangedAudioParameter& modeParam_;

    Voice& getVoice(unsigned v) { return *params_.voices_[v]; }

    static BusesProperties getBusesProperties() {
        BusesProperties props;
        for (auto i = 0; i < I_MAX; i++) { props.addBus(true, getInputBusName(i), AudioChannelSet::mono()); }
        for (auto i = 0; i < O_MAX; i++) { props.addBus(false, getOutputBusName(i), AudioChannelSet::mono()); }
        return props;
    }

    void getRMS(float& lIn, float& rIn, float& lOut, float& rOut) {
        lIn = inRms_[0].lvl();
        rIn = inRms_[1].lvl();
        lOut = outRms_[0].lvl();
        rOut = outRms_[1].lvl();
    }

    // The mode the parameter selects; the engine follows within a block.
    sfct::Mode mode();
    unsigned bufferOf(unsigned voice) { return sfct::bufferFor(mode(), voice); }

    // Loads a WAV/AIFF: mono into the voice's buffer, stereo into its track's L and R buffers,
    // resampled to the engine rate and truncated to the buffer length. fitLoops sets the voices
    // that play those buffers to loop the file, rec off. Message thread only.
    bool loadFile(const String& path, unsigned voice, bool fitLoops);
    String getBufferFile(unsigned b) {
        std::lock_guard<std::mutex> lock(fileLock_);
        return bufferFile_[b];
    }

    // Saves the first viewSeconds() of the buffers in `mask` as a 24-bit WAV, mono or L/R, and
    // points those buffers' preset reference at it. Asynchronous: the audio thread copies, a worker
    // thread writes; saveStatus() reports the outcome. Never overwrites: a loaded sample may be the
    // file at that path. Message thread only.
    bool saveBuffers(const String& path, unsigned mask);
    // Clears the loop voice v plays (after CV) on its buffer, and its partner's when linked, with
    // 5 ms edge fades. Asynchronous, like save. Message thread only.
    bool clearLoop(unsigned v);
    // Back to a new instance: every parameter at its default, all buffers silent, no file references,
    // norns mode. MIDI settings are kept. Message thread only.
    void clearAll();

    // Records one pass on voice v, and its partner when linked, from the next loop point.
    void recOnce(unsigned v);
    // MIDI clock tempo; 0 until four beats of clock have arrived
    float bpm() const {
        double spb = spb_.load(std::memory_order_relaxed);
        return spb > 0.0 ? float(60.0 / spb) : 0.0f;
    }

    String saveStatus() {
        std::lock_guard<std::mutex> lock(fileLock_);
        return saveStatus_;
    }

    // Waveform display, any thread. Lanes 0 and 1 are the displayed track's L and R buffers; peaks
    // cover the first viewSeconds(): the furthest loop end among enabled voices on those buffers.
    void setDisplayTrack(unsigned t) { displayTrack_.store(t, std::memory_order_relaxed); }
    float peak(unsigned lane, unsigned bin) const { return engine_.peak(lane, bin); }
    float viewSeconds() const { return float(engine_.viewFrames() / sampleRate_); }
    // subhead i (0, 1) of voice v: position in seconds, and its equal-power playback gain
    float headPosition(unsigned v, int i) { return engine_.voice(v).getSavedHeadPosition(i); }
    float headGain(unsigned v, int i) { return std::sin(engine_.voice(v).getSavedHeadFade(i) * float(M_PI_2)); }
    bool isRecording(unsigned v) { return engine_.voice(v).getSavedRecFlag(); }
    // the loop and rate voice v plays, after linking and CV
    float playedStart(unsigned v) const { return played_[v].start.load(std::memory_order_relaxed); }
    float playedEnd(unsigned v) const { return played_[v].end.load(std::memory_order_relaxed); }
    float playedRate(unsigned v) const { return played_[v].rate.load(std::memory_order_relaxed); }

    // processBlock time as a fraction of the block's duration, on one core
    float loadAverage() const { return loadAvg_.load(std::memory_order_relaxed); }
    float loadPeak() const { return loadPeak_.load(std::memory_order_relaxed); }

protected:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void customFromXml(juce::XmlElement*) override;
    void customToXml(juce::XmlElement*) override;
    void parameterChanged(const String& id, float newValue) override;
    void onMidiClock(double ts) override;
    void onMidiStart(double ts) override;

private:
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override { return true; }

    static const String getInputBusName(int channelIndex);
    static const String getOutputBusName(int channelIndex);

    inline float normValue(RangedAudioParameter& p) { return p.convertFrom0to1(p.getValue()); }

    sfct::Engine engine_;
    bool cutHeld_[VOICES] = {};
    std::atomic<unsigned> cutMask_{ 0 };  // voices to cut to loop start after a load
    bool trigHigh_[PAIRS] = {};            // cut trigger input state, per track
    double lastPhase_[PAIRS] = {};         // each track's last quantised phase, for Tn Phase
    std::atomic<unsigned> recOnceMask_{ 0 };
    sfct::ClockTempo tempo_;               // MIDI input thread only
    std::atomic<double> spb_{ 0.0 };       // seconds per beat, from tempo_
    std::atomic<bool> syncStart_{ false }; // MIDI start: cut synced tracks to their loop start
    struct {
        std::atomic<float> start{ 0.0f }, end{ 0.0f }, rate{ 1.0f };
    } played_[VOICES];
    std::atomic<unsigned> displayTrack_{ 0 };
    double sampleRate_ = 48000.0;

    std::atomic<bool> relink_[PAIRS] = {};
    bool restoring_ = false;  // a preset already holds both voices' values; do not mirror

    // channelFor[b]: file channel for buffer b, or -1 to leave it. Returns frames loaded, or -1.
    int loadChannels(const String& path, const int (&channelFor)[sfct::BUFFERS]);
    void reloadFiles();

    void setStatus(const String& s) {
        std::lock_guard<std::mutex> lock(fileLock_);
        saveStatus_ = s;
    }

    AudioFormatManager formatManager_;
    std::mutex fileLock_;  // bufferFile_, bufferChannel_, saveStatus_: message and save threads
    String bufferFile_[sfct::BUFFERS];
    int bufferChannel_[sfct::BUFFERS] = {};
    String saveStatus_;
    std::thread saveThread_;
    std::atomic<bool> saving_{ false };
    // frees the buffers a load swaps out, once the audio thread has swapped them
    std::thread housekeeper_;
    std::atomic<bool> quit_{ false };
    double fileLoadRate_ = 0.0;  // engine rate the files were resampled to

    ssp::RmsTrack inRms_[2];
    ssp::RmsTrack outRms_[2];

    std::atomic<float> loadAvg_{ 0.0f };
    std::atomic<float> loadPeak_{ 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
