# scsynth module: design proposal

Status: proposal; plan step 1 (host spike) done, step 2 (cross-build) built but not run on the SSP. [Spike results](#spike-results) supersede the sections they name. Claims marked *unverified* come from reading the source and have not been tested.

The goal is to run SuperCollider's synthesis server, scsynth, inside an SSP module. SynthDefs are written in Python with [nanosynth](https://github.com/shakfu/nanosynth) on a desktop, compiled to `.scsyndef`, and loaded on the SSP the way `csnd` loads a `.csd`.

Working name: `scsy` / `SCSY` (see the naming rule in [engines.md](engines.md#naming)).

## Why

- scsynth has about 300 UGens, written for real time and in use since 2002.

- nanosynth already vendors scsynth 3.14.1, builds it as a static library, and compiles SynthDefs from Python.

- `csnd` and `chuk` already cover "load a program from the card". If the SC UGen library and Python authoring are not the goal, this module duplicates them and adds scsynth plus boost to the build.

## Facts from the scsynth source

Paths are relative to nanosynth's `thirdparty/supercollider/server/scsynth/`.

| Fact | Where | Consequence |
|-|-|-|
| On Linux the audio driver is PortAudio, JACK or Bela, chosen at compile time | `server/CMakeLists.txt:140-148` | none of them lets a host drive the callback; a new driver is needed |
| `SC_AUAudioDriver` lets the host call `Run()`; `DriverStart`/`DriverStop` do nothing | `SC_AU.cpp` | the pattern to copy, but it derives from the CoreAudio driver, so it is macOS only |
| The PortAudio callback runs `numSamples / 64` blocks and drops the remainder | `SC_PortAudio.cpp:166-186` | the SSP block is 128 ([rack-design.md](rack-design.md)); other sizes need a FIFO |
| UGen libraries load once per process, guarded by `gLibInitted` | `SC_World.cpp:318` | several Worlds share one read-only UGen table |
| `World_Cleanup(w, true)` unloads that table for every World | `SC_World.cpp:939` | always pass `false` |
| `World_Cleanup` stops the asio (UDP/TCP) thread, which is process-wide | `SC_World.cpp:934` | if no World opens a port, nothing breaks |
| `STATIC_PLUGINS` links the UGens into the binary instead of loading `.scx` files | `SC_Lib_Cintf.cpp:88-177` | the source supports one `.so`; nanosynth's CMake does not build it yet |
| `World_SendPacket` copies the packet to a FIFO for the engine | `SC_World.cpp` | allocates; call it from the worker, not the audio thread |
| nanosynth sets `FFT_GREEN ON` only when supernova is enabled | nanosynth `CMakeLists.txt:55-58` | the SSP build must set it, or scsynth's CMake looks for FFTW |
| SC's plugin CMake skips X11 under `NO_X11`, which nanosynth sets | `server/plugins/CMakeLists.txt:99, 219, 252, 281` | `UIUGens` builds without X11 |

## Spike results

Code: `plugins/scsynth/Source/ScWorld.{h,cpp}`, tests in `plugins/scsynth/tests`, build in `scripts/scsynth/CMakeLists.txt` and `scripts/build_deps.sh`. Not yet a module.

- **Build.** `scripts/scsynth/CMakeLists.txt` compiles libscsynth from SC 3.14.1's source tarball without SC's CMake. No audio driver is compiled; `ScWorld.cpp` defines `SC_NewAudioDriver` and the timing functions. `SC_AUDIO_API` gets an unused value, so SC needs no driver patch. nanosynth's tree and its reentrant-World patch are not used: Worlds that open no port do not need it.
- **UGens load from a directory, not `STATIC_PLUGINS`.** `STATIC_PLUGINS` omits Chaos, ML, PV_ThirdParty and UnpackFFT UGens, and cannot add sc3-plugins. SC's 25 core plugin groups and sc3-plugins 3.14.1 (171 `.so`) build for the host and the SSP. A plugin `.so` imports only libc, libm and libstdc++: the UGen API is a function table.
- **sc3-plugins** needs FFTW (PitchDetection, NCAnalysisUGens), now built by `build_deps.sh`, and a patch that removes its `-stdlib=libc++` under clang.
- **A def with a missing UGen still replies `/done`** to `/d_recv`; scsynth only prints the error. `ScWorld::load` renames each def uniquely, treats `/s_new` "SynthDef not found" as the failure, and takes the reason from the print hook.
- **Two Worlds in one process work** (two loading at once, one closing while the other runs, one reopening), under ThreadSanitizer with libscsynth instrumented. ThreadSanitizer found four data races in SC 3.14.1, fixed in `scripts/patches/supercollider-3.14.1-threads.patch`. One of them, `MsgFifo`'s relaxed reads, is an ordering bug that ARMv7 can expose.
- **Block size.** Host blocks that are multiples of 64 run without latency; other sizes go through a 64-frame FIFO.
- **Python wrappers for sc3-plugins** are generated in nanosynth (`nanosynth.ugens.sc3`, 458 of 483 classes; see its CHANGELOG). `plugins/scsynth/tests/defs/defs.py` uses its `DFM1`.
- **`sc_SetDenormalFlags` sets flush-to-zero on ARM VFP** (`SC_World.cpp:158`), so the module needs no FTZ code of its own.

- **Device threads** (seen on the SSP, 2026-10-10). The kernel boots with `isolcpus=1,2,3`. Synthor's three `dsp` threads (`SCHED_RR` 30) and its `ALSA` thread (`SCHED_RR` 40) run on cores 1-3; its main thread and all other processes run on core 0. A thread inherits its creator's cores and policy, so `World_New` must run on the message thread: its NRT thread then runs on core 0 at normal priority. Two instances can run `process()` at once on different `dsp` threads.

- **On the SSP** (2026-10-10, `plugins/scsynth/tests/run_on_ssp.sh`): the engine and sc3-plugins tests pass, and the two-World test passes 70 times with the two audio threads on cores 1 and 2 at once. Loading all 196 UGen `.so` at the first open takes 380-390 ms from the SD card with a cold cache, 75 ms warm; the 25 core ones take 20 ms warm. That is a one-time stall of Synthor's message thread per session, so all plugins can ship. Linking libscsynth for the SSP needs `-lstdc++fs` (gcc 8) and `-lrt` (glibc 2.32).

Open from the spike: CPU per synth, measured in the module through Synthor's DSP load; the reference check against a nanosynth NRT render; and StkInst's rawwave files, which it reads from a path at run time.

## Updating SuperCollider

SuperCollider and sc3-plugins share one version, `SC_REF` in `scripts/build_deps.sh`. Plugins must be built against the same SuperCollider headers as libscsynth: scsynth refuses a plugin whose `api_version` differs.

1. Set `SC_REF` and run `scripts/build_deps.sh host`. If a patch in `scripts/patches/` no longer applies, `apply_patch` stops. Check whether upstream fixed the issue the patch names; drop the patch if so, else update it.
2. Check that `scripts/scsynth/CMakeLists.txt` still names every scsynth and core plugin source: compare it with `server/scsynth/CMakeLists.txt` and `server/plugins/CMakeLists.txt` in the new tree.
3. Run `make test`. `plugins/scsynth/tests` builds a ThreadSanitizer copy of libscsynth from the new source.
4. Run `scripts/build_deps.sh ssp`, then test on the device.
5. Update nanosynth to the same version, and regenerate its sc3-plugins wrappers: see nanosynth's `docs/dev/sc3-plugins-wrappers.md`. Recompile `plugins/scsynth/tests/defs` with it.

## Approaches

### A. One World per instance, host-driven driver (recommended)

An `Engine` subclass owns a `World` created with `mRealTime = true`. A new `SC_SSPAudioDriver` derives from `SC_AudioDriver`:

- `DriverSetup` returns the host rate and block.

- `DriverStart` and `DriverStop` do nothing.

- `process(in, out, n)` runs the loop body from `SC_PortAudio.cpp:155-240`: drain the engine FIFOs, copy inputs to buses 8..15, run due scheduled bundles, `World_Run` per 64 frames, copy buses 0..7 out.

The base class keeps the OSC FIFOs and the NRT thread, which runs `/d_recv`, `/b_alloc` and other async commands off the audio thread.

| Pros | Cons |
|-|-|
| Same thread model as `csnd`: audio, worker, message | one NRT thread and one memory pool per instance (`mRealTimeMemorySize`, 8 MB default) |
| Instances are independent; rack ordering works as for any module | relies on Worlds coexisting in one process; *unverified* beyond the source reading above |
| Reload is `/n_free` plus `/d_recv` in the same World; no `ReloadGate` | a custom driver to maintain against SC upgrades |
| Async commands already have a home (the NRT thread) | |

### B. One World per process, each instance a group

The first instance creates a shared World. Each instance owns a group and a private bus range.

| Pros | Cons |
|-|-|
| One memory pool, one NRT thread | `World_Run` computes the whole node tree. An instance cannot run only its own group when the host calls it |
| | So every instance after the first gets its input one block late, and rack's execution order ([rack-design.md](rack-design.md)) is lost |
| | Instances share failure: a bad def or a CPU overrun affects all of them |
| | Lifetime: the last instance must tear the World down |

Rejected: the ordering problem breaks rack.

### C. NRT-style manual stepping, no driver

Create the World with `mRealTime = false`, which creates no driver (`SC_World.cpp:442`). The module then runs the loop from `World_NonRealTimeSynthesis` (`SC_World.cpp:620-700`) itself: copy inputs, `PerformOSCBundle`, `World_Run`, copy outputs.

| Pros | Cons |
|-|-|
| No driver subclass, no NRT thread | *unverified*: with no NRT thread, async command stages may run inline, which would put `/d_recv` file work on the audio thread |
| Deterministic; the easiest to test natively | so a reload needs a second World, built on the worker and swapped behind `ReloadGate`, as `csnd` does |
| | depends on internals (`PerformOSCBundle`, `World_Start`) that are not exported API |

A fallback if A's driver fails. Worth a spike if A shows problems.

### D. scsynth as a separate process on the SSP

Run a stock `scsynth` binary. The module exchanges audio with it through shared memory.

| Pros | Cons |
|-|-|
| A crash in scsynth does not take down Synthor | the SSP has no JACK; the audio transport is custom, through a shared-memory ring |
| Stock scsynth with UDP, so sclang or nanosynth on a laptop can live-code it | at least one block of latency, and sync between two processes |
| No multi-World question | process start, stop and orphan cleanup on the device |

Rejected for v1: more moving parts than A for the same sound.

### E. A with UDP for live coding

Approach A, plus `World_OpenUDP` on one instance. A laptop running nanosynth (or sclang) sends `/d_recv`, `/s_new` and `/n_set` over the network.

| Pros | Cons |
|-|-|
| Live coding the SSP from Python | a network port in a module; only one instance can own a given port |
| Same engine as A | `World_Cleanup` of any World stops the shared asio thread (`SC_World.cpp:934`), so closing one instance breaks UDP for the others |
| | the SSP's network setup (Ethernet, Wi-Fi) is outside the module |

A later option once A works. Not v1.

### F. supernova instead of scsynth

supernova is SC's multi-threaded server. The SSP has four Cortex-A17 cores.

| Pros | Cons |
|-|-|
| Parallel groups could use more than one core | it runs its own DSP thread pool; a plugin host's single audio callback cannot easily drive it |
| | more threads, more memory, and Synthor already runs on these cores |
| | nanosynth notes that scsynth and supernova cannot share a process |

Rejected.

### G. Compile SynthDefs to C++

A generator turns a SynthDef graph into straight-line C++ with UGen code inlined, in the way Faust compiles its language. Each def becomes its own module.

| Pros | Cons |
|-|-|
| No runtime engine; small and fast | a compiler project: every UGen's calc functions, rate handling and buffer allocation reimplemented or extracted |
| No multi-World question | one build per def; no loading from the card |

Rejected: much more work for the same result. If this is the goal, Faust is the existing tool.

### H. Score playback

A variant of A. Instead of one SynthDef, the card holds a nanosynth `Score` exported as an OSC command list. The module plays it against the SSP transport, as `World_NonRealTimeSynthesis` does offline.

| Pros | Cons |
|-|-|
| Sequenced pieces, several defs, buffers | needs a transport (MIDI clock, as in sfct's `Tempo.h`, or an internal tempo) |
| nanosynth already writes the format (`Score.to_binary()`) | bundle timestamps must map onto host sample time |

A later mode of A, not a separate module.

## Design of A

### Fit with `plugins/common/engine`

`ScriptEngine` fits with two caveats:

- `ScriptEngine::readText` strips a BOM and converts CRLF. A `.scsyndef` is binary, so it must not go through `readText`. Options are below.

- `builtin()` returns text. The built-in here is a `.scsyndef` byte array generated at build time by nanosynth.

| ScriptEngine hook | scsynth |
|-|-|
| `prepare(sr, maxBlock)` | create the World at `sr`, 8 in, 8 out, 64-frame blocks; recreate it on a rate change |
| `compile(...)` on the worker | read the def, `World_SendPacket` a `/d_recv` with a completion message that frees the old node and starts the new one; wait for `/done` or a failure, with a timeout |
| `process` | the driver loop; write `p1`..`p16` to control buses first |
| `error()` | collect `FAILURE` replies through the reply function and `scprintf` output through `SetPrintFunc` |

### Program file

| Option | How | Pros | Cons |
|-|-|-|-|
| 1. Sidecar | the user picks `foo.scsyndef`; the engine reads it as binary and reads `@pN` lines from `foo.txt` beside it | the file picked is the def | `ScriptEngine` needs a binary path that skips `readText` |
| 2. Manifest | the user picks `foo.scs`, a text file with `@pN` lines and a line naming `foo.scsyndef` | `ScriptEngine` and `parseSpecs` unchanged | the browser shows a file the user did not write by hand |
| 3. No metadata | controls named `p1`..`p16`, range 0..1 | nothing to generate | the screen shows `P1`..`P16`; ranges live in the def |

Option 2 keeps the shared code unchanged. Option 1 is the more natural model for users. Decide before the spike.

Either way the `@pN` syntax is the one `csnd` and `chuk` use:

```
// @p1 cutoff 20 20000 Hz log
// @p2 res
// def filt.scsyndef
```

The `def` line is option 2 only.

### Controls

`p1`..`p16` map to named SynthDef controls through control buses. On load the worker sends:

```
/s_new filt 1000 0 0
/n_map 1000 cutoff 0 res 1 ...
```

Each block, before `World_Run`, the audio thread writes `value(i)` to `world->mControlBus[i]` and marks it touched. A mapped control reads its bus every control block. This avoids sending OSC from the audio thread, because `World_SendPacket` allocates.

The label in `@pN label` is the control name to map. A label that names no control in the def is an error at load.

### Audio and CV

| SSP | scsynth bus | In the SynthDef |
|-|-|-|
| outputs 1..8 | 0..7 | `Out.ar(bus=0, source=...)` |
| inputs 1..8 | 8..15 | `In.ar(bus=NumOutputBuses.ir(), channel_count=8)` |

SSP CV reads 0.2 per volt (`CV_PER_VOLT` in `Engine.h`). For 1 V/oct pitch on input 3: `freq = base * 2 ** (In.ar(bus=NumOutputBuses.ir() + 2) * 5)`.

### MIDI

scsynth has no MIDI. The module turns `midiNoteInput(note, velocity)` into commands. The convention:

- If the def has a `gate` control, it is a voice. A note on sends `/s_new` with `freq`, `velocity` and `gate 1`. A note off sends `/n_set gate 0`. The def frees itself with `DoneAction.FREE_SYNTH`.

- If it has no `gate` control, it is one persistent node, started at load. Notes set `freq` and `velocity` if the def has those controls.

Voice count and stealing need a limit. 16 is a starting value, not a measurement.

Notes come in on the audio thread. They must not allocate, so the module keeps pre-formatted `/s_new` and `/n_set` messages and runs them with `PerformOSCMessage` inside the driver loop, where scsynth's own engine thread would run them. This is *unverified*: check that `/s_new` does not allocate outside the RT pool.

### Threads

| Thread | Work |
|-|-|
| Audio | driver loop: FIFOs, controls, MIDI, `World_Run`. No allocation outside scsynth's RT pool |
| Worker (`idle`) | read files, `World_SendPacket`, wait for `/done` |
| scsynth NRT (one per World) | async command stages: `/d_recv`, `/b_allocRead` |
| Message | `prepare`: `World_New` / `World_Cleanup(w, false)` |

`sc_SetDenormalFlags` may do nothing on ARMv7 (*unverified*). Set flush-to-zero in `process`, as sfct does ([sfct.md](sfct.md)).

### Build

`scripts/build_deps.sh ssp|host` gains a step to build scsynth as a static PIC archive, as it does for Csound and ChucK:

- Source: nanosynth's trimmed `thirdparty/supercollider` (boost headers included), as a submodule or a copy under `external/`.

- Configuration: `SC_AUDIO_API` set to a new value for the SSP driver; `FFT_GREEN=ON` set explicitly; `NO_X11=ON`; `STATIC_PLUGINS` with the UGen sources compiled into the archive; no sclang, no supernova, no UDP.

- libsndfile: the PIC build that `build_deps.sh` already makes.

- `plugins/CMakeLists.txt` adds the module only when the archive exists, as for `csnd`.

Open build questions:

- Does the trimmed boost build with clang 18 against the buildroot's gcc 8.4 libstdc++?

- `STATIC_PLUGINS` calls `UIUGens_Unload`. `UIUGens` builds without X11 under `NO_X11`; whether the `STATIC_PLUGINS` path builds with it is untested.

- Size of the stripped `.so` with all UGens.

### Licence

scsynth is GPL-3.0 (some files GPL-2.0-or-later). This repo is AGPL-3.0. GPLv3 section 13 allows combining them, as the repo already does with softcut. Keep the SC licence headers.

## Role of nanosynth

nanosynth helps on the desktop and in the build. It does not run on the SSP.

| Role | What nanosynth has | Status |
|-|-|-|
| Authoring and compiling | `@synthdef`, `SynthDefBuilder`, `nanosynth compile` to `.scsyndef` | works; the examples below compile (602 and 528 bytes) |
| scsynth source | trimmed SC 3.14.1 tree, boost trimmed, libsndfile limited to WAV/AIFF; CMake already builds static libscsynth (`LIBSCSYNTH OFF`, `NO_X11 ON`, no sclang, no Qt) | reusable as the source for `build_deps.sh`; untested for armv7 |
| Reference output | `Score.render` with `input_path`, `input_channels=8`, `output_channels=8`: the same engine, offline | host tests can compare the module's output with an NRT render of the same def and input; *unverified* that block alignment gives identical samples |
| UGen check | `spec/nanosynth-ugens.json` lists each UGen and its plugin | a desktop check can reject a def that uses a UGen missing from the SSP build |
| Control metadata | control names, defaults, rates; no ranges or units | needs new code: a `param` spec in nanosynth, or a dict in `tools/scsy` |
| Score files (approach H) | `Score.to_binary()` writes the OSC command-file format | the file format exists; the player is new work |
| Live coding (approach E) | `Server`, `osc.py` | no client mode found that connects to a remote scsynth without booting a local one; `osc.py` over a UDP socket works |

It does not provide:

- A runtime on the SSP. The buildroot has no Python, and Python cannot run on the audio thread.

- The engine glue. `_scsynth.cpp` wraps `World_New`, `World_OpenUDP` and `World_WaitForQuit` for nanobind and uses scsynth's own audio driver. Its notes on process-global state (`_scsynth.cpp:181-217`) and its print and reply hooks carry over.

- An ARM build. Its CI builds wheels for macOS ARM64, Linux x86_64 and Windows x86_64.

Where the desktop tool lives is a choice:

| Option | Pros | Cons |
|-|-|-|
| `tools/scsy` in this repo, depending on nanosynth | SSP knowledge stays with the module, as `tools/py2rack` does | two places to change when the sidecar format changes |
| `nanosynth ssp` command: compile, check UGens, write sidecars, copy to the card or over `scp` | one tool for SynthDef work | nanosynth takes on one device's conventions |

## Making SynthDefs

### Authoring

Write defs in Python with nanosynth on a desktop:

```python
# filt.py
from nanosynth import synthdef
from nanosynth.ugens import In, NumOutputBuses, Out, RLPF


@synthdef()
def filt(cutoff=1000.0, res=0.3, mix=1.0):
    dry = In.ar(bus=NumOutputBuses.ir(), channel_count=2)
    wet = RLPF.ar(source=dry, frequency=cutoff, reciprocal_of_q=1.0 - res)
    Out.ar(bus=0, source=dry * (1.0 - mix) + wet * mix)
```

A MIDI voice:

```python
# pluck.py
from nanosynth import DoneAction, synthdef
from nanosynth.envelopes import EnvGen, Envelope
from nanosynth.ugens import Out, RLPF, Saw


@synthdef()
def pluck(freq=220.0, velocity=0.8, gate=1.0, cutoff=2000.0):
    env = EnvGen.kr(
        envelope=Envelope.adsr(),
        gate=gate,
        done_action=DoneAction.FREE_SYNTH,
    )
    sig = RLPF.ar(source=Saw.ar(frequency=freq), frequency=cutoff) * env * velocity
    Out.ar(bus=0, source=[sig, sig])
```

Rules for the SSP:

- Write to buses 0..7 only. Read inputs from `NumOutputBuses.ir()` onwards.

- Name every control a user should turn. Unnamed constants cannot be mapped.

- Only UGens in the module's static set load. A def using a missing UGen fails at `/d_recv` with a "UGen not installed" error on screen. sc3-plugins UGens are not included.

- `DiskIn`, `PlayBuf` and `Buffer` UGens need buffers. Loading samples (`/b_allocRead` from a path next to the def) is not in v1.

### Compiling

```
nanosynth compile filt.py -o build/scsy
# Wrote build/scsy/filt.scsyndef
```

Then write the `@pN` lines by hand (`filt.txt` or `filt.scs`, per the program-file decision). A later tool, `tools/scsy` next to `tools/py2rack`, could do both from one Python file with a range per control:

```python
SPECS = {"filt": {"cutoff": (20, 20000, "Hz", "log"), "res": (0, 1)}}
```

### Checking on the desktop

Render the def offline with nanosynth's NRT engine before copying it to the card. `Score.render` takes an input file and channel counts, so the same 8-in, 8-out bus layout can be checked with a test signal:

```python
from nanosynth import Score
from filt import filt

score = Score()
score.add_synthdef(0.0, filt)
# start a node, set controls, then render with input_path=..., input_channels=8, output_channels=8
```

Then check level, CPU and controls on the SSP. The DSP load shows on screen through `EngineProcessor`.

### Installing

Copy the `.scsyndef` and its metadata file to the card, for example `/media/BOOT/scsy/`, as `csnd` uses `/media/BOOT/csound`. The preset stores the path, not the contents.

## Risks

| Risk | Likelihood | Test |
|-|-|-|
| Two Worlds in one process interfere | medium; the source suggests they can coexist | spike: two Worlds under TSan, different defs, tear one down while the other runs |
| CPU on Cortex-A17 | unknown per def | measure the DSP load of a typical voice and filter on the device |
| Memory: 8 MB RT pool and buses per instance | low | measure with 8 instances in rack |
| Block sizes other than multiples of 64 | low on the SSP (128) | host test with sizes 37 and 511 |
| Build: boost, libstdc++ 8.4, `STATIC_PLUGINS` | medium | cross-build the archive before writing the module |
| A `/d_recv` error not reported | medium | load a def with a missing UGen and a corrupt file |

## Plan

1. Spike, host only, in `plugins/scsynth/tests`: build the archive, write the driver, run two Worlds under TSan, check output against a nanosynth NRT render of the same def. This settles approach A.

2. Cross-build the archive for the SSP with `build_deps.sh`.

3. Module: `ScriptEngine` subclass, program-file option, controls, error text. Tests as for `csnd`.

4. MIDI voices.

5. Later: buffers, score playback (H), UDP live coding (E).

## Decisions

Made 2026-10-10:

- Program file: the `.scsyndef` itself, chosen with Load. A sidecar `.txt` with `@pN` lines is optional; without one, `p1`..`p16` take the def's controls in order. See [plugins/scsynth/README.md](../../plugins/scsynth/README.md).
- No reload when the file changes, and no control that picks a def from the folder, in v1.
- Module name: `scsy` / `SCSY`.
- sc3-plugins wrappers live in nanosynth (`nanosynth.ugens.sc3`).

## Open decisions

- MIDI convention, and the voice limit (plan step 4).

- Whether nanosynth gains a control-spec API that writes the sidecar.
