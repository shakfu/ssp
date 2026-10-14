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

bool isScore(const std::string& bytes) {
    return bytes.size() >= 12 && bytes.compare(4, 8, std::string("#bundle\0", 8)) == 0;
}

namespace {

// One OSC message: its address, type tags and the position of its first argument.
struct Message {
    const std::string& b;
    size_t start, end, pos = 0;
    std::string address, tags;
    bool ok = true;

    Message(const std::string& bytes, size_t s, size_t e): b(bytes), start(s), end(e), pos(s) {
        address = str();
        tags = str();
        if (tags.empty() || tags[0] != ',') ok = false;
    }
    std::string str() {
        size_t z = b.find('\0', pos);
        if (z == std::string::npos || z >= end) {
            ok = false;
            return {};
        }
        std::string s = b.substr(pos, z - pos);
        pos = (z + 4 - start) / 4 * 4 + start;
        return s;
    }
    int32_t i32() {
        if (pos + 4 > end) {
            ok = false;
            return 0;
        }
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) v = v << 8 | uint8_t(b[pos++]);
        return int32_t(v);
    }
    std::string blob() {
        int32_t n = i32();
        if (n < 0 || pos + size_t(n) > end) {
            ok = false;
            return {};
        }
        std::string s = b.substr(pos, size_t(n));
        pos += (size_t(n) + 3) / 4 * 4;
        return s;
    }
};

}  // namespace

std::string resolveBufferPath(const std::string& message, const std::string& dir) {
    Message m(message, 0, message.size());
    if (!m.ok || m.address != "/b_allocRead" || m.tags.compare(0, 3, ",is") != 0) return message;
    int32_t number = m.i32();
    std::string path = m.str();
    if (!m.ok || path.empty() || path[0] == '/' || dir.empty()) return message;
    path = dir + "/" + path;
    // the rest of the arguments, as they were
    std::string rest = message.substr(m.pos);
    auto pad = [](std::string& b) { b.append(4 - b.size() % 4, '\0'); };
    std::string out = m.address;
    pad(out);
    out += m.tags;
    pad(out);
    for (int s = 24; s >= 0; s -= 8) out += char((uint32_t(number) >> s) & 0xff);
    out += path;
    pad(out);
    return out + rest;
}

bool readScore(const std::string& bytes, Score& score, std::string& error) {
    score = Score();
    std::vector<std::pair<std::string, int>> names;  // def name: -1 for the main def, else its voice
    std::vector<std::pair<std::string, std::string>> received;  // defs not yet started: name, bytes
    bool haveMain = false;
    std::string mainName;
    Reader r{ bytes };
    while (r.ok && r.pos < bytes.size()) {
        size_t size = r.u(4), start = r.pos;
        if (!r.ok || start + size > bytes.size() || size < 16 || bytes.compare(start, 8, std::string("#bundle\0", 8))) {
            error = "score is cut short or corrupt";
            return false;
        }
        r.pos = start + 8;
        double time = double(r.u(4)) + double(r.u(4)) / 4294967296.0;
        score.length = std::max(score.length, time);
        while (r.ok && r.pos < start + size) {
            size_t n = r.u(4), from = r.pos;
            if (!r.ok || from + n > start + size) break;
            r.pos = from + n;
            Message m(bytes, from, from + n);
            if (!m.ok) {
                error = "score holds a malformed message";
                return false;
            }
            if (m.address == "/d_recv") {
                std::string def = m.tags.size() > 1 && m.tags[1] == 'b' ? m.blob() : std::string();
                SynthDefInfo info;
                if (!m.ok || !readSynthDef(def, info, error)) {
                    error = "score: " + (error.empty() ? std::string("bad /d_recv") : error);
                    return false;
                }
                received.push_back({ info.name, def });
            } else if (m.address == "/s_new") {
                std::string name = m.tags.size() > 1 && m.tags[1] == 's' ? m.str() : std::string();
                auto known = std::find_if(names.begin(), names.end(), [&](auto& p) { return p.first == name; });
                int voice;
                if (known != names.end()) {
                    voice = known->second;
                } else {
                    auto got = std::find_if(received.begin(), received.end(), [&](auto& p) { return p.first == name; });
                    if (got == received.end()) {
                        error = "score starts " + name + " before receiving it";
                        return false;
                    }
                    if (!haveMain) {
                        score.main = got->second;
                        voice = -1;
                    } else {
                        voice = int(score.voices.size());
                        score.voices.push_back(got->second);
                    }
                    names.push_back({ name, voice });
                }
                if (!haveMain) {
                    haveMain = true;
                    mainName = name;
                } else if (voice < 0) {
                    error = "score starts its main def " + name + " twice";
                    return false;
                } else {
                    score.events.push_back({ time, voice });
                }
            } else if (m.address.compare(0, 3, "/b_") == 0) {
                score.setup.push_back(bytes.substr(from, n));
            } else if (m.address != "/d_free" && m.address != "/c_set" && m.address != "/g_freeAll") {
                error = "score command " + m.address + " is not supported";
                return false;
            }
        }
    }
    if (!r.ok) {
        error = "score is cut short or corrupt";
        return false;
    }
    if (!haveMain) {
        error = "score starts no synth";
        return false;
    }
    std::stable_sort(score.events.begin(), score.events.end(),
                     [](const Score::Event& a, const Score::Event& b) { return a.time < b.time; });
    return true;
}

}  // namespace scsy
