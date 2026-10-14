# Dexed port: `dexd`

Status: plan, 2026-10-14. Step 1 done; the rest is unbuilt.

`dexd` is a 6-operator FM synth played from MIDI, built on the Dexed engine. Name `dexd`, uid `DEXD` (see [engines.md](engines.md#naming)).

## Source

[Synth_Dexed](https://codeberg.org/dcoredump/Synth_Dexed) `e3f524c` (2025-09-10), not [Dexed](https://github.com/asb2m10/dexed) `eebb748` (1.1.0, 2026-10-10).

- Synth_Dexed is the Dexed engine as a plain `Dexed` class with no JUCE. It already has voice allocation, sustain, sostenuto, portamento, pitch bend, mod/breath/foot/aftertouch, sysex checks, voice decode, mono mode, and the MkI and OPL engines.
- Dexed's `DexedAudioProcessor` derives from `AudioProcessor` and pulls in CLAP, MTS-ESP, file choosers and a tuning UI. `SSPApi.h` needs a `PluginProcessor` on `BaseProcessor`. Lifting only Dexed's `msfa/` means re-porting `PluginMidi.cpp`'s voice and controller logic.
- The two have diverged: 25 to 276 lines differ per `msfa` file (`dx7note` 276, `fm_op_kernel` 231). An exact match with Dexed 1.1.0 is not a goal (decided 2026-10-14).

Changes to Synth_Dexed:

| Change | Why |
|-|-|
| `arm_fill_f32` to `std::fill` (`dexed.cpp:193`) | its only CMSIS use outside the compressor |
| drop `compressor.h` | CMSIS-only; the SSP has compressor modules |
| none for NEON | `__ARM_NEON` is defined by `-mfpu=neon-vfpv4` (`xcSSP.cmake:74`), so `neon_fm_kernel` compiles in. Check that it does |

## Engine

`DexedEngine : ssp::engine::Engine` owns one `Dexed(16, rate)`.

- **Block size.** `getSamples` writes whole 64-sample chunks (`_N_`) and ignores a remainder. The engine renders into a 64-sample FIFO and drains it to the host block. Latency is at most 63 samples, 1.3 ms at 48 kHz.
- **Voice reaping.** A released voice stops rendering only when `getNumNotesPlaying()` finds all its carriers silent (`dexed.cpp:594-630`). The engine calls it after every chunk. Without it, CPU stays at the voice cap after the first chord.
- **Static tables.** `Freqlut`, `Lfo`, `PitchEnv`, `Env` and `Porta` hold rate-dependent static tables, and the `Dexed` constructor rewrites them (`dexed.cpp:49-57`). A second instance would write them while the first is playing. Initialise them once per sample rate, under a lock, outside the constructor.
- **Output.** Mono. Out L and Out R carry the same signal.

## Voices

`Voices` parameter, 1 to 16, default set by the device measurement below. It maps to `setMaxNotes`, which allocates nothing. `setMaxNotes` calls `panic()`, so `control()` applies it only when the value changes. Notes still sounding are cut.

A fixed cap over one that tracks DSP load: polyphony then does not depend on what else is patched. The status panel shows DSP load and notes playing.

## MIDI

`BaseProcessor::handleMidi` (`BaseProcessor.cpp:443-488`) passes plugins only note on and off. Dexed also needs:

- pitch bend
- CC1 mod wheel, CC2 breath, CC4 foot, CC64 sustain, CC66 sostenuto
- channel aftertouch
- program change

`BaseProcessor::midiMessageInput(const juce::MidiMessage&)` (done) gets every channel message on the module's channel; notes only while Note In is set. `dexd` takes notes from it, not from `midiNoteInput`. The change touches every plugin and sits next to the 2026-10-11 device-lock fix, so it needs the existing MIDI tests plus a re-run of `midi-review.md`'s device steps on one script module.

Messages arrive on JUCE's ALSA thread. `Dexed` is not thread-safe. `PluginProcessor` pushes a fixed-size event into a `moodycamel::ReaderWriterQueue` (`external/readerwriterqueue`). `control()` drains it on the audio thread. Timing is quantised to the block, as in `scsy` and `csnd`.

Program change 0 to 31 selects that voice of the loaded cartridge. Values 32 to 127 are ignored, so a controller sending 33 does not silently select voice 1. The cartridge stays in memory, so selection decodes 128 bytes on the audio thread and does not touch a file.

Sysex voice and cartridge dumps: not in v1.

## Cartridges and state

- A `.syx` cartridge is 4104 bytes: a 6-byte header, 32 packed 128-byte voices, a checksum and `F7`. Load it with `EngineEditor`'s Load key, as in `radio`. Parse it with the standard library, so engine tests do not need JUCE. Reject a wrong size or checksum, with the reason on the status panel.
- The preset stores the cartridge path, the voice index and the unpacked 155-byte voice. Edits survive a reload even if the file changes.
- No cartridges ship. Dexed's `builtin_pgm.zip` contains Yamaha factory voices of unclear copyright. With no cartridge, `loadInitVoice()` plays.

## Parameters and editor

A performance subset (decided 2026-10-14), 4 or 5 pages: cartridge, voice, Voices, algorithm, feedback, 6 operator levels, LFO, cutoff, resonance, transpose. Voice editing happens on a computer; the full 155 parameters would take about 40 pages.

A parameter edit writes the voice through `setVoiceDataElement`, which sets `refreshVoice`; voices playing pick it up at the next block.

## License

Dexed and Synth_Dexed are GPL-3, with `msfa` under Apache-2.0. The repo is AGPL-3 and already ships GPL-3 modules (`README.md:62`).

## Tests

Host, under `plugins/dexed/tests`:

- one note renders non-silent output from the init voice and goes silent after release
- released voices are reaped: notes playing returns to 0
- FIFO output matches a 64-aligned render at block sizes 1, 63, 64, 65 and 256
- `Voices` caps notes playing; a note past the cap steals a voice
- cartridge parse: a valid file, a wrong size, a bad checksum
- program change 0 to 31 selects that voice; 32 and above are ignored
- state round-trip: path, index and edited voice
- ThreadSanitizer: a MIDI thread pushing notes, controllers and program changes against the audio thread
- `plugins/common/tests/midi` (done): `midiMessageInput` receives bend, CC, aftertouch and program change; notes only with Note In; the channel filter; no system messages; `midiNoteInput` still works

## Device checks

1. Listed in Synthor, audible from `midisend`.
2. Peak DSP load with 1, 4, 8 and 16 held notes, on a patch with all 6 operators and a long release. This sets the `Voices` default.
3. Bend, mod wheel, sustain and program change from a controller.
4. Two instances at once.
5. Preset save and reload; compact editor in rack; unload and reload (see [cleanup-problems.md](cleanup-problems.md)).

## Order

1. `BaseProcessor::midiMessageInput` and its test.
2. Synth_Dexed vendored with the changes above; `DexedEngine` with the FIFO, reaping and static-table init; engine tests.
3. `PluginProcessor`: the MIDI queue, `Voices`, cartridge load, state.
4. Editor: the performance pages.
5. Device checks.

## Open

- Whether Synth_Dexed steals the right voice when all are held: the full path through `keydown` (`dexed.cpp:276-300`) is unread.
