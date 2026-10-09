# Engine plugins

`radio`, `csound`, `chuck`, `edrums`, `pstretch`, `bard` and `glitch` are ports of engines from sk-engines and daisy-apps. They share `plugins/common/engine`; each plugin keeps only its engine and its mapping onto it.

## Layout

| File | Role | Used by |
|-|-|-|
| `Engine.h` | the DSP interface: `prepare`, `process`, `idle`. No JUCE, so engines test natively | all |
| `EngineProcessor.*` | copies inputs, splits blocks larger than prepared, runs `idle()` on a worker thread, measures DSP load | all |
| `EngineEditor.*` | parameter pages, scrolled 6 rows at a time; page groups (a deck or track, then the globals) that a long Up, Down, Left or Right jumps between; status panel; file browser behind Load; up to 7 buttons (5 is Load); a compact view for rack | all |
| `Dsp.h` | `softLimit` (DaisySP's) and a biquad section with infrasonic's coefficients | radio, edrums, pstretch, bard, glitch |
| `Station.*`, `Stream.h` | WAV and `.raw` probing, folder scans in name order, a looping mono reader; a ring the worker fills and the audio thread drains | radio, pstretch, bard |
| `ReloadGate.h` | lock-free handoff of an instance between the worker and audio threads | csound, chuck, faust, pstretch |
| `ScriptEngine.*` | program loading, built-in fallback, controls `p1`..`p16` with the program's `@pN` labels and ranges (or the compiler's, through `declared()`), error text | csound, chuck, faust |
| `ScriptProcessor.*` | eight in, eight out, `p1`..`p16`, the program path in the preset; editors that show only declared controls, rebuilt in `onSSPTimer` after a load | csound, chuck, faust |

`FaustArch.h`, `FaustEngine.h` and `FaustProcessor.*` host a compiled Faust kernel, used by `chorus`. `faust` is a `ScriptEngine` over libfaust's interpreter. See [faust.md](faust.md).

A plugin is an `Engine` subclass plus a `PluginProcessor` that derives from `EngineProcessor` or `ScriptProcessor`. `SSPApi.h` requires that class name.

## Naming

Synthor scans every `.so` in `plugins/` but lists only some. Every listed plugin's uid (`PLUGIN_CODE`) spells its name, ignoring case; `rdio` with uid `SFRD` was scanned (a `dlopen` trace on the device shows it) but not listed. So `PRODUCT_NAME` is at most four characters and `PLUGIN_CODE` is the name in capitals: `rdio`/`RDIO`, `csnd`/`CSND`, `chuk`/`CHUK`, `edrm`/`EDRM`, `strc`/`STRC`, `bard`/`BARD`, `gltc`/`GLTC`. The rule comes from the 39 plugins on the card; changing `rdio`'s uid from `SFRD` to `RDIO` made it appear.

## Threads

- Audio: `Engine::process`, and `EngineProcessor::control` before it. Never blocks or allocates.
- Worker (`EngineProcessor::IDLE_MS`, on the UI core): `Engine::idle`. File reads, compiles, ChucK globals.
- Message: `prepare` and state restore. `prepare` holds the worker's lock.

`radio` hands each open station to the audio thread as a `Stream` through an atomic pointer, and gets the old one back through another; the worker frees it. `pstretch` does the same for clips, and `bard` for a `Cue`: a `Stream` with the frame it opened at and a seek count, so the worker knows when the playhead the audio thread reports belongs to the latest seek. Csound and ChucK swap their instance behind `ReloadGate`; `pstretch` swaps its FFT tables and voices there when the window size changes.

`edrums` and `glitch` have no worker state. Their display reads atomics the audio thread writes.

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
| edrums | two decks of two drums; Rev swaps which drum the knobs edit | four tracks, each with its own pages | the SSP has a screen and pages |
| edrums | stepped by the platform transport | a clock input and a reset input; a reset and a clock on the same sample play step 1 | the SSP clock reaches a module as a CV |
| edrums | kit in QSPI flash, saved 1.5 s after the last change | the parameters, in the preset | `PersistentStorage` is Daisy-only |
| edrums | density, length and rotation as 0..1 knobs; division 1, 2 or 4 | hits, steps and rotation as whole numbers; Rate `/8` to `x8`, multiplying over the measured clock period | a parameter shows its value; a clock divider module supplies slower clocks but not faster ones |
| edrums | one probability seed for all four drums | a seed per drum | the drums' chance decisions were correlated |
| edrums | Play stops a drum, Alt+Play mutes a deck | Mute per track; buttons fire a track | a button press is momentary on the SSP |
| edrums | no swing | Swing per track, 50% to 75%, on the track's own steps | a swung clock would skew the multiplier's period and the divided tracks |
| edrums | stereo out | also an audio and a trigger output per track; Voice off sends only the trigger | |
| pstretch | 8192 window, fixed at build | 4096 to 32768, rebuilt live | on the Daisy, 8192 fit on-chip SRAM only with two buffers moved to SDRAM |
| pstretch | live read head held one window behind the input; file read-ahead 8192 | held `window * pitch` behind; read-ahead `window * (pitch + 1)` | a pitched-up grain read past the newest input |
| pstretch | capture stopped recording | a second ring records while the first loops | Grab only restarted the old loop |
| pstretch | per-block work budget of 14 or 16 ticks | ticks per sample from the window, times 1.5, times the block | the SSP block is the host's; 1.0 is the measured minimum |
| pstretch | SD clips in `/pstretch`, 8.3 names, 16-bit mono | any folder, chosen with Load; long names; PCM 16/24/32 and float WAV, any channel count | FatFs and SRAM limits |
| pstretch | Alt+Cycle locks the LFO to the transport | not ported | no transport |
| pstretch | Mod Type, LFO shape and Size/Pos switches | Mod Shape and Mod Target parameters | |
| pstretch | pitch +/-1 octave plus unbounded V/oct | total clamped to +/-2 octaves | bounds a grain to 4 windows of input |
| bard | main loop does seeks; ISR reads `_pos` and flags unsynchronized | worker and audio thread share atomics and `Cue` handoffs | the SSP has several cores |
| bard | shelves `bard/0`..`bard/15`, 8.3 names, 16-bit mono | any folder of shelves; long names; PCM 16/24/32 and float WAV, any channel count | FatFs and SRAM limits |
| bard | resume keys up to 19 characters, split at the first space | up to 255, split at the last space | Linux file names; the old format still parses |
| bard | `.raw` and rate ratio relative to 48 kHz | relative to the host rate | |
| bard | Flux and Grit pads hold or latch colour and room | on while their mix is above 0 | no pads |
| bard | Mod Type Follow enables the duck | on while Duck is above 0 | |
| bard | Alt+Seq held toggles loop and hold; sidecar `loop=` is the default | Loop parameter: `file`, `hold` or `loop` | a parameter is visible and saved in the preset |
| bard | Alt+Rev re-rolls the auto-marks for the session | Reroll parameter | saved in the preset |
| bard | Alt+Play drops a mark; tap-hold Play writes the sidecar; Alt+Seq arms to the transport | not ported | the six buttons free beside Load are used; no transport. Edit the sidecar on a computer |
| bard | 180 ms debounce on pads and gate | a 2 V / 1 V Schmitt trigger on the gate | the buttons do not bounce |
| bard | Seek on the open file handle | reopen at the frame | `Stream` opens at a frame |
| glitch | no CV | V/oct pitch and P1, P2 inputs per deck | |

## Tests

| Test | Covers |
|-|-|
| `plugins/radio/tests` | file parsing, station choice, free-running clock, reset, rates; the stream handoff under ThreadSanitizer |
| `plugins/csound/tests` | built-in, k-cycle FIFO, `0dbfs`, rate, MIDI, files, errors; the instance swap under ThreadSanitizer |
| `plugins/chuck/tests` | the same for ChucK. `tsan.supp` silences ChucK's own globals queue, which TSan cannot see inside the uninstrumented library |
| `plugins/edrums/tests` | pattern shapes, clock and division, reset, Schmitt edges, chance, mute, triggers, routes and models at two rates; the display reads under ThreadSanitizer |
| `plugins/pstretch/tests` | live, freeze, capture and re-capture, pitch and V/oct, the pitched read limit, clips and their selection, window rebuilds, the work budget at every window and three block sizes, LFO and gate outputs; worker, audio and display under ThreadSanitizer |
| `plugins/bard/tests` | sidecar and resume formats, play, pause, back, rate and pitch keep, marks in Read, Recite and Wander, loop policy, Next and the gate, auto-marks and reroll, shelves, resume across instances, `bard.cfg`, ducking; worker, audio and display under ThreadSanitizer |
| `plugins/glitch/tests` | every algorithm at two rates, routes, tone, pitch and P1 CV, regeneration; the display read under ThreadSanitizer |
| `plugins/common/tests` | builds the plugins for the host into `build/plugins-host` and drives them through the SSP API: block splitting, parameters, state save and restore |
