# tree-sitter-chuck (vendored)

The ChucK grammar for `chuk`'s code view. Upstream is
[DBraun/tree-sitter-chuck](https://github.com/DBraun/tree-sitter-chuck) at commit
`17ff3a818d3ab21ac0ccd78a514956eaeb546b35` (2025-11-27), MIT, language ABI 15. `src/parser.c`,
`src/tree_sitter/` and `queries/highlights.scm` are copied unchanged. `plugins/chuck/CMakeLists.txt` compiles
the parser and embeds the query. The query's `#any-of?` predicates are applied by `Highlighter`.

To update, copy those files from a newer commit and run `make test`.
