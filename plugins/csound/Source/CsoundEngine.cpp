#include "CsoundEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "csound.h"
#include "csound_rtaudio.h"
#include "csound_rtmidi.h"

namespace csnd {

// A drone, so the plugin sounds with nothing patched; MIDI notes play a plucked voice on any channel.
static const char* BUILTIN = R"csd(<CsoundSynthesizer>
<CsInstruments>
; @p1 pitch 110 880 Hz log
; @p2 level
; @p3 cutoff 1500 12000 Hz log
ksmps = 32
nchnls = 2
0dbfs = 1
massign 0, "Note"

instr Drone
  kfreq chnget "p1"
  klevel chnget "p2"
  kcut chnget "p3"
  asig vco2 0.3, kfreq
  asig tone asig, kcut
  asig *= 0.15 + klevel * 0.85
  outs asig, asig
endin

instr Note
  iamp ampmidi 0.5
  kcut chnget "p3"
  aenv madsr 0.005, 0.3, 0.4, 0.3
  asig vco2 iamp, cpsmidi()
  asig tone asig * aenv, kcut
  outs asig, asig
endin

schedule "Drone", 0, -1
</CsInstruments>
</CsoundSynthesizer>
)csd";

struct CsoundEngine::Instance {
    CsoundEngine* engine = nullptr;
    CSOUND* cs = nullptr;
    std::string* log = nullptr;  // set while compiling, so audio-thread messages are dropped
    MYFLT* spin = nullptr;
    const MYFLT* spout = nullptr;
    int ksmps = 0, ins = 0, outs = 0;
    float scale = 1.0f;  // 0dbfs
    int pos = 0;         // frame within the current k-cycle
    float applied[PARAMS];
};

CsoundEngine::~CsoundEngine() {
    destroy(gate_.take());
}

void CsoundEngine::destroy(Instance* i) {
    if (i == nullptr) return;
    if (i->cs) csoundDestroy(i->cs);
    delete i;
}

const char* CsoundEngine::builtin() const {
    return BUILTIN;
}

void CsoundEngine::message(CSOUND* cs, int32_t, const char* format, va_list args) {
    auto* i = static_cast<Instance*>(csoundGetHostData(cs));
    if (i == nullptr || i->log == nullptr) return;
    char buf[512];
    vsnprintf(buf, sizeof(buf), format, args);
    if (i->log->size() < 4096) *i->log += buf;
}

int32_t CsoundEngine::midiOpen(CSOUND* cs, void** user, const char*) {
    *user = csoundGetHostData(cs);
    return 0;
}

int32_t CsoundEngine::midiClose(CSOUND*, void*) {
    return 0;
}

// performance thread: drains the host's queue
int32_t CsoundEngine::midiRead(CSOUND*, void* user, unsigned char* buf, int32_t nBytes) {
    auto* i = static_cast<Instance*>(user);
    int32_t n = 0;
    Midi m;
    while (n + 3 <= nBytes && i->engine->midi_.try_dequeue(m)) {
        std::copy(m.b, m.b + 3, buf + n);
        n += 3;
    }
    return n;
}

std::string CsoundEngine::status() const {
    unsigned n = notes_.load(std::memory_order_relaxed);
    return n ? "MIDI notes: " + std::to_string(n) : std::string();
}

void CsoundEngine::midi(uint8_t status, uint8_t d1, uint8_t d2) {
    if ((status & 0xF0) == 0x90 && d2 > 0) notes_.fetch_add(1, std::memory_order_relaxed);
    midi_.try_enqueue(Midi{ { status, d1, d2 } });
}

// Csound's error lines, without the score and orchestra listing noise
static std::string lastLines(const std::string& log) {
    std::string out;
    size_t start = 0;
    while (start < log.size()) {
        size_t end = log.find('\n', start);
        if (end == std::string::npos) end = log.size();
        std::string line = log.substr(start, end - start);
        if (line.find("rror") != std::string::npos || line.find("line") != std::string::npos) {
            if (!out.empty()) out += "\n";
            out += line;
        }
        start = end + 1;
    }
    return out.empty() ? log.substr(0, 200) : out.substr(0, 400);
}

bool CsoundEngine::compile(const std::string& text, const std::string& path, std::string& error) {
    size_t tag = text.find("<CsoundSynthesizer");
    if (tag == std::string::npos) {
        error = "not a .csd: no <CsoundSynthesizer>";
        return false;
    }
    // the host owns signals and exit
    static const int init = csoundInitialize(CSOUNDINIT_NO_SIGNAL_HANDLER | CSOUNDINIT_NO_ATEXIT);
    (void)init;
    std::string log;
    auto* i = new Instance;
    i->engine = this;
    i->log = &log;
    i->cs = csoundCreate(i, nullptr);
    if (i->cs == nullptr) {
        delete i;
        error = "csoundCreate failed";
        return false;
    }
    csoundSetMessageCallback(i->cs, message);
    csoundSetHostAudioIO(i->cs);
    csoundSetHostMIDIIO(i->cs);
    csoundSetExternalMidiInOpenCallback(i->cs, midiOpen);
    csoundSetExternalMidiReadCallback(i->cs, midiRead);
    csoundSetExternalMidiInCloseCallback(i->cs, midiClose);
    char sr[48];
    std::snprintf(sr, sizeof(sr), "--sample-rate=%d", int(sampleRate()));
    for (const char* opt : { "-n", "-d", "-m0", "-M0", static_cast<const char*>(sr) }) csoundSetOption(i->cs, opt);
    // relative sound files and #includes resolve next to the .csd
    std::string dir = path.substr(0, path.find_last_of('/') + 1);
    if (!dir.empty())
        for (const char* var : { "SSDIR", "SADIR", "INCDIR" }) csoundSetOption(i->cs, ("--env:" + std::string(var) + "=" + dir).c_str());

    int rc = csoundCompileCSD(i->cs, text.c_str() + tag, 1, 0);
    if (rc == 0) rc = csoundStart(i->cs);
    if (rc != 0) {
        error = lastLines(log);
        if (error.empty()) error = "compile failed";
        destroy(i);
        return false;
    }
    i->log = nullptr;
    i->spin = csoundGetSpin(i->cs);
    i->spout = csoundGetSpout(i->cs);
    i->ksmps = int(csoundGetKsmps(i->cs));
    i->ins = int(csoundGetChannels(i->cs, 1));
    i->outs = int(csoundGetChannels(i->cs, 0));
    i->scale = float(csoundGet0dBFS(i->cs));
    i->pos = i->ksmps;  // perform before the first frame
    std::fill(std::begin(i->applied), std::end(i->applied), std::nanf(""));  // never equal: all sent

    Instance* old = gate_.take();
    gate_.publish(i);
    destroy(old);
    return true;
}

void CsoundEngine::process(const float* const* in, float* const* out, int n) {
    Instance* i = gate_.begin();
    if (i == nullptr) {
        for (int c = 0; c < CHANNELS; c++) std::fill(out[c], out[c] + n, 0.0f);
        gate_.end();
        return;
    }
    for (int p = 0; p < PARAMS; p++) {
        float v = value(p);
        if (v == i->applied[p]) continue;
        i->applied[p] = v;
        csoundSetControlChannel(i->cs, paramName(p), MYFLT(v));
    }
    int ins = std::min(i->ins, int(CHANNELS)), outs = std::min(i->outs, int(CHANNELS));
    float inScale = i->scale, outScale = 1.0f / i->scale;
    for (int f = 0; f < n; f++) {
        if (i->pos == i->ksmps) {
            csoundPerformKsmps(i->cs);
            i->pos = 0;
        }
        MYFLT* si = i->spin + i->pos * i->ins;
        const MYFLT* so = i->spout + i->pos * i->outs;
        for (int c = 0; c < ins; c++) si[c] = MYFLT(in[c][f] * inScale);
        for (int c = 0; c < outs; c++) out[c][f] = float(so[c]) * outScale;
        i->pos++;
    }
    for (int c = outs; c < CHANNELS; c++) std::fill(out[c], out[c] + n, 0.0f);
    gate_.end();
}

}  // namespace csnd
