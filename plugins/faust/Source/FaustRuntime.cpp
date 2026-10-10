#include "FaustRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <vector>

#include "faust/dsp/llvm-dsp.h"
#include "faust/gui/UI.h"

namespace fstr {

using ssp::engine::ScriptEngine;

// A drone, so the module sounds with nothing patched. Needs no libraries, and sounds with every
// control at 0, as a new module starts.
static const char* BUILTIN = R"dsp(declare name "built-in";
SR = fconstant(int fSamplingFreq, <math.h>);
pitch = hslider("[0] pitch [scale:log][unit:Hz]", 110, 110, 880, 0.01);
level = hslider("[1] level", 0, 0, 1, 0.01);
cutoff = hslider("[2] cutoff [scale:log][unit:Hz]", 1500, 1500, 12000, 1);
saw = (+(pitch / SR) : \(x).(x - floor(x))) ~ _ : *(2) : -(1);
lowpass(c) = *(1 - p) : + ~ *(p) with { p = exp(-6.283185307179586 * c / SR); };
process = saw : lowpass(cutoff) : *(0.3 * (0.15 + 0.85 * level)) <: _, _;
)dsp";

// libfaust's compiler keeps global state; each fstr compiles on its own worker thread
static std::mutex& compilerLock() {
    static std::mutex m;
    return m;
}

// LLVM 9 names the SSP's CPU "generic": ARMv4 soft-float code, whose calls into hard-float libm
// lose their results (docs/dev/faust-jit-tan.md). "" is the machine it runs on.
static const std::string& jitTarget() {
#if defined(__arm__)
    static const std::string t = [] {
        std::string m = getDSPMachineTarget();
        return m.substr(0, m.find(':')) + ":cortex-a17";
    }();
#else
    static const std::string t;
#endif
    return t;
}

struct FaustRuntime::Instance {
    llvm_dsp_factory* factory = nullptr;
    ::dsp* dsp = nullptr;
    FAUSTFLOAT* zones[PARAMS] = {};
    ScriptEngine::Specs specs;  // this program's ranges, so a swap never pairs it with another's
    int ins = 0, outs = 0;
    std::vector<float> zeros, scratch;  // the program's channels past CHANNELS
    std::vector<FAUSTFLOAT*> ip, op;
};

// The first PARAMS controls, in the program's order, with their metadata
class Collector : public UI {
public:
    Collector(ScriptEngine::Specs& specs, FAUSTFLOAT** zones) : specs_(specs), zones_(zones) {}

    void openTabBox(const char*) override {}
    void openHorizontalBox(const char*) override {}
    void openVerticalBox(const char*) override {}
    void closeBox() override {}
    void addButton(const char* l, FAUSTFLOAT* z) override { add(l, z, 0, 0, 1); }
    void addCheckButton(const char* l, FAUSTFLOAT* z) override { add(l, z, 0, 0, 1); }
    void addVerticalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT lo, FAUSTFLOAT hi, FAUSTFLOAT) override {
        add(l, z, i, lo, hi);
    }
    void addHorizontalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT lo, FAUSTFLOAT hi,
                             FAUSTFLOAT) override {
        add(l, z, i, lo, hi);
    }
    void addNumEntry(const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT lo, FAUSTFLOAT hi, FAUSTFLOAT) override {
        add(l, z, i, lo, hi);
    }
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}
    // metadata arrives before its control
    void declare(FAUSTFLOAT*, const char* key, const char* value) override {
        std::string k = key, v = value;
        if (k == "unit") unit_ = v;
        if (k == "scale") log_ = v == "log";
        if (k == "cv") {  // [cv:3]: input 3 as CV
            int input = std::atoi(v.c_str());
            cv_ = input >= 1 && input <= ScriptEngine::CHANNELS ? input - 1 : -1;
        }
    }

private:
    void add(const char* label, FAUSTFLOAT* zone, float init, float lo, float hi) {
        if (n_ < ScriptEngine::PARAMS && hi != lo) {
            ScriptEngine::ParamSpec sp;
            sp.label = *label ? label : ScriptEngine::paramName(n_);
            sp.unit = unit_;
            sp.min = lo;
            sp.max = hi;
            sp.log = log_ && lo > 0.0f && hi > 0.0f;
            sp.def = sp.unmap(init);
            sp.cv = cv_;
            specs_[size_t(n_)] = sp;
            zones_[n_++] = zone;
        }
        unit_.clear();
        log_ = false;
        cv_ = -1;
    }

    ScriptEngine::Specs& specs_;
    FAUSTFLOAT** zones_;
    int n_ = 0;
    std::string unit_;
    bool log_ = false;
    int cv_ = -1;
};

FaustRuntime::~FaustRuntime() {
    destroy(gate_.take());
}

void FaustRuntime::destroy(Instance* i) {
    if (i == nullptr) return;
    std::lock_guard<std::mutex> lock(compilerLock());
    delete i->dsp;
    if (i->factory) deleteDSPFactory(i->factory);
    delete i;
}

std::string FaustRuntime::status() const {
    unsigned n = resets();
    return n ? "non-finite output: " + std::to_string(n) + " blocks silenced" : std::string();
}

const char* FaustRuntime::builtin() const {
    return BUILTIN;
}

bool FaustRuntime::compile(const std::string& text, const std::string& path, std::string& error) {
    // imports resolve next to the program, then in the libraries
    std::string dir = path.substr(0, path.find_last_of('/') + 1);
    std::vector<const char*> args;
    if (!dir.empty()) args.insert(args.end(), { "-I", dir.c_str() });
    args.insert(args.end(), { "-I", libraries_.c_str() });

    auto* i = new Instance;
    {
        std::lock_guard<std::mutex> lock(compilerLock());
        // -1: LLVM's highest optimisation level
        i->factory = createDSPFactoryFromString(path.empty() ? "built-in" : path, text, int(args.size()), args.data(),
                                                jitTarget(), error, -1);
        if (i->factory) i->dsp = i->factory->createDSPInstance();
    }
    if (i->dsp == nullptr) {
        if (error.empty()) error = "compile failed";
        error = error.substr(0, 400);
        destroy(i);
        return false;
    }
    i->dsp->init(int(sampleRate()));
    Collector c(i->specs, i->zones);
    i->dsp->buildUserInterface(&c);
    i->ins = i->dsp->getNumInputs();
    i->outs = i->dsp->getNumOutputs();
    i->zeros.assign(size_t(maxBlock()), 0.0f);
    i->scratch.assign(size_t(maxBlock()), 0.0f);
    i->ip.assign(size_t(i->ins), i->zeros.data());
    i->op.assign(size_t(i->outs), i->scratch.data());

    specs_ = i->specs;
    Instance* old = gate_.take();
    gate_.publish(i);
    destroy(old);
    resets_.store(0, std::memory_order_relaxed);
    return true;
}

void FaustRuntime::process(const float* const* in, float* const* out, int n) {
    Instance* i = gate_.begin();
    if (i == nullptr) {
        for (int c = 0; c < CHANNELS; c++) std::fill(out[c], out[c] + n, 0.0f);
        gate_.end();
        return;
    }
    for (int p = 0; p < PARAMS; p++) {
        if (!i->zones[p]) continue;
        const ParamSpec& sp = i->specs[size_t(p)];
        *i->zones[p] = sp.modulated(param(p), sp.cv >= 0 ? in[sp.cv][0] / ssp::engine::CV_PER_VOLT : 0.0f);
    }
    int ins = std::min(i->ins, int(CHANNELS)), outs = std::min(i->outs, int(CHANNELS));
    // compute() does not write its inputs
    for (int c = 0; c < ins; c++) i->ip[size_t(c)] = const_cast<float*>(in[c]);
    for (int c = 0; c < outs; c++) i->op[size_t(c)] = out[c];
    i->dsp->compute(n, i->ip.data(), i->op.data());
    // a non-finite output means non-finite state, which a recursive program keeps forever: clear it
    bool finite = true;
    for (int c = 0; c < outs; c++)
        for (int f = 0; f < n; f++) finite = finite && std::isfinite(out[c][f]);
    if (!finite) {
        for (int c = 0; c < outs; c++) std::fill(out[c], out[c] + n, 0.0f);
        i->dsp->instanceClear();
        resets_.fetch_add(1, std::memory_order_relaxed);
    }
    for (int c = outs; c < CHANNELS; c++) std::fill(out[c], out[c] + n, 0.0f);
    gate_.end();
}

}  // namespace fstr
