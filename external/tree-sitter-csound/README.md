# tree-sitter-csound (vendored)

The Csound grammar for `csnd`'s code view, from [psnd](https://github.com/shakfu/psnd)
`source/thirdparty/tree-sitter-grammars/tree-sitter-csound` at commit
`bbf5f236e34c5362e0ac0dc1dcd1e3b3552b3713` (2026-01-30), by the author of this repository.
`grammar.js` and `queries/highlights.scm` are copied, and `src/` is generated with `tree-sitter-cli` 0.27.0
(language ABI 15). `grammar.js` differs from psnd's in two places, which psnd also needs:

- `xml_tag` matches `<Cs...>`: psnd's `<\/?Csound(Synthesizer|Options|Instruments|Score)>` matched only
  `<CsoundSynthesizer>`, so `<CsInstruments>` lexed as `<`, an identifier and `>`.
- The catch-all token after the others is `token(prec(-1, /[^ \t\n]/))`, and newline `token(prec(-1, /\n/))`.
  psnd's `/./` tied with one-character tokens and took them, so `1` and `x` were plain; and it matched spaces,
  which then joined the next token.

It is a token grammar: `source_file` is a sequence of tokens (keywords, opcodes, variables, numbers, strings,
comments), so the tree has no `instr` blocks. `plugins/csound/CMakeLists.txt` compiles the parser and embeds
the query.

To change it, edit `grammar.js`, run `tree-sitter generate` here, and run `make test`.
