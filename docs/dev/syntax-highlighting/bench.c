// Times tree-sitter on a file: full parse, a one-character incremental reparse, and a highlight query.
// usage: bench chuck|faust|csound FILE QUERY.scm; see ../syntax-highlighting.md
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tree_sitter/api.h"
const TSLanguage *tree_sitter_chuck(void), *tree_sitter_faust(void), *tree_sitter_csound(void);
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv) {
    const TSLanguage *lang = !strcmp(argv[1], "chuck") ? tree_sitter_chuck() : !strcmp(argv[1], "faust") ? tree_sitter_faust() : tree_sitter_csound();
    FILE *f = fopen(argv[2], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *src = malloc(n + 2); if (fread(src, 1, n, f) != (size_t)n) return 1; src[n] = 0; fclose(f);
    FILE *q = fopen(argv[3], "rb"); fseek(q, 0, SEEK_END); long qn = ftell(q); rewind(q);
    char *qs = malloc(qn + 1); if (fread(qs, 1, qn, q) != (size_t)qn) return 1; qs[qn] = 0; fclose(q);
    TSParser *p = ts_parser_new(); ts_parser_set_language(p, lang);
    int lines = 0; for (long i = 0; i < n; i++) lines += src[i] == '\n';
    const int N = 50;
    double t0 = now(); TSTree *tree = NULL;
    for (int i = 0; i < N; i++) { if (tree) ts_tree_delete(tree); tree = ts_parser_parse_string(p, NULL, src, n); }
    double full = (now() - t0) / N;
    TSNode root = ts_tree_root_node(tree);
    // insert a space at the middle, then reparse incrementally
    long at = n / 2; char *ed = malloc(n + 2); memcpy(ed, src, at); ed[at] = ' '; memcpy(ed + at + 1, src + at, n - at);
    TSPoint pt = {0, 0}; for (long i = 0; i < at; i++) { if (src[i] == '\n') { pt.row++; pt.column = 0; } else pt.column++; }
    t0 = now();
    for (int i = 0; i < N; i++) {
        TSTree *c = ts_tree_copy(tree);
        TSInputEdit e = { at, at, at + 1, pt, pt, { pt.row, pt.column + 1 } };
        ts_tree_edit(c, &e);
        TSTree *t2 = ts_parser_parse_string(p, c, ed, n + 1);
        ts_tree_delete(c); ts_tree_delete(t2);
    }
    double inc = (now() - t0) / N;
    uint32_t eo; TSQueryError et; TSQuery *query = ts_query_new(lang, qs, qn, &eo, &et);
    double qt = -1; unsigned caps = 0;
    if (query) {
        TSQueryCursor *cur = ts_query_cursor_new(); t0 = now();
        for (int i = 0; i < N; i++) { ts_query_cursor_exec(cur, query, root); TSQueryMatch m; uint32_t ci; caps = 0;
            while (ts_query_cursor_next_capture(cur, &m, &ci)) caps++; }
        qt = (now() - t0) / N;
    } else fprintf(stderr, "query error %d at %u\n", et, eo);
    printf("%-6s %5d lines %6ld B  full %7.3f ms  incr %7.3f ms  query %7.3f ms  %u captures  errors %s\n", argv[1], lines, n,
           full, inc, qt, caps, ts_node_has_error(root) ? "YES" : "no");
    return 0;
}
