#include "Grid.h"

namespace ssp::monome {

void Surface::update(const Frame& next, std::vector<Quad>& out) {
    for (int qy = 0; qy < ROWS; qy += QUAD)
        for (int qx = 0; qx < COLS; qx += QUAD) {
            Quad q{ qx, qy, {} };
            bool changed = !known_;
            for (int y = 0; y < QUAD; y++)
                for (int x = 0; x < QUAD; x++) {
                    const uint8_t l = next.at(qx + x, qy + y) < LEVELS ? next.at(qx + x, qy + y) : LEVELS - 1;
                    q.level[y * QUAD + x] = l;
                    changed = changed || l != shown_.at(qx + x, qy + y);
                    shown_.at(qx + x, qy + y) = l;
                }
            if (changed) out.push_back(q);
        }
    known_ = true;
}

}  // namespace ssp::monome
