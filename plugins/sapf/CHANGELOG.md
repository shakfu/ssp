# Changelog

sapf follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(SAPF VERSION ...)`).

## [Unreleased]

First version: sapf, through the fork [shakfu/sapf](https://github.com/shakfu/sapf), on the SSP for live coding from a USB keyboard or over ssh. sapf's own UGens make the sound; a thread of its own renders it 4 blocks (10.7 ms) ahead of the host's callback, so a render that runs late silences sapf and not the patch. A multi-line editor evaluates blocks with Ctrl-Enter; Ctrl-. interrupts and stops. The prelude loads at start, definitions persist, and the code is saved with the patch. Ctrl-O lists the files in `BOOT/sapf`, among them sapf's examples, which install with `sapf`. Eight inputs, read with `adc`, and eight outputs; level meters for inputs and outputs 1-2. GPL-3.0, as sapf.

The code is coloured by syntax, from a tree-sitter token grammar written for sapf (`external/tree-sitter-sapf`): comments, strings, numbers with their scale suffixes and ratios, each-operators, quoted, backquoted and form-key symbols, and `=` and `\`. Built-in words, sapf's and the prelude's, are cyan; words the code defines stay white. The grammar cannot tell them apart, so the session lists the words its interpreter holds once the prelude has loaded.

The keyboard moves between `sapf` and `ttyp` with the screen; see [docs/dev/sapf-sc-plugin.md](../../docs/dev/sapf-sc-plugin.md), Keys. The design is in [docs/dev/sapf-plugin.md](../../docs/dev/sapf-plugin.md).
