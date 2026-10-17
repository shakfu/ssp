// TextBuffer's block for Ctrl-E with a language's comment marker; built and run by test_keys.py.

#include <cstdio>
#include <vector>

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

    // edits, as tree-sitter takes them: bytes and points before and after
    b.setText("ab\ncd");
    unsigned v = b.version();
    std::vector<ssp::TextBuffer::Edit> e;
    b.key(0x51, 0);  // down: no edit
    CHECK(b.version() == v && b.editsSince(v, e) && e.empty());
    b.key(0x2A, 0);  // Backspace at the start of "cd" joins the lines
    b.key(0x05, 0);  // b
    CHECK(b.text() == "abbcd" && b.editsSince(v, e) && e.size() == 2);
    CHECK(e[0].start == 2 && e[0].oldEnd == 3 && e[0].newEnd == 2 && e[0].oldEndRow == 1 && e[0].oldEndCol == 0);
    CHECK(e[1].start == 2 && e[1].oldEnd == 2 && e[1].newEnd == 3 && e[1].newEndRow == 0 && e[1].newEndCol == 3);
    b.key(0x2A, 0), b.key(0x4A, 0), b.key(0x2A, 0);  // Backspace at the start of the text: no edit
    CHECK(b.editsSince(v, e) && e.size() == 3);
    b.setText("x");
    CHECK(!b.editsSince(v, e));  // setText came between
    v = b.version();
    for (size_t i = 0; i <= ssp::TextBuffer::MAX_EDITS; i++) b.key(0x1B, 0);
    CHECK(!b.editsSince(v, e));  // the first one is gone
    CHECK(b.editsSince(v + 1, e) && e.size() == ssp::TextBuffer::MAX_EDITS);
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
