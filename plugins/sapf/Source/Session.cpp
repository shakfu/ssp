#include "Session.h"

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>

#include "VM.hpp"
#include "sapf/Engine.hpp"
#include "sapf/MaxAudioBackend.hpp"

namespace sapf {

namespace {

std::atomic<bool> gOwned{ false };  // a Session holds sapf's globals
std::string gPrelude;               // sapf keeps a pointer to the prelude's path

// False where the process may not, such as a desktop user without rtprio
bool setRealtime(bool on) {
    sched_param p{};
    p.sched_priority = on ? Session::RENDER_PRIORITY : 0;
    return pthread_setschedparam(pthread_self(), on ? SCHED_FIFO : SCHED_OTHER, &p) == 0;
}

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
    renderQuit_ = true;
    renderWake_.notify_all();
    if (renderer_.joinable()) renderer_.join();
    if (owner_) {
        if (MaxAudioBackend* b = backend_.load()) b->stopAll();  // players run code from this library
        SapfSetPostHook(nullptr, nullptr);
        gInterrupt = false;
        gOwned = false;
    }
}

void Session::prepare(float rate) {
    if (!owner_) return;
    if (thread_.joinable()) {
        if (rate != rate_) note("sapf runs at " + std::to_string(int(rate_)) + " Hz until the patch reloads");
        return;
    }
    rate_ = rate;
    SapfSetPostHook(&Session::postHook, this);
    if (!paths_.files.empty()) setenv("SAPF_RECORDINGS", paths_.files.c_str(), 0);
    thread_ = std::thread([this] { run(); });
    watchdog_ = std::thread([this] { watch(); });
    renderer_ = std::thread([this] { render(); });
}

void Session::process(const float* const* in, float* const* out, int n) {
    const uint64_t w = written_.load(std::memory_order_acquire);
    if (w == 0) {  // before the first render: not late
        for (int c = 0; c < CHANNELS; c++) std::fill(out[c], out[c] + n, 0.0f);
        return;
    }
    MaxMSPSetInputBuffers(in, CHANNELS, n);
    const uint64_t r = read_.load(std::memory_order_relaxed);
    const int have = int(std::min<uint64_t>(w - r, uint64_t(n)));
    for (int c = 0; c < CHANNELS; c++) {
        for (int i = 0; i < have; i++) out[c][i] = ring_[c][(r + uint64_t(i)) & (RING - 1)];
        std::fill(out[c] + have, out[c] + n, 0.0f);
    }
    read_.store(r + uint64_t(have), std::memory_order_release);
    if (have < n) late_.fetch_add(uint64_t(n - have), std::memory_order_relaxed);
    renderWake_.notify_one();
}

void Session::render() {
    float block[CHANNELS][BLOCK];
    float* outs[CHANNELS];
    for (int c = 0; c < CHANNELS; c++) outs[c] = block[c];
    const double period = BLOCK / double(rate_);
    const bool canRealtime = setRealtime(true);
    realtime_ = canRealtime;
    std::unique_lock<std::mutex> lock(renderLock_);
    while (!renderQuit_) {
        MaxAudioBackend* backend = backend_.load(std::memory_order_acquire);
        const uint64_t w = written_.load(std::memory_order_relaxed);
        if (!backend || w - read_.load(std::memory_order_acquire) + BLOCK > uint64_t(FIFO_BLOCKS * BLOCK)) {
            // a timeout too: process notifies without the lock, so a wake can be missed
            renderWake_.wait_for(lock, std::chrono::milliseconds(1));
            continue;
        }
        auto t0 = std::chrono::steady_clock::now();
        backend->render(outs, CHANNELS, BLOCK);
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        for (int c = 0; c < CHANNELS; c++)
            for (int i = 0; i < BLOCK; i++) ring_[c][(w + uint64_t(i)) & (RING - 1)] = block[c][i];
        written_.store(w + BLOCK, std::memory_order_release);
        float avg = renderLoad_.load(std::memory_order_relaxed);
        avg += 0.01f * (float(dt / period) - avg);
        renderLoad_.store(avg, std::memory_order_relaxed);
        const bool rt = realtime_.load(std::memory_order_relaxed);
        if (canRealtime && rt != (avg < (rt ? REALTIME_EXIT : REALTIME_ENTER)) && setRealtime(!rt)) realtime_ = !rt;
    }
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

// the names in sapf's built-ins and in the workspace's forms, as the prelude leaves them
static std::vector<std::string> definedWords(Thread& th) {
    std::vector<std::string> words;
    auto add = [&](GTable* table) {
        for (const auto& node : table->sorted())
            if (node->mKey.isString()) words.push_back(static_cast<String*>(node->mKey.o())->s);
    };
    add(vm.builtins());
    for (GForm* form = th.mWorkspace(); form; form = form->mNextForm()) add(form->mTable());
    std::sort(words.begin(), words.end());
    words.erase(std::unique(words.begin(), words.end()), words.end());
    return words;
}

void Session::run() {
    SapfEngineConfig config;
    config.sampleRate = rate_;
    config.blockSize = BLOCK;
    config.enableManta = false;
    config.openFiles = false;  // system() would fork the host
    if (!paths_.prelude.empty()) {
        gPrelude = paths_.prelude;
        config.preludeFile = gPrelude.c_str();
    }
    SapfEngine& engine = GetSapfEngine();
    engine.configure(config);
    engine.initialize();
    MaxAudioBackend& backend = InstallMaxMSPBackend();
    RegisterMaxAudioPrimitives();
    MaxMSPSetInputLead(FIFO_BLOCKS * BLOCK);
    Thread th;
    engine.loadPrelude(th);
    words_ = definedWords(th);
    backend_.store(&backend, std::memory_order_release);
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
