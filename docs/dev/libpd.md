# A libpd module

Status, 2026-10-27: proposal; nothing built. `TODO.md` (Medium) lists it. Based on `ScriptEngine.h`, `CsoundEngine.h`, [engines.md](engines.md) and the `csnd` build; libpd has not been built for armv7l.

## Recommendation

A vanilla-only libpd `ScriptEngine`, built with `PDINSTANCE`, as a patch player first:

1. Load a patch; controls, audio, MIDI in and out.
2. Ctrl-E as a message box (see [Alternatives](#alternatives)).

Externals wait until target patches show which ones are needed.

## Fit with the engine layer

- `ScriptEngine` already supplies loading, `p1`..`p16`, `@pN` specs, keep-previous-on-failure and the `ReloadGate` swap. A `PdEngine` would mirror `CsoundEngine.cpp` (263 lines).
- Controls: `[r p1]`..`[r p16]`, fed by `libpd_float` once per block.
- Audio: In 1..8 and Out 1..8 are `[adc~ 1..8]` and `[dac~ 1..8]`.
- MIDI in: `libpd_noteon` and related calls.
- MIDI out: libpd's hooks (`libpd_set_noteonhook`, ...). `csnd` has no MIDI out. Unchecked: whether `ScriptProcessor` can pass MIDI to the host; `gseq` already sends MIDI.
- Blocks: `libpd_process_float(ticks, in, out)` runs 64-frame ticks on interleaved buffers ([z_libpd.h](https://github.com/libpd/libpd/blob/master/libpd_wrapper/z_libpd.h)). A host block that is not a multiple of 64 needs a FIFO, as `csnd`'s k-cycle FIFO.
- Build: a static archive from `scripts/build_deps.sh`, added in `plugins/CMakeLists.txt` only when it exists, as for csound and chuck.
- Abstractions: add the patch's folder with `libpd_add_to_search_path`.

## Risks

1. **Global state.** Pd keeps its state in process globals. Two `pd` modules in one Synthor process, or in one `rack`, would share one DSP graph; Csound instances do not. libpd's fix: build with `PDINSTANCE=1` and `PDTHREADS=1`, and use `libpd_new_instance` / `libpd_set_instance` ([libpd README](https://github.com/libpd/libpd#readme)). Every worker and audio call then sets its instance first. Needs a two-instance ThreadSanitizer test, as for the existing instance swaps.
2. **Load failure.** Pd opens a broken patch: missing objects print "couldn't create" and the rest runs. "A program that fails to compile leaves the previous one running" needs a rule, such as: any create error from the print hook fails the load.
3. **No editor.** A `.pd` file is serialized graph data. The shared script editor, highlighting and the Ctrl-E block model mostly do not apply. Patches are written on a desktop and copied to the card.
4. **Externals.** libpd ships vanilla plus `extra/` (`bonk~`, `sigmund~`, `expr~`, ...). Most real patches use ELSE or cyclone, which would need armv7l builds. Without them many existing patches will not open; with them the build and licence work grows. plugdata bundles both but is GPL-3 ([plugdata](https://github.com/plugdata-team/plugdata)). `expr~` is believed BSD now; unverified.
5. **Control labels.** `@pN` lines come from Pd comments (`#X text ... @p1 cutoff 20 20000 Hz log;`). Pd escapes commas and semicolons in comments, so `declared()` needs an override, not the default `parseSpecs`.
6. **Overlap.** Six live-coding modules exist (`csnd`, `chuk`, `fstr`, `scsy`, `sapf`, `ttyp`). Pd adds its existing patches and visual-patching users, not new DSP. That case depends on risk 4.

## Alternatives

- **Heavy, ahead of time** ([hvcc](https://github.com/Wasted-Audio/hvcc)). Compiles a patch to C, built as a normal module: what `chrs` is for Faust. No runtime, no shared globals, predictable CPU. Loses dynamic patching, most externals and loading on the device.
- **Ctrl-E as a message box.** Send the block as Pd messages (`; pd dsp 1`, `; pd-main.pd obj 10 10 osc~ 440`). This is dynamic patching, a niche form of live coding, and reuses `runPart` unchanged.
- **Reload on file change.** Edit in desktop Pd, `scp` to the card; the module reloads when the file's mtime changes. Likely the best edit loop, and it would serve `csnd` and `chuk` too.

## Open questions

1. Does Synthor share one `.so` load between two module instances, and does `rack`? If so, `PDINSTANCE` is required from the start.
2. Which patches are the targets, and do they need ELSE or cyclone? If so, externals and licensing are most of the work, not libpd.
3. Must patches run unchanged on the device, or is ahead-of-time compilation enough? The second points to Heavy, not libpd.
