#pragma once

#include "Session.h"
#include "engine/ScriptEngine.h"

namespace sapf {

// sapf as a ScriptEngine, on a Session. A run (Ctrl-Enter, Load, a preset) stops every sound, then
// evaluates the whole text; a part (Ctrl-E) evaluates beside what plays. Definitions persist across both.
// sapf reports errors after compile() returns, in output(), not error().
class SapfEngine : public ssp::engine::ScriptEngine {
public:
    explicit SapfEngine(Session::Paths paths) : session_(std::move(paths)) {}

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override { session_.process(in, out, n); }

    bool canReset() const override { return true; }
    std::string status() const override;
    std::string runState() const override;
    bool output(std::vector<OutputLine>& lines, size_t max) const override;
    std::vector<std::string> words() const override;

    const Session& session() const { return session_; }

protected:
    bool compile(const std::string& text, const std::string& path, std::string& error) override;
    const char* builtin() const override;
    bool runPart(const std::string& part, const std::string& before, const std::string& path,
                 std::string& result) override;
    bool dropAll(std::string& result) override;  // Ctrl-.: interrupts and stops

private:
    std::string load() const;  // the render thread's share of real time, and the sound missed

    Session session_;
};

}  // namespace sapf
