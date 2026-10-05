// Native test of rack::executionOrder; built and run by test_graph_order.py.

#include <cstdio>
#include <initializer_list>
#include <utility>

#include "GraphOrder.h"

static int failures = 0;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                              \
        }                                                                            \
    } while (0)

static constexpr unsigned N = 10;  // Track::M_MAX: IN, 8 slots, OUT
using Order = std::array<unsigned, N>;

struct Graph {
    bool adj[N][N] = {};
    Graph(std::initializer_list<std::pair<unsigned, unsigned>> wires) {
        for (auto& w : wires) adj[w.first][w.second] = true;
    }
};

static unsigned position(const Order& order, unsigned node) {
    for (unsigned k = 0; k < N; k++) {
        if (order[k] == node) return k;
    }
    return N;
}

static bool before(const Order& order, unsigned a, unsigned b) {
    return position(order, a) < position(order, b);
}

static bool isPermutation(const Order& order) {
    bool seen[N] = {};
    for (unsigned node : order) {
        if (node >= N || seen[node]) return false;
        seen[node] = true;
    }
    return true;
}

static void noWiresKeepsSlotOrder() {
    Graph g({});
    auto order = rack::executionOrder(g.adj);
    for (unsigned k = 0; k < N; k++) CHECK(order[k] == k);
}

static void backwardWireRunsSourceFirst() {
    // in -> s5 -> s2 -> out: slot order would delay s5 -> s2 by a block
    Graph g({ { 0, 5 }, { 5, 2 }, { 2, 9 } });
    auto order = rack::executionOrder(g.adj);
    CHECK(isPermutation(order));
    CHECK(before(order, 0, 5));
    CHECK(before(order, 5, 2));
    CHECK(before(order, 2, 9));
}

static void cycleBreaksAtLowestSlot() {
    // s3 <-> s6: s3 runs first, so s6 -> s3 is the delayed wire
    Graph g({ { 0, 3 }, { 3, 6 }, { 6, 3 }, { 6, 9 } });
    auto order = rack::executionOrder(g.adj);
    CHECK(before(order, 3, 6));
    CHECK(before(order, 6, 9));
}

static void cycleBreakSkipsNodesDownstream() {
    // s2 is fed by the s5 <-> s6 cycle but is not on it, so it must not be chosen to break it
    Graph g({ { 0, 5 }, { 5, 6 }, { 6, 5 }, { 6, 2 }, { 2, 9 } });
    auto order = rack::executionOrder(g.adj);
    CHECK(before(order, 5, 6));
    CHECK(before(order, 6, 2));
    CHECK(before(order, 2, 9));
}

static void selfWireIsIgnored() {
    Graph g({ { 3, 3 } });
    auto order = rack::executionOrder(g.adj);
    for (unsigned k = 0; k < N; k++) CHECK(order[k] == k);
    CHECK(rack::wireKind(order, 3, 3) == rack::WireKind::Self);
}

static void wireKindsOfALoop() {
    // s3 <-> s6: s3 runs first, so s3 -> s6 is forward and s6 -> s3 is delayed
    Graph g({ { 3, 6 }, { 6, 3 } });
    auto order = rack::executionOrder(g.adj);
    CHECK(rack::wireKind(order, 3, 6) == rack::WireKind::Forward);
    CHECK(rack::wireKind(order, 6, 3) == rack::WireKind::Delayed);
}

// Over random graphs: the order is a permutation, and a wire runs backward only if it is on a cycle.
static void onlyCycleWiresRunBackward() {
    unsigned state = 12345;
    auto rnd = [&state]() {
        state = state * 1103515245u + 12345u;
        return (state >> 16) & 0x7fff;
    };
    for (int trial = 0; trial < 2000; trial++) {
        Graph g({});
        unsigned density = 1 + rnd() % 30;  // percent
        for (unsigned a = 0; a < N; a++) {
            for (unsigned b = 0; b < N; b++) g.adj[a][b] = (rnd() % 100) < density;
        }
        auto order = rack::executionOrder(g.adj);
        CHECK(isPermutation(order));

        bool reach[N][N];
        for (unsigned a = 0; a < N; a++) {
            for (unsigned b = 0; b < N; b++) reach[a][b] = g.adj[a][b];
        }
        for (unsigned k = 0; k < N; k++) {
            for (unsigned a = 0; a < N; a++) {
                for (unsigned b = 0; b < N; b++) reach[a][b] = reach[a][b] || (reach[a][k] && reach[k][b]);
            }
        }
        for (unsigned a = 0; a < N; a++) {
            for (unsigned b = 0; b < N; b++) {
                if (g.adj[a][b] && a != b && before(order, b, a)) CHECK(reach[b][a]);
                // the screen's classification agrees with the order the engine runs
                if (a != b) CHECK((rack::wireKind(order, a, b) == rack::WireKind::Delayed) == before(order, b, a));
            }
        }
    }
}

int main() {
    noWiresKeepsSlotOrder();
    backwardWireRunsSourceFirst();
    cycleBreaksAtLowestSlot();
    cycleBreakSkipsNodesDownstream();
    selfWireIsIgnored();
    wireKindsOfALoop();
    onlyCycleWiresRunBackward();
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
