// Highlighter with the ChucK, Faust, Csound, sapf and Alda grammars and their highlights.scm; built and run
// by test_keys.py. usage: highlight_test chuck.scm faust.scm csound.scm sapf.scm alda.scm [dump]

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "keys/Highlight.h"

extern "C" const TSLanguage *tree_sitter_chuck(void), *tree_sitter_faust(void), *tree_sitter_csound(void),
    *tree_sitter_sapf(void), *tree_sitter_alda(void);

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond); \
            failures++;                                           \
        }                                                         \
    } while (0)

using ssp::Highlighter;

// the kind of `word`'s first character in line `row`
static int kindAt(const Highlighter& h, const ssp::TextBuffer& b, int row, const char* word) {
    size_t c = b.lines()[size_t(row)].find(word);
    return c == std::string::npos ? -1 : h.kinds()[size_t(row)][c];
}

static std::string slurp(const char* path) {
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// Faust's query is nvim-treesitter's, whose later patterns override earlier ones: `process` is a builtin
// only so. Csound's is a token grammar; its section tags are keywords.
static void testFaustAndCsound(const char* faust, const char* csound) {
    Highlighter f(tree_sitter_faust(), slurp(faust).c_str(), true);
    CHECK(f.error().empty());
    ssp::TextBuffer b;
    b.setText("// c\nimport(\"stdfaust.lib\");\nprocess = os.osc(440) * 0.1 <: _, _;");
    f.update(b);
    CHECK(kindAt(f, b, 0, "// c") == Highlighter::COMMENT);
    CHECK(kindAt(f, b, 1, "import") == Highlighter::KEYWORD && kindAt(f, b, 1, "\"std") == Highlighter::STRING);
    CHECK(kindAt(f, b, 2, "process") == Highlighter::BUILTIN && kindAt(f, b, 2, "osc") == Highlighter::FUNCTION);
    CHECK(kindAt(f, b, 2, "440") == Highlighter::NUMBER && kindAt(f, b, 2, "*") == Highlighter::OPERATOR);
    Highlighter first(tree_sitter_faust(), slurp(faust).c_str());
    first.update(b);
    CHECK(kindAt(first, b, 2, "process") == Highlighter::PLAIN);  // the catch-all identifier pattern, first

    Highlighter c(tree_sitter_csound(), slurp(csound).c_str());
    CHECK(c.error().empty());
    b.setText("<CsoundSynthesizer>\n<CsInstruments>\n; c\n0dbfs = 1\ninstr 1\n  asig oscili 0.5, 440\n"
              "  outs asig, asig\nendin\n</CsInstruments>\n<CsScore>\ni 1 0 1\n</CsScore>");
    c.update(b);
    for (int r : { 0, 1, 8, 9, 11 }) CHECK(c.kinds()[size_t(r)][0] == Highlighter::KEYWORD);
    CHECK(kindAt(c, b, 2, "; c") == Highlighter::COMMENT && kindAt(c, b, 3, "0dbfs") == Highlighter::BUILTIN);
    CHECK(kindAt(c, b, 4, "instr") == Highlighter::KEYWORD && kindAt(c, b, 5, "oscili") == Highlighter::FUNCTION);
    CHECK(kindAt(c, b, 5, "asig") == Highlighter::PLAIN && kindAt(c, b, 5, "0.5") == Highlighter::NUMBER);
    // one-character numbers, which tied with the grammar's catch-all token
    CHECK(kindAt(c, b, 3, "1") == Highlighter::NUMBER && kindAt(c, b, 10, "0") == Highlighter::NUMBER);
    CHECK(c.kinds()[3][7] == Highlighter::PLAIN);  // the space before `1`, which the catch-all token took in
}

// sapf's token grammar, against sapf's lexer (Parser.cpp): a number ends at a word's end, so 2x and 1/
// are words; scale suffixes and ratios are numbers; prefixed symbols
static void testSapf(const char* query) {
    Highlighter h(tree_sitter_sapf(), slurp(query).c_str());
    CHECK(h.error().empty());
    ssp::TextBuffer b;
    b.setText("; c\n440 .4 -2 4c 1h 3pi 1/2 0xff 2x 1/ +/ sinosc\n'sym `fn o.out ,x :key \"s\"\"q\" @ @@ @2 @01\n"
              "\\a b [a b *] = f #[1 2] {:k 1}");
    h.update(b);
    CHECK(kindAt(h, b, 0, "; c") == Highlighter::COMMENT);
    for (const char* n : { "440", ".4", "-2", "4c", "1h", "3pi", "1/2", "0xff" }) CHECK(kindAt(h, b, 1, n) == Highlighter::NUMBER);
    for (const char* w : { "2x", "1/ ", "+/", "sinosc" }) CHECK(kindAt(h, b, 1, w) == Highlighter::PLAIN);
    CHECK(h.kinds()[1].substr(b.lines()[1].find("1/2"), 3) == std::string(3, char(Highlighter::NUMBER)));
    CHECK(kindAt(h, b, 2, "'sym") == Highlighter::NUMBER);  // @constant
    CHECK(kindAt(h, b, 2, "`fn") == Highlighter::FUNCTION && kindAt(h, b, 2, ".out") == Highlighter::FUNCTION);
    CHECK(kindAt(h, b, 2, "o.") == Highlighter::PLAIN && kindAt(h, b, 2, ",x") == Highlighter::FUNCTION);
    CHECK(kindAt(h, b, 2, ":key") == Highlighter::TYPE && kindAt(h, b, 2, "\"s") == Highlighter::STRING);
    CHECK(kindAt(h, b, 2, "q\"") == Highlighter::STRING);  // "" inside a string
    for (const char* e : { "@ ", "@@", "@2", "@01" }) CHECK(kindAt(h, b, 2, e) == Highlighter::OPERATOR);
    CHECK(kindAt(h, b, 3, "\\") == Highlighter::KEYWORD && kindAt(h, b, 3, "= f") == Highlighter::KEYWORD);
    CHECK(kindAt(h, b, 3, "#[") == Highlighter::PUNCTUATION && kindAt(h, b, 3, "[a") == Highlighter::PUNCTUATION);

    // built-in words in their own kind; other words, and the same text as another token, stay as they were
    h.setWords("word", { "sinosc", "+/", "sym" }, Highlighter::BUILTIN);
    h.update(b);
    CHECK(kindAt(h, b, 1, "sinosc") == Highlighter::BUILTIN && kindAt(h, b, 1, "+/") == Highlighter::BUILTIN);
    CHECK(kindAt(h, b, 1, "2x") == Highlighter::PLAIN && kindAt(h, b, 2, "'sym") == Highlighter::NUMBER);
}

// psnd's Alda query: parts are types, octaves keywords, rests constants
static void testAlda(const char* query) {
    Highlighter h(tree_sitter_alda(), slurp(query).c_str());
    CHECK(h.error().empty());
    ssp::TextBuffer b;
    b.setText("# c\n(tempo! 120)\npiano: o4 c8 d+ > e r4 | c/e/g");
    h.update(b);
    CHECK(kindAt(h, b, 0, "# c") == Highlighter::COMMENT && kindAt(h, b, 1, "120") == Highlighter::NUMBER);
    CHECK(kindAt(h, b, 2, "piano") == Highlighter::TYPE && kindAt(h, b, 2, "o4") == Highlighter::KEYWORD);
    CHECK(kindAt(h, b, 2, "8") == Highlighter::NUMBER && kindAt(h, b, 2, "+") == Highlighter::OPERATOR);
    CHECK(kindAt(h, b, 2, ">") == Highlighter::KEYWORD && kindAt(h, b, 2, "r4") == Highlighter::NUMBER);
    CHECK(kindAt(h, b, 2, "|") == Highlighter::PUNCTUATION);
}

int main(int argc, char** argv) {
    if (argc < 6) return 2;
    std::stringstream q;
    q << slurp(argv[1]);
    Highlighter h(tree_sitter_chuck(), q.str().c_str());
    CHECK(h.error().empty());
    ssp::TextBuffer b;
    b.setText("// a comment\nSinOsc s => dac;\n1.5 => float x;\nfoo => now;\n\"str\" => string t;\n"
              "fun void go() { spork ~ go(); }\nif (x > 1) { <<< \"x\", x >>>; }");
    h.update(b);
    if (argc > 6)
        for (size_t r = 0; r < b.lines().size(); r++) {
            std::printf("%s\n", b.lines()[r].c_str());
            for (char k : h.kinds()[r]) std::printf("%d", int(k));
            std::printf("\n");
        }
    CHECK(kindAt(h, b, 0, "comment") == Highlighter::COMMENT);
    CHECK(kindAt(h, b, 1, "SinOsc") == Highlighter::TYPE);  // #any-of? applied
    CHECK(kindAt(h, b, 1, "dac") == Highlighter::TYPE);
    CHECK(kindAt(h, b, 1, "=>") == Highlighter::OPERATOR);
    CHECK(kindAt(h, b, 2, "1.5") == Highlighter::NUMBER);
    CHECK(kindAt(h, b, 3, "foo") == Highlighter::PLAIN);  // #any-of? rejects it
    CHECK(kindAt(h, b, 3, "now") == Highlighter::BUILTIN);
    CHECK(kindAt(h, b, 4, "\"str\"") == Highlighter::STRING);
    CHECK(kindAt(h, b, 6, "if") == Highlighter::KEYWORD);
    // a change reparses; the kinds follow the new text
    b.setText("1 => int y;");
    h.update(b);
    CHECK(h.kinds().size() == 1 && kindAt(h, b, 0, "1") == Highlighter::NUMBER);
    // Incremental reparses give the kinds a fresh parse gives, through random keys: text, quotes,
    // comment marks, brackets, Enter with indent, and Backspace and Delete across line ends
    b.setText("// seq\nSinOsc s => dac;\nwhile (true) {\n    440 => s.freq;\n    \"x\" => string t;\n}");
    h.update(b);
    ssp::TextBuffer copy;
    Highlighter fresh(tree_sitter_chuck(), q.str().c_str());
    const uint8_t keys[] = { 0x04, 0x16, 0x2C, 0x2C, 0x34, 0x38, 0x38, 0x2F, 0x30, 0x28, 0x28, 0x2A, 0x2A,
                             0x4C, 0x4C, 0x50, 0x4F, 0x52, 0x51, 0x27, 0x37, 0x2E, 0x26, 0x2B, 0x4A, 0x4D };
    uint32_t seed = 7;
    int checked = 0;
    for (int i = 0; i < 4000; i++) {
        seed = seed * 1103515245u + 12345u;
        b.key(keys[(seed >> 16) % sizeof(keys)], (seed >> 8) & 1 ? ssp::TextBuffer::SHIFT : 0);
        // the lines around the cursor, as the code view queries them
        const int first = std::max(b.row() - 3, 0), last = std::min(b.row() + 3, int(b.lines().size()) - 1);
        h.update(b, first, last);
        if (i % 5) continue;
        copy.setText(b.text());
        fresh.update(copy);
        checked++;
        bool same = true;
        for (int r = first; r <= last; r++) same = same && fresh.kinds()[size_t(r)] == h.kinds()[size_t(r)];
        if (!same) {
            std::printf("FAILED: key %d differs from a fresh parse of:\n%s\n", i, b.text().c_str());
            failures++;
            break;
        }
    }
    CHECK(checked == 800);

    // queried lines only; a comment that starts above them colours them
    b.setText("/* one\ntwo\nthree */\n1 => int a;\n2 => int b;");
    h.update(b, 1, 3);
    CHECK(h.kinds()[1][0] == Highlighter::COMMENT && h.kinds()[2][0] == Highlighter::COMMENT);
    CHECK(h.kinds()[3][0] == Highlighter::NUMBER);
    CHECK(h.kinds()[0][0] == Highlighter::PLAIN && h.kinds()[4][0] == Highlighter::PLAIN);  // not queried
    h.update(b, 4, 4);  // the text unchanged: line 3 keeps its kinds
    CHECK(h.kinds()[4][0] == Highlighter::NUMBER && h.kinds()[3][0] == Highlighter::NUMBER);

    // a query that does not compile reports it, and leaves everything plain
    Highlighter bad(tree_sitter_chuck(), "(nosuchnode) @comment");
    CHECK(!bad.error().empty());
    ssp::TextBuffer one;
    one.setText("1 => int y;");
    bad.update(one);
    CHECK(bad.kinds().size() == 1 && bad.kinds()[0] == std::string(one.lines()[0].size(), char(Highlighter::PLAIN)));
    testFaustAndCsound(argv[2], argv[3]);
    testSapf(argv[4]);
    testAlda(argv[5]);
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
