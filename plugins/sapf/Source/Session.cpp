#include "Session.h"

#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <fstream>

#include "Scsynth.hpp"
#include "Texture.hpp"
#include "VM.hpp"
#include "sapf/Engine.hpp"

namespace sapf {

namespace {

std::atomic<bool> gOwned{ false };  // a Session holds sapf-sc's globals
std::string gPrelude;               // sapf-sc keeps a pointer to the prelude's path

size_t resident() {
    std::ifstream f("/proc/self/statm");
    size_t pages = 0, rss = 0;
    f >> pages >> rss;
    return rss * size_t(sysconf(_SC_PAGESIZE));
}

// As sapf's REPL: errors posted, and what is left on the stack printed, then cleared.
void evaluateNow(Thread& th, const std::string& code) {
    th.clearStack();
    try {
        P<Fun> fun;
        if (th.compile(code.c_str(), fun, true)) fun->runREPL(th);
    } catch (V& v) {
        ::post("error: ");
        v.print(th);
        ::post("\n");
    } catch (int err) {
        if (err <= -1000 && err > -1000 - kNumErrors) ::post("error: %s\n", errString[-1000 - err]);
        else ::post("error: %d\n", err);
    } catch (std::bad_alloc&) {
        ::post("error: not enough memory\n");
    } catch (...) {
        ::post("error: unknown\n");
    }
    gInterrupt = false;  // printing the stack below must not stop at once
    try {
        if (th.stackDepth()) {
            ::post("stack: ");
            th.printStack();
            ::post("\n");
        }
    } catch (...) {
        ::post("\n");
    }
    th.clearStack();
}

}  // namespace

Session::Session(Paths paths) : paths_(std::move(paths)) {
    bool expected = false;
    owner_ = gOwned.compare_exchange_strong(expected, true);
    if (!owner_) {
        state_ = State::SECOND;
        error_ = "another sapf runs in this patch; one at a time";
    }
}

Session::~Session() {
    {
        std::lock_guard<std::mutex> lock(jobLock_);
        quit_ = true;
    }
    if (owner_) gInterrupt = true;
    jobReady_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (watchdog_.joinable()) watchdog_.join();
    if (owner_) {
        // ola threads run code from this library: none may outlive it
        stopTextures();
        for (int i = 0; i < 300 && runningTextures() > 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        setEmbeddedScsynth(nullptr);
        setPostHandler({});
        gInterrupt = false;
    }
    world_.close();
    if (owner_) gOwned = false;
}

void Session::prepare(float rate) {
    if (!owner_) return;
    if (!world_.isOpen() || rate != rate_) {
        if (world_.isOpen()) {  // a new rate: nothing may use the World while it reopens
            stopTextures();
            for (int i = 0; i < 300 && runningTextures() > 0; i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            open_ = false;
            setEmbeddedScsynth(nullptr);
            world_.close();
        }
        std::string err;
        if (!world_.open(rate, paths_.ugens, err)) {
            error_ = "scsynth: " + err;
            state_ = State::FAILED;
            return;
        }
        rate_ = rate;
        if (!server_) server_ = std::make_unique<SapfServer>(world_);
        setEmbeddedScsynth(server_.get());
        open_ = true;
    }
    if (!thread_.joinable()) {
        setPostHandler([this](const char* s) { post(s); });
        if (!paths_.files.empty()) {
            setenv("SAPF_SYNTHDEFS", paths_.files.c_str(), 0);
            setenv("SAPF_RECORDINGS", paths_.files.c_str(), 0);
        }
        thread_ = std::thread([this] { run(); });
        watchdog_ = std::thread([this] { watch(); });
    }
}

void Session::process(const float* const* in, float* const* out, int n) {
    if (open_.load(std::memory_order_acquire)) {
        world_.process(in, out, n);
        return;
    }
    for (int c = 0; c < CHANNELS; c++) std::fill(out[c], out[c] + n, 0.0f);
}

bool Session::evaluate(const std::string& code) {
    std::lock_guard<std::mutex> lock(jobLock_);
    const State s = state_.load();
    if (s != State::IDLE) {
        if (s == State::RUNNING) post("busy: Ctrl-. or Esc stops the running evaluation\n");
        else if (s == State::LOADING) post("the prelude is loading\n");
        else post((error_ + "\n").c_str());
        return false;
    }
    job_ = code;
    hasJob_ = true;
    state_ = State::RUNNING;  // from now: the job is the running evaluation
    jobReady_.notify_all();
    return true;
}

void Session::stop() {
    std::lock_guard<std::mutex> lock(jobLock_);
    if (!owner_) return;
    stopJob_ = true;
    hasJob_ = false;  // a queued evaluation is dropped
    if (state_ == State::RUNNING) gInterrupt = true;
    if (state_ == State::IDLE) state_ = State::RUNNING;  // until the stop is done
    jobReady_.notify_all();
}

std::string Session::error() const {
    return error_;
}

void Session::run() {
    SapfEngineConfig config;
    config.sampleRate = rate_;
    if (!paths_.prelude.empty()) {
        gPrelude = paths_.prelude;
        config.preludeFile = gPrelude.c_str();
    }
    SapfEngine& engine = GetSapfEngine();
    engine.configure(config);
    engine.initialize();
    Thread th;
    engine.loadPrelude(th);
    state_ = State::IDLE;

    for (;;) {
        std::string code;
        bool stopNow = false;
        {
            std::unique_lock<std::mutex> lock(jobLock_);
            jobReady_.wait(lock, [this] { return quit_ || hasJob_ || stopJob_; });
            if (quit_) return;
            if (stopJob_) {
                stopJob_ = false;
                stopNow = true;
            } else {
                code = job_;
                hasJob_ = false;
                baseline_ = resident();
            }
            gInterrupt = false;  // under the lock, so a stop() for this job comes after it
        }
        if (stopNow) {
            evaluateNow(th, "stop");
            post("stopped\n");
            std::lock_guard<std::mutex> lock(jobLock_);
            state_ = State::IDLE;
            continue;
        }
        size_t nl = code.find('\n');
        post(("> " + code.substr(0, nl) + (nl == std::string::npos ? "" : " ...") + "\n").c_str());
        evaluateNow(th, code);
        std::lock_guard<std::mutex> lock(jobLock_);
        state_ = State::IDLE;
    }
}

void Session::watch() {
    std::unique_lock<std::mutex> lock(jobLock_);
    while (!jobReady_.wait_for(lock, std::chrono::milliseconds(100), [this] { return quit_; })) {
        if (state_ != State::RUNNING || gInterrupt) continue;
        if (resident() > baseline_ + MEMORY_LIMIT) {
            gInterrupt = true;
            post("error: memory grew past 256 MB; interrupted\n");
        }
    }
}

void Session::post(const char* text) {
    std::lock_guard<std::mutex> lock(outLock_);
    for (const char* c = text; *c; c++) {
        if (*c != '\n') {
            partial_ += *c;
            continue;
        }
        out_.push_back({ partial_, partial_.find("error") != std::string::npos });
        partial_.clear();
        if (out_.size() > OUTPUT_LINES) out_.pop_front();
    }
    outputSeq_++;
}

std::vector<Session::Line> Session::output(size_t max) const {
    std::lock_guard<std::mutex> lock(outLock_);
    std::vector<Line> lines;
    size_t start = out_.size() > max ? out_.size() - max : 0;
    for (size_t i = start; i < out_.size(); i++) lines.push_back(out_[i]);
    if (!partial_.empty() && lines.size() < max + 1) lines.push_back({ partial_, false });
    return lines;
}

}  // namespace sapf
