#pragma once

#include <atomic>

namespace ssp::engine {

// Hands an object between one audio thread (use) and one control thread (replace) without a lock.
//
//   audio:   T* t = gate.begin(); if (t) ...; gate.end();
//   control: T* old = gate.take(); /* audio now sees null */ gate.publish(next); destroy(old);
//
// begin() marks the gate busy before it reads the pointer, and take() clears the pointer before it
// waits for busy to clear, so the audio thread never uses an object take() returned. Both sides store
// one atomic, then load the other (Dekker's pattern); acquire/release allows that store-load pair to
// reorder, so the handshake needs seq_cst.
template <typename T>
class ReloadGate {
public:
    T* begin() {
        busy_.store(true, std::memory_order_seq_cst);
        return active_.load(std::memory_order_seq_cst);
    }
    void end() { busy_.store(false, std::memory_order_seq_cst); }

    // Waits at most one audio block.
    T* take() {
        T* old = active_.exchange(nullptr, std::memory_order_seq_cst);
        while (busy_.load(std::memory_order_seq_cst)) {}
        return old;
    }
    void publish(T* t) { active_.store(t, std::memory_order_seq_cst); }
    T* current() const { return active_.load(std::memory_order_seq_cst); }

private:
    std::atomic<T*> active_{ nullptr };
    std::atomic<bool> busy_{ false };
};

}  // namespace ssp::engine
