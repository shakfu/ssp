#pragma once

#include <atomic>
#include <string>

#include "engine/ReloadGate.h"
#include "engine/ScriptEngine.h"

namespace fstr {

// Runs a Faust program (.dsp), compiled to machine code by libfaust's LLVM backend. Its first 16
// controls become p1..p16, with the program's labels, ranges, defaults, units and [scale:log]; its
// inputs and outputs are channels 1..8.
class FaustRuntime : public ssp::engine::ScriptEngine {
public:
    // libraries: the Faust libraries folder (stdfaust.lib), searched after the program's own folder
    explicit FaustRuntime(std::string libraries) : libraries_(std::move(libraries)) {}
    ~FaustRuntime() override;

    void process(const float* const* in, float* const* out, int n) override;
    // the blocks silenced for non-finite output since the program loaded, or empty
    std::string status() const override;
    unsigned resets() const { return resets_.load(std::memory_order_relaxed); }

protected:
    bool compile(const std::string& text, const std::string& path, std::string& error) override;
    const char* builtin() const override;
    Specs declared(const std::string&) const override { return specs_; }

private:
    struct Instance;
    static void destroy(Instance* i);

    const std::string libraries_;
    Specs specs_;  // the last program compiled; compile() and declared() share a thread
    ssp::engine::ReloadGate<Instance> gate_;
    std::atomic<unsigned> resets_{ 0 };
};

}  // namespace fstr
