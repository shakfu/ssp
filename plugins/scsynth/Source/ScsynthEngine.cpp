#include "ScsynthEngine.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include "ScsyBuiltin.h"

namespace scsy {

void ScsynthEngine::prepare(float sampleRate, int maxBlock) {
    // On the message thread, so scsynth's NRT thread starts there: on the SSP that is core 0 at
    // normal priority, not an audio core.
    if (!world_.isOpen() || sampleRate != rate_) {
        openError_.clear();
        world_.open(sampleRate, ugenDir_, openError_);
        rate_ = sampleRate;
    }
    capacity_ = std::max(maxBlock, 1);
    for (auto& s : signals_) s.assign(size_t(capacity_), 0.0f);
    pumping_ = true;
    ScriptEngine::prepare(sampleRate, maxBlock);
    pumping_ = false;
}

void ScsynthEngine::process(const float* const* in, float* const* out, int n) {
    // in pieces no larger than prepare()'s block, so the signals need no allocation here
    const float* ip[CHANNELS];
    float* op[CHANNELS];
    for (int pos = 0; pos < n; pos += capacity_) {
        for (int c = 0; c < CHANNELS; c++) {
            ip[c] = in[c] + pos;
            op[c] = out[c] + pos;
        }
        run(ip, op, std::min(capacity_, n - pos));
    }
}

void ScsynthEngine::run(const float* const* in, float* const* out, int n) {
    const float* controls[ScWorld::AUDIO_CONTROLS] = {};
    int bank = bank_.load(std::memory_order_acquire);
    if (bank >= 0) {
        const Ranges& r = ranges_[bank];
        for (int i = 0; i < PARAMS; i++) {
            ParamSpec sp;
            sp.min = r.min[i].load(std::memory_order_relaxed);
            sp.max = r.max[i].load(std::memory_order_relaxed);
            sp.log = r.log[i].load(std::memory_order_relaxed);
            int cv = r.cv[i].load(std::memory_order_relaxed);
            const float knob = param(i);
            if (r.audio[i].load(std::memory_order_relaxed)) {
                float* s = signals_[bank * ScWorld::CONTROLS + i].data();
                for (int f = 0; f < n; f++) s[f] = sp.modulated(knob, in[cv][f] / ssp::engine::CV_PER_VOLT);
                controls[bank * ScWorld::CONTROLS + i] = s;
            } else {
                float volts = cv >= 0 ? in[cv][0] / ssp::engine::CV_PER_VOLT : 0.0f;
                world_.setControl(bank, i, sp.modulated(knob, volts));
            }
        }
    }
    int pending = pending_.load(std::memory_order_acquire);
    if (pending >= 0) {
        const Ranges& r = ranges_[pending];
        for (int i = 0; i < PARAMS; i++) {
            float start = start_[i].load(std::memory_order_relaxed);
            if (r.audio[i].load(std::memory_order_relaxed)) {
                float* s = signals_[pending * ScWorld::CONTROLS + i].data();
                std::fill(s, s + n, start);
                controls[pending * ScWorld::CONTROLS + i] = s;
            } else {
                world_.setControl(pending, i, start);
            }
        }
    }
    world_.process(in, out, n, controls);
}

void ScsynthEngine::note(int note, int velocity) {
    if (velocity > 0) world_.noteOn(note, float(velocity) / 127.0f);
    else world_.noteOff(note);
}

std::string ScsynthEngine::status() const {
    unsigned notes = world_.notesReceived();
    if (notes == 0) return {};
    return "MIDI notes: " + std::to_string(notes) + ", held: " + std::to_string(world_.notesHeld());
}

// a voice def's controls that notes set
static bool noteControl(const std::string& name) {
    return name == "gate" || name == "freq" || name == "velocity";
}

static bool isVoice(const SynthDefInfo& info) {
    return std::any_of(info.controls.begin(), info.controls.end(),
                       [](const SynthDefInfo::Control& c) { return c.name == "gate"; });
}

bool ScsynthEngine::readProgram(const std::string& path, std::string& bytes) const {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool ScsynthEngine::controlSpecs(const SynthDefInfo& info, const std::string& sidecar, Specs& specs,
                                 std::vector<ScWorld::Mapping>& mapped, std::string& error) {
    specs = Specs();
    mapped.assign(PARAMS, ScWorld::Mapping());
    auto find = [&](const std::string& name) {
        return std::find_if(info.controls.begin(), info.controls.end(),
                            [&](const SynthDefInfo::Control& c) { return c.name == name; });
    };
    if (!sidecar.empty()) {
        Specs named = parseSpecs(sidecar);
        for (int i = 0; i < PARAMS; i++) {
            ParamSpec sp = named[size_t(i)];
            if (sp.label.empty()) continue;
            auto c = find(sp.label);
            if (c == info.controls.end()) {
                error = "@p" + std::to_string(i + 1) + " names " + sp.label + ", not a control of " + info.name;
                return false;
            }
            if (isVoice(info) && noteControl(sp.label)) {
                error = "@p" + std::to_string(i + 1) + " names " + sp.label + ", which MIDI notes set";
                return false;
            }
            sp.def = sp.unmap(c->value);
            specs[size_t(i)] = sp;
            mapped[size_t(i)] = { sp.label, sp.cv >= 0 };
        }
        return true;
    }
    const bool voice = isVoice(info);
    size_t i = 0;
    for (const auto& c : info.controls) {
        if (i == size_t(PARAMS)) break;
        if (voice && noteControl(c.name)) continue;
        ParamSpec sp;
        sp.label = c.name;
        float limit = std::max(1.0f, 2.0f * std::fabs(c.value));
        sp.min = c.value < 0.0f ? -limit : 0.0f;
        sp.max = limit;
        sp.def = sp.unmap(c.value);
        specs[i] = sp;
        mapped[i] = { c.name, false };
        i++;
    }
    return true;
}

bool ScsynthEngine::compile(const std::string& text, const std::string& path, std::string& error) {
    if (!world_.isOpen()) {
        error = openError_.empty() ? "scsynth is not running" : openError_;
        return false;
    }
    std::string bytes = path.empty() ? std::string(reinterpret_cast<const char*>(BUILTIN_DEF), sizeof BUILTIN_DEF)
                                     : text;
    SynthDefInfo info;
    if (!readSynthDef(bytes, info, error)) return false;
    std::string sidecar;
    if (path.empty()) {
        sidecar = BUILTIN_SPECS;
    } else {
        size_t dot = path.find_last_of('.'), slash = path.find_last_of('/');
        std::string stem = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? path.substr(0, dot)
                                                                                                  : path;
        readText(stem + ".txt", sidecar);  // none: every control, with ranges from its default
    }
    Specs specs;
    std::vector<ScWorld::Mapping> mapped;
    if (!controlSpecs(info, sidecar, specs, mapped, error)) return false;

    const int bank = world_.nextBank();
    Ranges& r = ranges_[bank];  // not the running synth's bank, so the audio thread does not read it
    for (int i = 0; i < PARAMS; i++) {
        const ParamSpec& sp = specs[size_t(i)];
        r.min[i].store(sp.min, std::memory_order_relaxed);
        r.max[i].store(sp.max, std::memory_order_relaxed);
        r.log[i].store(sp.log, std::memory_order_relaxed);
        r.cv[i].store(sp.cv, std::memory_order_relaxed);
        r.audio[i].store(mapped[size_t(i)].audio, std::memory_order_relaxed);
        float start = sp.def >= 0.0f ? sp.map(sp.def) : sp.min;
        start_[i].store(start, std::memory_order_relaxed);
        if (pumping_) {  // no audio thread to write them
            world_.setControl(bank, i, start);
            world_.setPumpControl(bank, i, start);
        }
    }
    pending_.store(bank, std::memory_order_release);
    bool ok = world_.loadDef(bytes, error, mapped, isVoice(info), pumping_);
    if (ok) bank_.store(bank, std::memory_order_release);
    pending_.store(-1, std::memory_order_release);
    if (!ok) return false;
    specs_ = specs;
    return true;
}

}  // namespace scsy
