#include "keys/Highlight.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "tree_sitter/api.h"

namespace ssp {

Highlighter::Kind Highlighter::kindOf(const std::string& c) {
    const std::string head = c.substr(0, c.find('.'));
    if (head == "comment") return COMMENT;
    if (head == "string" || head == "character") return STRING;
    if (head == "number" || head == "boolean" || head == "constant") return NUMBER;
    if (head == "keyword" || head == "attribute") return KEYWORD;
    if (head == "type") return TYPE;
    if (head == "function" || head == "method" || head == "constructor") return FUNCTION;
    if (c.compare(0, 16, "variable.builtin") == 0) return BUILTIN;
    if (head == "operator") return OPERATOR;
    if (head == "punctuation") return PUNCTUATION;
    return PLAIN;
}

Highlighter::Highlighter(const TSLanguage* language, const char* query, bool lastWins) : lastWins_(lastWins) {
    parser_ = ts_parser_new();
    if (!ts_parser_set_language(parser_, language)) {
        error_ = "grammar ABI not supported";
        return;
    }
    uint32_t offset;
    TSQueryError type;
    query_ = ts_query_new(language, query, uint32_t(std::strlen(query)), &offset, &type);
    if (query_ == nullptr) {
        error_ = "query error " + std::to_string(int(type)) + " at byte " + std::to_string(offset);
        return;
    }
    for (uint32_t i = 0; i < ts_query_capture_count(query_); i++) {
        uint32_t len;
        const char* name = ts_query_capture_name_for_id(query_, i, &len);
        captureKinds_.push_back(kindOf(std::string(name, len)));
    }
    anyOf_.resize(ts_query_pattern_count(query_));
    for (uint32_t p = 0; p < ts_query_pattern_count(query_); p++) {
        uint32_t n;
        const TSQueryPredicateStep* s = ts_query_predicates_for_pattern(query_, p, &n);
        // each predicate: its name, its arguments, then a Done step
        for (uint32_t i = 0; i < n;) {
            uint32_t len;
            const char* name = ts_query_string_value_for_id(query_, s[i].value_id, &len);
            const bool anyOf = std::string(name, len) == "any-of?" && i + 1 < n &&
                               s[i + 1].type == TSQueryPredicateStepTypeCapture;
            AnyOf pred{ anyOf ? s[i + 1].value_id : 0, {} };
            uint32_t j = i + (anyOf ? 2 : 1);
            for (; j < n && s[j].type != TSQueryPredicateStepTypeDone; j++)
                if (anyOf && s[j].type == TSQueryPredicateStepTypeString) {
                    const char* v = ts_query_string_value_for_id(query_, s[j].value_id, &len);
                    pred.values.insert(std::string(v, len));
                }
            if (anyOf) anyOf_[p].push_back(std::move(pred));
            else ts_query_disable_pattern(query_, p);
            i = j + 1;
        }
    }
    cursor_ = ts_query_cursor_new();
}

void Highlighter::setWords(const char* type, std::set<std::string> words, Kind kind) {
    wordType_ = type;
    words_ = std::move(words);
    wordKind_ = kind;
    queriedFirst_ = queriedLast_ = -1;  // queries again, with the same text
}

Highlighter::~Highlighter() {
    if (tree_) ts_tree_delete(tree_);
    if (cursor_) ts_query_cursor_delete(cursor_);
    if (query_) ts_query_delete(query_);
    ts_parser_delete(parser_);
}

void Highlighter::update(const TextBuffer& text, int first, int last) {
    const bool changed = text.version() != version_;
    if (changed) {
        std::vector<TextBuffer::Edit> edits;
        const bool incremental = tree_ != nullptr && text.editsSince(version_, edits);
        version_ = text.version();
        kinds_.assign(text.lines().size(), std::string());
        for (size_t r = 0; r < kinds_.size(); r++) kinds_[r].assign(text.lines()[r].size(), char(PLAIN));
        if (cursor_ == nullptr) return;
        source_ = text.text();
        if (incremental)
            for (const auto& e : edits) {
                const TSInputEdit ie{ e.start, e.oldEnd, e.newEnd, { e.startRow, e.startCol },
                                      { e.oldEndRow, e.oldEndCol }, { e.newEndRow, e.newEndCol } };
                ts_tree_edit(tree_, &ie);
            }
        TSTree* tree = ts_parser_parse_string(parser_, incremental ? tree_ : nullptr, source_.data(),
                                              uint32_t(source_.size()));
        if (tree_) ts_tree_delete(tree_);
        tree_ = tree;
    }
    if (tree_ == nullptr || cursor_ == nullptr || kinds_.empty()) return;
    first = std::max(first, 0);
    last = std::min(last, int(kinds_.size()) - 1);
    if (!changed && first == queriedFirst_ && last == queriedLast_) return;
    queriedFirst_ = first, queriedLast_ = last;
    query(uint32_t(first), uint32_t(last));
}

void Highlighter::query(uint32_t first, uint32_t last) {
    for (uint32_t r = first; r <= last; r++) std::fill(kinds_[r].begin(), kinds_[r].end(), char(PLAIN));
    // per node, the first or last pattern that captures it; of one pattern's captures, the first
    struct Span {
        TSPoint start, end;
        uint32_t length, pattern;
        Kind kind;
    };
    std::map<std::pair<uint32_t, uint32_t>, Span> nodes;  // by start and end byte
    // the nodes that reach into the lines, including those that start above them
    ts_query_cursor_set_point_range(cursor_, { first, 0 }, { last + 1, 0 });
    ts_query_cursor_exec(cursor_, query_, ts_tree_root_node(tree_));
    TSQueryMatch m;
    while (ts_query_cursor_next_match(cursor_, &m)) {
        bool pass = true;
        for (const auto& pred : anyOf_[m.pattern_index])
            for (uint16_t c = 0; c < m.capture_count; c++) {
                if (m.captures[c].index != pred.capture) continue;
                const TSNode n = m.captures[c].node;
                const uint32_t a = ts_node_start_byte(n), b = ts_node_end_byte(n);
                if (!pred.values.count(source_.substr(a, b - a))) pass = false;
            }
        if (!pass) continue;
        for (uint16_t c = 0; c < m.capture_count; c++) {
            const TSNode n = m.captures[c].node;
            const auto key = std::make_pair(ts_node_start_byte(n), ts_node_end_byte(n));
            auto it = nodes.find(key);
            if (it != nodes.end() &&
                (lastWins_ ? it->second.pattern >= m.pattern_index : it->second.pattern <= m.pattern_index))
                continue;
            Kind kind = captureKinds_[m.captures[c].index];
            if (!words_.empty() && wordType_ == ts_node_type(n) &&
                words_.count(source_.substr(key.first, key.second - key.first)))
                kind = wordKind_;
            nodes[key] = { ts_node_start_point(n), ts_node_end_point(n), key.second - key.first, m.pattern_index, kind };
        }
    }

    // outer nodes first, so the nodes inside them paint over them
    std::vector<const Span*> order;
    for (const auto& kv : nodes) order.push_back(&kv.second);
    std::stable_sort(order.begin(), order.end(), [](const Span* x, const Span* y) { return x->length > y->length; });
    for (const Span* s : order) {
        for (uint32_t row = std::max(s->start.row, first); row <= s->end.row && row <= last; row++) {
            std::string& k = kinds_[row];
            const uint32_t from = row == s->start.row ? s->start.column : 0;
            const uint32_t to = row == s->end.row ? std::min<uint32_t>(s->end.column, uint32_t(k.size()))
                                                  : uint32_t(k.size());
            for (uint32_t c = from; c < to; c++) k[c] = char(s->kind);
        }
    }
}

}  // namespace ssp
