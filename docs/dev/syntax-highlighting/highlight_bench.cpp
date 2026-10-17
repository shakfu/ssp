// Times Highlighter::update after each key typed into a ChucK file, as the code view runs it once per
// timer tick after an edit. usage: highlight_bench FILE.ck highlights.scm; see ../syntax-highlighting.md
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "keys/Highlight.h"

extern "C" const TSLanguage* tree_sitter_chuck(void);

static std::string slurp(const char* path) {
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string query = slurp(argv[2]);
    ssp::Highlighter h(tree_sitter_chuck(), query.c_str());
    ssp::TextBuffer b;
    b.setText(slurp(argv[1]));
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    h.update(b);
    const double first = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    for (size_t r = 0; r < b.lines().size() / 2; r++) b.key(0x51, 0);  // down to the middle
    // a comment typed there, then deleted: a quote, letters, Enter, Backspace
    const uint8_t keys[] = { 0x34, 0x04, 0x05, 0x06, 0x2C, 0x07, 0x28, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A };
    double total = 0, worst = 0;
    const int rounds = 10;
    for (int i = 0; i < rounds; i++)
        for (uint8_t k : keys) {
            b.key(k, 0);
            t0 = Clock::now();
            h.update(b, b.row() - 11, b.row() + 11);  // as the code view: a screen's rows around the cursor
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            total += ms;
            if (ms > worst) worst = ms;
        }
    std::printf("%zu lines  first %.2f ms  per key %.2f ms  worst %.2f ms\n", b.lines().size(), first,
                total / (rounds * sizeof(keys)), worst);
    return 0;
}
