#include "DexedEngine.h"

#include <algorithm>
#include <cstring>

#include "synth_dexed/dexed.h"

namespace dexed {

// getSamples is protected; init_voice is the voice Dexed starts with.
class DexedEngine::Synth : public Dexed {
public:
    using Dexed::Dexed;
    using Dexed::getSamples;
    const uint8_t* initVoice() const { return init_voice; }
};

// offsets into the unpacked voice
static constexpr int OP_BYTES = 21, OP_OUTPUT = 16;
static constexpr int ALGORITHM = 134, FEEDBACK = 135, LFO_SPEED = 137, LFO_PMD = 139;

static uint8_t add(uint8_t v, int offset, int hi) {
    return uint8_t(std::clamp(int(v) + offset, 0, hi));
}

void applySettings(const uint8_t* base, const Settings& s, uint8_t* out) {
    std::memcpy(out, base, Cartridge::VOICE_BYTES);
    for (int op = 0; op < 6; op++) {
        int at = (5 - op) * OP_BYTES + OP_OUTPUT;  // operator 6 is stored first
        out[at] = add(base[at], s.opLevel[op], 99);
    }
    if (s.algorithm > 0) out[ALGORITHM] = uint8_t(std::clamp(s.algorithm, 1, 32) - 1);
    out[FEEDBACK] = add(base[FEEDBACK], s.feedback, 7);
    out[LFO_SPEED] = add(base[LFO_SPEED], s.lfoRate, 99);
    out[LFO_PMD] = add(base[LFO_PMD], s.lfoDepth, 99);
}

DexedEngine::DexedEngine() = default;

DexedEngine::~DexedEngine() {
    delete cart_.take();
}

void DexedEngine::prepare(float sampleRate, int) {
    if (synth_ && sampleRate == sampleRate_) return;
    sampleRate_ = sampleRate;
    synth_ = std::make_unique<Synth>(uint8_t(MAX_VOICES), uint32_t(sampleRate));
    // DX7 defaults: the wheel and aftertouch add LFO pitch depth
    synth_->setMWController(99, PITCH, 0);
    synth_->setATController(99, PITCH, 0);
    std::memset(held_, 0, sizeof held_);
    fifoPos_ = CHUNK;
    cutoffPlayed_ = -1.0f;
    wheelPlayed_ = -1;
    seenVoice_ = -1;  // reload the voice into the new synth
    force_ = true;
}

void DexedEngine::setCartridge(std::unique_ptr<Cartridge> c) {
    Cartridge* old = cart_.take();
    cart_.publish(c.release());
    cartGen_.fetch_add(1, std::memory_order_release);
    delete old;
}

void DexedEngine::releaseAll() {
    for (int k = 0; k < 128; k++) {
        if (held_[k]) synth_->keyup(uint8_t(held_[k] - 1));
        held_[k] = 0;
    }
}

void DexedEngine::handle(const Event& e) {
    auto& s = *synth_;
    switch (e.status & 0xF0) {
        case 0x90:
            if (e.d2 > 0) {
                int k = e.d1 & 0x7F;
                if (held_[k]) s.keyup(uint8_t(held_[k] - 1));  // a repeated note-on
                int pitch = std::clamp(k + settings.transpose, 0, 127);
                held_[k] = uint8_t(pitch + 1);  // keyup uses this, if transpose changes meanwhile
                s.keydown(uint8_t(pitch), e.d2 & 0x7F);
                break;
            }
            [[fallthrough]];
        case 0x80: {
            int k = e.d1 & 0x7F;
            if (held_[k]) s.keyup(uint8_t(held_[k] - 1));
            held_[k] = 0;
            break;
        }
        case 0xB0:
            switch (e.d1) {
                case 1: wheelMidi_ = e.d2 & 0x7F; break;  // applyWheel sends it
                case 2: s.setBreathController(e.d2); s.ControllersRefresh(); break;
                case 4: s.setFootController(e.d2); s.ControllersRefresh(); break;
                case 64: s.setSustain(e.d2 >= 64); break;
                case 66: s.setSostenuto(e.d2 >= 64); break;
                case 120:  // all sound off
                    s.panic();
                    std::memset(held_, 0, sizeof held_);
                    break;
                case 121:  // reset controllers; they include the bend range
                    s.resetControllers();
                    wheelMidi_ = 0;
                    wheelPlayed_ = -1;
                    s.setPitchbendRange(uint8_t(std::clamp(settings.bendRange, 0, 12)));
                    applied_.bendRange = settings.bendRange;
                    break;
                case 123:  // all notes off
                    releaseAll();
                    break;
                default: break;
            }
            break;
        case 0xD0: s.setAftertouch(e.d1 & 0x7F); s.ControllersRefresh(); break;
        case 0xE0: s.setPitchbend(e.d1, e.d2); break;
        default: break;  // program change: PluginProcessor maps it to the voice setting
    }
}

// Settings changed since the last block reach the synth here; a voice or cartridge change reloads.
void DexedEngine::apply(float cvCutoff) {
    auto& s = *synth_;
    const Settings& n = settings;
    Settings& a = applied_;
    bool f = force_;
    force_ = false;

    // these cut the notes playing
    int voices = std::clamp(n.voices, 1, MAX_VOICES);
    if (f || voices != a.voices) {
        s.setMaxNotes(uint8_t(voices));
        std::memset(held_, 0, sizeof held_);
    }
    if (f || n.engine != a.engine) {
        s.setEngineType(uint8_t(std::clamp(n.engine, 0, 2)));
        std::memset(held_, 0, sizeof held_);
    }
    if (f || n.mono != a.mono) {
        s.setMonoMode(n.mono);
        std::memset(held_, 0, sizeof held_);
    }
    if (f || n.level != a.level) s.setGain(std::max(0.0f, n.level));
    float cutoff = std::clamp(n.cutoff + cvCutoff, 0.0f, 1.0f);
    if (f || cutoff != cutoffPlayed_) s.setFilterCutoff(cutoff);
    cutoffPlayed_ = cutoff;
    if (f || n.resonance != a.resonance) s.setFilterResonance(std::clamp(n.resonance, 0.0f, 1.0f));
    if (f || n.bendRange != a.bendRange) s.setPitchbendRange(uint8_t(std::clamp(n.bendRange, 0, 12)));
    if (f || n.portamento != a.portamento) s.setPortamento(0, 0, uint8_t(std::clamp(n.portamento, 0, 99)));

    unsigned gen = cartGen_.load(std::memory_order_acquire);
    int voice = std::clamp(n.voice, 0, Cartridge::VOICES - 1);
    bool reload = gen != seenGen_ || voice != seenVoice_;
    if (reload) {
        const Cartridge* c = cart_.begin();
        std::memcpy(base_, c ? c->voice[voice] : s.initVoice(), sizeof base_);
        cart_.end();
        seenGen_ = gen;
        seenVoice_ = voice;
    }
    uint8_t want[Cartridge::VOICE_BYTES];
    applySettings(base_, n, want);
    if (reload) {
        s.loadVoiceParameters(want);  // cuts the notes playing
        std::memset(held_, 0, sizeof held_);
    } else {
        for (int i = 0; i < Cartridge::VOICE_BYTES; i++)
            if (want[i] != played_[i]) s.setVoiceDataElement(uint8_t(i), want[i]);  // notes playing follow
    }
    std::memcpy(played_, want, sizeof played_);
    a = n;
}

void DexedEngine::applyWheel(float cvMod) {
    int wheel = std::max(wheelMidi_, int(std::clamp(cvMod, 0.0f, 1.0f) * 127.0f + 0.5f));
    if (wheel == wheelPlayed_) return;
    synth_->setModWheel(uint8_t(wheel));
    synth_->ControllersRefresh();
    wheelPlayed_ = wheel;
}

void DexedEngine::process(const float* const* in, float* const* out, int n) {
    apply(in ? in[I_CUTOFF][0] : 0.0f);
    Event e;
    while (events_.try_dequeue(e)) handle(e);
    applyWheel(in ? in[I_MOD][0] : 0.0f);

    float* l = out[O_L];
    for (int i = 0; i < n;) {
        if (fifoPos_ == CHUNK) {
            synth_->getSamples(fifo_, CHUNK);
            playing_.store(synth_->getNumNotesPlaying(), std::memory_order_relaxed);  // frees silent voices
            fifoPos_ = 0;
        }
        int k = std::min(n - i, CHUNK - fifoPos_);
        std::memcpy(l + i, fifo_ + fifoPos_, size_t(k) * sizeof(float));
        i += k;
        fifoPos_ += k;
    }
    std::memcpy(out[O_R], l, size_t(n) * sizeof(float));
}

}  // namespace dexed
