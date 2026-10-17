# tree-sitter-faust (vendored)

The Faust grammar for `fstr`'s code view. Upstream is
[khiner/tree-sitter-faust](https://github.com/khiner/tree-sitter-faust) at commit
`6074204d3511392fd0b9c4fa1cef347ded65b246` (2026-07-11), MIT, language ABI 15. `src/parser.c`,
`src/tree_sitter/` and `queries/highlights.scm` are copied unchanged. `plugins/faust/CMakeLists.txt` compiles
the parser and embeds the query.

To update, copy those files from a newer commit and run `make test`.
