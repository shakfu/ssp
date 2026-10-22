#include "SapfEngine.h"

namespace sapf {

namespace {

std::string stateText(const Session& s) {
    switch (s.state()) {
        case Session::State::LOADING: return "loading the prelude";
        case Session::State::IDLE: return "ready";
        case Session::State::RUNNING: return "evaluating";
        default: return s.error();
    }
}

}  // namespace

void SapfEngine::prepare(float sampleRate, int maxBlock) {
    session_.prepare(sampleRate);  // the first starts the interpreter, which the program waits for
    ScriptEngine::prepare(sampleRate, maxBlock);
}

bool SapfEngine::compile(const std::string& text, const std::string&, std::string& error) {
    if (session_.restart(text)) return true;
    error = session_.error();
    return false;
}

const char* SapfEngine::builtin() const {
    return "; Ctrl-Enter runs this text, Ctrl-E the block at the cursor, Ctrl-. stops\n"
           "; inputs 1-8 are 0 adc .. 7 adc; CV is 1.0 per 5 V\n"
           "[0 adc 1 adc] play\n";
}

bool SapfEngine::runPart(const std::string& part, const std::string&, const std::string&, std::string& result) {
    if (session_.evaluate(part)) return true;
    result = "not run: " + stateText(session_);
    return false;
}

bool SapfEngine::dropAll(std::string& result) {
    session_.stop();
    result = "stopped";
    return true;
}

std::string SapfEngine::load() const {
    const uint64_t late = session_.lateFrames();
    return "render " + std::to_string(int(session_.renderLoad() * 100.f)) + "%" +
           (session_.renderRealtime() ? " rt" : "") +
           (late ? "  late " + std::to_string(late * 1000 / uint64_t(sampleRate())) + " ms" : "");
}

std::string SapfEngine::status() const {
    return stateText(session_) + "\n" + load();
}

std::string SapfEngine::runState() const {
    const auto s = session_.state();
    const char* state = s == Session::State::LOADING ? "prelude" : s == Session::State::RUNNING ? "evaluating" : "ready";
    return state + ("  " + load());
}

bool SapfEngine::output(std::vector<OutputLine>& lines, size_t max) const {
    const auto out = session_.output(max);
    const size_t from = out.size() > max ? out.size() - max : 0;  // output() may add a partial line
    lines.clear();
    for (size_t i = from; i < out.size(); i++) lines.push_back({ out[i].text, out[i].error });
    return true;
}

std::vector<std::string> SapfEngine::words() const {
    if (session_.state() == Session::State::LOADING) return {};
    return session_.words();
}

}  // namespace sapf
