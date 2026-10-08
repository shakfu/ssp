#include "PstretchEngine.h"

#include "engine/Dsp.h"

#include <algorithm>
#include <cmath>

namespace pstretch {

using ssp::engine::Station;
using ssp::engine::Stream;

static constexpr float CENTRE = 0.70710678f;
static constexpr float TWO_PI = 6.28318530717958647692f;
static constexpr float GATE_ON = 0.4f, GATE_OFF = 0.2f;  // 2 V and 1 V
static constexpr float MOD_RATE_MIN = 0.03f;             // Hz at rate 0
static constexpr float MOD_DIFFUSE = 0.5f;               // full-depth swings, as sk-engines
static constexpr float MOD_STRETCH = 0.35f;
static constexpr float MOD_TONE = 0.4f;
static constexpr float MAX_OCTAVES = 2.0f;  // pitch with CV; bounds a grain to 4 windows of input
static constexpr float SETTLE_S = 0.06f;    // a clip or position change must hold this long to reopen
static constexpr float POSITION_STEP = 0.01f;
static constexpr float GATE_OUT_S = 0.005f;
static constexpr int CHUNK = 512;  // Stretcher's work tick: samples, bins or butterflies

// a Schmitt trigger over the block; true if it rose
static bool rose(const float* x, int n, bool& high) {
    bool up = false;
    for (int i = 0; i < n; i++) {
        if (!high && x[i] > GATE_ON) high = up = true;
        else if (high && x[i] < GATE_OFF) high = false;
    }
    return up;
}

struct PstretchEngine::Core {
    static constexpr uint32_t RING = 1u << 18;  // 5.5 s at 48 kHz: the live history and capture span; two per voice
    static constexpr int LUT = 1024;            // cos/sin table for the phase smear

    struct Voice {
        std::vector<float> ring, spare, re, im, ola, fifo;
        Stretcher s;
    };

    int n, hop;
    FFT fft;
    pstretch::Shared shared;
    std::vector<float> cosT, sinT, window, invnorm, lutc, luts;
    std::vector<uint16_t> brev;
    Voice v[DECKS];
    float ticks;  // work ticks per output sample for both voices, with margin

    Core(int window_, float sr)
        : n(window_), hop(window_ / 2), cosT(size_t(hop)), sinT(size_t(hop)), window(size_t(n)),
          invnorm(size_t(hop)), lutc(LUT), luts(LUT), brev(size_t(n)) {
        fft.init(n, cosT.data(), sinT.data(), brev.data());
        for (int i = 0; i < LUT; i++) {
            lutc[size_t(i)] = std::cos(TWO_PI * float(i) / float(LUT));
            luts[size_t(i)] = std::sin(TWO_PI * float(i) / float(LUT));
        }
        // PaulStretch window, (1 - x^2)^1.25, and the 50%-overlap gain that makes it sum to unity
        for (int i = 0; i < n; i++) {
            float x = 2.0f * float(i) / float(n - 1) - 1.0f;
            window[size_t(i)] = std::pow(1.0f - x * x, 1.25f);
        }
        for (int i = 0; i < hop; i++) {
            float a = window[size_t(i)], b = window[size_t(i + hop)], d = a * a + b * b;
            invnorm[size_t(i)] = d > 1e-6f ? 1.0f / d : 0.0f;
        }
        shared.fft = &fft;
        shared.window = window.data();
        shared.invnorm = invnorm.data();
        shared.lutc = lutc.data();
        shared.luts = luts.data();
        shared.lutmask = LUT - 1;
        shared.lutscale = float(LUT) / TWO_PI;
        shared.n = n;
        shared.hop = hop;
        static constexpr uint32_t seed[DECKS] = { 0x12345678u, 0x2545F491u };
        for (int d = 0; d < DECKS; d++) {
            auto& k = v[d];
            k.ring.resize(RING);
            k.spare.resize(RING);
            k.re.resize(size_t(n));
            k.im.resize(size_t(n));
            k.ola.resize(size_t(n));
            k.fifo.resize(size_t(2 * hop));
            k.s.init(sr, &shared, k.ring.data(), k.spare.data(), RING, k.re.data(), k.im.data(), k.ola.data(), k.fifo.data(),
                     2 * hop, seed[d]);
        }
        // per hop and voice: extract, overlap-add and scroll pass n samples each, the smear n/2 bins,
        // and each transform a bit-reversal tick plus log2(n) stages of n/2 butterflies
        float log2n = std::log2(float(n));
        float perHop = 3.0f * float(n) / CHUNK + float(hop) / CHUNK + 2.0f * (2.0f + log2n * float(hop) / CHUNK);
        ticks = 1.5f * DECKS * perHop / float(hop);
    }
};

PstretchEngine::PstretchEngine() {
    file_.resize(Stretcher::kSdMaxFeed);
}

PstretchEngine::~PstretchEngine() {
    for (int d = 0; d < DECKS; d++) {
        delete sh_[d].next.load();
        delete sh_[d].retired.load();
        delete au_[d].stream;
    }
    delete gate_.take();
}

PstretchEngine::Core* PstretchEngine::build(int window) {
    auto* core = new Core(window, sr_);
    window_.store(window, std::memory_order_relaxed);
    return core;
}

void PstretchEngine::prepare(float sampleRate, int maxBlock) {
    sr_ = sampleRate;
    maxBlock_ = maxBlock;
    for (auto& w : wet_) w.assign(size_t(maxBlock), 0.0f);
    Core* core = build(wantWindow_.load());
    delete gate_.take();
    gate_.publish(core);
    applied_ = nullptr;
    for (auto& a : au_) a.clip = -1;  // reopen: a .raw clip plays at the engine rate
}

void PstretchEngine::setRoute(Route r) {
    if (r == route_) return;
    route_ = r;
    if (r != Route::Random) return;
    for (auto& a : au_) {
        rng_ = rng_ * 1664525u + 1013904223u;
        float p = float(rng_ >> 8) * (1.0f / 16777216.0f);
        a.panL = std::cos(p * 1.5707963f);
        a.panR = std::sin(p * 1.5707963f);
    }
}

void PstretchEngine::setRoot(const std::string& dir) {
    std::lock_guard<std::mutex> lock(lock_);
    root_ = dir;
    rootChanged_ = true;
}

std::string PstretchEngine::root() const {
    std::lock_guard<std::mutex> lock(lock_);
    return root_;
}

PstretchEngine::Info PstretchEngine::info(int d) const {
    Info i;
    i.frozen = sh_[d].frozen.load();
    i.clip = sh_[d].open.load();
    i.clips = clips_.load();
    i.window = window_.load();
    std::lock_guard<std::mutex> lock(lock_);
    if (i.clip >= 0 && i.clip < int(names_.size())) i.clipName = names_[size_t(i.clip)];
    return i;
}

// The knobs plus CV plus one modulation target, pushed into the voice; returns the dry/wet.
float PstretchEngine::modulate(int d, const float* const* in, int n, Stretcher& v) {
    auto& a = au_[d];
    auto& c = ctl_[d];
    float m;
    if (c.modShape == ModShape::Follow) {
        float pk = 0.0f;
        for (int i = 0; i < n; i++) pk = std::max(pk, std::fabs(in[I_IN][i]));
        a.follow += (pk - a.follow) * (pk > a.follow ? 0.3f : 0.02f);  // fast attack, slow release
        m = 2.0f * std::min(a.follow, 1.0f) - 1.0f;
    } else {
        a.lfoPhase += MOD_RATE_MIN * std::exp2(c.modRate * 8.0f) * float(n) / sr_;
        if (a.lfoPhase >= 1.0f) a.gateOut = int(GATE_OUT_S * sr_);
        a.lfoPhase -= std::floor(a.lfoPhase);
        m = c.modShape == ModShape::Triangle ? 4.0f * std::fabs(a.lfoPhase - 0.5f) - 1.0f
                                             : std::sin(TWO_PI * a.lfoPhase);
    }
    a.lfo = 0.5f * (m + 1.0f);

    float stretch = c.stretch + in[I_STRETCH][n - 1];
    float diffuse = c.diffuse;
    float tone = c.tone * c.tone;  // as sk-engines: 0 dark, 1 open
    float oct = (c.pitch - 0.5f) * 2.0f + in[I_PITCH][n - 1] / ssp::engine::CV_PER_VOLT;
    if (c.modDepth > 0.0f) {
        switch (c.modTarget) {
            case ModTarget::Diffusion: diffuse += m * c.modDepth * MOD_DIFFUSE; break;
            case ModTarget::Stretch: stretch += m * c.modDepth * MOD_STRETCH; break;
            case ModTarget::Tone: tone += m * c.modDepth * MOD_TONE; break;
        }
    }
    v.set_stretch(std::pow(64.0f, std::clamp(stretch, 0.0f, 1.0f)));
    v.set_diffusion(std::clamp(diffuse, 0.0f, 1.0f));
    v.set_pitch(std::exp2(std::clamp(oct, -MAX_OCTAVES, MAX_OCTAVES)));
    a.tone = std::clamp(tone, 0.0f, 1.0f);
    return std::clamp(c.mix + in[I_MIX][n - 1], 0.0f, 1.0f);
}

// Asks the worker for the selected clip at Position, once a change has held SETTLE_S.
void PstretchEngine::requestFile(int d, int n) {
    auto& a = au_[d];
    auto& c = ctl_[d];
    if (c.source != Source::File) {
        a.clip = -1;  // entering File opens at once
        a.pendingAge = -1;
        return;
    }
    uint32_t gen = gen_.load(std::memory_order_acquire);
    int count = clips_.load(std::memory_order_relaxed);
    int want = count > 0 ? std::clamp(int(c.clip * float(count)), 0, count - 1) : -1;
    float pos = c.position;
    bool changed = gen != a.gen || want != a.clip || std::fabs(pos - a.position) > POSITION_STEP;
    if (!changed) {
        a.pendingAge = -1;
        return;
    }
    if (a.pendingAge < 0 || want != a.pendingClip || pos != a.pendingPos) {
        a.pendingClip = want;
        a.pendingPos = pos;
        a.pendingAge = 0;
    } else {
        a.pendingAge += n;
    }
    if (a.clip >= 0 && gen == a.gen && float(a.pendingAge) < SETTLE_S * sr_) return;
    a.clip = want;
    a.position = pos;
    a.gen = gen;
    a.pendingAge = -1;
    sh_[d].clip.store(want, std::memory_order_relaxed);
    sh_[d].position.store(pos, std::memory_order_relaxed);
    sh_[d].request.fetch_add(1, std::memory_order_release);
}

// Feeds the voice the clip frames its slow read head needs.
void PstretchEngine::feedFile(int d, Stretcher& v) {
    Stream* s = au_[d].stream;
    if (s == nullptr) return;
    size_t k = std::min({ size_t(v.sd_want()), s->available(), file_.size() });
    for (size_t i = 0; i < k; i++) file_[i] = s->pull();
    v.feed_sd(file_.data(), uint32_t(k));
}

void PstretchEngine::process(const float* const* in, float* const* out, int n) {
    n = std::min(n, maxBlock_);
    Core* core = gate_.begin();
    if (core == nullptr) {  // a window change is being swapped in
        for (int c = 0; c < O_MAX; c++) std::fill(out[c], out[c] + n, 0.0f);
        gate_.end();
        return;
    }
    bool fresh = core != applied_;
    applied_ = core;

    float mix[DECKS];
    for (int d = 0; d < DECKS; d++) {
        auto& a = au_[d];
        auto& c = ctl_[d];
        auto& s = sh_[d];
        Stretcher& v = core->v[d].s;
        const float* const* cv = in + d * I_PER_DECK;

        bool grab = c.grab && !a.grabHigh;
        a.grabHigh = c.grab;
        if (c.freeze && !a.freezeHigh) a.frozen = !a.frozen;
        a.freezeHigh = c.freeze;
        // the gate re-captures in Capture, else toggles freeze
        if (rose(cv[I_GATE], n, a.gateHigh)) {
            if (c.source == Source::Capture) grab = true;
            else a.frozen = !a.frozen;
        }
        s.frozen.store(a.frozen, std::memory_order_relaxed);
        v.set_freeze(a.frozen);
        v.set_capture(c.source == Source::Capture);  // entering snapshots the ring
        if (grab && c.source == Source::Capture) {
            v.set_capture(false);
            v.set_capture(true);
        }
        v.set_sd(c.source == Source::File);  // entering restarts the file heads

        // a new stream replaces the old once the worker has freed the one before
        if (s.retired.load(std::memory_order_acquire) == nullptr) {
            if (Stream* next = s.next.exchange(nullptr, std::memory_order_acq_rel)) {
                s.retired.store(a.stream, std::memory_order_release);
                a.stream = next;
                a.rewind = true;
            }
        }
        if (c.source == Source::File) {
            if (a.rewind || fresh) v.sd_rewind();
            a.rewind = false;
            v.set_sd_rate(a.stream ? a.stream->rate() : 0.0f);
        } else {
            v.set_sd_rate(0.0f);
        }

        mix[d] = modulate(d, cv, n, v);
        requestFile(d, n);
        if (c.source == Source::File) feedFile(d, v);
        else v.write_input(cv[I_IN], size_t(n));
    }

    // deck A takes what it needs of the shared budget, B the rest
    int budget = int(core->ticks * float(n)) + 1;
    budget -= core->v[0].s.work(budget);
    if (budget > 0) core->v[1].s.work(budget);
    for (int d = 0; d < DECKS; d++) core->v[d].s.drain(wet_[d].data(), size_t(n));
    gate_.end();

    float x = std::clamp(xfade_ + in[I_XFADE][n - 1], 0.0f, 1.0f);
    float gA = x <= 0.5f ? 1.0f : 2.0f * (1.0f - x), gB = x >= 0.5f ? 1.0f : 2.0f * x;
    float pLa, pRa, pLb, pRb;
    switch (route_) {
        case Route::Split: pLa = 1.0f, pRa = 0.0f, pLb = 0.0f, pRb = 1.0f; break;
        case Route::Random: pLa = au_[0].panL, pRa = au_[0].panR, pLb = au_[1].panL, pRb = au_[1].panR; break;
        default: pLa = pRa = pLb = pRb = CENTRE;
    }
    auto& A = au_[0];
    auto& B = au_[1];
    const float* inA = in[I_IN];
    const float* inB = in[I_PER_DECK + I_IN];
    for (int i = 0; i < n; i++) {
        float a = inA[i] * (1.0f - mix[0]) + wet_[0][size_t(i)] * mix[0];
        float b = inB[i] * (1.0f - mix[1]) + wet_[1][size_t(i)] * mix[1];
        A.lp += A.tone * (a - A.lp);
        B.lp += B.tone * (b - B.lp);
        out[O_A][i] = A.lp;
        out[O_B][i] = B.lp;
        out[O_L][i] = ssp::engine::softLimit(A.lp * gA * pLa + B.lp * gB * pLb);
        out[O_R][i] = ssp::engine::softLimit(A.lp * gA * pRa + B.lp * gB * pRb);
        out[O_LFO_A][i] = A.lfo;
        out[O_LFO_B][i] = B.lfo;
        out[O_GATE_A][i] = A.gateOut > 0 ? 1.0f : 0.0f;
        out[O_GATE_B][i] = B.gateOut > 0 ? 1.0f : 0.0f;
        A.gateOut -= A.gateOut > 0;
        B.gateOut -= B.gateOut > 0;
    }
}

void PstretchEngine::openClip(int d, int clip, float position) {
    auto& s = sh_[d];
    auto* st = new Stream();
    if (clip >= 0 && clip < int(stations_.size())) {
        const Station& info = stations_[size_t(clip)];
        float rate = info.rate > 0 ? float(info.rate) : sr_;
        auto frame = uint64_t(double(std::clamp(position, 0.0f, 1.0f)) * double(info.frames));
        st->open(info, frame % info.frames, rate);
    } else {
        clip = -1;
    }
    s.open.store(clip, std::memory_order_relaxed);
    delete s.next.exchange(st, std::memory_order_acq_rel);  // one the audio thread has not taken
    live_[d] = st;
}

void PstretchEngine::idle() {
    int want = wantWindow_.load(std::memory_order_relaxed);
    if (want != window_.load(std::memory_order_relaxed)) {
        Core* core = build(want);
        delete gate_.take();
        gate_.publish(core);
    }
    if (rootChanged_.exchange(false)) {
        stations_ = ssp::engine::scanBank(root());
        std::vector<std::string> names;
        for (auto& st : stations_) names.push_back(st.name);
        {
            std::lock_guard<std::mutex> lock(lock_);
            names_ = std::move(names);
        }
        clips_.store(int(stations_.size()), std::memory_order_relaxed);
        gen_.fetch_add(1, std::memory_order_release);
    }
    for (int d = 0; d < DECKS; d++) {
        auto& s = sh_[d];
        delete s.retired.exchange(nullptr, std::memory_order_acq_rel);
        uint32_t r = s.request.load(std::memory_order_acquire);
        if (r != served_[d]) {
            served_[d] = r;
            openClip(d, s.clip.load(std::memory_order_relaxed), s.position.load(std::memory_order_relaxed));
        }
        if (live_[d] != nullptr) live_[d]->fill();
    }
}

}  // namespace pstretch
