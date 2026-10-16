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

// globalCleanup() stops ChucK's process-wide subsystems, so only the last engine may run it
static std::atomic<int> liveEngines{ 0 };

ChuckEngine::ChuckEngine() {
    liveEngines.fetch_add(1);
}

ChuckEngine::~ChuckEngine() {
    gate_.take();
    delete vm_;
    if (liveEngines.fetch_sub(1) == 1) ChucK::globalCleanup();
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
        std::lock_guard<std::mutex> lock(globalsLock_);
        delete vm_;
        vm_ = newVm(sampleRate);
        vmRate_ = sampleRate;
        midiCount_ = 0;
        std::fill(std::begin(applied_), std::end(applied_), std::nanf(""));  // never equal: all sent
    }
    ScriptEngine::prepare(sampleRate, maxBlock);
}

Chuck_VM_Code* ChuckEngine::compileText(const std::string& text, const std::string& path, std::string& error) {
    std::string log;
    compileLog = &log;
    EM_reset_msg();
    Chuck_Compiler* c = vm_->compiler();
    c->m_originHint = ckte_origin_USERDEFINED;
    bool ok = c->compileCode(text, path);  // path: me.dir() and @import resolve next to the file
    c->m_originHint = ckte_origin_UNKNOWN;
    compileLog = nullptr;
    if (ok) return c->output();
    const char* last = EM_lasterror();
    error = last != nullptr && *last ? std::string(last) : log;
    if (error.empty()) error = "compile failed";
    std::string name = path.empty() ? "builtin" : path.substr(path.find_last_of('/') + 1);
    for (size_t at; (at = error.find(CHUCK_CODE_LITERAL_SIGNIFIER)) != std::string::npos;)
        error.replace(at, std::strlen(CHUCK_CODE_LITERAL_SIGNIFIER), name);
    return nullptr;
}

void ChuckEngine::removeAll() {
    // removal happens at the top of the next compute, so run one frame before any new spork
    vm_->removeAllShreds();
    vm_->run(in_.data(), out_.data(), 1);
    clearSources(vm_->vm()->m_dac);
    clearSources(vm_->vm()->m_bunghole);
    std::lock_guard<std::mutex> lock(partsLock_);
    partCount_.store(0);
    live_.store(0);
}

// Compiles with the VM out of the audio path, then swaps the program in: audio is silent for the
// compile. A failed compile keeps the old program's shreds and parts.
bool ChuckEngine::compile(const std::string& text, const std::string& path, std::string& error) {
    if (vm_ == nullptr) {
        error = "ChucK failed to start";
        return false;
    }
    gate_.take();
    Chuck_VM_Code* code = compileText(text, path, error);
    if (code != nullptr) {
        code->name = path.empty() ? "builtin" : path;
        removeAll();
        vm_->vm()->spork(code, nullptr, TRUE);
    }
    gate_.publish(vm_);
    return code != nullptr;
}

// the part's first line of code, for parts()
static std::string label(const std::string& part) {
    size_t at = 0;
    while (at < part.size()) {
        size_t end = part.find('\n', at);
        if (end == std::string::npos) end = part.size();
        size_t c = part.find_first_not_of(" \t", at);
        if (c < end && part.compare(c, 2, "//") != 0) return part.substr(c, std::min<size_t>(end - c, 24));
        at = end + 1;
    }
    return "?";
}

int ChuckEngine::livePart(int index) const {
    const uint32_t live = live_.load();
    const int count = partCount_.load();
    int seen = 0, newest = -1;
    for (int k = 0; k < count; k++) {
        if (!(live >> k & 1)) continue;
        newest = k;
        if (++seen == index) return k;
    }
    return index == 0 ? newest : -1;
}

// A new shred, compiled and sporked as compile() does, beside the running ones
bool ChuckEngine::runPart(const std::string& part, const std::string&, const std::string& path,
                          std::string& result) {
    if (vm_ == nullptr) {
        result = "ChucK failed to start";
        return false;
    }
    gate_.take();
    {
        // forget parts that ended; their slots go to the new one
        std::lock_guard<std::mutex> lock(partsLock_);
        const uint32_t live = live_.load();
        int kept = 0;
        for (int k = 0; k < partCount_.load(); k++)
            if (live >> k & 1) parts_[kept++] = parts_[k];
        partCount_.store(kept);
        live_.store((1u << kept) - 1);
    }
    bool ok = partCount_.load() < MAX_PARTS;
    if (!ok) result = std::to_string(MAX_PARTS) + " shreds running: remove one first";
    Chuck_VM_Code* code = ok ? compileText(part, path, result) : nullptr;
    if (code != nullptr) {
        Part p{ 0, label(part) };
        code->name = p.label;
        p.xid = vm_->vm()->spork(code, nullptr, TRUE)->xid;
        std::lock_guard<std::mutex> lock(partsLock_);
        const int k = partCount_.load();
        parts_[k] = p;
        partCount_.store(k + 1);
        live_.store((1u << (k + 1)) - 1);
        result = "shred " + std::to_string(k + 1) + ": " + p.label;
    }
    gate_.publish(vm_);
    return code != nullptr;
}

// Through the VM's message queue, which the audio thread reads at the top of each compute; only this
// thread writes it. The part leaves parts() when the audio thread finds it gone.
bool ChuckEngine::dropPart(int index, std::string& result) {
    const int k = livePart(index);
    if (vm_ == nullptr || k < 0) {
        result = index == 0 ? "no shreds to remove" : "no shred " + std::to_string(index);
        return false;
    }
    auto* msg = new Chuck_Msg;
    msg->type = CK_MSG_REMOVE;
    msg->param = parts_[k].xid;
    vm_->vm()->queue_msg(msg);
    result = "removed " + parts_[k].label;
    return true;
}

bool ChuckEngine::dropAll(std::string& result) {
    if (vm_ == nullptr) {
        result = "ChucK failed to start";
        return false;
    }
    gate_.take();
    removeAll();
    gate_.publish(vm_);
    result = "cleared every shred; Ctrl-Enter runs the program again";
    return true;
}

std::string ChuckEngine::parts() const {
    std::lock_guard<std::mutex> lock(partsLock_);
    const uint32_t live = live_.load();
    std::string s;
    int n = 0;
    for (int k = 0; k < partCount_.load(); k++)
        if (live >> k & 1) s += (s.empty() ? "shreds  " : "   ") + std::to_string(++n) + " " + parts_[k].label;
    return s;
}

void ChuckEngine::idle() {
    {
        std::lock_guard<std::mutex> lock(globalsLock_);
        if (vm_ != nullptr)
            if (Chuck_Globals_Manager* g = vm_->globals())
                for (int p = 0; p < PARAMS; p++) {
                    float v = value(p);
                    if (v == applied_[p]) continue;
                    applied_[p] = v;
                    g->setGlobalFloat(paramName(p), v);
                }
    }
    ScriptEngine::idle();
}

void ChuckEngine::midi(int note, int velocity) {
    std::lock_guard<std::mutex> lock(globalsLock_);
    Chuck_Globals_Manager* g = vm_ != nullptr ? vm_->globals() : nullptr;
    if (g == nullptr) return;
    if (velocity > 0) notes_.fetch_add(1, std::memory_order_relaxed);
    // a program that declares none of these ignores them
    g->setGlobalIntArrayValue("midiNotes", static_cast<t_CKUINT>(midiCount_ % MIDI_NOTES),
                              (note & 0x7F) << 8 | (velocity & 0x7F));
    g->setGlobalInt("midiCount", ++midiCount_);
    g->broadcastGlobalEvent("midiEvent");
}

std::string ChuckEngine::status() const {
    unsigned n = notes_.load(std::memory_order_relaxed);
    return n ? "MIDI notes: " + std::to_string(n) : std::string();
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
    // which parts still run: lookup walks the live shreds, without allocating
    const int count = partCount_.load();
    uint32_t live = 0;
    for (int k = 0; k < count; k++)
        if (vm->vm()->shreduler()->lookup(parts_[k].xid) != nullptr) live |= 1u << k;
    live_.store(live);
    const float* oo = out_.data();
    for (int f = 0; f < n; f++)
        for (int c = 0; c < CHANNELS; c++) out[c][f] = *oo++;
    gate_.end();
}

}  // namespace ck
