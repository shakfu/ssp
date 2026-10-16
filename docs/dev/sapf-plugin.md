# sapf: live-coding plugin (design)

Status: steps 1 and 2 done, 2026-10-15. Step 3 built, installed and partly tested on the SSP; the rest of the device test is pending.

`sapf` runs [sapf](https://github.com/lfnoise/sapf), through the fork [shakfu/sapf](https://github.com/shakfu/sapf), inside an SSP plugin. You type sapf code on a USB keyboard and evaluate it; sapf's own UGens compute the sound. No scsynth.

This replaces a design built on sapf-sc, which compiled sapf to scsynth graphs ([sapf-sc-plugin.md](sapf-sc-plugin.md)). sapf-sc differs too much from sapf and becomes `stac`; its plugin left this repository in step 3 (it is in commit 77050c7).

## Requirements

- Standalone: a USB keyboard on the SSP. A laptop over ssh also works.
- Audio and CV in.
- A multi-line editor: sapf one-liners get long.
- `sapf-prelude.txt` loaded at start.

## Components

| Part | Source | Work |
|-|-|-|
| Interpreter and UGens | the fork's engine, `SAPF_ENGINE_ONLY` | fetched, pinned by commit (`scripts/fetch_sapf.sh`) |
| Audio | the fork's `MaxAudioBackend`: `render` on a thread of its own, 128-frame sapf blocks, a 4-block FIFO; `adc` for input | `Session` |
| Keys, editor, evaluation thread | `plugins/common/keys` (`TextBuffer`, `KeyQueue`), `plugins/sapf` (`Session`) | kept from the sapf-sc plugin |

The evaluation model, keys, prelude, screen and file layout carry over from [sapf-sc-plugin.md](sapf-sc-plugin.md).

## Fork changes

In the fork's CHANGELOG, Unreleased. Those the plugin needs:

- `Array::setSize` and `addSize` take `int64_t`: every list operation crashed on 32-bit ARM.
- `gInterrupt` stops an evaluation; `render` ignores it, so the sound keeps playing.
- `adc` and `adcn`, from sapf-max, read the host's input through a FIFO; `MaxMSPSetInputLead` covers a render thread running ahead.
- `SapfEngineConfig::openFiles = false`: `>sfo`, `sgram` and the help words otherwise run `system("open ...")`, which would fork Synthor.
- `SapfEngineConfig::blockSize`, set to 128 (Step 2 results).
- `SAPF_ENGINE_ONLY`; libsndfile from the host as `SndFile::sndfile`, so the plugin links `build/deps`' static PIC copy.
- `setseed` reaches the threads a thread starts (Seeding).

Bugs the step 1 harness found, all on Linux, all fixed:

- `sgram` read past two buffers in the Accelerate emulation; it aborted on every call.
- `sinoscfb` started from uninitialized memory: different on ARM from the first sample, NaN after 1.35 s in one example.
- The FFTW inverse real FFT read an uninitialized Nyquist bin.

## Audio input

`adc` streams read a FIFO that `MaxMSPSetInputBuffers` fills each host block. A stream starts one host block plus one sapf block behind the input: a render pulls a host block's output, and a sapf UGen pulls its input a block at a time. sapf-max instead read the current host buffer on every pull, which repeats or skips input when the two block sizes differ. A pull past the input written gets zeros and stays that much further behind.

The render thread runs up to 4 blocks ahead of the input it has, so the plugin passes that depth to `MaxMSPSetInputLead`; without it, a stream's first pulls pad gaps into the input. Input reaches output after about 1280 frames, 27 ms: the FIFO, the lead, one host block and one sapf block (inferred; the device test measures it).

SSP inputs 1-8 become `0 adc` to `7 adc`; CV in is 1.0 per 5 V, as in the other plugins.

## Risks

| Risk | Evidence | Mitigation |
|-|-|-|
| Render spikes stall Synthor | 34 of 205 examples had a 128-frame block over its 2.67 ms (Step 1 results) | render thread, 4-block FIFO, 128-frame sapf blocks (Step 2 results), real-time priority (Render priority) |
| One interpreter per process | the VM is a global (`vm`) | second instance silent |
| Runaway evaluations | `ord 1e12 N +/` runs for hours; the SSP has 2 GB, no swap | `gInterrupt`, the memory watchdog |

## Step 1 results

`plugins/sapf/tests/run_on_ssp.sh` builds `harness.cpp` for the PC and the SSP (clang both), evaluates the 360 blocks of `sapf-examples.txt` twice on the PC and once on the SSP, renders each `play` for 2 s at 48 kHz in 128-frame blocks, and runs `compare_render.py`. `test_sapf_engine.py` covers the harness on the host.

- Match: with voice seeds taken from a counter instead of the clock (a diagnostic copy of the fork; the fork now seeds this way itself, see Seeding), both PC runs are identical, and the SSP matches the PC in 204 of 205 renders: 53 equal, 151 within 3.1e-7. The other is `crackle`, a chaotic map: one float32 step at frame 108 grows to 0.2. The 7 `.wav` files differ between PC runs too (not investigated; perhaps libsndfile's PEAK timestamp).
- No block skipped or crashed on the SSP once the bugs above were fixed.
- Render load (CPU time per second of audio) on the SSP: median 0.06, 90th percentile 0.19, 95th 0.25, worst 1.02 (`rain`, 16 voices with reverb). The SSP is about 12 times slower than the PC.
- Spikes: the slowest 128-frame block of each render, as a fraction of its 2.67 ms, has median 0.44, 90th percentile 1.34, worst 7.3. 30 of the 34 renders over 1 average under 0.3: the cost comes in bursts.
- Evaluation time: sound examples evaluate in milliseconds. Three number puzzles (Kaprekar, hailstone, Collatz) take 190-320 s on the SSP.
- Memory: 25 MB peak after the prelude, 119 MB after all examples.

## Step 2 results

`plugins/sapf/tests/measure_on_ssp.sh` renders the examples on the SSP with the sapf block at 512 and at 128; `loads.py` summarizes. For each render the harness also replays its per-callback CPU times through a FIFO 1, 2, 4 or 8 host blocks deep, filled by a thread with a core of its own: block i starts once block i - depth has played and is late if it ends after block i is due.

| | sapf block 512 | sapf block 128 |
|-|-|-|
| load: median, p95, max | 0.063, 0.231, 1.02 | 0.078, 0.340, 1.28 |
| slowest callback / its 2.67 ms: median, max | 0.43, 7.19 | 0.20, 2.56 |
| renders with a callback over its time | 37 | 3 |
| renders late with a FIFO of 1, 2, 4, 8 blocks | 37, 5, 2, 1 | 3, 2, 1, 1 |

- The one render late at every depth is `rain` (block 27), over real time on average at both sizes.
- At 512, `dancing shadows` (block 155) has a single 19 ms callback and needs 8 blocks; at 128 its worst is 6.8 ms and 4 blocks hold it.
- 128-frame blocks cost more CPU: 24% at the median, 47% at p95. Each pull has a fixed cost.
- 128 across all examples changes only renders with noise UGens: they draw from the thread's generator as they pull, so the block size reorders their random values (45 of 205 renders, host). 256 is not measured.

Decision: sapf block 128, FIFO 4 blocks (10.7 ms output latency). 512 would need 8 blocks (21.3 ms) for the same examples.

The scripts checked `pidof Synthor`; the process is `SYNTHOR`, so they never saw it running. Whether Synthor ran during these measurements and step 1's is not known. The check is fixed.

## Render priority

In Synthor, the hard sync example (30 s render: load 0.18, slowest callback 0.58 of its time, Synthor stopped) stuttered and `late` grew. Synthor's audio threads run real-time (dsp `SCHED_RR` 30, ALSA `SCHED_RR` 40); its GUI thread runs normal and used 37% of a core; the render thread ran normal too, at 14%. The GUI held the core longer than the FIFO's 10.7 ms.

The render thread now asks for `SCHED_FIFO` 20: below Synthor's audio, above its GUI. A real-time priority over a deeper FIFO, because it adds no latency. A render above 0.9 load returns to normal priority until it falls below 0.5, so code that cannot keep up, such as `rain`, does not hold a core. The status line shows `rt` while it holds the priority. On the SSP (2026-10-15): `rt` shows, and hard sync plays without `late`.

## Seeding

Players, `go` threads and so `ola` and `oltx` voices, which run on the player's thread, seeded from the clock: `setseed` fixed only the evaluating thread's numbers. In the fork a new thread now seeds from its parent's generator state and a count of the threads it has started since it was seeded; the parent's numbers do not move, and an unseeded top-level thread still starts from the clock. Seeded code sounds the same each time: all 206 example renders repeat exactly on the PC, each block run after `1 setseed`.

## Meters

Inputs and outputs 1-2, right of the output pane: Synthor's own view shows all eight. Each meter is the block peak averaged over 100 blocks (`ssp::RmsTrack`, `ssp::MonoVuMeter`), -70 to +6 dB. Peaks below -40 dB read 0: an unplugged SSP input reads about -55 dB (estimated from a photo of the meter).

## Plan

Each step ends with a check that must pass before the next.

1. **Fork on the SSP.** Done; see Step 1 results.
2. **Render thread.** Done; see Step 2 results. `render` runs on a thread of its own and fills a FIFO that the host's callback reads: a slow render then silences sapf only, not every module in the patch.
3. **Plugin.** Built. The sapf-sc parts of `plugins/sapf` are gone: World, `SapfServer`, the UGen folder, their tests and install steps. `Session` runs the interpreter, the watchdog and the render thread; the callback gives `adc` its input and reads the FIFO. Host check: `test_session.py` (sound through the render thread, `adc`, stop, interrupt, watchdog, a second instance, teardown with `ola` playing) passes, also under ThreadSanitizer, and on the SSP itself. Device check: [sapf-device-test.md](sapf-device-test.md).

## Licence

GPL-3, as sapf is.
