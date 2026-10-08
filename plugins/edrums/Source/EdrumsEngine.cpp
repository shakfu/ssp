#include "EdrumsEngine.h"

#include "engine/Dsp.h"

#include <algorithm>
#include <cmath>

namespace edrums {

static constexpr float GATE_ON = 0.4f, GATE_OFF = 0.2f;  // 2 V and 1 V
static constexpr float BUS_TRIM = 0.6f;                  // four voices sum hot before the limiter
static constexpr uint32_t SEED[EdrumsEngine::DRUMS] = { 0x9e3779b9u, 0x85ebca6bu, 0x6d2b79f5u, 0xc2b2ae35u };

static float rand01(uint32_t& rng) {
    rng = rng * 1664525u + 1013904223u;
    return float(rng >> 8) * (1.0f / 16777216.0f);
}

// a Schmitt trigger; true on a rising edge
static bool edge(float v, bool& high) {
    if (!high && v > GATE_ON) return high = true;
    if (high && v < GATE_OFF) high = false;
    return false;
}

EdrumsEngine::EdrumsEngine() {
    static constexpr Model model[DRUMS] = { Model::Kick, Model::Tom, Model::Snare, Model::Hat };
    for (int d = 0; d < DRUMS; d++) {
        ctl_[d].model = model[d];
        dk_[d].rng ^= SEED[d];
    }
}

void EdrumsEngine::prepare(float sampleRate, int) {
    sr_ = sampleRate;
    for (int d = 0; d < DRUMS; d++) {
        dk_[d].voice.init(sr_, ctl_[d].model, SEED[d]);
        dk_[d].fresh = true;
    }
}

EdrumsEngine::Info EdrumsEngine::info(int d) const {
    auto& k = dk_[d];
    Info i;
    i.onsets = k.onsets.load(std::memory_order_relaxed);
    i.steps = k.steps.load(std::memory_order_relaxed);
    i.position = k.position.load(std::memory_order_relaxed);
    i.hits = k.hits.load(std::memory_order_relaxed);
    return i;
}

// Pushes the controls that changed into the voice and pattern.
void EdrumsEngine::apply(int d) {
    auto& k = dk_[d];
    auto& c = ctl_[d];
    auto& a = k.applied;
    bool all = k.fresh;
    if (all || c.model != a.model) k.voice.setModel(c.model);
    if (all || c.model != a.model || c.pitch != a.pitch) k.voice.setPitch(c.pitch);
    if (all || c.model != a.model || c.decay != a.decay) k.voice.setDecay(c.decay);
    if (all || c.drive != a.drive) k.voice.setDrive(c.drive);
    if (all || c.sweep != a.sweep) k.voice.setSweep(c.sweep);
    if (all || c.tone != a.tone) k.voice.setTone(c.tone);
    if (all || c.bright != a.bright) k.voice.setBright(c.bright);
    bool shape = all || c.steps != a.steps || c.hits != a.hits || c.rotate != a.rotate;
    if (shape) {
        k.pattern.setLength(c.steps);
        k.pattern.setOnsets(c.hits);
        k.pattern.setRotation(c.rotate);
        uint16_t bits = 0;
        for (int s = 0; s < k.pattern.steps(); s++)
            if (k.pattern.isOnset(s)) bits |= uint16_t(1u << s);
        k.onsets.store(bits, std::memory_order_relaxed);
        k.steps.store(k.pattern.steps(), std::memory_order_relaxed);
    }
    if (c.trigger && !k.trigHigh) fire(k, c.voice, trigLen(d));
    k.trigHigh = c.trigger;
    a = c;
    k.fresh = false;
}

// samples per step at the track's rate; 0 before two clock pulses
double EdrumsEngine::stepLength(int d) const {
    return period_ * std::max(1, ctl_[d].div) / std::max(1, ctl_[d].mult);
}

// at most half the shortest gap between steps, which swing narrows to (1 - swing) x 2 steps
int EdrumsEngine::trigLen(int d) const {
    int len = int(TRIG_S * sr_);
    if (period_ > 0.0) len = std::min(len, int(stepLength(d) * (1.0 - std::clamp(ctl_[d].swing, 0.5f, 0.75f))));
    return std::max(1, len);
}

void EdrumsEngine::fire(Drum& k, bool voice, int trigLen) {
    k.pan = rand01(k.rng);  // for the random route
    if (voice) k.voice.trigger();
    k.trigGap = k.trigLeft > 0;
    k.trigLeft = trigLen;
    k.hits.fetch_add(1, std::memory_order_relaxed);
}

void EdrumsEngine::reset() {
    for (auto& k : dk_) {
        k.pattern.reset();
        k.position.store(0, std::memory_order_relaxed);
        k.pulses = 0;
        k.subLeft = 0;
        k.count = 0;
        k.swung = false;
    }
}

// A step is due: even steps play now, odd ones after the swing delay. A swung step still waiting
// plays first.
void EdrumsEngine::due(int d) {
    auto& k = dk_[d];
    if (k.swung) {
        k.swung = false;
        step(d);
    }
    double delay = (k.count++ & 1) ? (std::clamp(ctl_[d].swing, 0.5f, 0.75f) - 0.5) * 2.0 * stepLength(d) : 0.0;
    if (delay >= 1.0) {
        k.swung = true;
        k.swingIn = delay + 1.0;  // counted down from this sample on
    } else {
        step(d);
    }
}

void EdrumsEngine::step(int d) {
    auto& k = dk_[d];
    auto& c = ctl_[d];
    bool onset = k.pattern.trigger();  // a muted track keeps its place
    k.position.store(k.pattern.position(), std::memory_order_relaxed);
    if (onset && !c.mute && (c.chance >= 1.0f || rand01(k.rng) < c.chance)) fire(k, c.voice, trigLen(d));
}

// One clock pulse. A track with div N steps on every Nth pulse; one with mult N steps now and N-1
// more times, evenly over the last measured clock period. A pulse that comes early drops the rest.
void EdrumsEngine::clock() {
    if (sinceClock_ > 0.0) period_ = sinceClock_;
    sinceClock_ = 0.0;
    for (int d = 0; d < DRUMS; d++) {
        auto& k = dk_[d];
        auto& c = ctl_[d];
        int div = std::max(1, c.div), mult = std::max(1, c.mult);
        if (k.pulses++ % uint32_t(div) != 0) continue;
        due(d);
        k.subLeft = period_ > 0.0 ? mult - 1 : 0;  // the first pulse has no period yet
        k.subIn = period_ / mult + 1.0;  // counted down from this sample on
    }
}

void EdrumsEngine::process(const float* const* in, float* const* out, int n) {
    for (int d = 0; d < DRUMS; d++) apply(d);
    float gain[DRUMS];
    for (int d = 0; d < DRUMS; d++) gain[d] = ctl_[d].mute ? 0.0f : ctl_[d].level;

    for (int i = 0; i < n; i++) {
        // a reset and a clock on the same sample play the first step
        if (edge(in[I_RESET][i], resetHigh_)) reset();
        if (edge(in[I_CLOCK][i], clockHigh_)) clock();
        sinceClock_ += 1.0;
        float v[DRUMS];
        for (int d = 0; d < DRUMS; d++) {
            auto& k = dk_[d];
            if (k.subLeft > 0 && (k.subIn -= 1.0) <= 0.0) {
                k.subLeft--;
                k.subIn += period_ / std::max(1, ctl_[d].mult);
                due(d);
            }
            if (k.swung && (k.swingIn -= 1.0) <= 0.0) {
                k.swung = false;
                step(d);
            }
            v[d] = k.voice.process() * gain[d];
            out[O_1 + d][i] = v[d];
            if (k.trigGap) {
                out[O_TRIG_1 + d][i] = 0.0f;
                k.trigGap = false;
            } else {
                out[O_TRIG_1 + d][i] = k.trigLeft > 0 ? 1.0f : 0.0f;
                k.trigLeft -= k.trigLeft > 0;
            }
        }
        float l, r;
        switch (route_) {
            case Route::Split:  // drums 1 and 2 left, 3 and 4 right
                l = v[0] + v[1];
                r = v[2] + v[3];
                break;
            case Route::Random:
                l = r = 0.0f;
                for (int d = 0; d < DRUMS; d++) {
                    l += v[d] * (1.0f - dk_[d].pan);
                    r += v[d] * dk_[d].pan;
                }
                break;
            default: l = r = v[0] + v[1] + v[2] + v[3];
        }
        out[O_L][i] = ssp::engine::softLimit(l * BUS_TRIM);
        out[O_R][i] = ssp::engine::softLimit(r * BUS_TRIM);
    }
}

}  // namespace edrums
