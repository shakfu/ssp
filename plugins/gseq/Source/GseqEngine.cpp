#include "GseqEngine.h"

#include <algorithm>
#include <cmath>

namespace gseq {

namespace {

constexpr float SIGNAL_PER_VOLT = 0.2f;  // SSP: 1.0 is 5 V
constexpr float GATE_HIGH = 1.0f;

uint32_t bits(uint32_t w, int at, int width) {
    return (w >> at) & ((1u << width) - 1u);
}

}  // namespace

// on: bit 0; note: 1-7; velocity: 8-14; duration: 15-18
GseqEngine::Step GseqEngine::Step::unpack(uint32_t w) {
    Step s;
    s.on = bits(w, 0, 1);
    s.note = uint8_t(bits(w, 1, 7));
    s.velocity = uint8_t(std::max(1u, bits(w, 8, 7)));
    s.duration = uint8_t(bits(w, 15, 4));
    return s;
}

uint32_t GseqEngine::Step::pack() const {
    return uint32_t(on) | uint32_t(note & 127) << 1 | uint32_t(std::max<uint8_t>(velocity, 1) & 127) << 8 |
           uint32_t(std::min<int>(duration, DURATIONS - 1)) << 15;
}

// length - 1: bits 0-4; division: 5-7; channel: 8-12; voice: 13-15; velocity CV: 16; mute: 17; gated: 18
GseqEngine::Row GseqEngine::Row::unpack(uint32_t w) {
    Row r;
    r.length = uint8_t(bits(w, 0, 5) + 1);
    r.division = uint8_t(std::min<uint32_t>(bits(w, 5, 3), DIVISIONS - 1));
    r.channel = uint8_t(std::min<uint32_t>(bits(w, 8, 5), 16));
    r.voice = uint8_t(std::min<uint32_t>(bits(w, 13, 3), VOICES));
    r.velocityCv = bits(w, 16, 1);
    r.mute = bits(w, 17, 1);
    r.velocityGated = bits(w, 18, 1);
    return r;
}

uint32_t GseqEngine::Row::pack() const {
    return uint32_t(std::clamp<int>(length, 1, STEPS) - 1) | uint32_t(std::min<int>(division, DIVISIONS - 1)) << 5 |
           uint32_t(std::min<int>(channel, 16)) << 8 | uint32_t(std::min<int>(voice, VOICES)) << 13 |
           uint32_t(velocityCv) << 16 | uint32_t(mute) << 17 | uint32_t(velocityGated) << 18;
}

GseqEngine::GseqEngine() {
    for (int r = 0; r < ROWS; r++) {
        Row row;
        row.channel = uint8_t(r + 1);  // MIDI on the row's own channel; rows 1-4 on voices 1-4 too
        if (r < VOICES) {
            row.voice = uint8_t(r + 1);
            row.velocityCv = true;
        }
        rows_[r].store(row.pack(), std::memory_order_relaxed);
        for (int s = 0; s < STEPS; s++) steps_[r][s].store(Step().pack(), std::memory_order_relaxed);
        playing_[r].store(-1, std::memory_order_relaxed);
    }
}

bool GseqEngine::setRow(int r, Row row) {
    bool taken = false;
    for (int o = 0; o < ROWS; o++)
        if (o != r && row.voice && this->row(o).voice == row.voice) taken = true;
    if (taken) row.voice = this->row(r).voice;
    rows_[r].store(row.pack(), std::memory_order_relaxed);
    return !taken;
}

void GseqEngine::prepare(float sampleRate, int) {
    sampleRate_ = sampleRate;
    gap_ = std::max(1, int(GAP_MS * 0.001f * sampleRate));
    minGate_ = std::max(1, int(MIN_GATE_MS * 0.001f * sampleRate));
}

void GseqEngine::send(uint8_t status, uint8_t d1, uint8_t d2) {
    if (!midi_.try_enqueue({ { status, d1, d2 }, 3 })) midiDropped_.fetch_add(1, std::memory_order_relaxed);
}

void GseqEngine::start(int r, const Row& row, const Step& s, double at) {
    Playing& p = notes_[r];
    if (p.sounding) {  // retrigger: the MIDI note ends first; a voice the row left closes
        if (p.channel) send(uint8_t(0x80 | (p.channel - 1)), uint8_t(p.note), 0);
        if (p.voice && p.voice != row.voice) gate_[p.voice - 1] = false;
    }
    const double tps = double(ssp::engine::Clock::TICKS_PER_QUARTER) / DIVISION[row.division];
    p.end = at + std::min(DURATION_EIGHTHS[s.duration] / 8.0, double(row.length)) * tps;
    p.sounding = true;
    p.age = 0;
    p.minAge = minGate_;
    p.channel = row.channel, p.note = s.note, p.voice = row.voice;
    if (row.channel) send(uint8_t(0x90 | (row.channel - 1)), s.note, s.velocity);
    if (!row.voice) return;
    const int v = row.voice - 1;
    if (gate_[v]) {
        closed_[v] = gap_;
        p.minAge += gap_;
    }
    gate_[v] = true;
    pitch_[v] = std::clamp(float(s.note - 60) / 12.0f * SIGNAL_PER_VOLT, -1.0f, 1.0f);
    velocity_[v] = row.velocityCv ? float(s.velocity) / 127.0f : 0.0f;
    velocityGated_[v] = row.velocityGated;
}

void GseqEngine::stop(int r) {
    Playing& p = notes_[r];
    p.sounding = false;
    if (p.channel) send(uint8_t(0x80 | (p.channel - 1)), uint8_t(p.note), 0);
    if (p.voice) {
        gate_[p.voice - 1] = false;
        closed_[p.voice - 1] = 0;
    }
}

void GseqEngine::stopAll(bool replay) {
    for (int r = 0; r < ROWS; r++) {
        if (notes_[r].sounding) stop(r);
        if (replay) notes_[r].lastStep = std::numeric_limits<int64_t>::min();
    }
    if (replay && recordState_.load(std::memory_order_relaxed) != IDLE) {  // a take restarts from step 1
        recordState_.store(ARMED, std::memory_order_relaxed);
        take_ = 0;
        written_ = 0;
    }
}

void GseqEngine::noteIn(int note, int velocity) {
    notesIn_.try_enqueue({ uint8_t(note & 127), uint8_t(velocity & 127), false });  // full: the note is lost
}

void GseqEngine::playIn(int note, int velocity) {
    notesIn_.try_enqueue({ uint8_t(note & 127), uint8_t(velocity & 127), true });
}

void GseqEngine::startRecording(double at) {
    const int r = cursorRow();
    const Row row = this->row(r);
    const double tps = double(ssp::engine::Clock::TICKS_PER_QUARTER) / DIVISION[row.division];
    const auto k = int64_t(std::floor(at / tps));
    take_ = (k / row.length + 1) * row.length;  // the row's next pass
    takeLength_ = row.length;
    written_ = 0;
    recordRow_.store(r, std::memory_order_relaxed);
    recordState_.store(ARMED, std::memory_order_relaxed);
}

void GseqEngine::takeNotes() {
    const double at = clock_.swung();
    const bool stepping = stepEntry_.load(std::memory_order_relaxed);
    const bool toggled = recordToggle_.exchange(false, std::memory_order_acq_rel);
    if (recordState_.load(std::memory_order_relaxed) != IDLE) {
        if (toggled || stepping) recordState_.store(IDLE, std::memory_order_relaxed);
    } else if (toggled && !stepping) {
        startRecording(at);
    }
    if (recordState_.load(std::memory_order_relaxed) == ARMED &&
        cursorRow() != recordRow_.load(std::memory_order_relaxed))
        startRecording(at);  // the cursor moved to another row before the take
    NoteIn k;
    // the notes queued now; later ones wait, so a stream of notes cannot hold the block
    for (size_t due = notesIn_.size_approx(); due > 0 && notesIn_.try_dequeue(k); due--) {
        if (k.velocity) keyDown(k, at);
        else keyUp(k.note);
    }
}

void GseqEngine::keyDown(const NoteIn& k, double at) {
    keyUp(k.note);  // pressed again without a release
    const bool stepping = stepEntry_.load(std::memory_order_relaxed);
    const auto state = recordState_.load(std::memory_order_relaxed);
    if (!k.play && !stepping && state == IDLE) return;
    Held* h = nullptr;
    for (auto& f : held_)
        if (!f.active) h = &f;
    if (!h) return;
    *h = Held();
    h->active = true, h->note = k.note, h->velocity = k.velocity, h->since = samples_;
    int base;
    if (k.play) {
        base = cursorRow();
        h->chord = PLAY;
    } else if (stepping) {
        base = cursorRow();
        h->col = cursorCol();
        h->chord = -1;
        h->written = true;
    } else {
        base = recordRow_.load(std::memory_order_relaxed);
        const Row row = this->row(base);
        const double tps = double(ssp::engine::Clock::TICKS_PER_QUARTER) / DIVISION[row.division];
        const auto kr = int64_t(std::llround(at / tps));  // the nearest step
        h->chord = kr;
        h->col = int(kr - take_);
        h->written = kr >= take_ && kr < take_ + takeLength_;
        if (h->written) written_ |= 1u << h->col;
    }
    int before = 0;  // the chord's notes already held: this one echoes on the row below theirs
    for (const auto& o : held_)
        if (&o != h && o.active && o.chord == h->chord) before++;
    h->echo = (base + before) % ROWS;
    Step s;
    s.on = true, s.note = h->note, s.velocity = std::max<uint8_t>(h->velocity, 1);
    start(h->echo, row(h->echo), s, at);
    notes_[h->echo].end = std::numeric_limits<double>::infinity();  // until its key is up
    if (h->written) place(h->chord, base);
}

void GseqEngine::place(int64_t chord, int base) {
    Held* chordNotes[HELD];
    int n = 0;
    for (auto& h : held_)
        if (h.active && h.written && h.chord == chord) chordNotes[n++] = &h;
    std::sort(chordNotes, chordNotes + n, [](const Held* a, const Held* b) { return a->note < b->note; });
    for (int i = 0; i < n; i++) {
        Held& h = *chordNotes[i];
        h.row = (base + i) % ROWS;
        Step s = step(h.row, h.col);
        s.on = true, s.note = h.note, s.velocity = std::max<uint8_t>(h.velocity, 1);
        s.duration = durationIndex(samples_ - h.since, row(h.row));
        setStep(h.row, h.col, s);
    }
}

uint8_t GseqEngine::durationIndex(int64_t samples, const Row& row) const {
    const double tps = double(ssp::engine::Clock::TICKS_PER_QUARTER) / DIVISION[row.division];
    const double ticksPerSample = clock_.bpm() * ssp::engine::Clock::TICKS_PER_QUARTER / 60.0 / sampleRate_;
    const double eighths = double(samples) * ticksPerSample / tps * 8.0;
    int best = 0;
    for (int i = 1; i < DURATIONS && DURATION_EIGHTHS[i] <= row.length * 8; i++)  // nearest by ratio
        if (eighths > 0.0 && std::fabs(std::log(DURATION_EIGHTHS[i] / eighths)) <
                                     std::fabs(std::log(DURATION_EIGHTHS[best] / eighths)))
            best = i;
    return uint8_t(best);
}

void GseqEngine::keyUp(int note) {
    Held* h = nullptr;
    for (auto& f : held_)
        if (f.active && f.note == note) h = &f;
    if (!h) return;
    if (h->written) {
        Step s = step(h->row, h->col);
        if (s.on && s.note == h->note) {
            s.duration = durationIndex(samples_ - h->since, row(h->row));
            setStep(h->row, h->col, s);
        }
    }
    Playing& p = notes_[h->echo];
    if (p.sounding && p.note == h->note && std::isinf(p.end)) stop(h->echo);  // unless a step took the row
    h->active = false;
    if (h->chord != -1) return;
    for (const auto& o : held_)
        if (o.active && o.chord == -1) return;
    const int r = cursorRow();  // step entry: every key is up, so the cursor moves on
    setCursor(r, (cursorCol() + 1) % row(r).length);
}

void GseqEngine::process(const float* const* in, float* const* out, int n) {
    using ssp::engine::Clock;
    clock_.follow(source_, sampleRate_);
    switch (clock_.transport()) {
    case Clock::START: stopAll(true); break;
    case Clock::STOP: stopAll(false); break;  // Continue goes on from the step it stopped in
    default: break;
    }
    while (clock_.midiPulse()) {
    }
    takeNotes();
    Row rows[ROWS];
    for (int r = 0; r < ROWS; r++) rows[r] = row(r);
    for (int i = 0; i < n; i++) {
        if (clock_.resetInput(in[RESET_INPUT][i])) stopAll(true);
        clock_.clockInput(in[CLOCK_INPUT][i]);
        const double at = clock_.swung();
        for (int r = 0; r < ROWS; r++) {
            const Row& row = rows[r];
            Playing& p = notes_[r];
            const double tps = double(Clock::TICKS_PER_QUARTER) / DIVISION[row.division];
            const auto k = int64_t(std::floor(at / tps));
            if (k != p.lastStep) {  // a new step; one a clock pulse skipped does not play
                p.lastStep = k;
                const int s = int((k % row.length + row.length) % row.length);
                playing_[r].store(s, std::memory_order_relaxed);
                bool taken = false;  // written this take: kept, and not played this pass
                if (r == recordRow_.load(std::memory_order_relaxed)) taken = takeStep(r, k, s);
                const Step st = step(r, s);
                if (st.on && !row.mute && !taken) start(r, row, st, double(k) * tps);
            }
            if (p.sounding) {
                if (at >= p.end && p.age >= p.minAge) stop(r);
                else p.age++;
            }
        }
        for (int v = 0; v < VOICES; v++) {
            const bool open = gate_[v] && !closed_[v];
            out[v][i] = open ? GATE_HIGH : 0.0f;
            if (closed_[v]) closed_[v]--;
            out[VOICES + v][i] = pitch_[v];
            out[2 * VOICES + v][i] = open || !velocityGated_[v] ? velocity_[v] : 0.0f;
        }
        clock_.advance();
    }
    bpmShown_.store(clock_.bpm(), std::memory_order_relaxed);
    samples_ += n;
}

bool GseqEngine::takeStep(int r, int64_t k, int s) {
    const auto state = recordState_.load(std::memory_order_relaxed);
    if (state == IDLE) return false;
    if (state == ARMED) {
        if (k < take_) return false;
        take_ = k - s;  // the pass begins, or a clock jump landed inside one
        recordState_.store(RECORDING, std::memory_order_relaxed);
    }
    if (k >= take_ + takeLength_) {  // one pass, then the take stops
        recordState_.store(IDLE, std::memory_order_relaxed);
        return false;
    }
    if (written_ >> (k - take_) & 1u) return true;
    Step st = step(r, s);
    if (st.on) {  // replace: a step no note landed on is cleared
        st.on = false;
        setStep(r, s, st);
    }
    return false;
}

}  // namespace gseq
