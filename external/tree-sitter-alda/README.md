# tree-sitter-alda

The Alda grammar from [shakfu/psnd](https://github.com/shakfu/psnd), `source/thirdparty/tree-sitter-grammars/tree-sitter-alda`
at 421cf5a (2026-10-13), the commit `scripts/fetch_alda.sh` pins. Copied unchanged for `alda`'s code view. psnd
is GPL-3; the grammar has no licence file of its own.

`src/` is generated from `grammar.js` with `tree-sitter-cli` 0.25.10 (language ABI 14). `queries/highlights.scm`
captures leaf nodes only: the code view, as psnd, colours a character by the first capture that covers it.

Changes belong upstream in psnd; copy them here again, and run `make test`.
