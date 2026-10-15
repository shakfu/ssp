#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MaxAudioBackend;

namespace sapf {

// The sapf interpreter on its own thread, and sapf's audio rendered on another into a FIFO the host's
// audio callback reads: a slow render silences sapf, not the host (docs/dev/sapf-plugin.md, Step 2).
// No JUCE, so it tests natively. sapf's interpreter, audio backend and output hook are global, so one
// Session per process holds them; another reports that and stays silent.
//
// Evaluations run one at a time. Definitions persist; the stack starts empty, and what an evaluation
// leaves on it is printed, then cleared. While one runs, a watchdog interrupts it when the process
// grows by more than MEMORY_LIMIT.
class Session {
public:
    struct Paths {
        std::string prelude;  // sapf-prelude.txt; empty for none
        std::string files;    // where >sf and >sfo write, unless SAPF_RECORDINGS is set
    };
    static constexpr int CHANNELS = 8;
    static constexpr int BLOCK = 128;       // sapf's block, and a render's
    static constexpr int FIFO_BLOCKS = 4;   // rendered ahead of the callback
    static constexpr int RING = 1024;       // FIFO frames, a power of two above FIFO_BLOCKS * BLOCK
    // below Synthor's audio threads (dsp 30, ALSA 40), above its GUI thread, which otherwise holds a core
    // longer than the FIFO lasts. A render above REALTIME_EXIT load drops to normal priority until
    // REALTIME_ENTER, so code that cannot keep up does not hold a core.
    static constexpr int RENDER_PRIORITY = 20;
    static constexpr float REALTIME_EXIT = 0.9f, REALTIME_ENTER = 0.5f;
    static constexpr size_t MEMORY_LIMIT = 256u << 20;
    static constexpr size_t OUTPUT_LINES = 400;

    enum class State { LOADING, IDLE, RUNNING, FAILED, SECOND };

    explicit Session(Paths paths);
    ~Session();  // message thread, audio stopped

    // Message thread, audio stopped. The first call starts the interpreter at `rate`, which loads the
    // prelude, and the render thread. sapf keeps its first rate: another is reported, not applied.
    void prepare(float rate);
    // audio thread: input to `adc`, output from the FIFO; frames it lacks are silent and counted
    void process(const float* const* in, float* const* out, int n);

    // Any thread: runs `code` on the interpreter's thread. False, with a line in the output, while
    // another evaluation runs or the prelude loads.
    bool evaluate(const std::string& code);
    // Any thread: interrupts a running evaluation, then stops every sound.
    void stop();
    // Any thread: a line of the plugin's own in the output, such as a save's result.
    void note(const std::string& line) { post((line + "\n").c_str()); }

    State state() const { return state_.load(); }
    std::string error() const;  // why the Session is silent

    // the render thread's time per second of audio, smoothed; and the frames the callback lacked
    float renderLoad() const { return renderLoad_.load(std::memory_order_relaxed); }
    // the render thread runs at RENDER_PRIORITY, SCHED_FIFO: above the host's GUI, below its audio
    bool renderRealtime() const { return realtime_.load(std::memory_order_relaxed); }
    uint64_t lateFrames() const { return late_.load(std::memory_order_relaxed); }

    struct Line {
        std::string text;
        bool error;
    };
    // the last `max` lines of output; `seq` changes when they do
    std::vector<Line> output(size_t max) const;
    uint32_t outputSeq() const { return outputSeq_.load(); }

private:
    void run();     // the interpreter's thread
    void watch();   // the watchdog's thread
    void render();  // the render thread
    void post(const char* text);
    static void postHook(const char* text, void* self) { static_cast<Session*>(self)->post(text); }

    Paths paths_;
    bool owner_ = false;
    std::atomic<State> state_{ State::LOADING };
    std::string error_;
    float rate_ = 0.0f;

    std::thread thread_, watchdog_, renderer_;
    mutable std::mutex jobLock_;
    std::condition_variable jobReady_;
    std::string job_;
    bool hasJob_ = false, stopJob_ = false, quit_ = false;
    std::atomic<size_t> baseline_{ 0 };  // resident bytes when the running evaluation started

    // the FIFO: frames [read_, written_) of ring_ hold sound; the render thread writes, process reads
    std::atomic<MaxAudioBackend*> backend_{ nullptr };
    float ring_[CHANNELS][RING] = {};
    std::atomic<uint64_t> written_{ 0 }, read_{ 0 }, late_{ 0 };
    std::atomic<bool> renderQuit_{ false };
    std::mutex renderLock_;
    std::condition_variable renderWake_;
    std::atomic<float> renderLoad_{ 0.0f };
    std::atomic<bool> realtime_{ false };

    mutable std::mutex outLock_;
    std::deque<Line> out_;
    std::string partial_;  // output since the last newline
    std::atomic<uint32_t> outputSeq_{ 0 };
};

}  // namespace sapf
