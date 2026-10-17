# tree-sitter (vendored)

The tree-sitter runtime, for the code view's colours (`plugins/common/keys/Highlight.cpp`). Upstream is
[tree-sitter/tree-sitter](https://github.com/tree-sitter/tree-sitter) at tag `v0.27.0` (2026-08-30), MIT.
`lib/include` and `lib/src` are copied unchanged; the build compiles `lib/src/lib.c` alone, with `-std=gnu11`.
It reads grammars of language ABI 13 to 15.

To update, copy `lib/include` and `lib/src` from a newer tag and run `make test`.
