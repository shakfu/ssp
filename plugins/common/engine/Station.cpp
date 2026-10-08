// stations longer than 2 GB on 32-bit ARM
#define _FILE_OFFSET_BITS 64

#include "Station.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ssp::engine {

unsigned Station::bytesPerFrame() const {
    static constexpr unsigned bytes[] = { 2, 3, 4, 4 };
    return bytes[format] * channels;
}

static bool hasExt(const std::string& s, const char* ext) {
    size_t n = std::strlen(ext);
    return s.size() > n && strcasecmp(s.c_str() + s.size() - n, ext) == 0;
}

static uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

static uint16_t le16(const uint8_t* p) {
    return uint16_t(p[0] | p[1] << 8);
}

static bool probeWav(FILE* f, Station& st) {
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return false;
    bool fmt = false;
    for (;;) {
        uint8_t c[8];
        if (fread(c, 1, 8, f) != 8) return false;
        uint32_t size = le32(c + 4);
        if (memcmp(c, "fmt ", 4) == 0) {
            uint8_t b[40] = {};
            size_t len = std::min<size_t>(size, sizeof(b));
            if (size < 16 || fread(b, 1, len, f) != len) return false;
            uint16_t tag = le16(b);
            if (tag == 0xFFFE && size >= 26) tag = le16(b + 24);  // WAVE_FORMAT_EXTENSIBLE subformat
            st.channels = le16(b + 2);
            st.rate = le32(b + 4);
            uint16_t bits = le16(b + 14);
            if (tag == 1 && bits == 16) st.format = Station::PCM16;
            else if (tag == 1 && bits == 24) st.format = Station::PCM24;
            else if (tag == 1 && bits == 32) st.format = Station::PCM32;
            else if (tag == 3 && bits == 32) st.format = Station::FLOAT32;
            else return false;
            if (st.channels == 0 || st.rate == 0) return false;
            fmt = true;
            if (fseeko(f, off_t(size - len + (size & 1)), SEEK_CUR) != 0) return false;
        } else if (memcmp(c, "data", 4) == 0) {
            if (!fmt) return false;
            st.dataOffset = uint64_t(ftello(f));
            st.frames = size / st.bytesPerFrame();
            return st.frames > 0;
        } else if (fseeko(f, off_t(size + (size & 1)), SEEK_CUR) != 0) {
            return false;
        }
    }
}

bool probe(const std::string& path, Station& st) {
    st = Station();
    st.path = path;
    st.name = path.substr(path.find_last_of('/') + 1);
    if (hasExt(path, ".raw")) {
        struct stat s;
        if (stat(path.c_str(), &s) != 0 || !S_ISREG(s.st_mode)) return false;
        st.frames = uint64_t(s.st_size) / 2;
        return st.frames > 0;
    }
    if (!hasExt(path, ".wav")) return false;
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    bool ok = probeWav(f, st);
    fclose(f);
    return ok;
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool nameLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            while (i < a.size() && a[i] == '0') i++;
            while (j < b.size() && b[j] == '0') j++;
            size_t ie = i, je = j;
            while (ie < a.size() && isDigit(a[ie])) ie++;
            while (je < b.size() && isDigit(b[je])) je++;
            if (ie - i != je - j) return ie - i < je - j;  // fewer digits, smaller number
            int c = a.compare(i, ie - i, b, j, je - j);
            if (c != 0) return c < 0;
            i = ie;
            j = je;
            continue;
        }
        int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[j]));
        if (ca != cb) return ca < cb;
        i++;
        j++;
    }
    return a.size() - i < b.size() - j;
}

std::vector<std::string> listDir(const std::string& dir, bool dirs) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) return out;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string p = dir + "/" + e->d_name;
        struct stat s;
        if (stat(p.c_str(), &s) != 0) continue;
        if (S_ISDIR(s.st_mode) == dirs) out.push_back(e->d_name);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), nameLess);
    return out;
}

std::vector<Station> scanBank(const std::string& dir) {
    std::vector<Station> out;
    for (auto& name : listDir(dir, false)) {
        Station st;
        if (probe(dir + "/" + name, st)) out.push_back(std::move(st));
    }
    return out;
}

std::vector<std::string> scanBanks(const std::string& root) {
    std::vector<std::string> out;
    for (auto& name : listDir(root, true)) out.push_back(root + "/" + name);
    if (out.empty()) out.push_back(root);
    return out;
}

bool Reader::open(const Station& st, uint64_t frame) {
    close();
    st_ = st;
    f_ = fopen(st.path.c_str(), "rb");
    if (f_ == nullptr || st.frames == 0) return false;
    return seek(frame % st.frames);
}

void Reader::close() {
    if (f_ != nullptr) fclose(f_);
    f_ = nullptr;
}

bool Reader::seek(uint64_t frame) {
    pos_ = frame;
    return fseeko(f_, off_t(st_.dataOffset + frame * st_.bytesPerFrame()), SEEK_SET) == 0;
}

static float sampleAt(const uint8_t* p, Station::Format fmt) {
    switch (fmt) {
        case Station::PCM16: return float(int16_t(le16(p))) * (1.0f / 32768.0f);
        case Station::PCM24: return float(int32_t(le32(p - 1) & 0xFFFFFF00u)) * (1.0f / 2147483648.0f);
        case Station::PCM32: return float(int32_t(le32(p))) * (1.0f / 2147483648.0f);
        case Station::FLOAT32: {
            uint32_t u = le32(p);
            float v;
            std::memcpy(&v, &u, 4);
            return v;
        }
    }
    return 0.0f;
}

size_t Reader::read(float* dst, size_t n) {
    if (f_ == nullptr) return 0;
    unsigned bpf = st_.bytesPerFrame(), bps = bpf / st_.channels;
    float gain = 1.0f / float(st_.channels);
    size_t done = 0;
    while (done < n) {
        if (pos_ >= st_.frames && !seek(0)) break;
        size_t want = size_t(std::min<uint64_t>(n - done, st_.frames - pos_));
        // one spare byte in front, so PCM24 can read a 32-bit word ending at each sample
        raw_.resize(want * bpf + 1);
        size_t got = fread(raw_.data() + 1, bpf, want, f_);
        for (size_t i = 0; i < got; i++) {
            const uint8_t* fr = raw_.data() + 1 + i * bpf;
            float acc = 0.0f;
            for (unsigned c = 0; c < st_.channels; c++) acc += sampleAt(fr + c * bps, st_.format);
            dst[done + i] = acc * gain;
        }
        done += got;
        pos_ += got;
        if (got < want) break;
    }
    return done;
}

}  // namespace ssp::engine
