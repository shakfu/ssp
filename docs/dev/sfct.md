# sfct implementation notes

User guide: [technobear/sfct/README.md](../../technobear/sfct/README.md). Open work: [technobear/sfct/TODO.md](../../technobear/sfct/TODO.md).

## Layout

- `Source/Engine.h`: the DSP host, free of JUCE. Voices, buffers, mixing, feedback, and the cross-thread transfers below. Tested natively.

- `Source/FadeZones.h`: crossfade geometry for the display. Tested natively.

- `Source/Tempo.h`: beat length from MIDI clock. Tested natively.

- `Source/PluginProcessor.*`: parameters, linking, CV, file IO, the save and housekeeping threads.

- `Source/PluginEditor.*`: full UI. `PluginMiniEditor.*`: compact UI.

- `external/softcut-lib/`: vendored softcut, with three patches; see its README.

`make test` builds `tests/engine_test.cpp` with the host compiler and runs it.

The version is `project(SFCT VERSION ...)` in `technobear/sfct/CMakeLists.txt`. `SSP_VERSION_FROM_PROJECT` makes the shared code show it on screen and report it in the descriptor; other modules report their build date. Record each version in `technobear/sfct/CHANGELOG.md`.

## Threads

The audio thread never allocates, frees or blocks. Work that touches a whole buffer is either an O(1) swap or sliced across blocks:

| Operation | Control side | Audio side |
|-|-|-|
| Load, mode switch | fill staging buffers, commit | swap vectors at the start of the next block |
| Clear all (Global view) | stage zeroed shared buffers and empty per-track ones, set `SHARED`, commit | the same swap |
| Free swapped-out buffers | housekeeping thread, every 50 ms, only when the load lock is idle | - |
| Save | worker thread waits, then writes the WAV | copy 32768 frames per buffer per block |
| Clear a loop | request | clear 32768 frames per buffer per block |
| Waveform peaks | read atomics | scan 4096 frames per block |

The load lock (`loadState_`) has four states. A committed load not yet swapped in can be reclaimed by the next load, which keeps the earlier load's buffers in the mask. This lets a preset restore before the audio thread starts.

## Linking

An edit to a linked voice is copied to its partner (pan negated) in `parameterChanged`, so the UI and presets hold both voices' values. `processBlock` also derives the follower from the leader, so both change in the same block; copying alone could land the two changes a block apart. Mirroring is suppressed during `setStateInformation`, which would otherwise copy a delinked preset's R values back over L.

## CV and feedback

CV is read once per block, before the outputs overwrite the input channels. Per block, a track's settings apply in this order, after linking so a linked track stays locked: sync sets the loop length, then input source and phase settings, then CV. Feedback reads each voice's previous 64-frame chunk, so its latency is fixed at 1.3 ms whatever the host block size. `processBlock` sets flush-to-zero: silent voices' filters otherwise decay through denormals, which are slow on VFP.

## UI

`BaseEditor` creates the RS and LS buttons but only puts EN-/EN+ on screen. sfct adds RS itself (labelled CLR); without that, the RS action works with nothing on screen to show it. Holding RS runs the current view's destructive action: clear a loop in a voice view, clear everything in the Global view. Long presses act at the framework's hold threshold (15 screen frames), in `eventButtonHeld`, while the button is still down. The release then reports a long press, which `consumeHeld` swallows; if the held event was missed, the release acts instead. Clear is the exception and acts only on release: RS is also the first half of the RS+LS system-panel combo, and on release the framework consumes the combo before reporting RS. Short TRK-/TRK+ presses navigate in `eventLeft`/`eventRight`, on release, since only release distinguishes short from long.

Page names live in `voicePageNames` and `globalPageNames` in `PluginEditor.cpp`; a `jassert` checks them against the pages added. `SYNC_PAGE` indexes the sync page, whose header shows the tempo.

## Sync, phase and record-once

`ClockTempo` averages the last 4 beats (96 ticks) of MIDI clock and ignores changes under 0.2%. A 1-beat window was tried first: with +-1 ms of jitter per tick at 120 bpm it varies by up to 0.4%, so loop lengths would follow the jitter; the test shows 176 failures at 1 beat and none at 4. The MIDI input thread owns `ClockTempo` and publishes seconds per beat through an atomic.

Sync replaces a track's loop end per block, after linking and before CV, so `Pos` CV still moves a synced loop. The `end` parameter keeps its value and is ignored meanwhile.

softcut's phase quantisation only changes the position it reports. sfct turns it into the `Tn Phase` outputs: a block-long pulse when the L voice's quantised position changes. Timing is per block.

Record-once uses softcut's `setRecOnceFlag`: recording starts at the next cut or loop point and stops after one pass. `Engine::recOnce` cuts a stopped voice to its loop start, which supplies that first cut; without it a stopped voice would never start.

The output filter's dry level used to be `1 - lp`. It is now an independent parameter, as on norns. Presets saved with an `lp` between 0 and 1 restore with the default dry level instead.

## Performance

`external/softcut-lib/patches/sfct-perf.patch` removes per-sample work with byte-identical output: `setRate` while the rate ramp holds still, `sinf` at fade 0 and 1, and reads of a silent head. It cut the time per block by 21% on x86. Skipping the SVFs when their mixes are 0 would save about 12% more, but changes output when a filter resumes from stale state.

Measured on the SSP, as the DSP readout shows it:

| Build | Voices on | Average | Peak |
|-|-|-|-|
| before the patch | 4 | 10% | |
| with the patch | 8 (`norns` mode) | 10% | about 20% |

Whether voices were recording is not recorded for either; playing-only voices skip the write path. If both runs were alike, the patch halved the per-voice cost on the A17, against 21% on x86, plausibly because `sinf` costs more on ARMv7 VFP. The peak is wall-clock time, so it includes pre-emption of the audio thread as well as heavier blocks (crossfades, rate ramps, sliced transfers); it decays 0.1% per block.
