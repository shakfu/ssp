# Changelog

chuck follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(CHUCK VERSION ...)`) and shown at the top right of the screen.

## [Unreleased]

A USB keyboard, or TCP port 7774, edits the program in a Code view. Ctrl-Enter compiles the edited text in place of the program without writing its file; Ctrl-S writes it. The preset stores the code run, so a patch keeps a live edit even when the file is unsaved or changed later. See [README.md](README.md#keyboard).

Ctrl-E runs the block around the cursor as a shred beside the program. Ctrl-1 to Ctrl-0 remove one by its place in the list, Ctrl-- the newest, and Ctrl-. every shred. Removal goes through the VM's message queue, so audio continues; adding a shred stops audio for its compile, as Ctrl-Enter does.

## [0.1.0] - 2026-10-10

First release, as the module `chuk`. ChucK 1.5.5.8, linked in. Runs a `.ck` chosen with Load, or a built-in drone. Eight audio inputs and outputs, globals `p1`..`p16`, and MIDI notes. A failed compile keeps the previous program running. A comment line `@pN label [min max [unit]] [log]` names a control and sets its range; the editor shows only declared controls. Four example programs in `examples/`. See [README.md](README.md).

`cv N` at the end of an `@pN` line adds input N to the control as CV, every 10 ms: an octave per volt on a `log` range, else a tenth of the range per volt. See [README.md](README.md#labels-and-ranges).

MIDI notes from the general panel's input reach the program as the globals `midiNotes`, `midiCount` and `midiEvent`, not through `MidiIn`, which opens an ALSA device itself and so ignores the panel. `examples/midi.ck` reads them. See [README.md](README.md#midi).

Deleting the last `chuk` crashed Synthor. ChucK's standard library starts a keyboard-polling thread (`KBHit`) once per process, and only `ChucK::globalCleanup()` stops it, without joining it. When Synthor unloaded `chuk.so`, that thread woke in unmapped code. ChucK is now built without `KBHit` and `Hid`, as ChucK's own `__CHIP_MODE__` does; `Hid`'s input thread has the same lifetime. A program that uses either fails to compile. The last `chuk` to close also runs `ChucK::globalCleanup()`, as `chuck-max` and `pd-chuck` do. That alone would not be enough here: it signals the `KBHit` thread without joining it, and Max and Pd keep an external loaded, where Synthor unloads it.
