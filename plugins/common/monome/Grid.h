#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ssp::monome {

// A monome grid's LEDs and keys, free of libmonome and JUCE: what a view draws and reads. x is the column,
// from the left; y the row, from the top, as the grid numbers them.
constexpr int COLS = 16, ROWS = 8, LEVELS = 16;  // LEVELS: brightness 0..15 (varibright)
constexpr int QUAD = 8;                           // the grid updates LEDs in 8 x 8 quadrants

struct Key {
    int x = 0, y = 0;
    bool down = false;
};

struct Frame {
    std::array<uint8_t, COLS * ROWS> level{};
    uint8_t& at(int x, int y) { return level[size_t(y * COLS + x)]; }
    uint8_t at(int x, int y) const { return level[size_t(y * COLS + x)]; }
};

// One quadrant's levels, row by row, for monome_led_level_map
struct Quad {
    int x = 0, y = 0;  // its top-left key
    uint8_t level[QUAD * QUAD];
};

// What the grid shows, and the quadrants a new frame changes.
class Surface {
public:
    Surface() { forget(); }
    // Appends the quadrants that differ from what the grid shows, and takes them as shown.
    void update(const Frame& next, std::vector<Quad>& out);
    // The grid's LEDs are unknown, as after it opens: the next update sends every quadrant.
    void forget() { known_ = false; }

private:
    Frame shown_;
    bool known_ = false;
};

}  // namespace ssp::monome
