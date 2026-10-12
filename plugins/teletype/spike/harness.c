// Feasibility spike (2026-10-12): dlopen two copies of the firmware, type into each, check DAC
// isolation, step cost and the cost of a long live command. See docs/dev/teletype.md. Superseded by
// tests/engine_test.cpp; kept as the record of the measurements.
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "conf_board.h"

typedef struct {
    void *h;
    void (*init)(void);
    void (*step)(void);
    void (*irq)(int);
    uint16_t (*getDAC)(int);
    bool (*getGPIO)(uint32_t);
    void (*setGPIO)(uint32_t, bool);
    void (*setScreen)(uint8_t *);
    void (*hidConnect)(void);
    void (*hid)(uint8_t, uint8_t, bool, bool);
    uint8_t screen[128 * 64];
} fw_t;

static void load(fw_t *f, const char *src, const char *copy) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "cp %s %s", src, copy);
    if (system(cmd)) exit(1);
    f->h = dlopen(copy, RTLD_NOW | RTLD_LOCAL);
    if (!f->h) { fprintf(stderr, "%s\n", dlerror()); exit(1); }
#define SYM(field, name) *(void **)&f->field = dlsym(f->h, "hardware_" name)
    SYM(init, "init"); SYM(step, "step"); SYM(irq, "triggerInterrupt");
    SYM(getDAC, "getDAC"); SYM(getGPIO, "getGPIO"); SYM(setGPIO, "setGPIO");
    SYM(setScreen, "setScreenBuffer"); SYM(hidConnect, "hidConnect"); SYM(hid, "hidMessage");
    f->setScreen(f->screen);
    f->init();
}

// advance ms milliseconds; steps_per_ms event-loop pumps per 1 ms timer tick
static void run(fw_t *f, int ms, int steps_per_ms) {
    for (int i = 0; i < ms; i++) {
        f->irq(0);
        for (int s = 0; s < steps_per_ms; s++) f->step();
    }
}

static uint8_t hid_of(char c) {
    if (c >= 'a' && c <= 'z') return 0x04 + c - 'a';
    if (c >= '1' && c <= '9') return 0x1E + c - '1';
    if (c == '0') return 0x27;
    if (c == ' ') return 0x2C;
    if (c == '\n') return 0x28;
    return 0;
}

static void type(fw_t *f, const char *s) {
    for (; *s; s++) {
        uint8_t k = *s == ':' ? 0x33 : hid_of(*s), m = *s == ':' ? 0x02 : 0;
        f->hid(k, m, false, false); run(f, 2, 1);
        f->hid(k, m, false, true); run(f, 2, 1);
    }
}

static void dump(fw_t *f) {
    for (int y = 0; y < 64; y += 2) {
        for (int x = 0; x < 128; x++) {
            int a = f->screen[y * 128 + x], b = f->screen[(y + 1) * 128 + x];
            putchar(a && b ? '8' : a ? '"' : b ? '_' : ' ');
        }
        putchar('\n');
    }
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    const char *so = argc > 1 ? argv[1] : "./teletype5.so";
    const char *dir = argc > 2 ? argv[2] : "/tmp";
    char p1[256], p2[256];
    snprintf(p1, sizeof p1, "%s/tt_a.so", dir);
    snprintf(p2, sizeof p2, "%s/tt_b.so", dir);
    static fw_t a, b;
    load(&a, so, p1);
    load(&b, so, p2);
    run(&a, 3000, 1); run(&b, 3000, 1);  // boot splash
    a.hidConnect(); b.hidConnect();
    run(&a, 50, 1); run(&b, 50, 1);
    type(&a, "cv 1 v 3\n");
    type(&b, "cv 1 v 7\ntr 1 1\n");
    run(&a, 100, 1); run(&b, 100, 1);
    // CV1 = DAC 2, TR1 = B08 (monome-rack TeletypeModule.cpp)
    printf("A: CV1 dac=%u (%.3f V)  TR1=%d\n", a.getDAC(2), 10.0 * a.getDAC(2) / 65535, a.getGPIO(B08));
    printf("B: CV1 dac=%u (%.3f V)  TR1=%d\n", b.getDAC(2), 10.0 * b.getDAC(2) / 65535, b.getGPIO(B08));
    puts("A screen:");
    dump(&a);
    // step cost: 10 s simulated, 1 pump/ms vs 48 pumps/ms (per sample at 48 kHz)
    double t0 = now();
    run(&a, 10000, 1);
    double t1 = now();
    run(&a, 10000, 48);
    double t2 = now();
    printf("10 s simulated: 1 pump/ms %.1f ms CPU, 48 pumps/ms %.1f ms CPU\n",
           (t1 - t0) * 1e3, (t2 - t1) * 1e3);
    // worst case: one live command; Enter executes on the first step after the key event
    type(&a, "l 1 16000: cv 2 i");
    t0 = now();
    a.hid(0x28, 0, false, false);
    double in_hid = now() - t0;
    double worst = 0;
    for (int i = 0; i < 50; i++) {
        a.irq(0);
        t0 = now();
        a.step();
        t1 = now();
        if (t1 - t0 > worst) worst = t1 - t0;
    }
    printf("L 1 16000: CV 2 I: in hidMessage %.2f ms, slowest step after %.2f ms, CV2 dac=%u\n", in_hid * 1e3, worst * 1e3, a.getDAC(3));
    dlclose(a.h); dlclose(b.h);
    remove(p1); remove(p2);
    return 0;
}
