#include "RadioEngine.h"

#include "engine/Dsp.h"

#include <strings.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace radio {

static constexpr float STATION_HYST = 0.25f;  // fraction of a station's width
static constexpr float SETTLE_S = 0.18f;      // a choice must hold this long to switch, below STATIC_THRESH
static constexpr float STATIC_THRESH = 0.15f; // static at or above this switches at once, as a tuning dial
static constexpr float STATIC_S = 0.1f;       // tuning burst decay
static constexpr float NOISE_FC = 1700.0f;    // static colour
static constexpr float NOISE_LEVEL = 0.3f;
static constexpr int64_t RESCAN_MS = 1000;
static constexpr float CENTRE = 0.70710678f;
static constexpr float START_STEP = 1.0f / 256.0f;  // a Start move smaller than this is noise

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

RadioEngine::RadioEngine() {
    for (auto& m : mono_) m.resize(1);
}

RadioEngine::~RadioEngine() {
    for (int d = 0; d < DECKS; d++) {
        delete sh_[d].next.load();
        delete sh_[d].retired.load();
        delete au_[d].cur.s;
        delete au_[d].prev.s;
    }
}

void RadioEngine::prepare(float sampleRate, int maxBlock) {
    sr_ = sampleRate;
    noiseK_ = 1.0f - std::exp(-6.2831853f * NOISE_FC / sr_);
    for (auto& m : mono_) m.assign(size_t(maxBlock), 0.0f);
}

void RadioEngine::setCrossfade(float x) {
    gA_ = x <= 0.5f ? 1.0f : 2.0f * (1.0f - x);
    gB_ = x >= 0.5f ? 1.0f : 2.0f * x;
}

void RadioEngine::setRoute(Route r) {
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

void RadioEngine::setRoot(const std::string& dir) {
    std::lock_guard<std::mutex> lock(lock_);
    root_ = dir;
    rootChanged_ = true;
}

std::string RadioEngine::root() const {
    std::lock_guard<std::mutex> lock(lock_);
    return root_;
}

RadioEngine::Info RadioEngine::info(int d) const {
    Info i;
    i.banks = banks_.load();
    i.bank = sh_[d].bank.load();
    i.stations = sh_[d].stations.load();
    i.station = sh_[d].open.load();
    i.playing = sh_[d].playing.load();
    i.error = sh_[d].error.load();
    std::lock_guard<std::mutex> lock(lock_);
    i.bankName = bankName_[d];
    i.stationName = stationName_[d];
    return i;
}

// Holds the open station until x passes its edge by STATION_HYST, so CV noise at an edge cannot
// flip between two stations.
int RadioEngine::quantise(int d, float x, int n) const {
    if (n <= 0) return -1;
    float pos = std::clamp(x, 0.0f, 1.0f) * float(n - 1);
    auto nearest = [n](float p) { return std::clamp(int(std::lround(p)), 0, n - 1); };
    int cur = sh_[d].open.load(std::memory_order_relaxed);
    if (cur < 0 || cur >= n) return nearest(pos);
    if (pos > float(cur) + 0.5f + STATION_HYST || pos < float(cur) - 0.5f - STATION_HYST) return nearest(pos);
    return cur;
}

float RadioEngine::noise(int d) {
    rng_ = rng_ * 1664525u + 1013904223u;
    float w = float(int32_t(rng_)) * (1.0f / 2147483648.0f);
    au_[d].noiseLp += noiseK_ * (w - au_[d].noiseLp);
    return au_[d].noiseLp * NOISE_LEVEL;
}

// One output frame of v, read at `step` source frames per frame; 0 when it is not playing.
float RadioEngine::read(Voice& v, float step) {
    if (!v.playing()) {
        v.primed = false;
        return 0.0f;
    }
    if (!v.primed) {
        v.a = v.s->pull();
        v.b = v.s->pull();
        v.phase = 0.0f;
        v.primed = true;
    }
    float x = v.a + (v.b - v.a) * v.phase;
    v.phase += step;
    while (v.phase >= 1.0f) {
        v.phase -= 1.0f;
        v.a = v.b;
        v.b = v.s->pull();
    }
    return x;
}

void RadioEngine::render(int d, float* mono, int n, float octaves) {
    auto& a = au_[d];
    auto& c = ctl_[d];
    float dec = 1.0f / (STATIC_S * sr_);
    float fadeStep = 1.0f / std::max(1.0f, fadeMs_ * 0.001f * sr_);
    float speed = std::exp2(octaves) / sr_;
    float stepCur = a.cur.s ? speed * a.cur.s->rate() : 0.0f;
    float stepPrev = a.prev.s ? speed * a.prev.s->rate() : 0.0f;
    bool tuned = a.cur.playing();
    for (int i = 0; i < n; i++) {
        float x = read(a.cur, stepCur);
        if (a.fade < 1.0f) {
            float y = read(a.prev, stepPrev);
            x = y + (x - y) * a.fade;
            a.fade = std::min(1.0f, a.fade + fadeStep);
        }
        // between stations, static alone at the Static level
        float st = tuned ? c.noise * a.staticEnv : c.noise;
        mono[i] = x * (1.0f - st) + noise(d) * st;
        a.staticEnv = std::max(0.0f, a.staticEnv - dec);
    }
}

void RadioEngine::process(const float* const* in, float* const* out, int n) {
    n = std::min(n, int(mono_[0].size()));
    for (int d = 0; d < DECKS; d++) {
        auto& s = sh_[d];
        auto& a = au_[d];
        auto& c = ctl_[d];
        const float* const* cv = in + d * I_PER_DECK;

        // a finished fade hands the old stream back; a new one starts only once it has
        if (a.prev.s != nullptr && a.fade >= 1.0f && s.retired.load(std::memory_order_acquire) == nullptr) {
            s.retired.store(a.prev.s, std::memory_order_release);
            a.prev = Voice();
        }
        if (a.prev.s == nullptr) {
            if (Stream* next = s.next.exchange(nullptr, std::memory_order_acq_rel)) {
                a.prev = a.cur;
                a.cur = Voice();
                a.cur.s = next;
                a.fade = 0.0f;
                if (next->tuned()) a.staticEnv = 1.0f;
            }
        }
        s.playing.store(a.cur.playing(), std::memory_order_relaxed);

        int banks = banks_.load(std::memory_order_relaxed);
        s.bank.store(banks > 0 ? std::clamp(int(c.bank * float(banks)), 0, banks - 1) : -1, std::memory_order_relaxed);
        s.start.store(std::clamp(c.start + cv[I_START][n - 1], 0.0f, 1.0f), std::memory_order_relaxed);

        // the gen load pairs with the worker's release of the station count
        uint32_t gen = s.gen.load(std::memory_order_acquire);
        int want = quantise(d, c.station + cv[I_STATION][n - 1], s.stations.load(std::memory_order_relaxed));
        uint32_t packed = pack(gen, want);
        if (packed != a.pending) {
            a.pending = packed;
            a.pendingAge = 0;
        } else {
            a.pendingAge += n;
        }
        // nothing open: nothing to protect from chatter
        int open = s.open.load(std::memory_order_relaxed);
        bool settled = open < 0 || float(a.pendingAge) >= SETTLE_S * sr_;
        if (want != open && (c.noise >= STATIC_THRESH || settled))
            s.commit.store(packed, std::memory_order_release);

        bool high = c.reset;
        for (int i = 0; i < n; i++) high = high || cv[I_RESET][i] > 0.5f;
        if (high && !a.resetHigh) s.reset.store(true, std::memory_order_release);
        a.resetHigh = high;

        // Start Immediate: a move jumps to the new Start, as a reset does
        float cvStart = cv[I_START][n - 1];
        bool potMoved = a.startKnob >= 0.0f && std::fabs(c.start - a.startKnob) > START_STEP;
        bool cvMoved = std::fabs(cvStart - a.startCv) > START_STEP;
        if ((startPotImmediate_ && potMoved) || (startCvImmediate_ && cvMoved))
            s.reset.store(true, std::memory_order_release);
        if (potMoved || a.startKnob < 0.0f) a.startKnob = c.start;
        if (cvMoved) a.startCv = cvStart;

        render(d, mono_[d].data(), n, c.speed + cv[I_SPEED][n - 1] / ssp::engine::CV_PER_VOLT);
    }

    float pLa, pRa, pLb, pRb;
    switch (route_) {
        case Route::Split: pLa = 1.0f, pRa = 0.0f, pLb = 0.0f, pRb = 1.0f; break;
        case Route::Random: pLa = au_[0].panL, pRa = au_[0].panR, pLb = au_[1].panL, pRb = au_[1].panR; break;
        default: pLa = pRa = pLb = pRb = CENTRE;
    }
    float la = ctl_[0].level, lb = ctl_[1].level;
    for (int i = 0; i < n; i++) {
        float A = mono_[0][size_t(i)] * la, B = mono_[1][size_t(i)] * lb;
        out[O_A][i] = A;
        out[O_B][i] = B;
        out[O_L][i] = ssp::engine::softLimit(A * gA_ * pLa + B * gB_ * pLb);
        out[O_R][i] = ssp::engine::softLimit(A * gA_ * pRa + B * gB_ * pRb);
    }
    clock_.fetch_add(uint64_t(n), std::memory_order_relaxed);
}

// Replaces the stream the audio thread will pick up; one it has not picked up yet is freed here.
void RadioEngine::publish(int d, Stream* s) {
    delete sh_[d].next.exchange(s, std::memory_order_acq_rel);
    wk_[d].live = s;
}

// live: at the station's free-running position; else at the start offset (a reset).
void RadioEngine::openStation(int d, int station, bool live) {
    auto& w = wk_[d];
    auto& s = sh_[d];
    auto* st = new Stream(station != w.open);
    w.open = station;
    s.open.store(station, std::memory_order_relaxed);
    s.error.store(false, std::memory_order_relaxed);
    std::string name;
    if (station >= 0 && station < int(w.stations.size())) {
        const Station& info = w.stations[size_t(station)];
        float rate = info.rate > 0 ? float(info.rate) : rawRate_.load(std::memory_order_relaxed);
        auto offset = uint64_t(double(s.start.load(std::memory_order_relaxed)) * double(info.frames));
        if (live) offset += uint64_t(double(clock_.load(std::memory_order_relaxed)) / sr_ * rate);
        if (st->open(info, offset % info.frames, rate)) name = info.name;
        else s.error.store(true, std::memory_order_relaxed);
    }
    publish(d, st);  // an unopened stream is silence
    std::lock_guard<std::mutex> lock(lock_);
    stationName_[d] = name;
}

void RadioEngine::scanBank(int d, int bank) {
    auto& w = wk_[d];
    auto& s = sh_[d];
    w.bank = bank;
    w.stations = bank >= 0 && bank < int(bankDirs_.size()) ? ssp::engine::scanBank(bankDirs_[size_t(bank)])
                                                          : std::vector<Station>();
    w.rescanAt = nowMs() + RESCAN_MS;
    s.stations.store(int(w.stations.size()), std::memory_order_relaxed);
    s.gen.store(++w.gen, std::memory_order_release);
    openStation(d, -1, true);
    std::string name = bank >= 0 && bank < int(bankDirs_.size()) ? bankDirs_[size_t(bank)] : std::string();
    std::lock_guard<std::mutex> lock(lock_);
    bankName_[d] = name.substr(name.find_last_of('/') + 1);
}

void RadioEngine::idle() {
    bool rescanAll = rootChanged_.exchange(false);
    if (rescanAll) {
        bankDirs_ = ssp::engine::scanBanks(root());
        banks_.store(int(bankDirs_.size()));
    }
    for (int d = 0; d < DECKS; d++) {
        auto& w = wk_[d];
        auto& s = sh_[d];
        delete s.retired.exchange(nullptr, std::memory_order_acq_rel);

        int bank = s.bank.load(std::memory_order_relaxed);
        if (rescanAll || bank != w.bank || (w.stations.empty() && nowMs() >= w.rescanAt)) scanBank(d, bank);

        uint32_t c = s.commit.load(std::memory_order_acquire);
        if (c >> 16 == (w.gen & 0xFFFF)) {
            int want = int(c & 0xFFFF) - 1;
            if (want != w.open) openStation(d, want, true);
        }
        if (s.reset.exchange(false) && w.open >= 0) openStation(d, w.open, false);
        if (w.live != nullptr) w.live->fill();
    }
}

Settings readSettings(const std::string& root) {
    Settings st;
    std::string path;
    for (auto& name : ssp::engine::listDir(root, false))
        if (strcasecmp(name.c_str(), "settings.txt") == 0) path = root + "/" + name;
    FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "r");
    if (f == nullptr) return st;
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        char* eq = std::strchr(line, '=');
        if (eq == nullptr) continue;
        *eq = '\0';
        std::string key = line;
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        int v = std::atoi(eq + 1);
        if (strcasecmp(key.c_str(), "crossfadeTime") == 0 || strcasecmp(key.c_str(), "DECLICK") == 0) st.fadeMs = v;
        else if (strcasecmp(key.c_str(), "startPotImmediate") == 0) st.startPotImmediate = v != 0;
        else if (strcasecmp(key.c_str(), "startCVImmediate") == 0) st.startCvImmediate = v != 0;
    }
    std::fclose(f);
    return st;
}

}  // namespace radio
