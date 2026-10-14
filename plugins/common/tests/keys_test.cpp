// Two plugin libraries share the keyboard through KeyInput's claim: each loads keys_shim.so privately,
// as Synthor loads plugins, so each has its own KeyInput.
//
// usage: keys_test LIB_A LIB_B

#include <dlfcn.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace {

int failures = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond);  \
            failures++;                                            \
        }                                                          \
    } while (0)

struct Lib {
    void* h;
    void (*acquire)(uint16_t);
    void (*show)();
    bool (*wants)();
    void (*release)();
    explicit Lib(const char* path) : h(dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
        if (!h) {
            std::printf("%s\n", dlerror());
            return;
        }
        acquire = reinterpret_cast<void (*)(uint16_t)>(dlsym(h, "keys_acquire"));
        show = reinterpret_cast<void (*)()>(dlsym(h, "keys_show"));
        wants = reinterpret_cast<bool (*)()>(dlsym(h, "keys_wants"));
        release = reinterpret_cast<void (*)()>(dlsym(h, "keys_release"));
    }
};

void wait(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    Lib a(argv[1]), b(argv[2]);
    if (!a.h || !b.h) return 1;
    a.acquire(47770);
    b.acquire(47771);

    // nothing shown yet: both want it, and the kernel gives the grab to one
    CHECK(a.wants() && b.wants());

    // A on screen: B gives it up
    a.show();
    CHECK(a.wants());
    CHECK(!b.wants());

    // both on screen, as mini editors may be: both want it; the holder keeps it, as the grab refuses
    // the other
    b.show();
    CHECK(a.wants() && b.wants());

    // A off screen, B on screen: A gives it up and B takes it
    wait(600);
    b.show();
    CHECK(!a.wants());
    CHECK(b.wants());

    // nothing on screen: both want it again, so whoever holds it keeps it and a holder that goes is
    // replaced; the console and SysRq never see keys
    wait(600);
    CHECK(a.wants() && b.wants());

    a.release();
    b.release();
    std::printf("%s\n", failures ? "keys: failed" : "keys: passed");
    return failures ? 1 : 0;
}
