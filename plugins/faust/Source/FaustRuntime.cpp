#include "FaustRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <vector>

#include "faust/dsp/llvm-dsp.h"
#include "faust/dsp/poly-dsp.h"
#include "faust/gui/UI.h"

// GUI.h's statics, which a program using it defines once: mydsp_poly's grouped controls are a GUI
std::list<GUI*> GUI::fGuiList;
ztimedmap GUI::gTimedZoneMap;

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
    ::dsp* dsp = nullptr;      // the program, or poly
    dsp_poly* poly = nullptr;  // its voices, when it declares [nvoices:N]
    std::vector<FAUSTFLOAT*> zones[PARAMS];  // each control's zone, or its zone in every voice
    ScriptEngine::Specs specs;  // this program's ranges, so a swap never pairs it with another's
    int ins = 0, outs = 0;
    std::vector<float> zeros, scratch;  // the program's channels past CHANNELS
    std::vector<FAUSTFLOAT*> ip, op;
};

// Faust's polyphonic convention: notes set these, in every voice
static bool noteControl(const std::string& label) {
    return label == "freq" || label == "gain" || label == "gate" || label == "key" || label == "vel" ||
           label == "velocity";
}

// The first PARAMS controls, in the program's order, with their metadata. In a polyphonic program
// (`voices` > 0) each voice repeats the controls: they join by their path in the voice, without
// the note controls; with several voices only those inside a "VoiceN" or "VN" box count, which
// leaves out mydsp_poly's grouped copy and its Panic button.
class Collector : public UI {
public:
    Collector(ScriptEngine::Specs& specs, std::vector<FAUSTFLOAT*>* zones, int voices)
        : specs_(specs), zones_(zones), voices_(voices) {}

    void openTabBox(const char* l) override { path_.push_back(l); }
    void openHorizontalBox(const char* l) override { path_.push_back(l); }
    void openVerticalBox(const char* l) override { path_.push_back(l); }
    void closeBox() override {
        if (!path_.empty()) path_.pop_back();
    }
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
    // the index in path_ after a voice's box, or -1 outside one
    int inVoice() const {
        for (size_t k = 0; k < path_.size(); k++) {
            const std::string& b = path_[k];
            size_t digits = b.rfind("Voice", 0) == 0 ? 5 : b.rfind('V', 0) == 0 ? 1 : 0;
            if (digits && b.size() > digits && b.find_first_not_of("0123456789", digits) == std::string::npos)
                return int(k + 1);
        }
        return -1;
    }

    void add(const char* label, FAUSTFLOAT* zone, float init, float lo, float hi) {
        std::string key;
        bool take = hi != lo;
        if (voices_ > 0) {
            int from = voices_ > 1 ? inVoice() : 0;
            take = take && from >= 0 && !noteControl(label);
            for (size_t k = size_t(std::max(from, 0)); k < path_.size(); k++) key += path_[k] + "/";
            key += label;
            for (int p = 0; take && p < n_; p++)
                if (keys_[size_t(p)] == key) {  // the same control in a later voice
                    zones_[p].push_back(zone);
                    take = false;
                }
        }
        if (take && n_ < ScriptEngine::PARAMS) {
            ScriptEngine::ParamSpec sp;
            sp.label = *label ? label : ScriptEngine::paramName(n_);
            sp.unit = unit_;
            sp.min = lo;
            sp.max = hi;
            sp.log = log_ && lo > 0.0f && hi > 0.0f;
            sp.def = sp.unmap(init);
            sp.cv = cv_;
            specs_[size_t(n_)] = sp;
            keys_[size_t(n_)] = key;
            zones_[n_++].push_back(zone);
        }
        unit_.clear();
        log_ = false;
        cv_ = -1;
    }

    ScriptEngine::Specs& specs_;
    std::vector<FAUSTFLOAT*>* zones_;
    const int voices_;
    std::vector<std::string> path_;
    std::string keys_[ScriptEngine::PARAMS];
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
    std::string s;
    if (unsigned n = resets()) s = "non-finite output: " + std::to_string(n) + " blocks silenced";
    if (unsigned n = notesReceived_.load(std::memory_order_relaxed))
        s += (s.empty() ? "" : "\n") + ("MIDI notes: " + std::to_string(n));
    return s;
}

void FaustRuntime::midi(int note, int velocity) {
    if (velocity > 0) notesReceived_.fetch_add(1, std::memory_order_relaxed);
    notes_.try_enqueue(Note{ uint8_t(note & 0x7F), uint8_t(velocity & 0x7F) });
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
    // [nvoices:N] in its options: polyphonic, its voices started by notes
    int voices = 0;
    if (i->dsp) {
        bool midi = false, sync = false;
        MidiMeta::analyse(i->dsp, midi, sync, voices);
    }
    if (voices > 0) {
        std::lock_guard<std::mutex> lock(compilerLock());  // a GUI joins a process-wide list
        i->poly = new mydsp_poly(i->dsp, voices, true, false);
        i->dsp = i->poly;
    }
    if (i->dsp == nullptr) {
        if (error.empty()) error = "compile failed";
        error = error.substr(0, 400);
        destroy(i);
        return false;
    }
    i->dsp->init(int(sampleRate()));
    Collector c(i->specs, i->zones, voices);
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
    for (Note m; notes_.try_dequeue(m);) {
        if (!i->poly) continue;
        if (m.velocity > 0) i->poly->keyOn(0, m.note, m.velocity);
        else i->poly->keyOff(0, m.note, 0);
    }
    for (int p = 0; p < PARAMS; p++) {
        if (i->zones[p].empty()) continue;
        const ParamSpec& sp = i->specs[size_t(p)];
        float v = sp.modulated(param(p), sp.cv >= 0 ? in[sp.cv][0] / ssp::engine::CV_PER_VOLT : 0.0f);
        for (FAUSTFLOAT* z : i->zones[p]) *z = v;
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
