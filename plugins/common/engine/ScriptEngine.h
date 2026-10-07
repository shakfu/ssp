#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <string>

#include "engine/Engine.h"

namespace ssp::engine {

// An engine that runs a program from a text file, such as a Csound .csd or a ChucK .ck. The program
// sees CHANNELS audio inputs and outputs, and PARAMS controls named p1..p16. A comment line in the
// program can name a control and give its range:
//
//   ; @p1 cutoff 20 20000 Hz log      (Csound)
//   // @p2 mix                         (ChucK)
//
// The program then receives the control in that range, logarithmically with `log`; else 0..1.
class ScriptEngine : public Engine {
public:
    static constexpr int CHANNELS = 8;
    static constexpr int PARAMS = 16;

    ScriptEngine() { setSpecs(Specs()); }

    // Message thread, audio stopped: (re)compiles the current program at the new rate.
    void prepare(float sampleRate, int maxBlock) override;
    // Worker thread: runs a load that load() queued.
    void idle() override;

    // Message thread: queues a load; an empty path is the built-in program. A program that fails to
    // compile leaves the previous one running.
    void load(const std::string& path);
    // The chosen program's file, queued or running; empty for the built-in. A failed load sets error().
    std::string path() const;
    // The last load's error, or empty.
    std::string error() const;

    // A control as the running program declares it.
    struct ParamSpec {
        std::string label, unit;  // label empty: undeclared
        float min = 0.0f, max = 1.0f;
        bool log = false;
        float map(float normalised) const;
    };
    using Specs = std::array<ParamSpec, PARAMS>;
    static Specs parseSpecs(const std::string& text);
    ParamSpec spec(int i) const;  // not the audio thread
    // changes whenever a load replaces the specs
    unsigned specsGeneration() const { return specsGen_.load(std::memory_order_acquire); }

    // any thread: the control in 0..1
    void setParam(int i, float v) { params_[i].store(v, std::memory_order_relaxed); }
    float param(int i) const { return params_[i].load(std::memory_order_relaxed); }
    // any thread: the control in the program's declared range
    float value(int i) const;

    // "p1".."p16"
    static const char* paramName(int i);

    // Reads a text file without a UTF-8 BOM and with LF line ends. False if unreadable.
    static bool readText(const std::string& path, std::string& text);

protected:
    // Worker or message thread: compiles and starts `text`, read from `path` (empty for the built-in),
    // in place of the running program. On failure, returns false with `error` set and leaves the running
    // program alone.
    virtual bool compile(const std::string& text, const std::string& path, std::string& error) = 0;
    virtual const char* builtin() const = 0;

    float sampleRate() const { return sampleRate_; }
    int maxBlock() const { return maxBlock_; }

private:
    void setSpecs(const Specs& specs);

    std::atomic<float> params_[PARAMS] = {};
    // the range, for value() on the audio thread; labels and units are under lock_
    std::atomic<float> min_[PARAMS] = {}, max_[PARAMS] = {};
    std::atomic<bool> log_[PARAMS] = {};
    Specs specs_;
    std::atomic<unsigned> specsGen_{ 0 };

    void run(const std::string& path);

    float sampleRate_ = 48000.0f;
    int maxBlock_ = 512;
    mutable std::mutex lock_;  // the strings below
    std::string pending_, path_, error_;
    bool hasPending_ = false;
    bool running_ = false;
};

}  // namespace ssp::engine
