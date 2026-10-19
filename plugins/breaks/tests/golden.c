// _core's Rack fixture (a resident sine sample, no card) rendered for 3000 blocks, hashed. C, as the
// fixture is.
#include "engine_fixture.h"

uint64_t brks_golden(void) {
    CoreEngine *e = create(901);
    uint64_t h = 1469598103934665603ULL;
    for (unsigned f = 0; f < 3000u * CORE_BLOCK; f++) {
        int16_t pcm[2];
        core_engine_process(e, pcm);
        for (int c = 0; c < 2; c++) h = (h ^ (uint16_t)pcm[c]) * 1099511628211ULL;
    }
    core_engine_destroy(e);
    return h;
}
