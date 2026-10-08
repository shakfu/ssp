#include "BardEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

namespace bard {

using ssp::engine::Station;

static constexpr float CENTRE = 0.70710678f;
static constexpr float GATE_ON = 0.4f, GATE_OFF = 0.2f;  // 2 V and 1 V
static constexpr int64_t SETTLE_MS = 180;                // a selector change must hold this long
static constexpr float SEL_HYST = 0.25f;                 // selector deadband, in steps
static constexpr int64_t RESCAN_MS = 1000;               // retries an empty shelf
static constexpr int64_t ERR_FLASH_MS = 1200;
static constexpr int64_t CHECKPOINT_MS = 30000;          // resume write while playing
static constexpr float SEAM_MAX_MS = 500.0f;
static constexpr float DUCK_KNEE = 4.0f;  // envelope -> duck curve steepness
static constexpr float GATE_OUT_S = 0.005f;
static constexpr float POSITION_STEP = 0.01f;

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static bool readFile(const std::string& path, std::string& text) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    text = s.str();
    return true;
}

// Holds the committed index until x passes its edge by `hyst` steps, so CV noise on an edge
// cannot flip between two targets.
static int quantise(float x, int n, int cur, float hyst) {
    if (n <= 0) return -1;
    if (n == 1) return 0;
    float pos = std::clamp(x, 0.0f, 1.0f) * float(n - 1);
    auto nearest = [n](float p) { return std::clamp(int(std::lround(p)), 0, n - 1); };
    if (cur < 0 || cur >= n) return nearest(pos);
    if (pos > float(cur) + 0.5f + hyst || pos < float(cur) - 0.5f - hyst) return nearest(pos);
    return cur;
}

// a Schmitt trigger over the block; true if it rose
static bool rose(const float* x, int n, bool& high) {
    bool up = false;
    for (int i = 0; i < n; i++) {
        if (!high && x[i] > GATE_ON) high = up = true;
        else if (high && x[i] < GATE_OFF) high = false;
    }
    return up;
}

static std::string baseName(const std::string& path) {
    return path.substr(path.find_last_of('/') + 1);
}

BardEngine::BardEngine() {
    for (auto& a : au_) a.wsola.init();
}

BardEngine::~BardEngine() {
    for (int d = 0; d < DECKS; d++) {
        delete sh_[d].next.load();
        delete sh_[d].retired.load();
        delete au_[d].cue;
    }
}

void BardEngine::prepare(float sampleRate, int maxBlock) {
    sr_ = sampleRate;
    maxBlock_ = maxBlock;
    for (auto& m : mono_) m.assign(size_t(maxBlock), 0.0f);
    voice_.assign(size_t(maxBlock), 0.0f);
    for (int d = 0; d < DECKS; d++) {
        auto& a = au_[d];
        a.roomMem.assign(Room::capacity_floats(sr_), 0.0f);
        a.room.init(a.roomMem.data(), sr_);
        a.room.set_character(a.character);
        a.roomSize = a.colour = a.rate = -1.0f;  // re-apply on the next block
        if (a.cue) a.ratio = a.cue->rate / sr_;
    }
}

void BardEngine::setRoute(Route r) {
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

void BardEngine::setRoot(const std::string& dir) {
    std::lock_guard<std::mutex> lock(lock_);
    root_ = dir;
    rootChanged_ = true;
}

std::string BardEngine::root() const {
    std::lock_guard<std::mutex> lock(lock_);
    return root_;
}

BardEngine::Info BardEngine::info(int d) const {
    auto& s = sh_[d];
    Info i;
    i.shelf = s.shelf.load();
    i.shelves = s.shelves.load();
    i.book = s.book.load();
    i.books = s.books.load();
    i.mark = s.mark.load();
    i.marks = s.marks.load();
    i.autoMarks = s.autoMarks.load();
    i.paused = s.paused.load();
    i.error = s.error.load();
    double rate = double(std::max(1u, s.rate.load()));
    i.seconds = double(s.pos.load()) / rate;
    i.length = double(s.frames.load()) / rate;
    std::lock_guard<std::mutex> lock(lock_);
    i.shelfName = shelfName_[d];
    i.bookName = bookName_[d];
    return i;
}

// ---- audio thread ------------------------------------------------------------------------------

// Takes a newly published cue once the worker has freed the one before.
void BardEngine::adopt(int d) {
    auto& a = au_[d];
    auto& s = sh_[d];
    if (s.retired.load(std::memory_order_acquire) != nullptr) return;
    Cue* next = s.next.exchange(nullptr, std::memory_order_acq_rel);
    if (next == nullptr) return;
    s.retired.store(a.cue, std::memory_order_release);
    a.cue = next;
    a.pos = next->start;
    a.ratio = next->rate / sr_;
    a.primed = false;
    a.wsola.reset();  // drop audio buffered from the old position
    a.seamGain = ctl_[d].seam > 0.001f ? 0.0f : 1.0f;
    s.pos.store(a.pos, std::memory_order_relaxed);
    s.gen.store(next->gen, std::memory_order_release);
}

// One source frame. On an underrun the playhead does not advance, so it stays what was heard.
bool BardEngine::pull(int d, float& out) {
    auto& a = au_[d];
    if (a.cue->stream.available() == 0) {
        out = 0.0f;
        return false;
    }
    out = a.cue->stream.pull();
    a.pos++;
    return true;
}

// Rate chain, voice colour and room, recomputed when their controls move.
void BardEngine::updateFx(int d) {
    auto& a = au_[d];
    auto& c = ctl_[d];
    if (c.rate != a.rate || c.keep != a.keep) {
        a.rate = c.rate;
        a.keep = c.keep;
        // unity at the centre, 0.5x at 0, 2.5x at 1: exponential each side
        float r = c.rate <= 0.5f ? std::exp2((c.rate - 0.5f) * 2.0f) : std::exp2((c.rate - 0.5f) * 2.0f * 1.32192809f);
        float k = std::clamp(c.keep, 0.0f, 1.0f);
        // resample by r^(1-keep), time-scale by r^-keep: speed x r, pitch x r^(1-keep)
        a.resK = k < 0.001f ? r : std::pow(r, 1.0f - k);
        a.wsola.set_scale(k < 0.001f ? 1.0f : std::pow(r, -k));
    }
    if (c.colour != a.colour) {
        a.colour = c.colour;
        // clean (20 Hz .. 20 kHz) to wireless to telephone (300 Hz .. 3 kHz, driven)
        a.hp.set(ssp::engine::Biquad::HighPass, sr_, 20.0f + c.colour * 280.0f, 0.707f);
        a.lp.set(ssp::engine::Biquad::LowPass, sr_, std::min(20000.0f - c.colour * 17000.0f, sr_ * 0.45f), 0.707f);
        a.drive = 1.0f + c.colour * 7.0f;
    }
    if (c.character != a.character) {
        a.character = c.character;
        a.room.set_character(c.character);
        a.roomSize = -1.0f;
    }
    if (c.room != a.roomSize) {
        a.roomSize = c.room;
        a.room.set_size(c.room);
    }
}

void BardEngine::render(int d, float* mono, int n) {
    auto& a = au_[d];
    auto& c = ctl_[d];
    auto& s = sh_[d];
    uint32_t segEnd = s.segEnd.load(std::memory_order_relaxed);
    bool live = a.cue != nullptr && a.cue->stream.playing() && !s.paused.load(std::memory_order_relaxed) &&
                a.pos < segEnd;
    if (!live) {
        a.primed = false;
        std::fill(mono, mono + n, 0.0f);
        return;
    }
    if (!a.primed) {
        pull(d, a.cur);
        pull(d, a.next);
        a.phase = 0.0f;
        a.primed = true;
    }
    // feed the time-scaler what it needs for this block, stopping at the segment end
    const float step = a.resK * a.ratio;
    for (uint32_t need = a.wsola.want(uint32_t(n)); need > 0; need--) {
        a.wsola.feed1(a.cur + (a.next - a.cur) * a.phase);
        a.phase += step;
        while (a.phase >= 1.0f) {
            a.phase -= 1.0f;
            a.cur = a.next;
            pull(d, a.next);
        }
        if (a.pos >= segEnd) break;
    }
    uint32_t got = a.wsola.drain(voice_.data(), uint32_t(n));
    bool colour = c.colourMix > 0.001f, room = c.roomMix > 0.001f;
    float seamInc = c.seam > 0.001f ? 1.0f / (c.seam * SEAM_MAX_MS * 0.001f * sr_) : 1.0f;
    for (int i = 0; i < n; i++) {
        if (uint32_t(i) >= got) {
            mono[i] = 0.0f;
            continue;
        }
        float x = voice_[size_t(i)];
        if (colour) {
            float band = a.lp.process(a.hp.process(ssp::engine::softLimit(x * a.drive)));
            x += (band - x) * c.colourMix;
        }
        if (room) x += (a.room.process(x) - x) * c.roomMix;
        if (a.seamGain < 1.0f) {
            x *= a.seamGain;
            a.seamGain = std::min(1.0f, a.seamGain + seamInc);
        }
        mono[i] = x;
    }
}

void BardEngine::process(const float* const* in, float* const* out, int n) {
    n = std::min(n, maxBlock_);
    for (int d = 0; d < DECKS; d++) {
        auto& a = au_[d];
        auto& c = ctl_[d];
        auto& s = sh_[d];
        const float* const* cv = in + d * I_PER_DECK;

        // what the worker acts on
        s.bookX.store(c.book + cv[I_BOOK][n - 1], std::memory_order_relaxed);
        s.markX.store(c.mark + cv[I_MARK][n - 1], std::memory_order_relaxed);
        s.shelfX.store(c.shelf, std::memory_order_relaxed);
        s.position.store(c.position, std::memory_order_relaxed);
        s.seq.store(int(c.seq), std::memory_order_relaxed);
        s.loop.store(int(c.loop), std::memory_order_relaxed);
        s.reroll.store(c.reroll, std::memory_order_relaxed);
        if (c.play && !a.playHigh) s.plays.fetch_add(1, std::memory_order_relaxed);
        if (c.back && !a.backHigh) s.backs.fetch_add(1, std::memory_order_relaxed);
        bool gate = rose(cv[I_GATE], n, a.gateHigh);
        if ((c.next && !a.nextHigh) || gate) s.nexts.fetch_add(1, std::memory_order_relaxed);
        a.playHigh = c.play;
        a.backHigh = c.back;
        a.nextHigh = c.next;
        uint32_t gates = s.gates.load(std::memory_order_relaxed);
        if (gates != a.gates) a.gateOut = int(GATE_OUT_S * sr_);
        a.gates = gates;

        adopt(d);
        updateFx(d);
        render(d, mono_[d].data(), n);
        s.pos.store(a.pos, std::memory_order_relaxed);
    }

    // each deck's envelope ducks the other deck, one block late
    for (int d = 0; d < DECKS; d++) {
        auto& a = au_[d];
        float pk = 0.0f;
        for (int i = 0; i < n; i++) pk = std::max(pk, std::fabs(mono_[d][size_t(i)]));
        float rel = 0.02f + 0.28f * ctl_[d].release;  // fast attack
        a.env += (pk > a.env ? 0.5f : rel) * (pk - a.env);
    }
    for (int d = 0; d < DECKS; d++) {
        int o = 1 - d;
        float g = 1.0f;
        if (ctl_[o].duck > 0.001f) g = 1.0f - ctl_[o].duck * std::min(1.0f, au_[o].env * DUCK_KNEE);
        au_[d].duckGain = g;
    }

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
    float va = std::clamp(ctl_[0].volume + in[I_VOLUME][n - 1], 0.0f, 1.0f) * A.duckGain;
    float vb = std::clamp(ctl_[1].volume + in[I_PER_DECK + I_VOLUME][n - 1], 0.0f, 1.0f) * B.duckGain;
    float envA = std::min(A.env, 1.0f), envB = std::min(B.env, 1.0f);
    for (int i = 0; i < n; i++) {
        float a = mono_[0][size_t(i)] * va, b = mono_[1][size_t(i)] * vb;
        out[O_A][i] = a;
        out[O_B][i] = b;
        out[O_L][i] = ssp::engine::softLimit(a * gA * pLa + b * gB * pLb);
        out[O_R][i] = ssp::engine::softLimit(a * gA * pRa + b * gB * pRb);
        out[O_ENV_A][i] = envA;
        out[O_ENV_B][i] = envB;
        out[O_GATE_A][i] = A.gateOut > 0 ? 1.0f : 0.0f;
        out[O_GATE_B][i] = B.gateOut > 0 ? 1.0f : 0.0f;
        A.gateOut -= A.gateOut > 0;
        B.gateOut -= B.gateOut > 0;
    }
}

// ---- worker thread -----------------------------------------------------------------------------

void BardEngine::idle() {
    int64_t now = nowMs();
    if (rootChanged_.exchange(false)) {
        // save and close against the old library before reading the new one
        for (auto& w : wk_) resumeDirty_ = resumeDirty_ || w.open >= 0;
        saveResume(now);
        for (int d = 0; d < DECKS; d++) {
            openBook(d, -1, now);
            wk_[d].shelf = -2;
        }
        loadLibrary();
    }
    for (int d = 0; d < DECKS; d++) {
        delete sh_[d].retired.exchange(nullptr, std::memory_order_acq_rel);
        work(d, now);
    }
    saveResume(now);
}

// The shelves, bard.cfg and resume.txt of the root.
void BardEngine::loadLibrary() {
    std::string dir = libRoot_ = root();
    shelfDirs_ = ssp::engine::scanBanks(dir);
    cfg_ = Config();
    resume_.clear();
    std::string text;
    if (readFile(dir + "/bard.cfg", text)) parse_config(text.c_str(), cfg_);
    if (readFile(dir + "/resume.txt", text)) resume_.parse(text.c_str());
    resumeWritable_ = true;
    resumeDirty_ = false;
}

// The playhead: the audio thread's once it has taken the newest cue, else where that cue starts.
uint32_t BardEngine::playhead(int d) const {
    auto& w = wk_[d];
    auto& s = sh_[d];
    return s.gen.load(std::memory_order_acquire) == w.gen ? s.pos.load(std::memory_order_relaxed) : w.cueStart;
}

void BardEngine::publish(int d, Cue* cue) {
    auto& w = wk_[d];
    cue->gen = ++w.gen;
    w.cueStart = cue->start;
    delete sh_[d].next.exchange(cue, std::memory_order_acq_rel);  // one the audio thread has not taken
    w.live = cue;
}

void BardEngine::scanShelf(int d, int64_t now) {
    auto& w = wk_[d];
    auto& s = sh_[d];
    int n = int(shelfDirs_.size());
    int shelf = n > 0 ? std::clamp(int(s.shelfX.load(std::memory_order_relaxed) * float(n)), 0, n - 1) : -1;
    bool moved = shelf != w.shelf;
    if (!moved && !(w.books.empty() && now >= w.rescanAt)) return;
    if (moved) {
        if (w.open >= 0) resumeDirty_ = true;  // remember where we were leaving
        saveResume(now);
        openBook(d, -1, now);
        w.pendingBook = -1;
    }
    w.shelf = shelf;
    w.books = shelf >= 0 ? ssp::engine::scanBank(shelfDirs_[size_t(shelf)]) : std::vector<Station>();
    w.rescanAt = now + RESCAN_MS;
    s.shelf.store(shelf);
    s.shelves.store(n);
    s.books.store(int(w.books.size()));
    std::lock_guard<std::mutex> lock(lock_);
    shelfName_[d] = shelf >= 0 ? baseName(shelfDirs_[size_t(shelf)]) : std::string();
}

void BardEngine::work(int d, int64_t now) {
    auto& w = wk_[d];
    auto& s = sh_[d];
    scanShelf(d, now);

    auto seq = Seq(s.seq.load(std::memory_order_relaxed));
    if (seq != w.seq) {
        w.seq = seq;
        enterSegment(d, w.seg);  // Read unbounds the segment; Recite and Wander bound it
    }
    auto loop = LoopPolicy(s.loop.load(std::memory_order_relaxed));
    if (loop != w.loop) {
        w.loop = loop;
        applyLoop(d, true);
    }
    int reroll = s.reroll.load(std::memory_order_relaxed);
    if (reroll != w.reroll) {
        w.reroll = reroll;
        if (w.open >= 0) {
            loadMarks(d);
            enterSegment(d, mark_at(w.marks, playhead(d)));
        }
    }

    // buttons and the gate
    uint32_t plays = s.plays.load(std::memory_order_relaxed);
    if (plays != w.plays) {
        if (w.open >= 0 && (plays - w.plays) % 2 == 1) {
            w.paused = !w.paused;
            resumeDirty_ = true;  // checkpoint the position on a pause
        }
        w.plays = plays;
    }
    uint32_t backs = s.backs.load(std::memory_order_relaxed);
    if (backs != w.backs) {
        uint32_t times = backs - w.backs;
        w.backs = backs;
        if (w.open >= 0) {
            // never back out of the segment being recited
            uint32_t back = times * JUMP_BACK_S * w.srcRate;
            uint32_t base = w.seq != Seq::Read && w.seg >= 0 ? w.marks.mark[w.seg].start : 0u;
            uint32_t p = playhead(d);
            requestJump(d, std::max(base, p > back ? p - back : 0u));
        }
    }
    uint32_t nexts = s.nexts.load(std::memory_order_relaxed);
    if (nexts != w.nexts) {
        int times = int(nexts - w.nexts);
        w.nexts = nexts;
        if (w.open >= 0) advance(d, times);
    }

    applySelectors(d, now);

    if (w.req) {  // one seek per pass, at most
        w.req = false;
        if (w.open >= 0 && seek(d, w.reqFrame)) w.paused = false;
    }

    // segment or book end: the audio thread has already stopped at segEnd
    if (w.open >= 0 && !w.paused) {
        uint32_t p = playhead(d);
        bool atEnd = p >= w.segEnd || (w.live && !w.live->stream.playing());
        if (atEnd) {
            switch (w.seq) {
                case Seq::Read:
                    if (w.marks.loop_set && w.marks.loop == LoopMode::Book) {
                        seek(d, 0);
                        enterSegment(d, mark_at(w.marks, 0));
                    } else {
                        w.paused = true;
                    }
                    break;
                case Seq::Recite:
                    if (w.loopSeg && w.seg >= 0) {
                        seek(d, w.marks.mark[w.seg].start);
                        s.gates.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        w.paused = true;
                    }
                    break;
                case Seq::Wander: advance(d, 1); break;
            }
        }
    }

    // a gate pulse whenever the playhead enters a different bookmark
    if (w.open >= 0) {
        int m = mark_at(w.marks, playhead(d));
        if (m != w.seenMark) {
            if (m >= 0 && w.seenMark >= 0) s.gates.fetch_add(1, std::memory_order_relaxed);
            w.seenMark = m;
        }
    }

    s.paused.store(w.paused, std::memory_order_relaxed);
    s.mark.store(w.seg, std::memory_order_relaxed);
    s.marks.store(w.marks.count, std::memory_order_relaxed);
    s.autoMarks.store(w.marks.generated, std::memory_order_relaxed);
    s.error.store(now < w.errUntil, std::memory_order_relaxed);
    if (w.live != nullptr) w.live->stream.fill();
}

// The BOOK and BOOKMARK selectors and the Position control; each can cause at most one seek.
void BardEngine::applySelectors(int d, int64_t now) {
    auto& w = wk_[d];
    auto& s = sh_[d];

    int wantBook = quantise(s.bookX.load(std::memory_order_relaxed), int(w.books.size()), w.open, SEL_HYST);
    if (wantBook != w.pendingBook) {
        w.pendingBook = wantBook;
        w.pendingBookAt = now;
    }
    // nothing open: open at once, there is no playback to protect from a re-open storm
    if (wantBook != w.open && (w.open < 0 || now - w.pendingBookAt >= SETTLE_MS)) {
        if (w.open >= 0) resumeDirty_ = true;
        saveResume(now);
        openBook(d, wantBook, now);
        return;  // a fresh book: settle its bookmark on the next pass
    }
    if (w.open < 0) return;

    // The bookmark selector walks mark[] in the sidecar's line order. It acts when it moves, not when
    // it disagrees with the segment, so a parked knob does not undo a Next or a resumed position.
    float mx = s.markX.load(std::memory_order_relaxed);
    if (w.markX < -0.5f || std::fabs(mx - w.markX) > 0.01f) {
        w.markMoved = true;
        w.markX = mx;
    }
    if (w.markMoved) {
        int wantSeg = quantise(mx, w.marks.count, w.seg, SEL_HYST);
        if (wantSeg != w.pendingSeg) {
            w.pendingSeg = wantSeg;
            w.pendingSegAt = now;
        }
        if (w.seg < 0 || now - w.pendingSegAt >= SETTLE_MS) {
            w.markMoved = false;
            if (wantSeg >= 0 && wantSeg != w.seg) {
                enterSegment(d, wantSeg);
                requestJump(d, w.marks.mark[wantSeg].start);
                return;
            }
        }
    }

    // Position: a move jumps within the book (Read) or the segment, once it settles
    float pos = s.position.load(std::memory_order_relaxed);
    if (w.position < -0.5f) w.position = pos;  // seeded on open: no jump
    if (std::fabs(pos - w.position) > POSITION_STEP) {
        w.position = pos;
        w.scrubbed = true;
        w.scrubAt = now;
    }
    if (w.scrubbed && now - w.scrubAt >= SETTLE_MS) {
        w.scrubbed = false;
        bool inSeg = w.seg >= 0 && w.seq != Seq::Read;
        uint32_t a = inSeg ? w.marks.mark[w.seg].start : 0u;
        uint32_t b = inSeg ? w.marks.mark[w.seg].end : w.frames;
        if (b > a) requestJump(d, a + uint32_t(std::clamp(w.position, 0.0f, 1.0f) * float(b - a - 1)));
    }
}

void BardEngine::openBook(int d, int book, int64_t now) {
    auto& w = wk_[d];
    auto& s = sh_[d];
    w.seenMark = -1;
    w.req = false;
    if (book < 0 || book >= int(w.books.size())) {
        w.open = -1;
        w.frames = 0;
        w.seg = -1;
        w.marks.clear();
        w.paused = true;
        enterSegment(d, -1);
        auto* cue = new Cue;  // an unopened stream is silence
        publish(d, cue);
        s.book.store(-1);
        s.frames.store(0);
        std::lock_guard<std::mutex> lock(lock_);
        bookName_[d].clear();
        return;
    }
    const Station& b = w.books[size_t(book)];
    w.open = book;
    w.frames = uint32_t(std::min<uint64_t>(b.frames, 0xFFFFFFFEu));
    w.srcRate = b.rate > 0 ? b.rate : cfg_.rate;
    s.book.store(book);
    s.frames.store(w.frames);
    s.rate.store(w.srcRate);
    {
        std::lock_guard<std::mutex> lock(lock_);
        bookName_[d] = b.name;
    }
    loadMarks(d);
    uint32_t start = 0, f = 0;
    if (resume_.get(resumeKey(d).c_str(), f) && f + 1 < w.frames) start = f;
    enterSegment(d, mark_at(w.marks, start));
    // seed the selectors where they are, so a fresh book keeps its resumed position
    w.markX = s.markX.load(std::memory_order_relaxed);
    w.markMoved = false;
    w.position = -1.0f;
    w.scrubbed = false;
    if (!seek(d, start)) {
        w.errUntil = now + ERR_FLASH_MS;
        w.open = -1;
        s.book.store(-1);
    }
    w.paused = false;
}

bool BardEngine::seek(int d, uint32_t frame) {
    auto& w = wk_[d];
    if (w.open < 0) return false;
    if (w.frames && frame + 1 >= w.frames) frame = w.frames - 1;
    auto* cue = new Cue;
    cue->start = frame;
    cue->rate = float(w.srcRate);
    bool ok = cue->stream.open(w.books[size_t(w.open)], frame, cue->rate);
    publish(d, cue);
    return ok;
}

// The book's sidecar, NAME.txt beside NAME.wav, or deterministic auto-marks if it has none.
// A directive line survives a sidecar without marks.
void BardEngine::loadMarks(int d) {
    auto& w = wk_[d];
    if (w.open < 0) {
        w.marks.clear();
        return;
    }
    const Station& b = w.books[size_t(w.open)];
    std::string stem = b.path.substr(0, b.path.find_last_of('.'));
    std::string text;
    bool have = false;
    MarkOrder order = MarkOrder::File;
    LoopMode loop = LoopMode::Off;
    bool loopSet = false;
    if (readFile(stem + ".txt", text) || readFile(stem + ".TXT", text)) {
        parse_sidecar(text.c_str(), w.srcRate, w.frames, w.marks);
        order = w.marks.ordering;
        loop = w.marks.loop;
        loopSet = w.marks.loop_set;
        have = w.marks.count > 0;
    }
    if (!have) {
        auto_marks(b.name.c_str(), w.frames, w.srcRate, uint32_t(w.reroll), w.marks);
        w.marks.ordering = order;
        w.marks.loop = loop;
        w.marks.loop_set = loopSet;
    }
    resolve(w.marks, w.frames, book_seed(b.name.c_str(), w.frames, uint32_t(w.reroll)));
    applyLoop(d, false);
}

// The segment-end policy. The sidecar's loop= decides under File, else hold: a silent stop is undone
// with one knob turn, an unexpected loop of a spoken passage is the worse surprise.
void BardEngine::applyLoop(int d, bool resume) {
    auto& w = wk_[d];
    bool was = w.loopSeg;
    w.loopSeg = w.loop == LoopPolicy::Loop ||
                (w.loop == LoopPolicy::File && w.marks.loop_set && w.marks.loop == LoopMode::Segment);
    // turning loop on at the end of a held segment starts it again
    if (!resume || was || !w.loopSeg || w.open < 0 || playhead(d) < w.segEnd) return;
    if (w.seq == Seq::Wander) advance(d, 1);
    else if (w.seg >= 0) requestJump(d, w.marks.mark[w.seg].start);
}

// In Read the segment is the whole book: marks are only jump targets.
void BardEngine::enterSegment(int d, int mark) {
    auto& w = wk_[d];
    w.seg = mark;
    if (w.seq == Seq::Read || mark < 0 || mark >= w.marks.count) w.segEnd = w.frames ? w.frames : 0xFFFFFFFFu;
    else w.segEnd = w.marks.mark[mark].end;
    sh_[d].segEnd.store(w.segEnd, std::memory_order_relaxed);
}

// Steps through the play order from the current segment. Past the end it wraps under loop, else holds.
void BardEngine::advance(int d, int steps) {
    auto& w = wk_[d];
    int n = w.marks.count;
    if (n <= 0) return;
    int slot = order_slot(w.marks, w.seg);
    slot = slot < 0 ? 0 : slot + steps;
    if (slot >= n) {
        if (!w.loopSeg) {
            w.paused = true;
            return;
        }
        slot %= n;
    }
    int m = int(w.marks.order[slot]);
    enterSegment(d, m);
    requestJump(d, w.marks.mark[m].start);
    sh_[d].gates.fetch_add(1, std::memory_order_relaxed);
}

void BardEngine::requestJump(int d, uint32_t frame) {
    wk_[d].reqFrame = frame;
    wk_[d].req = true;
}

// "<shelf>/<book>": the same file on two shelves keeps two positions
std::string BardEngine::resumeKey(int d) const {
    auto& w = wk_[d];
    return baseName(shelfDirs_[size_t(w.shelf)]) + "/" + w.books[size_t(w.open)].name;
}

// Writes resume.txt on a 30 s checkpoint while playing, and after a pause or a book change. The first
// failed write stops writing until the root changes.
void BardEngine::saveResume(int64_t now) {
    if (!cfg_.resume || !resumeWritable_) return;
    // Both decks can hold the same book: deck A's position wins.
    std::string first;
    for (int d = 0; d < DECKS; d++) {
        auto& w = wk_[d];
        if (w.open < 0) continue;
        std::string key = resumeKey(d);
        if (d == 1 && key == first) continue;
        if (d == 0) first = key;
        if (!w.paused || resumeDirty_) resume_.set(key.c_str(), playhead(d));
    }
    if (!resumeDirty_ && now - resumeAt_ < CHECKPOINT_MS) return;
    resumeAt_ = now;
    resumeDirty_ = false;
    if (resume_.count == 0) return;
    std::vector<char> text(ResumeTable::kTextMax);
    int n = resume_.serialize(text.data(), int(text.size()));
    std::ofstream f(libRoot_ + "/resume.txt", std::ios::binary | std::ios::trunc);
    if (!f || !f.write(text.data(), n)) resumeWritable_ = false;
}

}  // namespace bard
