# Engine plugins

`radio`, `csound` and `chuck` are ports of engines from sk-engines and daisy-apps. They share `plugins/common/engine`; each plugin keeps only its engine and its mapping onto it.

## Layout

| File | Role | Used by |
|-|-|-|
| `Engine.h` | the DSP interface: `prepare`, `process`, `idle`. No JUCE, so engines test natively | all |
| `EngineProcessor.*` | copies inputs, splits blocks larger than prepared, runs `idle()` on a worker thread, measures DSP load | all |
| `EngineEditor.*` | parameter pages, status panel, file browser behind Load; a compact view for rack | all |
| `ReloadGate.h` | lock-free handoff of an instance between the worker and audio threads | csound, chuck |
| `ScriptEngine.*` | program loading, built-in fallback, controls `p1`..`p16` with the program's `@pN` labels and ranges, error text | csound, chuck |
| `ScriptProcessor.*` | eight in, eight out, `p1`..`p16`, the program path in the preset; editors that show only declared controls, rebuilt in `onSSPTimer` after a load | csound, chuck |

A plugin is an `Engine` subclass plus a `PluginProcessor` that derives from `EngineProcessor` or `ScriptProcessor`. `SSPApi.h` requires that class name.

## Naming

Synthor scans every `.so` in `plugins/` but lists only some. Every listed plugin's uid (`PLUGIN_CODE`) spells its name, ignoring case; `rdio` with uid `SFRD` was scanned (a `dlopen` trace on the device shows it) but not listed. So `PRODUCT_NAME` is at most four characters and `PLUGIN_CODE` is the name in capitals: `rdio`/`RDIO`, `csnd`/`CSND`, `chuk`/`CHUK`. The rule comes from the 39 plugins on the card; changing `rdio`'s uid from `SFRD` to `RDIO` made it appear.

## Threads

- Audio: `Engine::process`, and `EngineProcessor::control` before it. Never blocks or allocates.
- Worker (`EngineProcessor::IDLE_MS`, on the UI core): `Engine::idle`. File reads, compiles, ChucK globals.
- Message: `prepare` and state restore. `prepare` holds the worker's lock.

`radio` hands each open station to the audio thread as a `Stream` through an atomic pointer, and gets the old one back through another; the worker frees it. Csound and ChucK swap their instance behind `ReloadGate`.

## Libraries

`scripts/build_deps.sh ssp|host` builds libsndfile, Csound and the ChucK core as static PIC archives into `build/deps/<target>`. libsndfile is built because the buildroot's `libsndfile.a` is not PIC. `plugins/CMakeLists.txt` adds csound and chuck only when their archive exists.

## Changes from sk-engines

These are Daisy constraints that do not hold on the SSP.

| Engine | sk-engines | here | why |
|-|-|-|-|
| all | 48 kHz assumed | the host rate | radio's rate ratios, csound's `sr` and the static decay assumed it |
| radio | banks `radio/0`..`radio/15`, 8.3 names, 16-bit mono | any folder of banks; long names; PCM 16/24/32 and float WAV, any channel count | FatFs and SRAM limits |
| radio | `rate.txt` for `.raw` | `Raw Rate` parameter, default 44.1 kHz, Radio Music's rate | a parameter is visible and saved in the preset |
| radio | reset re-tunes only a silent deck | reset jumps to Start, as Radio Music does | the guard was for a floating gate jack and a capacitive pad |
| radio | first station waits 180 ms | opens at once | nothing is playing to protect from chatter |
| radio | hard cut on a switch | crossfade of `Fade ms`, on switches and resets | the cut clicked unless Static masked it |
| radio | Start applies on a switch or reset | also at once, with `Start Pot Imm` / `Start CV Imm` | Radio Music's `startPotImmediate` / `startCVImmediate` |
| radio | FAT order, then case-insensitive name | numbers compared by value | banks `0`..`15` sorted `0, 1, 10, ...` |
| radio | no `SETTINGS.TXT` | its fade and Start keys, applied when a root is chosen with Load | a Radio Music card works unchanged; a preset still restores its own values |
| radio | free-running offset counted in output frames | in seconds, then the station's frames | stations at other rates landed at the wrong place |
| csound | `ksmps` forced to the block; a larger block was truncated | the orchestra's `ksmps`, through a one-k-cycle FIFO | the SSP block size is the host's |
| csound | output read as if `0dbfs` = 1 | scaled by `0dbfs` | orchestras without `0dbfs = 1` clipped |
| csound | NoteOn only, fixed 0.6 s notes | full note on/off with velocity through Csound's host MIDI | the Daisy UI delivered no NoteOff |
| csound | knob names `speedA`, `mixA`, ... | `p1`..`p16`, named and ranged by the program | the names were Spotykach panel controls |
| csound | numbered slots `csound/0.csd`..`7.csd`, picked with Alt+PITCH | any file, picked with the browser; path saved in the preset | the SSP has a screen |
| csound | Csound's signal and atexit handlers | disabled with `csoundInitialize` | the host process owns them |
| chuck | MIDI injected through a patched `MidiIn` | ChucK's own ALSA `MidiIn` | Linux has ALSA |
| chuck | compiled bytecode cached per slot | compiled on each load | the cache bounded leaks in 64 MB of SDRAM |
| chuck | `adc => dac` links kept across programs | cleared on each load | a reload doubled the input |
| chuck/csound | a bad program fell back to the built-in | the previous program keeps running | |
| chuck | CPU overrun muted the patch | not ported | the DWT cycle counter is Cortex-M only; the DSP load shows on screen |

## Tests

| Test | Covers |
|-|-|
| `plugins/radio/tests` | file parsing, station choice, free-running clock, reset, rates; the stream handoff under ThreadSanitizer |
| `plugins/csound/tests` | built-in, k-cycle FIFO, `0dbfs`, rate, MIDI, files, errors; the instance swap under ThreadSanitizer |
| `plugins/chuck/tests` | the same for ChucK. `tsan.supp` silences ChucK's own globals queue, which TSan cannot see inside the uninstrumented library |
| `plugins/common/tests` | builds the three plugins for the host into `build/plugins-host` and drives them through the SSP API: block splitting, parameters, state save and restore |
