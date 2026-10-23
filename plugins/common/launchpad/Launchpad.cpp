#include "Launchpad.h"

#include <iterator>

namespace ssp::launchpad {

namespace {
const uint8_t HEADER[] = { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D };
constexpr uint8_t PROGRAMMER = 0x0E, LIGHT = 0x03;
}  // namespace

bool decode(const uint8_t* data, int size, Event& e) {
    if (size < 3) return false;
    const uint8_t status = data[0], i = data[1];
    const bool note = status == 0x90 || status == 0x80, cc = status == 0xB0;
    if (!note && !cc) return false;
    const int x = i % 10 - 1, y = i / 10 - 1;
    if (x < 0 || x >= SIZE || y < 0 || y >= SIZE) return false;
    const bool pad = x < 8 && y < 8;
    if (note != pad) return false;  // pads send notes, the keys round them CCs
    e.x = x, e.y = y;
    e.down = status != 0x80 && data[2] > 0;
    return true;
}

std::vector<uint8_t> programmerMode(bool on) {
    std::vector<uint8_t> m(std::begin(HEADER), std::end(HEADER));
    m.insert(m.end(), { PROGRAMMER, uint8_t(on ? 1 : 0), 0xF7 });
    return m;
}

int Surface::update(const Frame& next, std::vector<uint8_t>& out) {
    int sent = 0;
    for (int y = 0; y < SIZE; y++)
        for (int x = 0; x < SIZE; x++) {
            const Led& l = next.at(x, y);
            if (known_ && l == shown_.at(x, y)) continue;
            if (sent++ == 0) {
                out.insert(out.end(), std::begin(HEADER), std::end(HEADER));
                out.push_back(LIGHT);
            }
            out.insert(out.end(), { uint8_t(l.type), uint8_t(index(x, y)), l.a });
            if (l.type == Led::FLASH) out.push_back(l.b);
            if (l.type == Led::RGB) out.insert(out.end(), { l.b, l.c });
        }
    if (sent) out.push_back(0xF7);
    shown_ = next;
    known_ = true;
    return sent;
}

}  // namespace ssp::launchpad
