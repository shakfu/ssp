#include "ScsyDef.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace scsy {

namespace {

// big-endian reads that stop at the end of the data
struct Reader {
    const std::string& b;
    size_t pos = 0;
    bool ok = true;

    uint32_t u(int bytes) {
        if (pos + size_t(bytes) > b.size()) {
            ok = false;
            return 0;
        }
        uint32_t v = 0;
        for (int i = 0; i < bytes; i++) v = v << 8 | uint8_t(b[pos++]);
        return v;
    }
    int32_t i32() { return int32_t(u(4)); }
    float f32() {
        uint32_t bits = u(4);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    std::string pstring() {
        size_t n = u(1);
        if (pos + n > b.size()) {
            ok = false;
            return {};
        }
        std::string s = b.substr(pos, n);
        pos += n;
        return s;
    }
    void skip(size_t n) {
        if (pos + n > b.size()) ok = false;
        else pos += n;
    }
};

}  // namespace

bool readSynthDef(const std::string& bytes, SynthDefInfo& info, std::string& error) {
    Reader r{ bytes };
    if (bytes.compare(0, 4, "SCgf") != 0) {
        error = "not a SynthDef file";
        return false;
    }
    r.skip(4);
    int32_t version = r.i32();
    if (r.ok && version != 2) {
        error = "SynthDef file version " + std::to_string(version) + "; only version 2 is read";
        return false;
    }
    int defs = int(r.u(2));
    if (r.ok && defs < 1) {
        error = "SynthDef file holds no SynthDef";
        return false;
    }
    info = SynthDefInfo();
    info.name = r.pstring();
    int32_t constants = r.i32();
    r.skip(size_t(std::max(constants, 0)) * 4);
    int32_t values = r.i32();
    std::vector<float> initial;
    for (int32_t i = 0; r.ok && i < values; i++) initial.push_back(r.f32());
    int32_t names = r.i32();
    for (int32_t i = 0; r.ok && i < names; i++) {
        SynthDefInfo::Control c;
        c.name = r.pstring();
        c.index = r.i32();
        if (c.index < 0 || c.index >= values) r.ok = false;
        else c.value = initial[size_t(c.index)];
        info.controls.push_back(c);
    }
    if (!r.ok || constants < 0 || values < 0 || names < 0) {
        error = "SynthDef file is cut short or corrupt";
        return false;
    }
    std::sort(info.controls.begin(), info.controls.end(),
              [](const SynthDefInfo::Control& a, const SynthDefInfo::Control& b) { return a.index < b.index; });
    return true;
}

std::string renameSynthDef(const std::string& bytes, const std::string& name) {
    // "SCgf", int32 version, int16 number of defs, then the first def's name as a Pascal string
    if (bytes.size() < 11 || bytes.compare(0, 4, "SCgf") != 0 || name.size() > 255)
        return {};
    size_t len = uint8_t(bytes[10]);
    if (bytes.size() < 11 + len || (bytes[8] == 0 && bytes[9] == 0))
        return {};
    return bytes.substr(0, 10) + char(name.size()) + name + bytes.substr(11 + len);
}

}  // namespace scsy
