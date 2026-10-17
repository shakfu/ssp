#pragma once

#include <climits>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "keys/TextBuffer.h"

struct TSLanguage;
struct TSParser;
struct TSQuery;
struct TSQueryCursor;
struct TSTree;

namespace ssp {

// Highlight kinds for a TextBuffer from a tree-sitter grammar and its highlights.scm. No JUCE, so it
// tests natively. A node takes the first pattern that captures it, as tree-sitter's own highlighter
// does, or with lastWins the last, as nvim-treesitter does; a node inside it overrides it. Of the
// predicates, only #any-of? is applied: a pattern with another is disabled.
class Highlighter {
public:
    enum Kind : uint8_t { PLAIN, COMMENT, STRING, NUMBER, KEYWORD, TYPE, FUNCTION, BUILTIN, OPERATOR, PUNCTUATION };

    Highlighter(const TSLanguage* language, const char* query, bool lastWins = false);
    ~Highlighter();
    Highlighter(const Highlighter&) = delete;
    Highlighter& operator=(const Highlighter&) = delete;

    // the query's error, or empty when it compiled
    const std::string& error() const { return error_; }
    // Reparses `text` if it changed since the last call: from the last tree and the text's edits when it
    // kept them all, else afresh. Then queries lines first..last, if they or the text changed.
    void update(const TextBuffer& text, int first = 0, int last = INT_MAX);
    // Nodes of `type` whose text is in `words` take `kind`, over what the query gives them: a language's
    // built-in words, which the grammar cannot tell from others. Applies from the next update().
    void setWords(const char* type, std::set<std::string> words, Kind kind);

    // A Kind per byte, line by line: lines the last update() queried, and lines queried before it while
    // the text has not changed; the rest PLAIN
    const std::vector<std::string>& kinds() const { return kinds_; }

    static Kind kindOf(const std::string& capture);  // by the capture name's first part

private:
    struct AnyOf {
        uint32_t capture;
        std::set<std::string> values;
    };
    void query(uint32_t first, uint32_t last);

    TSParser* parser_ = nullptr;
    TSQuery* query_ = nullptr;
    TSQueryCursor* cursor_ = nullptr;
    TSTree* tree_ = nullptr;
    const bool lastWins_;
    std::vector<Kind> captureKinds_;
    std::vector<std::vector<AnyOf>> anyOf_;  // by pattern
    std::string error_, source_;
    unsigned version_ = ~0u;
    int queriedFirst_ = -1, queriedLast_ = -1;
    std::string wordType_;
    std::set<std::string> words_;
    Kind wordKind_ = PLAIN;
    std::vector<std::string> kinds_;
};

}  // namespace ssp
