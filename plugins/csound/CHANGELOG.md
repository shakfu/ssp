# Changelog

csound follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(CSOUND VERSION ...)`) and shown at the top right of the screen.

## [Unreleased]

- Two examples from Csound's own: Kung's `xanadu.csd` and Boulanger's `trapped.csd`. Each has `massign 0, 0` added, so it ignores MIDI: in `xanadu.csd` a MIDI note started instrument 1 at octave 0, the vibrato took `cpsoct` below 0, which returns garbage in Csound, and `pluck` read outside its buffer and crashed. See [docs/dev/issues/261021-csound-cpsoct.md](../../docs/dev/issues/261021-csound-cpsoct.md).

- Csound 7.0.0, the release, replaces 7.0.0-beta.17. `scripts/build_deps.sh` turns off 7.0.0's new `USE_WASMTIME` option with the other optional libraries, and builds each version in its own directory, as CMake refuses a cache made from another source tree. Run `make deps` to rebuild.

- The tests' fallback build called `scripts/build_csound.sh`, which does not exist; it calls `scripts/build_deps.sh`.

- The first block after each compile created the `p1`..`p16` control channels on the audio thread, which allocates; they are created at compile time.

## [0.2.0] - 2026-10-18

A USB keyboard, or TCP port 7773, edits the program in a Code view. Ctrl-Enter compiles the edited text in place of the program without writing its file; Ctrl-S writes it. The preset stores the code run, so a patch keeps a live edit even when the file is unsaved or changed later. See [README.md](README.md#keyboard).

The code view colours Csound with a token grammar from psnd (`external/tree-sitter-csound`): section tags, keywords, opcodes, header variables, numbers, strings and comments.

Ctrl-E evaluates the block around the cursor in the running orchestra: score events inside `<CsScore>`, orchestra code (`csoundCompileOrc`) elsewhere. Global state and running notes survive, unlike Ctrl-Enter's full restart.

## [0.1.0] - 2026-10-10

First release, as the module `csnd`. Csound 7.0.0-beta.17, linked in. Runs a `.csd` chosen with Load, or a built-in drone. Eight audio inputs and outputs, controls `p1`..`p16`, and MIDI notes through Csound's MIDI opcodes. A failed compile keeps the previous program running. A comment line `@pN label [min max [unit]] [log]` names a control and sets its range; the editor shows only declared controls. Five example orchestras in `examples/`. The status panel counts MIDI notes. See [README.md](README.md).

`cv N` at the end of an `@pN` line adds input N to the control as CV, once per block: an octave per volt on a `log` range, else a tenth of the range per volt. See [README.md](README.md#labels-and-ranges).
