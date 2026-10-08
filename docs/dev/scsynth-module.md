# scsynth module: design proposal

Status: proposal. Nothing here is built. Claims marked *unverified* come from reading the source and have not been tested.

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

## Open decisions

- Program file: sidecar, manifest, or no metadata.

- MIDI convention, and the voice limit.

- Module name: `scsy` / `SCSY`.

- Whether nanosynth gains a control-spec API, or the spec stays in a separate dict in `tools/scsy`.

- Whether the desktop tool is `tools/scsy` here or a `nanosynth ssp` command.
