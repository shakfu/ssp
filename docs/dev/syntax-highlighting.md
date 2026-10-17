# Syntax highlighting in the code view

Investigation, 2026-10-16: tree-sitter grammars for the keyboard modules, their size and speed, and what the code view needs to use them. Since built for `chuk`, `csnd`, `fstr` and `sapf`: `Highlighter` (`plugins/common/keys/Highlight.cpp`), `ssp_highlights()` (`plugins/common/keys/CMakeLists.txt`), and the runtime and grammars in `external/`. sapf's grammar, `external/tree-sitter-sapf`, is new: a token grammar after sapf's own lexer. A token grammar cannot tell built-in words from the code's own, so `sapf` passes its interpreter's words (built-ins and the prelude's, over 500) to `Highlighter::setWords`.

## Scope

| Module | Editor | Highlighting |
|-|-|-|
| `chuk`, `csnd`, `fstr` | `ScriptEditor`'s Code view | yes |
| `sapf` | its own editor | yes |
| `ttyp` | none: teletype's firmware draws the screen | no |

## Grammars

| Language | Grammar | License | State |
|-|-|-|-|
| ChucK | [DBraun/tree-sitter-chuck](https://github.com/DBraun/tree-sitter-chuck) | MIT | ABI 15, no external scanner. Parses `plugins/chuck/examples` without errors. Its `highlights.scm` uses `#any-of?`. |
| Faust | [khiner/tree-sitter-faust](https://github.com/khiner/tree-sitter-faust) | MIT | ABI 15, no external scanner, used by nvim-treesitter. Parses `plugins/faust/examples` without errors. No predicates in `highlights.scm`. |
| Csound | psnd's `source/thirdparty/tree-sitter-grammars/tree-sitter-csound` | the author's own; psnd is GPL-3 | A token grammar: `source_file` is `repeat($._token)`, so the tree holds tokens, not `instr` blocks. Parses `plugins/csound/examples` without errors. |
| sapf | `external/tree-sitter-sapf`, written here | this repository's | A token grammar after sapf's lexer (`src/engine/Parser.cpp`). [bsssssss/tree-sitter-sapf](https://github.com/bsssssss/tree-sitter-sapf) is a stub (`source_file: $ => "hello"`). |

Rejected:

- [tymbalodeon/tree-sitter-chuck](https://github.com/tymbalodeon/tree-sitter-chuck): ABI 14, an 835-byte `highlights.scm`.
- [caelia/tree-sitter-chuck](https://github.com/caelia/tree-sitter-chuck): no generated `parser.c`.
- [PasqualeMainolfi/tree-sitter-csound](https://github.com/PasqualeMainolfi/tree-sitter-csound): a full Csound grammar, but it has no license, so it cannot be vendored. Its `parser.c` is 6.6 MB and it needs an external scanner.

The repository is AGPL-3, which can include MIT code. The grammars' generated `parser.c` are committed upstream, so the build needs no tree-sitter CLI. Only a new grammar, such as sapf's, needs the CLI, and only to generate its `parser.c`.

## Size and speed

The runtime is the copy vendored in psnd, `source/thirdparty/tree-sitter/lib`: language ABI 15, compatible down to 13. It was built with the SSP's gcc 8.4, `-O2 -std=gnu11`. Strict `-std=c11` hides `le16toh` and `clock_gettime`.

ARM `.text` sizes:

| Part | KB |
|-|-|
| runtime (`lib.c`) | 157 |
| ChucK grammar | 107 |
| Faust grammar | 222 |
| psnd Csound grammar | 13 |

A plugin `.so` is about 14 MB, so a module grows by 1-3%.

[`syntax-highlighting/bench.c`](syntax-highlighting/bench.c) measures three things, each averaged over 50 runs:

- a full parse;
- a reparse after inserting one character at the middle of the file;
- a highlight query over the whole tree.

The input is each plugin's `examples/` concatenated. Host results (x86-64):

| Grammar | Lines | Full parse | Reparse | Query |
|-|-|-|-|-|
| ChucK | 129 | 1.07 ms | 0.17 ms | 0.64 ms |
| Faust | 106 | 0.27 ms | 0.03 ms | 0.18 ms |
| Csound | 136 | 0.25 ms | 0.18 ms | 0.14 ms |

SSP results (Cortex-A17), the same runs, with Synthor running. Inputs of four times the size are marked x4:

| Grammar | Lines | Full parse | Reparse | Query |
|-|-|-|-|-|
| ChucK | 129 | 7.6 ms | 1.5 ms | 5.1 ms |
| ChucK x4 | 516 | 30.5 ms | 5.1 ms | 20.8 ms |
| Faust | 106 | 2.4 ms | 0.2 ms | 1.4 ms |
| Faust x4 | 424 | 9.4 ms | 0.9 ms | 5.6 ms |
| Csound | 136 | 2.2 ms | 1.6 ms | 1.1 ms |
| Csound x4 | 544 | 8.9 ms | 6.3 ms | 4.7 ms |

The SSP runs 7-9x slower than the host. To repeat, build `bench.c` with the runtime and the grammars' `parser.c` for the SSP (`-O2 -std=gnu11`), and run `bench <grammar> FILE highlights.scm` there.

[`syntax-highlighting/highlight_bench.cpp`](syntax-highlighting/highlight_bench.cpp) times `Highlighter::update` after each key, as the code view runs it. On the SSP, ChucK, per key, mean and worst:

| Lines | Full reparse and query | Incremental reparse, full query | Incremental reparse, visible query (built) |
|-|-|-|-|
| 130 | 16.0 ms, 25.6 ms | 9.2 ms, 20.3 ms | 3.7 ms, 10.5 ms |
| 517 | 63.0 ms, 73.7 ms | 35.5 ms, 44.0 ms | 9.0 ms, 17.5 ms |

The editor's timer runs every 66 ms, and a held key repeats every 50 ms. Opening a file still costs a full parse: about 30 ms at 517 lines.

## Integration

1. **Predicates.** The C library parses `#any-of?`, `#eq?` and `#match?` but does not apply them. `Highlighter` applies `#any-of?`, which ChucK's query needs, and disables patterns with any other.
2. **Precedence.** Queries disagree on which pattern wins a node. DBraun's ChucK query ends with a catch-all `(identifier) @variable` "must be last": the first pattern wins, as in tree-sitter's own highlighter. khiner's Faust query is nvim-treesitter's: it starts with `(identifier) @variable`, and later patterns override it. `CodeKeys::lastWins` picks the order per grammar.
3. **Reparse.** Done: `TextBuffer` records each edit (`editsSince`), and `Highlighter` applies them to the last tree before reparsing. After `setText`, or more than 256 edits, it parses afresh.
4. **Query range.** Done: the code view queries a screen's rows (11) above and below the cursor's line, which holds every line on screen whatever the wrapping. `ts_query_cursor_set_point_range` includes nodes that start above the range, such as a block comment.
5. **Drawing.** `drawCode` draws one `drawText` per screen row. Colour needs one per token run, about 10x the calls per frame on JUCE's software renderer. This cost is not measured.
6. **One runtime per plugin.** Each `.so` links its own copy. Synthor loads plugins with local symbols (as `keys_test.cpp` models), so the copies do not clash.

## Lexer instead of tree-sitter

Highlighting alone needs only tokens. A lexer per language, about 100 lines each, avoids:

- 500 KB of code;
- predicate handling;
- following tree-sitter ABI versions.

psnd's Csound grammar already does only that. Tree-sitter pays off only if the tree is used:

- **Ctrl-E blocks from the tree.** A Csound `instr` with blank lines inside it would no longer split. This needs a structural Csound grammar, which psnd's is not.
- **Error marks while typing.**
- **Bracket matching.**

## Recommendation

Use tree-sitter, with these grammars:

- DBraun's for `chuk`;
- khiner's for `fstr`;
- psnd's for `csnd`;
- a new token grammar for `sapf`.

Start with full reparses. Decide on incremental parsing after the SSP timings.

Open questions:

- Is structure-aware Ctrl-E wanted? If so, a structural Csound grammar is needed: write one, or ask for a license on PasqualeMainolfi's.
- Should colours follow psnd's theme or a fixed palette on the black background?
