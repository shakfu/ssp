# tree-sitter-sapf

A token grammar for sapf, written for `sapf`'s code view: `source_file` is a sequence of tokens, split as sapf's
own lexer splits them (`src/engine/Parser.cpp` in [shakfu/sapf](https://github.com/shakfu/sapf)). It is not a
port of [bsssssss/tree-sitter-sapf](https://github.com/bsssssss/tree-sitter-sapf), which is a stub.

`src/` is generated from `grammar.js` with `tree-sitter-cli` 0.27.0 (language ABI 15). `queries/highlights.scm`
colours comments, strings, numbers, each-operators (`@`), quoted, backquoted, `.`, `,` and `:` symbols, `=` and
`\`, and brackets. Words are plain in the query, which cannot tell a built-in from a name: the `sapf` module colours
built-ins from the list its interpreter holds after the prelude (`Session::words`, `Highlighter::setWords`).

To change it, edit `grammar.js`, run `npx tree-sitter-cli@0.27.0 generate` here, and run `make test`.
