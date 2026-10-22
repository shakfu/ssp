#pragma once

#include "Session.h"
#include "engine/ScriptEngine.h"

namespace sapf {

// sapf as a ScriptEngine, on a Session. A run (Ctrl-Enter, Load, a preset) stops every sound, then
// evaluates the whole text; a part (Ctrl-E) evaluates beside what plays. Definitions persist across both.
// sapf reports errors after compile() returns, in output(), not error().
//
// p1 and p2 are the position sapf's mouse words read (mousex, xmousey, ...), each 0 to 1: the SSP has no
// mouse. The other controls do nothing; `@pN` comments are ignored.
class SapfEngine : public ssp::engine::ScriptEngine {
public:
    enum Control { MOUSE_X, MOUSE_Y };
    static Specs controls();

    explicit SapfEngine(Session::Paths paths) : session_(std::move(paths)) { declare(controls()); }

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;

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
    Specs declared(const std::string&) const override { return controls(); }

private:
    std::string load() const;  // the render thread's share of real time, and the sound missed

    Session session_;
};

}  // namespace sapf
