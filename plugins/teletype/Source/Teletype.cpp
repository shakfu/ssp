#include "Teletype.h"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace ttyp {

namespace {

// event loop pumps per tick: hardware_step handles one queued event, and a key or a trigger can
// queue several. 48 per tick cost 0.2% of an SSP core.
constexpr int STEPS_PER_TICK = 16;
constexpr unsigned SCREEN_EVERY = 16;  // ticks between screen copies, about 60 Hz
constexpr char STATE_MAGIC[4] = { 'T', 'T', 'Y', '1' };

uint16_t adc(float unit) {
    return uint16_t(std::clamp(unit, 0.0f, 1.0f) * 4095.0f);
}

void putU32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back(uint8_t(x >> (8 * i)));
}

uint32_t getU32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// scene text through teletype's serializer callbacks
void writeBuffer(void* self, uint8_t* b, uint16_t n) {
    static_cast<std::string*>(self)->append(reinterpret_cast<char*>(b), n);
}
void writeChar(void* self, uint8_t c) {
    static_cast<std::string*>(self)->push_back(char(c));
}
void noDebug(const char*) {}
struct Cursor {
    const std::string& text;
    size_t at;
};
uint16_t readChar(void* self) {
    auto* c = static_cast<Cursor*>(self);
    return c->at < c->text.size() ? uint8_t(c->text[c->at++]) : 0;
}
bool atEnd(void* self) {
    auto* c = static_cast<Cursor*>(self);
    return c->at >= c->text.size();
}

constexpr size_t MAX_SCENE_FILE = 64 << 10;  // a full scene's text is a few KB

bool writeFile(const std::string& path, const std::string& text) {
    std::string tmp = path + ".tmp";  // renamed into place, so a failed write leaves the old file
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    if (ok) ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    if (!ok) std::remove(tmp.c_str());
    return ok;
}

// false if missing; a file over MAX_SCENE_FILE leaves text empty
bool readFile(const std::string& path, std::string& text) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    text.clear();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() <= MAX_SCENE_FILE) text.append(buf, n);
    std::fclose(f);
    if (text.size() > MAX_SCENE_FILE) text.clear();
    return true;
}

}  // namespace

Teletype::Teletype(const void* image, size_t size) {
    fw_ = Firmware::load(image, size, error_);
    if (!fw_) return;
    fw_->setScreenBuffer(screenFw_);
    fw_->init();
    fw_->setGPIO(Firmware::PIN_FRONT, true);  // active low
    fw_->hidConnect();
}

Teletype::~Teletype() {
    stop();
}

void Teletype::start() {
    if (!fw_ || running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void Teletype::stop() {
    if (!running_.exchange(false)) return;
    thread_.join();
}

void Teletype::run() {
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    while (running_.load()) {
        tick();
        next += std::chrono::milliseconds(1);
        auto now = clock::now();
        // after a long script, carry on from now rather than replaying the missed ticks at once
        if (now - next > std::chrono::milliseconds(50)) next = now;
        std::this_thread::sleep_until(next);
    }
}

void Teletype::setTrigger(unsigned i, bool high) {
    if (i >= TRIGS) return;
    bool was = level_[i].load(std::memory_order_relaxed);
    if (high && !was) rises_[i].fetch_add(1, std::memory_order_relaxed);
    level_[i].store(high, std::memory_order_relaxed);
}

void Teletype::push(const Key& k) {
    std::lock_guard<std::mutex> lock(keyLock_);
    keys_.push_back(k);
}

void Teletype::tick() {
    if (!fw_) return;
    std::lock_guard<std::mutex> lock(fwLock_);
    {
        std::lock_guard<std::mutex> kl(keyLock_);
        keysDrained_.swap(keys_);
    }
    for (auto& k : keysDrained_) applyKey(k);
    keysDrained_.clear();

    for (unsigned i = 0; i < TRIGS; i++) {
        uint32_t r = rises_[i].load(std::memory_order_relaxed);
        if (r != seenRises_[i]) {
            // The mock interrupts on a rising edge only; several rises in one tick count once. The
            // pin stays high until the next tick: the trigger handler reads it when the event runs.
            fw_->setGPIO(Firmware::PIN_TRIG_1 + i, false);
            fw_->setGPIO(Firmware::PIN_TRIG_1 + i, true);
            seenRises_[i] = r;
            continue;
        }
        fw_->setGPIO(Firmware::PIN_TRIG_1 + i, level_[i].load(std::memory_order_relaxed));
    }
    fw_->setADC(Firmware::ADC_IN, adc(in_.load(std::memory_order_relaxed) / 10.0f));
    fw_->setADC(Firmware::ADC_PARAM, adc(param_.load(std::memory_order_relaxed)));
    bool front = front_.load(std::memory_order_relaxed);
    if (front != frontSent_) {
        fw_->setGPIO(Firmware::PIN_FRONT, !front);
        fw_->triggerInterrupt(3);
        frontSent_ = front;
    }

    fw_->triggerInterrupt(0);  // the 1 ms timer
    for (int s = 0; s < STEPS_PER_TICK; s++) fw_->step();

    for (unsigned i = 0; i < OUTS; i++) {
        uint16_t d = fw_->getDAC(Firmware::DAC_OF_CV[i]);
        cv_[i].store(10.0f * d / 65535.0f, std::memory_order_relaxed);
        tr_[i].store(fw_->getGPIO(Firmware::PIN_TR_1 + i), std::memory_order_relaxed);
    }
    if (++ticks_ % SCREEN_EVERY == 0) {
        std::lock_guard<std::mutex> sl(screenLock_);
        if (std::memcmp(screenShared_, screenFw_, sizeof screenFw_) != 0) {
            std::memcpy(screenShared_, screenFw_, sizeof screenFw_);
            screenSeq_++;
        }
    }
}

void Teletype::applyKey(const Key& k) {
    switch (k.kind) {
        case K_TAP:
            fw_->hidMessage(k.usage, k.mod, false, false);
            fw_->hidMessage(k.usage, k.mod, false, true);
            return;
        case K_RELEASE_ALL:
            mods_ = 0;
            std::memset(pressed_, 0, sizeof pressed_);
            break;
        case K_DOWN:
            if (k.usage >= 0xE0 && k.usage <= 0xE7) {
                mods_ |= uint8_t(1u << (k.usage - 0xE0));
            } else if (std::find(std::begin(pressed_), std::end(pressed_), k.usage) == std::end(pressed_)) {
                auto slot = std::find(std::begin(pressed_), std::end(pressed_), 0);
                if (slot == std::end(pressed_)) return;  // a seventh key: the boot report has six slots
                *slot = k.usage;
            }
            break;
        case K_UP:
            if (k.usage >= 0xE0 && k.usage <= 0xE7) {
                mods_ &= uint8_t(~(1u << (k.usage - 0xE0)));
            } else {
                auto it = std::find(std::begin(pressed_), std::end(pressed_), k.usage);
                if (it == std::end(pressed_)) return;
                std::copy(it + 1, std::end(pressed_), it);  // keep press order
                pressed_[5] = 0;
            }
            break;
    }
    sendReport();
}

void Teletype::sendReport() {
    uint8_t report[8] = { mods_, 0 };
    std::memcpy(report + 2, pressed_, 6);
    fw_->hidFrame(report, 8);
}

uint32_t Teletype::screen(uint8_t* out) {
    std::lock_guard<std::mutex> lock(screenLock_);
    std::memcpy(out, screenShared_, sizeof screenShared_);
    return screenSeq_;
}

std::vector<uint8_t> Teletype::saveState() {
    std::vector<uint8_t> v;
    if (!fw_) return v;
    std::lock_guard<std::mutex> lock(fwLock_);
    void* nv = nullptr;
    void* vr = nullptr;
    uint32_t nvSize = 0, vrSize = 0;
    fw_->readNVRAM(&nv, &nvSize);
    fw_->readVRAM(&vr, &vrSize);
    if (!nv || !vr) return v;
    v.reserve(12 + nvSize + vrSize);
    v.insert(v.end(), STATE_MAGIC, STATE_MAGIC + 4);
    putU32(v, nvSize);
    v.insert(v.end(), static_cast<uint8_t*>(nv), static_cast<uint8_t*>(nv) + nvSize);
    putU32(v, vrSize);
    v.insert(v.end(), static_cast<uint8_t*>(vr), static_cast<uint8_t*>(vr) + vrSize);
    return v;
}

// A preset's data: accepted only with this firmware's exact NVRAM and VRAM sizes, so a state from
// another teletype version is refused rather than misread.
bool Teletype::loadState(const uint8_t* data, size_t size) {
    if (!fw_ || size < 12 || std::memcmp(data, STATE_MAGIC, 4) != 0) return false;
    std::lock_guard<std::mutex> lock(fwLock_);
    void* nv = nullptr;
    void* vr = nullptr;
    uint32_t nvSize = 0, vrSize = 0;
    fw_->readNVRAM(&nv, &nvSize);
    fw_->readVRAM(&vr, &vrSize);
    if (getU32(data + 4) != nvSize || size != 12 + size_t(nvSize) + vrSize) return false;
    if (getU32(data + 8 + nvSize) != vrSize) return false;
    fw_->writeNVRAM(data + 8, nvSize);
    fw_->writeVRAM(data + 12 + nvSize, vrSize);
    return true;
}


std::string Teletype::exportScene(unsigned slot) {
    std::string text;
    if (!fw_ || slot >= Firmware::SCENES) return text;
    SceneWriter w = { writeBuffer, writeChar, noDebug, &text };
    std::lock_guard<std::mutex> lock(fwLock_);
    fw_->serializePreset(&w, uint8_t(slot));
    return text;
}

bool Teletype::importScene(unsigned slot, const std::string& text) {
    if (!fw_ || slot >= Firmware::SCENES) return false;
    Cursor c = { text, 0 };
    SceneReader r = { readChar, atEnd, noDebug, &c };
    std::lock_guard<std::mutex> lock(fwLock_);
    fw_->deserializePreset(&r, uint8_t(slot), true);
    return true;
}

// Files are read and written outside the firmware lock, so the firmware pauses only to convert.
Teletype::DiskResult Teletype::syncDisk(const std::string& dir) {
    DiskResult r;
    if (!fw_) {
        r.error = "no firmware";
        return r;
    }
    if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        r.error = "cannot create " + dir;
        return r;
    }
    char name[16];
    for (unsigned i = 0; i < Firmware::SCENES; i++) {
        std::snprintf(name, sizeof name, "tt%02us.txt", i);
        if (!writeFile(dir + "/" + name, exportScene(i))) {
            r.error = std::string("cannot write ") + name;
            return r;
        }
        r.written++;
    }
    for (unsigned i = 0; i < Firmware::SCENES; i++) {
        std::snprintf(name, sizeof name, "tt%02u.txt", i);
        std::string text;
        if (!readFile(dir + "/" + name, text)) continue;
        if (text.empty()) {
            r.error = std::string(name) + " is empty or over 64 KB";
            continue;
        }
        importScene(i, text);
        r.read++;
    }
    return r;
}

}  // namespace ttyp
