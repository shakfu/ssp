#include "AldaEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <sstream>

#include "alda/context.h"
#include "alda/interpreter.h"
#include "alda/parser.h"

namespace alda {

namespace {

constexpr float SIGNAL_PER_VOLT = 0.2f;  // SSP: 1.0 is 5 V
constexpr float GATE_HIGH = 1.0f;
// input 1's reset, with teletype's thresholds
constexpr float TRIG_HIGH = 2.21f * SIGNAL_PER_VOLT;
constexpr float TRIG_LOW = 0.8f * SIGNAL_PER_VOLT;

const char* const BUILTIN = R"alda(# alda: parts 1-4 gate outs 1-4 and set pitch CV on outs 5-8, 1 V/octave from C4.
# A rising edge on input 1 restarts the loop.
(tempo! 120)

lead:
  o4 c8 e g > c < g e c4 | d8 f a > d < a f d4

bass:
  o2 c2 g | d2 a
)alda";

struct ContextFree {
    void operator()(AldaContext* c) const {
        alda_context_cleanup(c);
        std::free(c);
    }
};

// `# @loop N`: N quarter notes, as ticks; 0 if no such line, -1 if N is not a positive number
int32_t loopLength(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    int32_t length = 0;
    while (std::getline(lines, line)) {
        size_t c = line.find_first_not_of(" \t");
        if (c == std::string::npos || line[c] != '#') continue;
        size_t at = line.find("@loop", c);
        if (at == std::string::npos) continue;
        const char* s = line.c_str() + at + 5;
        char* end = nullptr;
        double quarters = std::strtod(s, &end);
        if (end == s || !(quarters > 0.0) || quarters > 1e6) return -1;
        length = int32_t(std::lround(quarters * AldaEngine::TICKS_PER_QUARTER));
    }
    return length;
}

// `# @clock`, `# @clock N` or `# @clock midi`; false with `error` set for anything else
bool clockLine(const std::string& text, AldaEngine::Score& s, std::string& error) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        size_t c = line.find_first_not_of(" \t");
        if (c == std::string::npos || line[c] != '#') continue;
        size_t at = line.find("@clock", c);
        if (at == std::string::npos) continue;
        std::istringstream words(line.substr(at + 6));
        std::string w;
        s.clock = AldaEngine::INPUT;
        s.ppqn = 4;
        if (!(words >> w)) continue;
        if (w == "midi") {
            s.clock = AldaEngine::MIDI;
            s.ppqn = 24;
            continue;
        }
        char* end = nullptr;
        long n = std::strtol(w.c_str(), &end, 10);
        if (*end || n < 1 || n > 96) {
            error = "@clock takes nothing, pulses per quarter note (1-96), or midi";
            return false;
        }
        s.ppqn = int(n);
    }
    return true;
}

}  // namespace

AldaEngine::AldaEngine() {}

AldaEngine::~AldaEngine() {
    delete score_;
    delete pending_.exchange(nullptr);
    delete retired_.exchange(nullptr);
}

const char* AldaEngine::builtin() const {
    return BUILTIN;
}

bool AldaEngine::build(const std::string& text, const std::string& path, Score& score, std::string& error) {
    int32_t loop = loopLength(text);
    if (loop < 0) {
        error = "@loop needs a positive number of quarter notes";
        return false;
    }
    std::string name = path.empty() ? "live.alda" : path;
    char* err = nullptr;
    AldaNode* ast = alda_parse(text.c_str(), name.c_str(), &err);
    if (err) {
        error = err;
        std::free(err);
        if (ast) alda_ast_free(ast);
        return false;
    }
    if (!ast) {
        error = "cannot parse the score";
        return false;
    }
    std::unique_ptr<AldaContext, ContextFree> ctx(static_cast<AldaContext*>(std::malloc(sizeof(AldaContext))));
    if (!ctx) {
        alda_ast_free(ast);
        error = "out of memory";
        return false;
    }
    alda_context_init(ctx.get());
    int r = alda_interpret_ast(ctx.get(), ast);
    alda_ast_free(ast);
    if (r != 0) {
        error = "cannot interpret the score";  // psnd prints the reason to stderr only
        return false;
    }

    Score s;
    if (!clockLine(text, s, error)) return false;
    s.bpm = ALDA_DEFAULT_TEMPO;
    int32_t last = 0;
    for (int i = 0; i < ctx->event_count; i++) {
        const AldaScheduledEvent& e = ctx->events[i];
        uint8_t ch = uint8_t(std::min(std::max(e.channel, 0), 15));
        uint8_t voice = e.part_index >= 0 && e.part_index < VOICES ? uint8_t(e.part_index) : Event::NO_VOICE;
        auto b7 = [](int v) { return uint8_t(std::min(std::max(v, 0), 127)); };
        switch (e.type) {
        case ALDA_EVT_TEMPO:
            if (e.data1 <= 0) continue;
            if (e.tick == 0) s.bpm = e.data1;
            s.events.push_back({ e.tick, Event::TEMPO, Event::NO_VOICE, 0, int16_t(e.data1), 0 });
            break;
        case ALDA_EVT_NOTE_ON:
        case ALDA_EVT_NOTE_OFF: {
            bool on = e.type == ALDA_EVT_NOTE_ON && e.data2 > 0;
            s.events.push_back({ e.tick, on ? Event::ON : Event::OFF, voice, ch, b7(e.data1), on ? b7(e.data2) : uint8_t(0) });
            break;
        }
        case ALDA_EVT_PROGRAM:
            s.events.push_back({ e.tick, Event::PROGRAM, voice, ch, b7(e.data1), 0 });
            break;
        case ALDA_EVT_PAN:  // CC 10
            s.events.push_back({ e.tick, Event::CC, voice, ch, 10, b7(e.data1) });
            break;
        case ALDA_EVT_CC:
            s.events.push_back({ e.tick, Event::CC, voice, ch, b7(e.data1), b7(e.data2) });
            break;
        default:
            continue;
        }
        last = std::max(last, e.tick);
    }
    s.length = loop > 0 ? loop : std::max<int32_t>(BAR, (last + BAR - 1) / BAR * BAR);
    // a voice's notes past the loop's end never sound: the loop cuts them at the wrap
    s.events.erase(std::remove_if(s.events.begin(), s.events.end(), [&](const Event& e) { return e.tick >= s.length; }),
                   s.events.end());
    for (int i = 0; i < std::min(ctx->part_count, VOICES); i++) {
        if (i) s.parts += ' ';
        s.parts += ctx->parts[i].name;
    }
    score = std::move(s);
    return true;
}

bool AldaEngine::compile(const std::string& text, const std::string& path, std::string& error) {
    auto s = std::make_unique<Score>();
    if (!build(text, path, *s, error)) return false;
    if (std::none_of(s->events.begin(), s->events.end(), [](const Event& e) { return e.type == Event::ON; })) {
        std::string ignored;
        return dropAll(ignored);  // nothing to play: stop
    }
    {
        std::lock_guard<std::mutex> lock(partsLock_);
        parts_ = s->parts;
    }
    // a score the audio thread never took is still ours
    delete pending_.exchange(s.release(), std::memory_order_acq_rel);
    return true;
}

bool AldaEngine::dropAll(std::string& result) {
    delete pending_.exchange(nullptr, std::memory_order_acq_rel);
    stop_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(partsLock_);
        parts_.clear();
    }
    result = "stopped";
    return true;
}

void AldaEngine::idle() {
    delete retired_.exchange(nullptr, std::memory_order_acq_rel);
    ScriptEngine::idle();
}

void AldaEngine::send(uint8_t status, uint8_t d1, uint8_t d2, uint8_t size) {
    if (!midi_.try_enqueue({ { status, d1, d2 }, size })) midiDropped_.fetch_add(1, std::memory_order_relaxed);
}

void AldaEngine::release() {
    for (int ch = 0; ch < 16; ch++)
        for (int w = 0; w < 4; w++)
            for (uint32_t bits = sounding_[ch][w]; bits; bits &= bits - 1)
                send(uint8_t(0x80 | ch), uint8_t(w * 32 + __builtin_ctz(bits)), 0);
    std::memset(sounding_, 0, sizeof sounding_);
    int gap = int(GAP_MS * 0.001f * sampleRate());
    for (Voice& v : voices_) {
        if (v.count) v.quiet = gap;
        v.count = 0;
    }
}

void AldaEngine::restart() {
    release();
    if (!retired_.load(std::memory_order_acquire)) {
        if (Score* next = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
            retired_.store(score_, std::memory_order_release);
            score_ = next;
        }
    }
    cursor_ = 0;
    if (score_) {
        bpm_ = score_->bpm;
        lengthShown_.store(score_->length, std::memory_order_relaxed);
    }
}

void AldaEngine::apply(const Event& e) {
    switch (e.type) {
    case Event::TEMPO:
        bpm_ = e.value;
        return;
    case Event::PROGRAM:
        send(uint8_t(0xC0 | e.channel), uint8_t(e.value), 0, 2);
        return;
    case Event::CC:
        send(uint8_t(0xB0 | e.channel), uint8_t(e.value), e.data2);
        return;
    case Event::ON:
    case Event::OFF: {
        uint32_t& word = sounding_[e.channel][e.value >> 5];
        uint32_t bit = 1u << (e.value & 31);
        if (e.type == Event::ON) {
            word |= bit;
            send(uint8_t(0x90 | e.channel), uint8_t(e.value), e.data2);
        } else if (word & bit) {  // a note the restart already released sends nothing
            word &= ~bit;
            send(uint8_t(0x80 | e.channel), uint8_t(e.value), 0);
        }
        break;
    }
    }
    if (e.voice == Event::NO_VOICE) return;
    Voice& v = voices_[e.voice];
    int8_t note = int8_t(e.value);
    auto end = v.held + v.count;
    auto it = std::find(v.held, end, note);
    if (it != end) {  // a note already held moves to the top, or leaves
        std::move(it + 1, end, it);
        v.count--;
    }
    if (e.type == Event::ON) {
        if (v.count == HELD) {  // the oldest goes
            std::move(v.held + 1, v.held + HELD, v.held);
            v.count--;
        }
        v.held[v.count++] = note;
    }
    if (v.count) v.pitch = v.held[v.count - 1];
}

void AldaEngine::midiClock(double seconds) {
    double gap = seconds - lastMidiTick_;
    midiInterval_.store(lastMidiTick_ >= 0.0 && gap > 0.0 && gap < 1.0 ? gap : 0.0, std::memory_order_relaxed);
    lastMidiTick_ = seconds;
    midiPulses_.fetch_add(1, std::memory_order_release);
}

void AldaEngine::pulse(double interval) {
    const double tpp = double(TICKS_PER_QUARTER) / score_->ppqn;
    if (interval > 0.0) rate_ = tpp / interval;
    double target = lastGrid_ + tpp;
    if (target >= score_->length) {  // the loop ends within this pulse: play its last events first
        const auto& ev = score_->events;
        while (cursor_ < ev.size()) apply(ev[cursor_++]);
        target -= score_->length;
        restart();
    }
    pos_ = lastGrid_ = target;
}

void AldaEngine::process(const float* const* in, float* const* out, int n) {
    if (!retired_.load(std::memory_order_acquire) && stop_.exchange(false, std::memory_order_acq_rel)) {
        release();
        retired_.store(score_, std::memory_order_release);
        score_ = nullptr;
        lengthShown_.store(0, std::memory_order_relaxed);
    }
    if (!score_ && pending_.load(std::memory_order_acquire)) {
        restart();
        pos_ = lastGrid_ = 0.0;
    }
    const double perSample = double(TICKS_PER_QUARTER) / 60.0 / double(sampleRate());
    const Clock clock = score_ ? score_->clock : FREE;
    if (clock == MIDI) {
        switch (transport_.exchange(NONE, std::memory_order_acq_rel)) {
        case START:
            restart();
            pos_ = lastGrid_ = 0.0;
            halted_ = false;
            break;
        case STOP:
            release();
            halted_ = true;
            break;
        case CONTINUE: halted_ = false; break;
        default: break;
        }
    }
    for (uint32_t now = midiPulses_.load(std::memory_order_acquire); pulsesSeen_ != now; pulsesSeen_++)
        if (clock == MIDI && !halted_ && score_) pulse(midiInterval_.load(std::memory_order_relaxed) * sampleRate());
    for (int i = 0; i < n; i++) {
        float r = in[0][i];
        if (!resetHigh_ && r > TRIG_HIGH) {
            resetHigh_ = true;
            restart();
            pos_ = lastGrid_ = 0.0;
        } else if (resetHigh_ && r < TRIG_LOW) {
            resetHigh_ = false;
        }
        float k = in[CLOCK_INPUT][i];
        if (!clockHigh_ && k > TRIG_HIGH) {
            clockHigh_ = true;
            if (clock == INPUT && score_) pulse(sinceEdge_);
            sinceEdge_ = 0.0;
        } else if (clockHigh_ && k < TRIG_LOW) {
            clockHigh_ = false;
        }
        if (sinceEdge_ > 0.0 || clockHigh_) sinceEdge_ = std::min(sinceEdge_ + 1.0, 1e9);
        if (score_) {
            const auto& ev = score_->events;
            while (cursor_ < ev.size() && ev[cursor_].tick <= pos_) apply(ev[cursor_++]);
        }
        for (int k = 0; k < VOICES; k++) {
            Voice& v = voices_[k];
            out[k][i] = v.count && !v.quiet ? GATE_HIGH : 0.0f;
            if (v.quiet) v.quiet--;
            float cv = float(v.pitch - 60) / 12.0f * SIGNAL_PER_VOLT;
            out[VOICES + k][i] = std::min(1.0f, std::max(-1.0f, cv));
        }
        if (score_ && !(clock == MIDI && halted_)) {
            if (clock == FREE) {
                pos_ += bpm_ * perSample;
            } else {
                double limit = lastGrid_ + double(TICKS_PER_QUARTER) / score_->ppqn;
                pos_ = std::min(pos_ + (rate_ > 0.0 ? rate_ : bpm_ * perSample), std::nextafter(limit, 0.0));
            }
            if (pos_ >= score_->length) {
                pos_ -= score_->length;
                lastGrid_ -= score_->length;
                restart();
            }
        }
    }
    posShown_.store(pos_, std::memory_order_relaxed);
    double bpm = clock != FREE && rate_ > 0.0 ? rate_ / perSample : bpm_;
    bpmShown_.store(score_ ? bpm : 0.0, std::memory_order_relaxed);
}

std::string AldaEngine::runState() const {
    std::string s = status();
    return s.substr(0, s.find('\n'));
}

std::string AldaEngine::status() const {
    int length = lengthShown_.load(std::memory_order_relaxed);
    if (length <= 0) return "stopped";
    double pos = posShown_.load(std::memory_order_relaxed);
    char line[96];
    int n = std::snprintf(line, sizeof line, "bar %d/%d  %.0f bpm", int(pos / BAR) + 1, (length + BAR - 1) / BAR,
                          bpmShown_.load(std::memory_order_relaxed));
    if (unsigned dropped = midiDropped())
        std::snprintf(line + n, sizeof line - size_t(n), "  midi dropped %u", dropped);
    std::lock_guard<std::mutex> lock(partsLock_);
    return parts_.empty() ? std::string(line) : std::string(line) + "\n" + parts_;
}

}  // namespace alda
