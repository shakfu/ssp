#include "Cartridge.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace dexed {

static constexpr size_t MAX_FILE = 65536;  // bulk dumps can share a file with other sysex

// highest value of each unpacked byte, from the DX7 voice data format
static const uint8_t* maxima() {
    static uint8_t m[Cartridge::VOICE_BYTES];
    static bool done = [] {
        static const uint8_t op[21] = { 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,  // EG, breakpoint, depths
                                        3,  3,  7,  3,  7,  99, 1,  31, 99, 14 };    // curves .. detune
        for (int o = 0; o < 6; o++) std::memcpy(m + o * 21, op, 21);
        static const uint8_t global[19] = { 99, 99, 99, 99, 99, 99, 99, 99,  // pitch EG
                                            31, 7,  1,  99, 99, 99, 99, 1, 5, 7, 48 };
        std::memcpy(m + 126, global, sizeof global);
        std::fill(m + 145, m + Cartridge::VOICE_BYTES, 127);  // name
        return true;
    }();
    (void)done;
    return m;
}

void Cartridge::sanitize(uint8_t* voice) {
    const uint8_t* m = maxima();
    for (int i = 0; i < VOICE_BYTES; i++) voice[i] = std::min(voice[i], m[i]);
}

// Dexed's Cartridge::unpackProgram
void Cartridge::unpack(const uint8_t* p, uint8_t* v) {
    for (int op = 0; op < 6; op++) {
        const uint8_t* b = p + op * 17;
        uint8_t* o = v + op * 21;
        for (int i = 0; i < 11; i++) o[i] = b[i] & 0x7F;
        o[11] = b[11] & 3;
        o[12] = (b[11] >> 2) & 3;
        o[13] = b[12] & 7;
        o[20] = (b[12] >> 3) & 0x0F;
        o[14] = b[13] & 3;
        o[15] = (b[13] >> 2) & 7;
        o[16] = b[14] & 0x7F;
        o[17] = b[15] & 1;
        o[18] = (b[15] >> 1) & 0x1F;
        o[19] = b[16] & 0x7F;
    }
    for (int i = 0; i < 8; i++) v[126 + i] = p[102 + i] & 0x7F;
    v[134] = p[110] & 0x1F;
    v[135] = p[111] & 7;
    v[136] = (p[111] >> 3) & 1;
    for (int i = 0; i < 4; i++) v[137 + i] = p[112 + i] & 0x7F;
    v[141] = p[116] & 1;
    v[142] = (p[116] >> 1) & 7;
    v[143] = (p[116] >> 4) & 7;
    v[144] = p[117] & 0x7F;
    for (int i = 0; i < 10; i++) v[145 + i] = p[118 + i] & 0x7F;
    sanitize(v);
}

std::string Cartridge::name(int v) const {
    std::string s;
    for (int i = 0; i < 10; i++) {
        uint8_t c = voice[v][145 + i];
        s += c >= 32 && c < 127 ? char(c) : ' ';
    }
    s.erase(s.find_last_not_of(' ') + 1);
    return s;
}

static bool isBulkHeader(const uint8_t* d) {
    // F0, Yamaha, any device, format 9 (32 voices), byte count 0x1000
    return d[0] == 0xF0 && d[1] == 0x43 && (d[2] & 0xF0) == 0 && d[3] == 0x09 && d[4] == 0x20 && d[5] == 0x00 &&
           d[Cartridge::SYSEX_BYTES - 1] == 0xF7;
}

std::string Cartridge::parse(const uint8_t* d, size_t size) {
    const uint8_t* bank = nullptr;
    bool sumOk = true;
    if (size == size_t(BANK_BYTES) && d[0] != 0xF0) {
        bank = d;
    } else {
        for (size_t i = 0; i + SYSEX_BYTES <= size && !bank; i++) {
            if (!isBulkHeader(d + i)) continue;
            bank = d + i + 6;
            unsigned sum = 0;
            for (int j = 0; j < BANK_BYTES; j++) sum += bank[j];
            sumOk = ((0x80 - (sum & 0x7F)) & 0x7F) == d[i + SYSEX_BYTES - 2];
        }
    }
    if (!bank) return "not a DX7 32-voice cartridge";
    for (int v = 0; v < VOICES; v++) unpack(bank + v * PACKED_BYTES, voice[v]);
    checksumOk = sumOk;
    return {};
}

std::string Cartridge::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "cannot open the file";
    std::vector<uint8_t> data;
    data.reserve(SYSEX_BYTES);
    char c;
    while (data.size() <= MAX_FILE && f.get(c)) data.push_back(uint8_t(c));
    if (data.size() > MAX_FILE) return "larger than 64 KB";
    return parse(data.data(), data.size());
}

}  // namespace dexed
