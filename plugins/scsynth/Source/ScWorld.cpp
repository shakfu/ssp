// SuperCollider's scsynth headers and sources are GPL-3.0; see build/deps/src/SuperCollider-*/COPYING.

#include "ScWorld.h"

#include "ScsyDef.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

#include "SC_CoreAudio.h"
#include "SC_HiddenWorld.h"
#include "SC_Prototypes.h"
#include "SC_ReplyImpl.hpp"
#include "SC_Time.hpp"
#include "SC_WorldOptions.h"

// ---------------------------------------------------------------------------------------------------
// The functions libscsynth expects from its audio driver's source file.

int32 server_timeseed() { return timeSeed(); }
int64 oscTimeNow() { return OSCTime(getTime()); }
void initializeScheduler() {}
void sc_SetDenormalFlags();

namespace {

// Runs when the host calls, not on a thread of its own. DriverStart starts nothing.
class SC_SSPDriver : public SC_AudioDriver {
public:
    explicit SC_SSPDriver(World* world): SC_AudioDriver(world) {}

    // before and after the blocks of one host callback: as SC_PortAudio.cpp's callback
    void begin() {
        mFromEngine.Free();
        mToEngine.Perform();
        mOscPacketsToEngine.Perform();
    }
    void end() { mAudioSync.Signal(); }

    // one block of mBufLength frames; `controls`, if any, go to the `count` buses from `firstBus`
    void block(const float* const* in, float* const* out, const float* const* controls, int firstBus, int count) {
        World* w = mWorld;
        const int len = w->mBufLength;
        const int32 counter = w->mBufCounter;
        float* inBus = w->mAudioBus + w->mNumOutputs * len;
        for (uint32 c = 0; c < w->mNumInputs; c++) {
            std::memcpy(inBus + c * len, in[c], len * sizeof(float));
            w->mAudioBusTouched[w->mNumOutputs + c] = counter;
        }
        for (int k = 0; controls && k < count; k++) {
            if (!controls[k]) continue;
            std::memcpy(w->mAudioBus + (firstBus + k) * len, controls[k], len * sizeof(float));
            w->mAudioBusTouched[firstBus + k] = counter;  // an audio-rate control reads only a fresh bus
        }
        const int64 next = mOSCbuftime + mOSCincrement;
        int64 when;
        while ((when = mScheduler.NextTime()) <= next) {
            float at = float(when - mOSCbuftime) * float(mOSCtoSamples) + 0.5f;
            float floorAt = std::floor(at);
            w->mSampleOffset = std::min(std::max(int(floorAt), 0), len - 1);
            w->mSubsampleOffset = at - floorAt;
            SC_ScheduledEvent event = mScheduler.Remove();
            event.Perform();
        }
        w->mSampleOffset = 0;
        w->mSubsampleOffset = 0.0f;

        World_Run(w);

        for (uint32 c = 0; c < w->mNumOutputs; c++) {
            if (w->mAudioBusTouched[c] == counter)
                std::memcpy(out[c], w->mAudioBus + c * len, len * sizeof(float));
            else
                std::memset(out[c], 0, len * sizeof(float));
        }
        mOSCbuftime = next;
        w->mBufCounter++;
    }

protected:
    bool DriverSetup(int* outNumSamples, double* outSampleRate) override {
        *outNumSamples = int(mPreferredHardwareBufferFrameSize);
        *outSampleRate = mPreferredSampleRate;
        return mPreferredSampleRate > 0;
    }
    bool DriverStart() override {
        mOSCbuftime = oscTimeNow();
        return true;
    }
    bool DriverStop() override { return true; }
};

// scprintf output of every World in the process. scsynth reports a SynthDef it cannot build only
// here, not in a reply.
std::mutex gLogLock;
std::string gLog;
constexpr size_t LOG_MAX = 8192;

int logPrint(const char* fmt, va_list args) {
    char line[512];
    int n = std::vsnprintf(line, sizeof line, fmt, args);
    std::lock_guard<std::mutex> lock(gLogLock);
    gLog += line;
    if (gLog.size() > LOG_MAX)
        gLog.erase(0, gLog.size() - LOG_MAX / 2);
    return n;
}

// --- OSC, big-endian, padded to 4 bytes

void putInt(std::string& b, int32_t v) {
    for (int s = 24; s >= 0; s -= 8)
        b += char((uint32_t(v) >> s) & 0xff);
}
void putStr(std::string& b, const std::string& s) {
    b += s;
    b.append(4 - s.size() % 4, '\0');
}

class Osc {
public:
    explicit Osc(const char* address): address_(address) {}
    Osc& i(int32_t v) { return tag('i'), putInt(args_, v), *this; }
    Osc& f(float v) {
        int32_t bits;
        std::memcpy(&bits, &v, 4);
        return tag('f'), putInt(args_, bits), *this;
    }
    Osc& s(const std::string& v) { return tag('s'), putStr(args_, v), *this; }
    Osc& b(const std::string& v) {
        tag('b'), putInt(args_, int32_t(v.size())), args_ += v;
        args_.append((4 - v.size() % 4) % 4, '\0');
        return *this;
    }
    std::string bytes() const {
        std::string out;
        putStr(out, address_);
        putStr(out, tags_);
        return out + args_;
    }

private:
    void tag(char t) { tags_ += t; }
    std::string address_, tags_ = ",", args_;
};

// a reply's address, and its string and int arguments as text
bool parseReply(const char* d, int size, std::string& address, std::vector<std::string>& args) {
    int pos = 0;
    auto str = [&](std::string& out) {
        int end = pos;
        while (end < size && d[end])
            end++;
        if (end >= size)
            return false;
        out.assign(d + pos, end - pos);
        pos = (end + 4) & ~3;
        return true;
    };
    std::string tags;
    if (!str(address) || !str(tags) || tags.empty() || tags[0] != ',')
        return false;
    for (size_t t = 1; t < tags.size(); t++) {
        if (tags[t] == 's') {
            args.emplace_back();
            if (!str(args.back()))
                return false;
        } else if (tags[t] == 'i' && pos + 4 <= size) {
            const auto* u = reinterpret_cast<const uint8_t*>(d + pos);
            args.push_back(std::to_string(int32_t(u[0] << 24 | u[1] << 16 | u[2] << 8 | u[3])));
            pos += 4;
        } else {
            return true;  // scsynth's replies need nothing past this
        }
    }
    return true;
}

// messages performed together, before the next block
std::string bundle(const std::vector<std::string>& messages) {
    std::string b("#bundle\0\0\0\0\0\0\0\0\1", 16);  // timetag 1: now
    for (const auto& m : messages) {
        putInt(b, int32_t(m.size()));
        b += m;
    }
    return b;
}

std::atomic<unsigned> gDefs{ 0 };

}  // namespace

SC_AudioDriver* SC_NewAudioDriver(World* world) { return new SC_SSPDriver(world); }

namespace scsy {

ScWorld::ScWorld() {
    for (int k = 0; k < AUDIO_CONTROLS; k++) pumpPtrs_[k] = pumpControls_[k];
}

bool ScWorld::open(float sampleRate, const std::string& pluginDir, std::string& error) {
    close();
    std::error_code ec;
    if (!std::filesystem::is_directory(pluginDir, ec)) {
        error = "no UGen directory " + pluginDir;
        return false;
    }
    static std::once_flag printSet;
    std::call_once(printSet, [] { SetPrintFunc(logPrint); });

    WorldOptions o;
    o.mPreferredSampleRate = uint32(sampleRate + 0.5f);
    o.mPreferredHardwareBufferFrameSize = BLOCK;
    o.mBufLength = BLOCK;
    o.mNumInputBusChannels = CHANNELS;
    o.mNumOutputBusChannels = CHANNELS;
    o.mLoadGraphDefs = 0;  // else it loads every def in SC's default directories
    o.mRendezvous = false;
    o.mUGensPluginPath = pluginDir.c_str();
    World* world = World_New(&o);
    if (!world) {
        error = "scsynth did not start";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(noteLock_);
        world_ = world;
    }
    send(Osc("/g_new").i(VOICE_GROUP).i(1).i(0).bytes());  // voices, after the running synth
    return true;
}

void ScWorld::close() {
    std::lock_guard<std::mutex> lock(noteLock_);
    if (!world_)
        return;
    World_Cleanup(world_, false);  // true would unload the UGens of every World in the process
    world_ = nullptr;
    node_ = 0;
    bank_ = -1;
    def_.clear();
    voices_ = false;
    mapped_.clear();
    for (Voice& v : voice_) v = Voice();
    held_.store(0, std::memory_order_relaxed);
    fill_ = 0;
    std::memset(fifoOut_, 0, sizeof fifoOut_);
}

void ScWorld::onReply(ReplyAddress* addr, char* buf, int size) {
    auto* self = static_cast<ScWorld*>(addr->mReplyData);
    Reply& r = self->replies_[self->replyWrite_.fetch_add(1, std::memory_order_relaxed) % REPLIES];
    if (r.full.load(std::memory_order_acquire) || size > int(sizeof r.data))
        return;
    std::memcpy(r.data, buf, size);
    r.size = size;
    r.full.store(true, std::memory_order_release);
}

bool ScWorld::send(const std::string& packet) {
    std::string copy = packet;  // World_SendPacket takes a mutable buffer and copies it
    return World_SendPacketWithContext(world_, int(copy.size()), copy.data(), onReply, this);
}

bool ScWorld::waitFor(const std::string& address, const std::string& arg, int timeoutMs,
                      std::vector<std::string>& failures) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    static const float silence[BLOCK] = {};
    float scratch[CHANNELS][BLOCK];
    const float* in[CHANNELS];
    float* out[CHANNELS];
    for (int c = 0; c < CHANNELS; c++) {
        in[c] = silence;
        out[c] = scratch[c];
    }
    bool found = false;
    for (;;) {
        for (Reply& r : replies_) {
            if (!r.full.load(std::memory_order_acquire))
                continue;
            std::string a;
            std::vector<std::string> s;
            if (parseReply(r.data, r.size, a, s)) {
                if (a == address && !s.empty() && s[0] == arg)
                    found = true;
                else if (a == "/fail" && s.size() >= 2)
                    failures.push_back(s[0] + ": " + s[1]);
            }
            r.full.store(false, std::memory_order_release);
        }
        if (found || std::chrono::steady_clock::now() > deadline)
            return found;
        if (pump_)
            process(in, out, BLOCK, pumpPtrs_);  // audio is stopped: run the engine here
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool ScWorld::load(const std::string& path, std::string& error, int timeoutMs) {
    std::ifstream f(path, std::ios::binary);
    std::string file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f) {
        error = "cannot read " + path;
        return false;
    }
    return loadDef(file, error, {}, false, false, timeoutMs);
}

bool ScWorld::loadDef(const std::string& file, std::string& error, const std::vector<Mapping>& mapped,
                      bool voices, bool pump, int timeoutMs) {
    if (!world_) {
        error = "scsynth is not running";
        return false;
    }
    // A def that fails to build still replies /done, so each load gets a new name: if /s_new
    // cannot find it, the build failed, and the reason is in the log.
    std::string def = "scsy" + std::to_string(gDefs.fetch_add(1) + 1);
    std::string bytes = renameSynthDef(file, def);
    if (bytes.empty()) {
        error = "not a SynthDef file";
        return false;
    }
    pump_ = pump;
    size_t logFrom;
    {
        std::lock_guard<std::mutex> lock(gLogLock);
        logFrom = gLog.size();
    }
    std::vector<std::string> failures;
    waitFor("", "", 0, failures);  // drops stale replies

    // a voice def starts a silent synth, gate 0, to show it builds, and frees it after
    const int node = node_ == 1000 ? 1001 : 1000;
    const int bank = nextBank();
    Osc start("/s_new");
    start.s(def).i(node).i(0).i(0);
    if (voices) start.s("gate").f(0.0f);
    bool answered = send(Osc("/d_recv").b(bytes).bytes()) && waitFor("/done", "/d_recv", timeoutMs, failures) &&
                    send(bundle({ start.bytes(), mapMessage("/n_map", node, mapped, bank, false),
                                  mapMessage("/n_mapa", node, mapped, bank, true) })) &&
                    send(Osc("/sync").i(node).bytes()) &&
                    waitFor("/synced", std::to_string(node), timeoutMs, failures);
    pump_ = false;
    if (!answered) {
        error = "scsynth did not answer";
        return false;
    }
    // a /fail from an earlier /n_free, of a synth that freed itself, is not this load's
    failures.erase(std::remove_if(failures.begin(), failures.end(),
                                  [](const std::string& f) { return f.compare(0, 6, "/s_new") != 0; }),
                   failures.end());
    if (!failures.empty()) {
        std::string log;
        {
            std::lock_guard<std::mutex> lock(gLogLock);
            log = gLog.substr(std::min(logFrom, gLog.size()));
        }
        const std::string tag = "exception in GraphDef_Recv: ";
        size_t at = log.find(tag);
        error = at == std::string::npos ? failures.front()
                                        : log.substr(at + tag.size(), log.find('\n', at) - at - tag.size());
        send(Osc("/d_free").s(def).bytes());
        return false;
    }
    std::lock_guard<std::mutex> lock(noteLock_);
    if (node_)
        send(Osc("/n_free").i(node_).bytes());
    send(Osc("/g_freeAll").i(VOICE_GROUP).bytes());  // the old def's voices
    for (Voice& v : voice_) v = Voice();
    held_.store(0, std::memory_order_relaxed);
    if (!def_.empty())
        send(Osc("/d_free").s(def_).bytes());
    if (voices)
        send(Osc("/n_free").i(node).bytes());
    node_ = voices ? 0 : node;
    bank_ = bank;
    def_ = def;
    voices_ = voices;
    mapped_ = mapped;
    error.clear();
    return true;
}

std::string ScWorld::mapMessage(const char* address, int node, const std::vector<Mapping>& mapped, int bank,
                                bool audio) const {
    Osc m(address);  // the synth reads its mapped controls from its first block
    m.i(node);
    for (size_t i = 0; i < mapped.size() && i < size_t(CONTROLS); i++) {
        if (mapped[i].name.empty() || mapped[i].audio != audio) continue;
        m.s(mapped[i].name).i(audio ? audioControlBus(bank * CONTROLS + int(i)) : controlBus(bank, int(i)));
    }
    return m.bytes();
}

void ScWorld::noteOn(int note, float velocity) {
    std::lock_guard<std::mutex> lock(noteLock_);
    if (!world_ || def_.empty())
        return;
    notes_.fetch_add(1, std::memory_order_relaxed);
    const float hz = 440.0f * std::exp2(float(note - 69) / 12.0f);
    if (!voices_) {
        if (node_)
            send(Osc("/n_set").i(node_).s("freq").f(hz).s("velocity").f(velocity).bytes());
        return;
    }
    // a held note past VOICES releases the oldest held one; the oldest synth gives up its slot
    int held = 0;
    Voice* oldestHeld = nullptr;
    for (int k = 0; k < VOICE_NODES; k++) {
        Voice& v = voice_[(voiceNext_ + k) % VOICE_NODES];
        if (v.node && v.held) {
            if (!oldestHeld) oldestHeld = &v;
            held++;
        }
    }
    if (held >= VOICES)
        release(*oldestHeld);
    Voice& slot = voice_[voiceNext_];
    if (slot.node)
        send(Osc("/n_free").i(slot.node).bytes());
    slot.node = VOICE_FIRST + int(voiceIds_++ % VOICE_IDS);
    slot.note = note;
    slot.held = true;
    voiceNext_ = (voiceNext_ + 1) % VOICE_NODES;
    Osc start("/s_new");
    start.s(def_).i(slot.node).i(1).i(VOICE_GROUP).s("freq").f(hz).s("velocity").f(velocity).s("gate").f(1.0f);
    send(bundle({ start.bytes(), mapMessage("/n_map", slot.node, mapped_, bank_, false),
                  mapMessage("/n_mapa", slot.node, mapped_, bank_, true) }));
    countHeld();
}

void ScWorld::countHeld() {
    int n = 0;
    for (const Voice& v : voice_) n += v.node && v.held;
    held_.store(n, std::memory_order_relaxed);
}

void ScWorld::noteOff(int note) {
    std::lock_guard<std::mutex> lock(noteLock_);
    if (!world_ || !voices_)
        return;
    for (Voice& v : voice_)
        if (v.node && v.held && v.note == note)
            release(v);
    countHeld();
}

void ScWorld::release(Voice& v) {
    send(Osc("/n_set").i(v.node).s("gate").f(0.0f).bytes());
    v.held = false;
}

// the last 2 x CONTROLS buses, so a def's own use of the low ones does not collide
int ScWorld::controlBus(int bank, int i) const {
    return int(world_->mNumControlBusChannels) - (2 - bank) * CONTROLS + i;
}

int ScWorld::audioControlBus(int k) const {
    return int(world_->mNumAudioBusChannels) - AUDIO_CONTROLS + k;
}

void ScWorld::setPumpControl(int bank, int i, float value) {
    std::fill(pumpControls_[bank * CONTROLS + i], pumpControls_[bank * CONTROLS + i] + BLOCK, value);
}

void ScWorld::setControl(int bank, int i, float value) {
    if (world_)
        world_->mControlBus[controlBus(bank, i)] = value;
}

void ScWorld::set(const std::string& control, float value) {
    if (world_ && node_)
        send(Osc("/n_set").i(node_).s(control).f(value).bytes());
}

void ScWorld::process(const float* const* in, float* const* out, int n, const float* const* controls) {
    if (!world_) {
        for (int c = 0; c < CHANNELS; c++)
            std::memset(out[c], 0, n * sizeof(float));
        return;
    }
    sc_SetDenormalFlags();
    auto* driver = static_cast<SC_SSPDriver*>(world_->hw->mAudioDriver);
    driver->begin();
    const int firstBus = audioControlBus(0);
    const float* ip[CHANNELS];
    float* op[CHANNELS];
    const float* cp[AUDIO_CONTROLS];
    if (fill_ == 0 && n % BLOCK == 0) {
        for (int pos = 0; pos < n; pos += BLOCK) {
            for (int c = 0; c < CHANNELS; c++) {
                ip[c] = in[c] + pos;
                op[c] = out[c] + pos;
            }
            for (int k = 0; controls && k < AUDIO_CONTROLS; k++) cp[k] = controls[k] ? controls[k] + pos : nullptr;
            driver->block(ip, op, controls ? cp : nullptr, firstBus, AUDIO_CONTROLS);
        }
    } else {
        for (int c = 0; c < CHANNELS; c++) {
            ip[c] = fifoIn_[c];
            op[c] = fifoOut_[c];
        }
        for (int k = 0; k < AUDIO_CONTROLS; k++) cp[k] = controls && controls[k] ? fifoIn_[CHANNELS + k] : nullptr;
        for (int pos = 0; pos < n;) {
            int k = std::min(BLOCK - fill_, n - pos);
            for (int c = 0; c < CHANNELS; c++) {
                std::memcpy(fifoIn_[c] + fill_, in[c] + pos, k * sizeof(float));
                std::memcpy(out[c] + pos, fifoOut_[c] + fill_, k * sizeof(float));
            }
            for (int a = 0; a < AUDIO_CONTROLS; a++)
                if (cp[a]) std::memcpy(fifoIn_[CHANNELS + a] + fill_, controls[a] + pos, k * sizeof(float));
            fill_ += k;
            pos += k;
            if (fill_ == BLOCK) {
                driver->block(ip, op, controls ? cp : nullptr, firstBus, AUDIO_CONTROLS);
                fill_ = 0;
            }
        }
    }
    driver->end();
}

}  // namespace scsy
