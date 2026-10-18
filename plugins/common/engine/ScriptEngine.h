#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

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
// `cv N` adds input N (1..8) to the control as CV, read once per block: one octave per volt on a
// log range, else a tenth of the range per volt, within the range.
//
//   ; @p1 cutoff 20 20000 Hz log cv 3
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
    // Message thread: queues `code` as if read from `path`, against which its relative files resolve.
    // It runs from the code, not the file, until the next load().
    void loadCode(const std::string& code, const std::string& path);
    // The chosen program's file, queued or running; empty for the built-in. A failed load sets error().
    std::string path() const;
    // Message thread: the program's file becomes `path`, without a load: Save As wrote the text there.
    void rename(const std::string& path);
    // The code loadCode() chose, queued or running; false when the program comes from path().
    bool code(std::string& code) const;
    bool loading() const;  // a load not yet compiled

    // Message thread: queues `part`, a block of the program, to run beside it (Ctrl-E). `before` is the
    // text above the block, `path` as for loadCode. Queued work runs in order, after a pending load.
    void evaluate(const std::string& part, const std::string& before, const std::string& path);
    // Message thread: queues the removal of part `index` of parts(), 1-based; 0 for the newest.
    void removePart(int index);
    void clearParts();  // queues the end of the program and every part
    // UI thread: the parts running, as one line in the order they were added; empty for none
    virtual std::string parts() const { return {}; }
    // the result of the last part, removal or clear; changes with noteSeq()
    std::string note() const;
    unsigned noteSeq() const { return noteSeq_.load(std::memory_order_acquire); }
    // the program an empty path runs
    virtual const char* builtin() const = 0;
    // An empty program runs as silence, so the editor's reset (Ctrl-R) can unload the program.
    virtual bool canReset() const { return false; }
    // The last load's error, or empty.
    std::string error() const;
    // UI thread: lines about the running program for the status panel, or empty.
    virtual std::string status() const { return {}; }
    // UI thread: what the Code view's status line shows in place of "running", such as a position; empty
    // for "running".
    virtual std::string runState() const { return {}; }

    // A control as the running program declares it.
    struct ParamSpec {
        std::string label, unit;  // label empty: undeclared
        float min = 0.0f, max = 1.0f;
        bool log = false;
        std::vector<std::string> choices;  // names of the whole values min, min + 1, ...; empty for a number
        float def = -1.0f;  // the program's default, 0..1; negative: none
        int cv = -1;        // the input, 0..CHANNELS-1, that modulates it; negative: none
        float map(float normalised) const;
        float unmap(float value) const;  // clamped to 0..1
        // map() of the knob's position, moved by `volts` of CV and kept within the range
        float modulated(float normalised, float volts) const;
    };
    using Specs = std::array<ParamSpec, PARAMS>;
    static Specs parseSpecs(const std::string& text);
    ParamSpec spec(int i) const;  // not the audio thread
    // changes whenever a load replaces the specs
    unsigned specsGeneration() const { return specsGen_.load(std::memory_order_acquire); }

    // any thread: the control in 0..1
    void setParam(int i, float v) { params_[i].store(v, std::memory_order_relaxed); }
    float param(int i) const { return params_[i].load(std::memory_order_relaxed); }
    // any thread: the control in the program's declared range, moved by its CV
    float value(int i) const;
    // Audio thread, before process(): each control's CV, from the first frame of its input.
    void readCv(const float* const* in);
    // The volts readCv() last read for control i; 0 without a CV input.
    float cvVolts(int i) const { return cvVolts_[i].load(std::memory_order_relaxed); }

    // "p1".."p16"
    static const char* paramName(int i);

    // Reads a text file without a UTF-8 BOM and with LF line ends. False if unreadable.
    static bool readText(const std::string& path, std::string& text);

protected:
    // Worker or message thread: compiles and starts `text`, read from `path` (empty for the built-in),
    // in place of the running program. On failure, returns false with `error` set and leaves the running
    // program alone.
    virtual bool compile(const std::string& text, const std::string& path, std::string& error) = 0;
    // Worker or message thread: a program file's contents. By default readText; a binary format
    // overrides it.
    virtual bool readProgram(const std::string& path, std::string& text) const { return readText(path, text); }
    // After a compile() that succeeded, on its thread: the controls of `text`. By default its @pN lines.
    virtual Specs declared(const std::string& text) const { return parseSpecs(text); }
    // Worker thread, for the queue above: true with a note in `result`, or false with an error. By
    // default the engine runs whole programs only.
    virtual bool runPart(const std::string& part, const std::string& before, const std::string& path,
                         std::string& result);
    virtual bool dropPart(int index, std::string& result);
    virtual bool dropAll(std::string& result);

    float sampleRate() const { return sampleRate_; }
    int maxBlock() const { return maxBlock_; }
    // In a constructor: controls the engine declares itself, before any program compiles. An engine that
    // does also overrides declared(), so a compile keeps them.
    void declare(const Specs& specs) { setSpecs(specs); }

private:
    void setSpecs(const Specs& specs);

    std::atomic<float> params_[PARAMS] = {};
    // the range, for value() on the audio thread; labels and units are under lock_
    std::atomic<float> min_[PARAMS] = {}, max_[PARAMS] = {};
    std::atomic<bool> log_[PARAMS] = {};
    std::atomic<int> cv_[PARAMS] = {};
    std::atomic<float> cvVolts_[PARAMS] = {};
    Specs specs_;
    std::atomic<unsigned> specsGen_{ 0 };

    // a program file, or code given in place of its contents
    struct Source {
        std::string path, code;
        bool hasCode = false;
    };
    void run(const Source& src);
    void post(std::function<bool(std::string&)> job);  // a queued job's result: note or error

    float sampleRate_ = 48000.0f;
    int maxBlock_ = 512;
    mutable std::mutex lock_;  // the sources and error_
    Source pending_, current_;
    std::string error_, note_;
    bool hasPending_ = false;
    std::deque<std::function<bool(std::string&)>> jobs_;
    std::atomic<unsigned> noteSeq_{ 0 };
    bool running_ = false;
};

}  // namespace ssp::engine
