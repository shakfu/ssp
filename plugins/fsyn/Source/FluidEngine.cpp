#include "FluidEngine.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#include <fluidsynth.h>

namespace fsyn {

// A synth and the settings it reads; FluidSynth requires the settings to outlive the synth.
struct FluidEngine::Synth {
    fluid_settings_t* settings = nullptr;
    fluid_synth_t* synth = nullptr;
    ~Synth() {
        if (synth) delete_fluid_synth(synth);
        if (settings) delete_fluid_settings(settings);
    }
};

// FluidSynth logs to stderr by default, from the audio thread too. Only a load's errors are kept, as
// its reason; the rest is dropped.
static thread_local std::string* loadLog = nullptr;

static void logError(int, const char* message, void*) {
    if (loadLog && loadLog->empty() && message) *loadLog = message;  // the first is the most specific
}

FluidEngine::FluidEngine() {
    static std::once_flag once;
    std::call_once(once, [] {
        for (int level : { FLUID_PANIC, FLUID_ERR }) fluid_set_log_function(level, logError, nullptr);
        for (int level : { FLUID_WARN, FLUID_INFO, FLUID_DBG }) fluid_set_log_function(level, nullptr, nullptr);
    });
    std::memset(cc_, -1, sizeof cc_);
    std::memset(program_, -1, sizeof program_);
    std::memset(bend_, -1, sizeof bend_);
}

FluidEngine::~FluidEngine() {
    delete synth_.take();
}

// A new rate needs a new synth: FluidSynth reads it only at creation.
void FluidEngine::prepare(float sampleRate, int) {
    if (sampleRate == rate_.load() && synth_.current()) return;
    rate_.store(sampleRate);
    delete synth_.take();
    std::lock_guard<std::mutex> lock(lock_);
    font_.clear();
    pending_ = !path_.empty();
}

void FluidEngine::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(lock_);
    path_ = path;
    pending_ = true;
}

std::string FluidEngine::path() const {
    std::lock_guard<std::mutex> lock(lock_);
    return path_;
}

std::string FluidEngine::font() const {
    std::lock_guard<std::mutex> lock(lock_);
    return font_;
}

std::string FluidEngine::error() const {
    std::lock_guard<std::mutex> lock(lock_);
    return error_;
}

bool FluidEngine::loading() const {
    std::lock_guard<std::mutex> lock(lock_);
    return pending_ || busy_;
}

void FluidEngine::idle() {
    std::string path;
    float rate = rate_.load();
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!pending_ || rate <= 0.0f) return;  // loads wait for prepare()
        path = path_;
        pending_ = false;
        busy_ = true;
    }
    Synth* next = nullptr;
    std::string err, log;
    if (!path.empty()) {
        loadLog = &log;
        next = new Synth;
        next->settings = new_fluid_settings();
        fluid_settings_setnum(next->settings, "synth.sample-rate", rate);
        fluid_settings_setint(next->settings, "synth.polyphony", MAX_VOICES);
        // only the audio thread calls the synth once it is published, so FluidSynth's mutex is not needed
        fluid_settings_setint(next->settings, "synth.threadsafe-api", 0);
        next->synth = new_fluid_synth(next->settings);
        if (!next->synth) err = "cannot create the synth";
        else if (fluid_synth_sfload(next->synth, path.c_str(), 1) == FLUID_FAILED) err = log.empty() ? "cannot load" : log;
        loadLog = nullptr;
        if (!err.empty()) {
            delete next;
            next = nullptr;
        }
    }
    if (err.empty()) {
        Synth* old = synth_.take();
        synth_.publish(next);
        synthGen_.fetch_add(1, std::memory_order_release);
        delete old;
    }
    std::lock_guard<std::mutex> lock(lock_);
    busy_ = false;
    if (err.empty()) {
        font_ = path;
        error_.clear();
    } else {
        error_ = err;
        if (!pending_) path_ = font_;  // the font playing stays the chosen one
    }
}

void FluidEngine::replay(Synth& s) {
    for (int c = 0; c < 16; c++) {
        for (int k = 0; k < 120; k++)
            if (cc_[c][k] >= 0) fluid_synth_cc(s.synth, c, k, cc_[c][k]);
        if (program_[c] >= 0) fluid_synth_program_change(s.synth, c, program_[c]);
        if (bend_[c] >= 0) fluid_synth_pitch_bend(s.synth, c, bend_[c]);
    }
}

void FluidEngine::track(const Event& e) {
    int c = e.status & 0x0F, d1 = e.d1 & 0x7F, d2 = e.d2 & 0x7F;
    switch (e.status & 0xF0) {
        case 0xB0:
            if (d1 < 120) cc_[c][d1] = int16_t(d2);  // 120..127 are channel mode messages, not state
            if (d1 == 121) std::fill(cc_[c], cc_[c] + 120, int16_t(-1));  // reset controllers
            break;
        case 0xC0: program_[c] = int16_t(d1); break;
        case 0xE0: bend_[c] = int16_t(d1 | d2 << 7); break;
        default: break;
    }
}

void FluidEngine::send(Synth& s, const Event& e) {
    int c = e.status & 0x0F, d1 = e.d1 & 0x7F, d2 = e.d2 & 0x7F;
    auto* f = s.synth;
    switch (e.status & 0xF0) {
        case 0x80: fluid_synth_noteoff(f, c, d1); break;
        case 0x90: fluid_synth_noteon(f, c, d1, d2); break;  // velocity 0 is a note-off
        case 0xA0: fluid_synth_key_pressure(f, c, d1, d2); break;
        case 0xB0: fluid_synth_cc(f, c, d1, d2); break;
        case 0xC0: fluid_synth_program_change(f, c, d1); break;
        case 0xD0: fluid_synth_channel_pressure(f, c, d1); break;
        case 0xE0: fluid_synth_pitch_bend(f, c, d1 | d2 << 7); break;
        default: break;
    }
}

void FluidEngine::apply(Synth& s, float cvGain) {
    const Settings& n = settings;
    float gain = std::clamp(n.gain + cvGain, 0.0f, 2.0f);
    if (gain != gainPlayed_) fluid_synth_set_gain(s.synth, gain);
    gainPlayed_ = gain;
    // below MAX_VOICES, a lower polyphony ends voices and allocates nothing
    if (n.voices != applied_.voices) fluid_synth_set_polyphony(s.synth, std::clamp(n.voices, 1, MAX_VOICES));
    if (n.reverb != applied_.reverb) fluid_synth_set_reverb_group_level(s.synth, -1, std::clamp(n.reverb, 0.0f, 1.0f));
    if (n.chorus != applied_.chorus)
        fluid_synth_set_chorus_group_level(s.synth, -1, 4.0 * std::clamp(n.chorus, 0.0f, 1.0f));
    applied_ = n;
}

void FluidEngine::process(const float* const* in, float* const* out, int n) {
    std::memset(out[O_L], 0, size_t(n) * sizeof(float));
    std::memset(out[O_R], 0, size_t(n) * sizeof(float));
    Synth* s = synth_.begin();
    Event e;
    if (!s) {
        while (events_.try_dequeue(e)) track(e);  // for the next synth; notes are dropped
        playing_.store(0, std::memory_order_relaxed);
        synth_.end();
        return;
    }
    unsigned gen = synthGen_.load(std::memory_order_acquire);
    if (gen != seenGen_) {
        seenGen_ = gen;
        replay(*s);
        gainPlayed_ = -1.0f;
        applied_ = Settings{ -1.0f, -1.0f, -1.0f, -1 };  // every setting to the new synth
    }
    apply(*s, in ? in[I_GAIN][0] : 0.0f);
    while (events_.try_dequeue(e)) {
        track(e);
        send(*s, e);
    }
    // reverb and chorus mix into the dry outputs: the effect buffers alias them
    float* dry[2] = { out[O_L], out[O_R] };
    float* fx[4] = { out[O_L], out[O_R], out[O_L], out[O_R] };
    fluid_synth_process(s->synth, n, 4, fx, 2, dry);
    playing_.store(fluid_synth_get_active_voice_count(s->synth), std::memory_order_relaxed);
    synth_.end();
}

}  // namespace fsyn
