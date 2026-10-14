#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "SapfServer.h"
#include "ScWorld.h"

namespace sapf {

// The sapf-sc interpreter on its own thread, playing through a ScWorld the host's audio callback runs.
// No JUCE, so it tests natively. sapf-sc's interpreter, server and output hook are global, so one
// Session per process holds them; another reports that and stays silent.
//
// Evaluations run one at a time. Definitions persist; the stack starts empty, and what an evaluation
// leaves on it is printed, then cleared. While one runs, a watchdog interrupts it when the process
// grows by more than MEMORY_LIMIT.
class Session {
public:
    struct Paths {
        std::string ugens;    // UGen plugins
        std::string prelude;  // sapf-prelude.txt; empty for none
        std::string files;    // where >sd and >nrt write, unless SAPF_SYNTHDEFS is set
    };
    static constexpr int CHANNELS = scsy::ScWorld::CHANNELS;
    static constexpr size_t MEMORY_LIMIT = 256u << 20;
    static constexpr size_t OUTPUT_LINES = 400;

    enum class State { LOADING, IDLE, RUNNING, FAILED, SECOND };

    explicit Session(Paths paths);
    ~Session();  // message thread, audio stopped

    // Message thread, audio stopped: opens the World at `rate`, again if the rate changed. The first
    // call starts the interpreter, which loads the prelude.
    void prepare(float rate);
    // audio thread
    void process(const float* const* in, float* const* out, int n);

    // Any thread: runs `code` on the interpreter's thread. False, with a line in the output, while
    // another evaluation runs or the prelude loads.
    bool evaluate(const std::string& code);
    // Any thread: interrupts a running evaluation, then stops every synth and ola voice.
    void stop();
    // Any thread: a line of the plugin's own in the output, such as a save's result.
    void note(const std::string& line) { post((line + "\n").c_str()); }

    State state() const { return state_.load(); }
    std::string error() const;  // why the World did not open, or why the Session is silent

    struct Line {
        std::string text;
        bool error;
    };
    // the last `max` lines of output; `seq` changes when they do
    std::vector<Line> output(size_t max) const;
    uint32_t outputSeq() const { return outputSeq_.load(); }

private:
    void run();    // the interpreter's thread
    void watch();  // the watchdog's thread
    void post(const char* text);

    Paths paths_;
    bool owner_ = false;
    std::atomic<State> state_{ State::LOADING };
    std::string error_;

    scsy::ScWorld world_;
    std::unique_ptr<SapfServer> server_;
    float rate_ = 0.0f;
    std::atomic<bool> open_{ false };

    std::thread thread_, watchdog_;
    mutable std::mutex jobLock_;
    std::condition_variable jobReady_;
    std::string job_;
    bool hasJob_ = false, stopJob_ = false, quit_ = false;
    std::atomic<size_t> baseline_{ 0 };  // resident bytes when the running evaluation started

    mutable std::mutex outLock_;
    std::deque<Line> out_;
    std::string partial_;  // output since the last newline
    std::atomic<uint32_t> outputSeq_{ 0 };
};

}  // namespace sapf
