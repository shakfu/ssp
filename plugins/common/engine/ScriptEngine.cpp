#include "ScriptEngine.h"

#include <algorithm>
#include <vector>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ssp::engine {

const char* ScriptEngine::paramName(int i) {
    static const char* names[PARAMS] = { "p1", "p2",  "p3",  "p4",  "p5",  "p6",  "p7",  "p8",
                                         "p9", "p10", "p11", "p12", "p13", "p14", "p15", "p16" };
    return names[i];
}

bool ScriptEngine::readText(const std::string& path, std::string& text) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    text = ss.str();
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        if (c != '\r') out += c;
    text.swap(out);
    return true;
}

float ScriptEngine::ParamSpec::map(float n) const {
    return log ? min * std::pow(max / min, n) : min + (max - min) * n;
}

float ScriptEngine::ParamSpec::unmap(float v) const {
    float n = log ? std::log(v / min) / std::log(max / min) : (v - min) / (max - min);
    return std::isfinite(n) ? std::min(std::max(n, 0.0f), 1.0f) : 0.0f;
}

// Lines starting with ; or // that hold "@pN label [min max [unit]] [log]"
ScriptEngine::Specs ScriptEngine::parseSpecs(const std::string& text) {
    Specs specs;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        size_t c = line.find_first_not_of(" \t");
        if (c == std::string::npos || !(line[c] == ';' || line.compare(c, 2, "//") == 0)) continue;
        size_t at = line.find("@p", c);
        if (at == std::string::npos) continue;
        size_t digits = at + 2, past = digits;
        while (past < line.size() && past - digits < 2 && line[past] >= '0' && line[past] <= '9') past++;
        if (past == digits || (past < line.size() && line[past] != ' ' && line[past] != '\t')) continue;
        int i = std::atoi(line.substr(digits, past - digits).c_str()) - 1;
        if (i < 0 || i >= PARAMS) continue;
        std::istringstream words(line.substr(past));
        ParamSpec sp;
        if (!(words >> sp.label)) continue;
        std::string w;
        std::vector<std::string> rest;
        while (words >> w) rest.push_back(w);
        if (!rest.empty() && rest.back() == "log") {
            sp.log = true;
            rest.pop_back();
        }
        char* end = nullptr;
        if (rest.size() >= 2) {
            float lo = std::strtof(rest[0].c_str(), &end);
            bool okLo = *end == '\0';
            float hi = std::strtof(rest[1].c_str(), &end);
            if (okLo && *end == '\0' && lo != hi) {
                sp.min = lo;
                sp.max = hi;
                if (rest.size() >= 3) sp.unit = rest[2];
            }
        }
        if (sp.log && !(sp.min > 0.0f && sp.max > 0.0f)) sp.log = false;  // log needs a positive range
        specs[size_t(i)] = sp;
    }
    return specs;
}

void ScriptEngine::setSpecs(const Specs& specs) {
    for (int i = 0; i < PARAMS; i++) {
        min_[i].store(specs[size_t(i)].min, std::memory_order_relaxed);
        max_[i].store(specs[size_t(i)].max, std::memory_order_relaxed);
        log_[i].store(specs[size_t(i)].log, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> lock(lock_);
        specs_ = specs;
    }
    specsGen_.fetch_add(1, std::memory_order_release);
}

ScriptEngine::ParamSpec ScriptEngine::spec(int i) const {
    std::lock_guard<std::mutex> lock(lock_);
    return specs_[size_t(i)];
}

float ScriptEngine::value(int i) const {
    ParamSpec sp;
    sp.min = min_[i].load(std::memory_order_relaxed);
    sp.max = max_[i].load(std::memory_order_relaxed);
    sp.log = log_[i].load(std::memory_order_relaxed);
    return sp.map(param(i));
}

void ScriptEngine::prepare(float sampleRate, int maxBlock) {
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlock;
    std::string p;
    {
        std::lock_guard<std::mutex> lock(lock_);
        p = hasPending_ ? pending_ : path_;
        hasPending_ = false;
    }
    running_ = false;  // the running program, if any, is at the old rate
    run(p);
}

void ScriptEngine::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(lock_);
    pending_ = path;
    hasPending_ = true;
}

void ScriptEngine::idle() {
    std::string p;
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!hasPending_) return;
        p = pending_;
        hasPending_ = false;
    }
    run(p);
}

void ScriptEngine::run(const std::string& path) {
    std::string text, err;
    bool ok;
    if (path.empty()) {
        ok = compile(builtin(), path, err);
    } else if (!readText(path, text)) {
        ok = false;
        err = "cannot read " + path;
    } else {
        ok = compile(text, path, err);
    }
    // nothing running: fall back to the built-in, so the plugin always sounds
    bool fellBack = false;
    if (!ok && !running_ && !path.empty()) {
        std::string ignored;
        fellBack = compile(builtin(), std::string(), ignored);
    }
    running_ = running_ || ok || fellBack;
    if (ok) setSpecs(declared(path.empty() ? std::string(builtin()) : text));
    else if (fellBack) setSpecs(declared(builtin()));
    // keep a failed choice, so a preset saved now still names it
    std::lock_guard<std::mutex> lock(lock_);
    path_ = path;
    error_ = ok ? std::string() : fellBack ? err + " (built-in running)" : err;
}

std::string ScriptEngine::path() const {
    std::lock_guard<std::mutex> lock(lock_);
    return hasPending_ ? pending_ : path_;
}

std::string ScriptEngine::error() const {
    std::lock_guard<std::mutex> lock(lock_);
    return error_;
}

}  // namespace ssp::engine
