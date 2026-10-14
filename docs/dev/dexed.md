# Dexed port: `dexd`

Status, 2026-10-14: steps 1-4 built and tested on the host. Device checks 1-6 and part of 7 pass on the SSP; the rest of 7, and 8, were not run. Usage is in [plugins/dexed/README.md](../../plugins/dexed/README.md).

## Source

[Synth_Dexed](https://codeberg.org/dcoredump/Synth_Dexed) `e3f524c` (2025-09-10), not [Dexed](https://github.com/asb2m10/dexed) `eebb748` (1.1.0, 2026-10-10).

- Synth_Dexed is the Dexed engine as a plain `Dexed` class with no JUCE: voice allocation, controllers, mono, portamento, and the MSFA, Mark I and OPL engines.
- Dexed's `DexedAudioProcessor` derives from `AudioProcessor` and pulls in CLAP, MTS-ESP, file choosers and a tuning UI. `SSPApi.h` needs a `PluginProcessor` on `BaseProcessor`.
- The two have diverged: 25 to 276 lines differ per `msfa` file. An exact match with Dexed 1.1.0 is not a goal (decided 2026-10-14).

Synth_Dexed is copied into `plugins/dexed/Source/synth_dexed`, not fetched like teletype: it is 6k lines, and the patch is part of the port. The changes are in [its README](../../plugins/dexed/Source/synth_dexed/README.md) and `plugins/dexed/synth_dexed.patch`.

Found while porting:

- `keydown` stamped `voices[i]`, the loop counter, instead of the voice it started. A steal took the wrong voice. With every voice in use, `i` equals the voice count, so it wrote one entry past `voices`: `malloc(): invalid size` in `testStealsOldest` without the fix. Patched.
- `decodeVoice` masks LFO pitch sensitivity with `0x30`, so 4-7 read as 0-3. `dexd` unpacks cartridges itself, from Dexed's `Cartridge::unpackProgram`, and does not call it.
- `setModWheel` and the other controller setters do not refresh the controller mix; the caller must call `ControllersRefresh`.

## Engine

`DexedEngine` owns one `Dexed(16, rate)`, created in `prepare`.

- **Block size.** `getSamples` writes whole 64-sample chunks. A 64-sample FIFO adapts the host block. Latency is at most 63 samples, 1.3 ms at 48 kHz.
- **Voice reaping.** A released voice stops rendering only when `getNumNotesPlaying()` finds its carriers silent. The engine calls it after every chunk. Without it, CPU stays at the voice cap after the first chord.
- **Static tables.** The rate tables are filled once per rate, and the `Dexed` constructor runs under one process-wide lock. Instances at two different rates would still share one set; Synthor runs one rate.
- **Transpose** is applied to the note, not the voice, and each key remembers the pitch it started, so a key released after a transpose change still releases its voice.

## Performance controls

Offsets over the cartridge's voice, not edits to it (decided 2026-10-14, performance subset). A program change then keeps the offsets, and a preset is the cartridge, the voice number and the offsets. Absolute values would have to be rewritten from the new voice on every program change, on the audio thread.

Every block the engine applies the offsets to the voice and compares the result with what it plays. A changed byte goes to `setVoiceDataElement`, which notes playing follow. A voice or cartridge change goes to `loadVoiceParameters`, which calls `panic()`.

`Voices` defaults to 16, the DX7's count and the engine's maximum; 16 notes cost under 9% of one core (below). `setMaxNotes` allocates nothing but calls `panic()`, so it applies only on a change. A fixed cap over one that tracks DSP load: polyphony then does not depend on what else is patched.

## MIDI

`BaseProcessor::midiMessageInput` gets every channel message on the module's channel; notes only while Note In is set. `dexd` takes notes from it, not from `midiNoteInput`.

Messages arrive on JUCE's ALSA thread; `Dexed` is not thread-safe. Program change sets the Voice parameter from the MIDI thread, as MIDI-learn automation already does, so the screen and the preset follow. Everything else goes as 3 bytes into a 512-entry `moodycamel::ReaderWriterQueue`, drained at the start of each block; a full queue drops the event and counts it on the status panel. Timing is quantised to the block, as in `scsy` and `csnd`.

The mod wheel and aftertouch are set to range 99, target pitch: the DX7's LFO pitch depth. Breath and foot reach the engine with no target.

## Cartridges and state

- `Cartridge` parses with the standard library only, so engine tests need no JUCE.
- A failed checksum loads anyway, as in Dexed: dumps in the wild often have bad checksums. The status panel says so.
- Every unpacked byte is clamped to its DX7 range, from a file or a preset: the engine indexes tables with them.
- The audio thread gets a cartridge through `ReloadGate` and a generation counter; `setCartridge` frees the old one after `take()`.
- The preset stores the path and the selected voice's 155 bytes. Storing the whole bank would put 8 KB of hex in each module's state.
- No cartridges ship: Dexed's `builtin_pgm.zip` contains Yamaha factory voices of unclear copyright. [Yamaha-DX7-patch-library](https://github.com/visualizersdotnl/Yamaha-DX7-patch-library) is CC0, but its uploader compiled other people's patches, Yamaha's and commercial packs among them, and can waive only their own rights. The README links it instead.

## Jacks

Cutoff CV and Mod CV inputs, so the module has inputs: every module in this repo has some, and Synthor with none is untested. Both are useful on a modular. They are read once per block.

## Tests

`plugins/dexed/tests` (engine, natively, and under ThreadSanitizer):

- a note sounds, releases, and is reaped
- block sizes 1, 63, 65, 256 and 1000 render the same samples as 64
- the voice cap; the oldest held voice is stolen (the patched bug)
- transpose while held, sustain, all notes off, all sound off, pitch bend, the mod wheel from CC1 and from Mod CV, Cutoff CV
- program change mapping; offsets and their clamping
- cartridges: a round trip of 32 random voices through DX7 packing, agreement with Synth_Dexed's decoder, raw data, a dump inside a longer file, a bad checksum, wrong formats, clamping, file errors
- voice selection from a cartridge, an offset reaching a held note, and back to the init voice
- threads: MIDI, audio and a message thread swapping cartridges, while a second instance is prepared

`plugins/common/tests/test_engine_plugins.py::test_dexed` drives the built plugin: jack names, the editor frame, the cartridge path and voice in the state, and the saved voice when the file has gone. `plugin_host` cannot send MIDI, so sound from notes is checked only by the engine tests.

## Device checks

`make midisend` now also sends `cc`, `bend`, `pc` and `at` (see `tools/midisend/midisend.c`), so these run over ssh.

1. Install: `make install MOD=dexd`, a few `.syx` files in `BOOT/dexed`, then `make midisend`.
2. Listed in Synthor; MIDI IN set to `midisend`, channel OMNI. `echo "on 60 100" > /tmp/midisend` sounds the init voice; `off 60` releases it and `notes` returns to 0.
3. Load a cartridge; `pc 0` to `pc 31` change the voice and its name on the panel; `pc 40` does nothing.
4. DSP: hold 1, 4, 8 and 16 notes of a voice with all 6 operators and a long release; note average and peak. This sets the Voices default.
5. `bend 8191`, `bend 0`; `cc 1 127`, `at 100` add vibrato; `cc 64 127`, then `off`, holds the note until `cc 64 0`.
6. Patch an LFO into Mod CV and Cutoff CV.
7. Two instances at once; save and reload a preset; the compact editor in rack; unload and reload (see [cleanup-problems.md](cleanup-problems.md)).
8. The `BaseProcessor` change: re-run [midi-review.md](midi-review.md)'s steps on one script module.

## Device results (2026-10-14)

Steps 1-6 pass, with the factory bank `ROM1A.syx` from [MiniDexed discussion 456](https://github.com/probonopd/MiniDexed/discussions/456)'s `sysexFinal.zip`. Its 1,304 files through `Cartridge::load`: 1,288 load, 14 load with a bad checksum, 2 are rejected (`TX802BankB1.syx`, `B2.syx`: headerless and 4084 bytes, 12 short of raw data). The library has no licence and is not shipped.

Step 4, voice 4 `STRINGS 1`, held notes at 48 kHz:

| notes | DSP average | Synthor `dsp` thread, % of one core |
|-|-|-|
| 0 | 0.4% | under 1 |
| 1 | 1.1% | 1.6 |
| 4 | 2.6% | 3.0 |
| 8 | 4.5% | 4.6 |
| 16 | 8.9% | 8.6 |

The thread figure is the `utime`+`stime` delta of `/proc/<pid>/task/*/stat` over 5 s; only one `dsp` thread tracked the note count. About 0.5% per note, so the Voices default stays at 16.

Step 6: Cutoff CV is heard only with Cutoff below 1.0, its default; the sum is clamped to 1. Kept, and documented, over a default of 0.5 (every voice filtered) or a CV that closes the filter (the reverse of the usual direction).

Step 7: two instances with different cartridges both play from one `midisend`, and a rack preset restores each one's cartridge and voice. Both instances' inputs are ports of one ALSA client, `dexd.so`'s copy of JUCE. `notes` counts voices sounding now, and only the open editor redraws; an instance viewed after the notes end shows 0.

PG- and PG+ (Left and Right) were added during the checks, at the user's request.

## Open

- Breath and foot have no target. A parameter for each, or fixed to amplitude, waits for a use.
- Not run on the device: the compact editor, unload and reload (step 7), and the script-module MIDI check (step 8).
- `BaseProcessor` saves MIDI IN as an ALSA `client-port` address, which is assigned at runtime. After a reboot a preset can point at a missing device, or at a different one that took the number. Affects every module.
