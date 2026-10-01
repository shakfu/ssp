# sfct implementation notes

User guide: [technobear/sfct/README.md](../../technobear/sfct/README.md).

## Layout

- `Source/Engine.h`: the DSP host, free of JUCE. Voices, buffers, mixing, feedback, and the cross-thread transfers below. Tested natively.

- `Source/FadeZones.h`: crossfade geometry for the display. Tested natively.

- `Source/PluginProcessor.*`: parameters, linking, CV, file IO, the save and housekeeping threads.

- `Source/PluginEditor.*`: full UI. `PluginMiniEditor.*`: compact UI.

- `external/softcut-lib/`: vendored softcut, with three patches; see its README.

`make test` builds `tests/engine_test.cpp` with the host compiler and runs it.

## Threads

The audio thread never allocates, frees or blocks. Work that touches a whole buffer is either an O(1) swap or sliced across blocks:

| Operation | Control side | Audio side |
|-|-|-|
| Load, mode switch | fill staging buffers, commit | swap vectors at the start of the next block |
| Free swapped-out buffers | housekeeping thread, every 50 ms, only when the load lock is idle | - |
| Save | worker thread waits, then writes the WAV | copy 32768 frames per buffer per block |
| Clear | request | clear 32768 frames per buffer per block |
| Waveform peaks | read atomics | scan 4096 frames per block |

The load lock (`loadState_`) has four states. A committed load not yet swapped in can be reclaimed by the next load, which keeps the earlier load's buffers in the mask. This lets a preset restore before the audio thread starts.

## Linking

An edit to a linked voice is copied to its partner (pan negated) in `parameterChanged`, so the UI and presets hold both voices' values. `processBlock` also derives the follower from the leader, so both change in the same block; copying alone could land the two changes a block apart. Mirroring is suppressed during `setStateInformation`, which would otherwise copy a delinked preset's R values back over L.

## CV and feedback

CV is read once per block, before the outputs overwrite the input channels, and applied after linking. Feedback reads each voice's previous 64-frame chunk, so its latency is fixed at 1.3 ms whatever the host block size. `processBlock` sets flush-to-zero: silent voices' filters otherwise decay through denormals, which are slow on VFP.

## Performance

`external/softcut-lib/patches/sfct-perf.patch` removes per-sample work with byte-identical output: `setRate` while the rate ramp holds still, `sinf` at fade 0 and 1, and reads of a silent head. It cut the time per block by 21% on x86. Skipping the SVFs when their mixes are 0 would save about 12% more, but changes output when a filter resumes from stale state.

Measured on the SSP: 4 voices, 10% DSP, before the patch.
