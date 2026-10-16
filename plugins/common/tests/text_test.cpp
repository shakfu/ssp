// TextBuffer's block for Ctrl-E with a language's comment marker; built and run by test_keys.py.

#include <cstdio>

#include "keys/TextBuffer.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond); \
            failures++;                                           \
        }                                                         \
    } while (0)

int main() {
    ssp::TextBuffer b;
    // ChucK: ; ends a statement, // starts a comment; a blank line inside braces is not a block's end
    b.setText("SinOsc s => dac;\n\n0 => int i; while (i < 4) {\n\n    1::second => now; // a { here\n}\n\n"
              "// }\n1 => int x;");
    int first, last;
    for (int r = 0; r < 4; r++) b.key(0x51, 0);  // down to the loop's blank line
    CHECK(b.block(first, last, "//") && first == 2 && last == 5);
    for (int r = 0; r < 4; r++) b.key(0x51, 0);  // the last line
    CHECK(b.block(first, last, "//") && first == 7 && last == 8);
    // with sapf's ;, the { follows a comment, so the blank line inside the loop ends a block
    for (int r = 0; r < 5; r++) b.key(0x52, 0);
    CHECK(b.block(first, last, ";") && first == 2 && last == 2);
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
