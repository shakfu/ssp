#include "ChuckEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "chuck.h"
#include "chuck_compile.h"
#include "chuck_errmsg.h"
#include "chuck_ugen.h"
#include "chuck_globals.h"
#include "chuck_vm.h"

namespace ck {

// a drone on outputs 1 and 2, so the plugin sounds unpatched
static const char* BUILTIN = R"ck(// @p1 pitch 110 880 Hz log
// @p2 level
// @p3 cutoff 1500 12000 Hz log
global float p1, p2, p3;
SawOsc s => LPF f => dac.chan(0);
f => dac.chan(1);
while (true) {
    Math.max(p1, 20) => s.freq;   // globals are 0 until the plugin sets them
    Math.max(p3, 20) => f.freq;
    (0.15 + p2 * 0.85) * 0.3 => s.gain;
    10::ms => now;
}
)ck";

// ChucK prints compile errors through one process-wide callback; a compile collects them here.
static thread_local std::string* compileLog = nullptr;

static void toLog(const char* s) {
    if (compileLog != nullptr && compileLog->size() < 4096) *compileLog += s;
}

static void discard(const char*) {}

// A program's links between the VM's own UGens (adc => dac, adc => blackhole) outlive its shreds;
// links from the shreds' UGens go with them.
static void clearSources(Chuck_UGen* u) {
    if (u == nullptr) return;
    while (u->m_num_src > 0)
        if (!u->remove(u->m_src_list[0])) break;
    for (t_CKUINT c = 0; c < u->m_multi_chan_size; c++)
        if (u->m_multi_chan[c] != u) clearSources(u->m_multi_chan[c]);
}

static ChucK* newVm(float sampleRate) {
    static const bool redirected = ChucK::setStderrCallback(toLog) && ChucK::setStdoutCallback(discard);
    (void)redirected;
    auto* vm = new ChucK();
    vm->setParam(CHUCK_PARAM_SAMPLE_RATE, t_CKINT(sampleRate));
    vm->setParam(CHUCK_PARAM_INPUT_CHANNELS, t_CKINT(ssp::engine::ScriptEngine::CHANNELS));
    vm->setParam(CHUCK_PARAM_OUTPUT_CHANNELS, t_CKINT(ssp::engine::ScriptEngine::CHANNELS));
    vm->setParam(CHUCK_PARAM_VM_HALT, t_CKINT(FALSE));  // keep running between programs
    vm->setParam(CHUCK_PARAM_OTF_ENABLE, t_CKINT(FALSE));
    vm->setParam(CHUCK_PARAM_CHUGIN_ENABLE, t_CKINT(FALSE));
    vm->setParam(CHUCK_PARAM_IS_REALTIME_AUDIO_HINT, t_CKINT(TRUE));
    vm->setParam(CHUCK_PARAM_TTY_COLOR, t_CKINT(FALSE));
    vm->setChoutCallback(discard);  // the program's <<< >>> and chout: no console on the SSP
    vm->setCherrCallback(discard);
    if (!vm->init() || !vm->start()) {
        delete vm;
        return nullptr;
    }
    return vm;
}

ChuckEngine::~ChuckEngine() {
    gate_.take();
    delete vm_;
}

const char* ChuckEngine::builtin() const {
    return BUILTIN;
}

void ChuckEngine::prepare(float sampleRate, int maxBlock) {
    in_.assign(size_t(maxBlock * CHANNELS), 0.0f);
    out_.assign(size_t(maxBlock * CHANNELS), 0.0f);
    if (vm_ == nullptr || sampleRate != vmRate_) {
        // ChucK keeps part of a deleted VM's type system, so a VM is replaced only on a rate change
        gate_.take();
        delete vm_;
        vm_ = newVm(sampleRate);
        vmRate_ = sampleRate;
        std::fill(std::begin(applied_), std::end(applied_), std::nanf(""));  // never equal: all sent
    }
    ScriptEngine::prepare(sampleRate, maxBlock);
}

// Compiles with the VM out of the audio path, then swaps the program in: audio is silent for the
// compile. A failed compile keeps the old program's shreds.
bool ChuckEngine::compile(const std::string& text, const std::string& path, std::string& error) {
    if (vm_ == nullptr) {
        error = "ChucK failed to start";
        return false;
    }
    gate_.take();
    std::string log;
    compileLog = &log;
    EM_reset_msg();
    Chuck_Compiler* c = vm_->compiler();
    c->m_originHint = ckte_origin_USERDEFINED;
    bool ok = c->compileCode(text, path);  // path: me.dir() and @import resolve next to the file
    c->m_originHint = ckte_origin_UNKNOWN;
    compileLog = nullptr;
    if (ok) {
        Chuck_VM_Code* code = c->output();
        code->name = path.empty() ? "builtin" : path;
        // removal happens at the top of the next compute, so run one frame before the new spork
        vm_->removeAllShreds();
        vm_->run(in_.data(), out_.data(), 1);
        clearSources(vm_->vm()->m_dac);
        clearSources(vm_->vm()->m_bunghole);
        vm_->vm()->spork(code, nullptr, TRUE);
    } else {
        const char* last = EM_lasterror();
        error = last != nullptr && *last ? std::string(last) : log;
        if (error.empty()) error = "compile failed";
        std::string name = path.empty() ? "builtin" : path.substr(path.find_last_of('/') + 1);
        for (size_t at; (at = error.find(CHUCK_CODE_LITERAL_SIGNIFIER)) != std::string::npos;)
            error.replace(at, std::strlen(CHUCK_CODE_LITERAL_SIGNIFIER), name);
    }
    gate_.publish(vm_);
    return ok;
}

void ChuckEngine::idle() {
    if (vm_ != nullptr)
        if (Chuck_Globals_Manager* g = vm_->globals())
            for (int p = 0; p < PARAMS; p++) {
                float v = value(p);
                if (v == applied_[p]) continue;
                applied_[p] = v;
                g->setGlobalFloat(paramName(p), v);
            }
    ScriptEngine::idle();
}

void ChuckEngine::process(const float* const* in, float* const* out, int n) {
    ChucK* vm = gate_.begin();
    if (vm == nullptr) {
        for (int c = 0; c < CHANNELS; c++) std::fill(out[c], out[c] + n, 0.0f);
        gate_.end();
        return;
    }
    float* ii = in_.data();
    for (int f = 0; f < n; f++)
        for (int c = 0; c < CHANNELS; c++) *ii++ = in[c][f];
    vm->run(in_.data(), out_.data(), n);
    const float* oo = out_.data();
    for (int f = 0; f < n; f++)
        for (int c = 0; c < CHANNELS; c++) out[c][f] = *oo++;
    gate_.end();
}

}  // namespace ck
