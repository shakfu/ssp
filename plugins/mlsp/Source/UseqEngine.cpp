#include "UseqEngine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "signal_engine/signal_engine.h"

namespace mlsp {

using ssp::engine::Clock;

namespace {
constexpr int INPUTS = 32;           // in1-in8, ain1-ain8, p1-p16: the fork's resolve_hardware_input
constexpr int AIN = 8, PARAM_IN = 16;
constexpr int D_OUT = 8;             // d1 is engine output 8; a1 is 0
constexpr float GATE_HIGH = 1.0f;    // 5 V
constexpr double FALLBACK_BPM = 120.0;

const char* const BUILTIN = R"(; mlsp: ModuLisp, the language of the uSEQ module.
; Ctrl-Enter runs the buffer, Ctrl-E the form at the cursor, Ctrl-. clears.
; d1-d4 are out 1-4 (gates), a1-a4 out 5-8 (0 to 5 V).
; @p1 rate 1 4
; @p2 depth 0 1

(set-bpm 120)

(d1 (sqr beat))
(d2 (gates [1 0 1 1 0 1 0 0] bar))
(d3 (euclid 3 8 bar))
(d4 (sqr (fast p1 beat)))

(a1 (usin bar))
(a2 (seq [0 0.2 0.4 0.6] bar))
(a3 (* p2 (tri phrase)))
(a4 ain3)
)";

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isDelimiter(char c) { return isSpace(c) || c == '(' || c == ')' || c == '[' || c == ']' || c == ';' || c == '"'; }
}  // namespace

std::mutex& UseqEngine::evalLock() {
    static std::mutex lock;
    return lock;
}

UseqEngine::UseqEngine()
    : live_(std::make_unique<sig::SignalEngine>()),
      cells_(sig::MAX_CELLS),
      inputs_(INPUTS),
      values_(sig::MAX_OUTPUTS),
      workspace_(sig::MAX_TOTAL_NODES) {
    std::lock_guard<std::mutex> lock(evalLock());
    live_->init_defaults();
}

UseqEngine::~UseqEngine() = default;

const char* UseqEngine::builtin() const {
    return BUILTIN;
}

bool UseqEngine::split(const std::string& text, int line, std::vector<Form>& forms, std::string& error) {
    forms.clear();
    size_t i = 0;
    const size_t n = text.size();
    auto fail = [&](int at, const char* what) {
        error = "line " + std::to_string(at) + ": " + what;
        return false;
    };
    while (i < n) {
        char c = text[i];
        if (c == '\n') {
            line++;
            i++;
        } else if (isSpace(c)) {
            i++;
        } else if (c == ';') {
            while (i < n && text[i] != '\n') i++;
        } else if (c == ')' || c == ']') {
            return fail(line, "unexpected closing bracket");
        } else {
            Form f{ i, 0, line };
            int depth = 0;
            do {
                c = text[i];
                if (c == '\n') {
                    line++;
                    i++;
                } else if (c == ';') {
                    while (i < n && text[i] != '\n') i++;
                } else if (c == '"') {
                    int from = line;
                    for (i++; i < n && text[i] != '"'; i++) {
                        if (text[i] == '\\' && i + 1 < n) i++;
                        if (text[i] == '\n') line++;
                    }
                    if (i >= n) return fail(from, "string not closed");
                    i++;
                } else if (c == '(' || c == '[') {
                    depth++;
                    i++;
                } else if (c == ')' || c == ']') {
                    depth--;
                    i++;
                } else if (depth == 0) {
                    while (i < n && !isDelimiter(text[i])) i++;
                } else {
                    i++;
                }
            } while (depth > 0 && i < n);
            if (depth > 0) return fail(f.line, "form not closed");
            f.length = i - f.offset;
            if (f.length > MAX_FORM) return fail(f.line, "form too long");
            forms.push_back(f);
        }
    }
    return true;
}

UseqEngine::Options UseqEngine::options(const std::string& text) {
    Options o;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        size_t c = line.find_first_not_of(" \t");
        if (c == std::string::npos || line[c] != ';') continue;
        c = line.find_first_not_of("; \t", c);
        if (c == std::string::npos) continue;
        std::istringstream words(line.substr(c));
        std::string word, arg;
        words >> word;
        if (word == "@reset") {
            o.reset = true;
        } else if (word == "@clock") {
            words >> arg;
            if (arg == "midi") {
                o.clock = Clock::MIDI;
            } else {
                o.clock = Clock::INPUT;
                int ppqn = arg.empty() ? 4 : std::atoi(arg.c_str());
                o.ppqn = std::clamp(ppqn, 1, 96);
            }
        }
    }
    return o;
}

bool UseqEngine::eval(sig::SignalEngine& engine, const std::string& text, const std::vector<Form>& forms,
                      std::string& error, std::string* result) {
    sig::EvalResult r;
    for (const Form& f : forms) {
        r = sig::eval_cold(text.data() + f.offset, uint32_t(f.length), engine);
        if (r.kind != sig::EvalResult::Error) continue;
        int line = f.line;
        std::string msg = "error";
        if (r.diagnostic_count > 0) {
            const sig::Diagnostic& d = r.diagnostics[0];
            size_t at = std::min<size_t>(d.span_start, f.length);
            line += int(std::count(text.begin() + long(f.offset), text.begin() + long(f.offset + at), '\n'));
            if (d.message) msg = d.message;
            if (d.suggestion) msg += std::string(" (") + d.suggestion + ")";
        }
        error = "line " + std::to_string(line) + ": " + msg;
        return false;
    }
    if (result) {
        char s[64];
        if (r.kind == sig::EvalResult::Number) std::snprintf(s, sizeof s, "= %g", r.number);
        else if (r.kind == sig::EvalResult::Text && r.text) std::snprintf(s, sizeof s, "%.*s", int(r.text_length), r.text);
        else std::snprintf(s, sizeof s, "ok");
        *result = s;
    }
    return true;
}

bool UseqEngine::compile(const std::string& text, const std::string&, std::string& error) {
    std::vector<Form> forms;
    if (!split(text, 1, forms, error)) return false;
    auto fresh = std::make_unique<sig::SignalEngine>();
    std::lock_guard<std::mutex> evalGuard(evalLock());
    fresh->init_defaults();
    if (!eval(*fresh, text, forms, error, nullptr)) return false;

    std::lock_guard<std::mutex> liveGuard(liveLock_);
    bool drops = false;  // the running program assigns an output this one does not
    for (size_t i = 0; i < sig::MAX_OUTPUTS; i++)
        drops = drops || (live_->output_sources[i].has_source && !fresh->output_sources[i].has_source);
    static const char clear[] = "(useq-clear)";
    if (drops) sig::eval_cold(clear, sizeof clear - 1, *live_);
    live_->state.is_playing = true;
    live_->state.current_time = tShown_.load(std::memory_order_relaxed);
    if (!eval(*live_, text, forms, error, nullptr)) return false;  // ran on a fresh engine, so not expected
    Options o = options(text);
    clockSource_.store(o.clock, std::memory_order_relaxed);
    ppqn_.store(o.ppqn, std::memory_order_relaxed);
    reset_.store(o.reset, std::memory_order_relaxed);
    showOutputs();
    return true;
}

bool UseqEngine::runPart(const std::string& part, const std::string& before, const std::string&, std::string& result) {
    std::vector<Form> forms;
    int line = 1 + int(std::count(before.begin(), before.end(), '\n'));
    if (!split(part, line, forms, result)) return false;
    if (forms.empty()) {
        result = "nothing to run";
        return false;
    }
    std::lock_guard<std::mutex> evalGuard(evalLock());
    std::lock_guard<std::mutex> liveGuard(liveLock_);
    live_->state.current_time = tShown_.load(std::memory_order_relaxed);
    bool ok = eval(*live_, part, forms, result, &result);
    showOutputs();
    return ok;
}

bool UseqEngine::dropAll(std::string& result) {
    static const char clear[] = "(useq-clear)";
    std::lock_guard<std::mutex> evalGuard(evalLock());
    std::lock_guard<std::mutex> liveGuard(liveLock_);
    sig::eval_cold(clear, sizeof clear - 1, *live_);
    showOutputs();
    result = "cleared";
    return true;
}

void UseqEngine::showOutputs() {
    std::string s;
    for (char kind : { 'd', 'a', 's' }) {
        int base = kind == 'a' ? 0 : kind == 'd' ? D_OUT : 2 * D_OUT;
        for (int k = 0; k < 8; k++) {
            if (!live_->output_sources[base + k].has_source) continue;
            if (!s.empty()) s += ' ';
            s += kind;
            s += char('1' + k);
        }
    }
    std::lock_guard<std::mutex> lock(shownLock_);
    outputs_ = s;
}

void UseqEngine::restartTime() {
    t_ = 0.0;
    lastPos_ = clock_.position();
}

void UseqEngine::tick(const float* const* in, int i) {
    for (int c = 0; c < CHANNELS; c++) {
        float v = in[c][i];
        inputs_[size_t(c)] = v > GATE_IN ? 1.0 : 0.0;
        inputs_[size_t(AIN + c)] = std::clamp(double(v), 0.0, 1.0);
    }
    std::fill(values_.begin(), values_.end(), 0.0);  // an output no program assigns is 0
    sig::ExecutionContext ctx;
    ctx.t = t_ + offset_;
    ctx.dt = dt_;
    ctx.cell_values = cells_.data();
    ctx.hw_inputs = inputs_.data();
    ctx.data_pool = live_->cells.data_pool;
    ctx.data_offsets = live_->cells.data_offsets;
    ctx.data_lengths = live_->cells.data_lengths;
    ctx.prev_outputs = live_->pool.prev_output_values;
    ctx.output_values = values_.data();
    ctx.workspace = workspace_.data();
    sig::execute_all_outputs(live_->pool, ctx);
    sig::commit_state(live_->pool, workspace_.data());
    sig::commit_outputs(live_->pool, values_.data());
    dt_ = 0.0;
}

void UseqEngine::process(const float* const* in, float* const* out, int n) {
    std::unique_lock<std::mutex> lock(liveLock_, std::try_to_lock);
    if (lock.owns_lock()) {
        live_->cells.snapshot_values(cells_.data(), cells_.size());
        double bpm = cells_[sig::GraphBuilder::sym.bpm];
        bpm_ = bpm > 0.0 ? bpm : FALLBACK_BPM;
        beatsPerBar_ = std::max(1.0, cells_[sig::GraphBuilder::sym.beats_per_bar]);
        offset_ = live_->state.time_offset;
        playing_ = live_->state.is_playing;
    }
    const bool run = lock.owns_lock() && playing_;

    // the free clock runs at the program's tempo, so t is real time; the others make it follow the clock
    clock_.setPpqn(ppqn_.load(std::memory_order_relaxed));
    clock_.setBpm(bpm_);
    clock_.follow(Clock::Source(clockSource_.load(std::memory_order_relaxed)), sampleRate());
    if (clock_.transport() == Clock::START) restartTime();
    while (clock_.midiPulse()) {
    }
    for (int p = 0; p < PARAMS; p++) inputs_[size_t(PARAM_IN + p)] = value(p);
    const bool reset = reset_.load(std::memory_order_relaxed);

    for (int i = 0; i < n; i++) {
        if (reset && clock_.resetInput(in[RESET_INPUT][i])) restartTime();
        clock_.clockInput(in[CLOCK_INPUT][i]);
        if (--countdown_ <= 0) {
            countdown_ = TICK;
            if (run) tick(in, i);
        }
        for (int k = 0; k < GATES; k++) out[k][i] = values_[size_t(D_OUT + k)] > 0.5 ? GATE_HIGH : 0.0f;
        for (int k = 0; k < CVS; k++) out[GATES + k][i] = float(std::clamp(values_[size_t(k)], 0.0, 1.0));
        if (clock_.advance()) {
            double pos = clock_.position();
            double step = std::max(0.0, pos - lastPos_) / Clock::TICKS_PER_QUARTER * 60.0 / bpm_;
            lastPos_ = pos;
            t_ += step;
            dt_ += step;
        }
    }
    tShown_.store(t_ + offset_, std::memory_order_relaxed);
    bpmShown_.store(bpm_, std::memory_order_relaxed);
    clockBpmShown_.store(clock_.bpm(), std::memory_order_relaxed);
    barShown_.store(beatsPerBar_, std::memory_order_relaxed);
    playingShown_.store(playing_, std::memory_order_relaxed);
}

std::string UseqEngine::runState() const {
    if (!playingShown_.load(std::memory_order_relaxed)) return "paused";
    double bpm = bpmShown_.load(std::memory_order_relaxed);
    double beats = tShown_.load(std::memory_order_relaxed) * bpm / 60.0;
    char s[64];
    std::snprintf(s, sizeof s, "bar %ld  %.0f bpm", long(beats / barShown_.load(std::memory_order_relaxed)) + 1,
                  clockSource_.load(std::memory_order_relaxed) == Clock::FREE ? bpm
                                                                             : clockBpmShown_.load(std::memory_order_relaxed));
    return s;
}

std::string UseqEngine::status() const {
    std::string s = runState();
    switch (clockSource_.load(std::memory_order_relaxed)) {
    case Clock::INPUT: s += "  clock In 2 x" + std::to_string(ppqn_.load(std::memory_order_relaxed)); break;
    case Clock::MIDI: s += "  clock midi"; break;
    default: break;
    }
    if (reset_.load(std::memory_order_relaxed)) s += "  reset In 1";
    std::lock_guard<std::mutex> lock(shownLock_);
    return s + "\n" + (outputs_.empty() ? std::string("no outputs") : outputs_);
}

}  // namespace mlsp
