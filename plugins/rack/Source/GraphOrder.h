#pragma once

// Module execution order for one track. Header-only and JUCE-free, so tests build natively.
// See docs/dev/rack-design.md, section 3.

#include <array>
#include <cstddef>

namespace rack {

// Order nodes so each runs after every node that feeds it; adj[a][b] means a feeds b.
// Ties go to the lowest index. When only cycles remain, the next node is the lowest index whose
// remaining inputs all lie on its own cycles; those inputs read their previous block.
// Self-wires are ignored. Allocation-free, so it can run on the audio thread.
template <unsigned N>
std::array<unsigned, N> executionOrder(const bool (&adj)[N][N]) {
    std::array<unsigned, N> order{};
    bool done[N] = {};
    unsigned pending[N] = {};  // inputs from nodes not yet run
    for (unsigned a = 0; a < N; a++) {
        for (unsigned b = 0; b < N; b++) {
            if (adj[a][b] && a != b) pending[b]++;
        }
    }

    bool reach[N][N];  // transitive closure, built only if a cycle must be broken
    bool haveReach = false;

    for (unsigned k = 0; k < N; k++) {
        unsigned next = N;
        for (unsigned i = 0; i < N && next == N; i++) {
            if (!done[i] && pending[i] == 0) next = i;
        }

        if (next == N && !haveReach) {
            for (unsigned a = 0; a < N; a++) {
                for (unsigned b = 0; b < N; b++) reach[a][b] = adj[a][b];
            }
            for (unsigned m = 0; m < N; m++) {
                for (unsigned a = 0; a < N; a++) {
                    for (unsigned b = 0; b < N; b++) reach[a][b] = reach[a][b] || (reach[a][m] && reach[m][b]);
                }
            }
            haveReach = true;
        }
        for (unsigned i = 0; i < N && next == N; i++) {
            if (done[i]) continue;
            bool closesCycle = true;
            for (unsigned p = 0; p < N && closesCycle; p++) {
                if (adj[p][i] && p != i && !done[p] && !reach[i][p]) closesCycle = false;
            }
            if (closesCycle) next = i;
        }
        // unreachable: some cycle always has no inputs from outside it. Guards the index.
        for (unsigned i = 0; i < N && next == N; i++) {
            if (!done[i]) next = i;
        }

        done[next] = true;
        order[k] = next;
        for (unsigned b = 0; b < N; b++) {
            if (adj[next][b] && b != next && !done[b]) pending[b]--;
        }
    }
    return order;
}

// What a wire from src to dest does under a given order. A delayed wire reads its source's
// previous block, because the destination runs first. A self-wire carries nothing: process
// clears a module's buffer before summing its inputs.
enum class WireKind { Forward, Delayed, Self };

template <std::size_t N>
WireKind wireKind(const std::array<unsigned, N>& order, unsigned src, unsigned dest) {
    if (src == dest) return WireKind::Self;
    for (unsigned node : order) {
        if (node == src) return WireKind::Forward;
        if (node == dest) return WireKind::Delayed;
    }
    return WireKind::Forward;  // unreachable for a permutation
}

}  // namespace rack
