# scsy: sample buffers

Status: implemented as proposed, apart from the tests: the file, missing-file, reload and memory-cap tests are in `plugins/scsynth/tests/scsy_test.cpp`; the alloc test checks that a recording survives a reload rather than that its second second repeats the first. User docs: [README](../../plugins/scsynth/README.md#buffers). Claims marked *unverified* come from reading the source and have not been tested.

## Problem

`scsy` never allocates a scsynth buffer. Buffer UGens (`PlayBuf`, `BufRd`, `BufWr`, `RecordBuf`, `GrainBuf`, `TGrains`, `Warp1`, `DiskIn`) load, but a def that names a buffer number reads nothing. The README says so, and [scsynth-module.md](scsynth-module.md) lists `/b_allocRead` as "not in v1".

A def can still sample with `LocalBuf`, which the synth allocates itself. [sapf-sc](https://github.com/shakfu/sapf-sc) tests this in `poc/looper.py` and `docs/dev/localbuf-poc.md`: a loop recorded from an input repeats sample-exactly in a scsynth NRT render. `LocalBuf` has three limits:

| Limit | Cause |
|-|-|
| 43.6 s of mono at 48 kHz per instance, shared by every `LocalBuf` in the def | `LocalBuf` allocates from the real-time pool. `ScWorld::open` leaves `mRealTimeMemorySize` at scsynth's 8192 KB. A larger request fails with `LocalBuf_allocBuffer: alloc failed` and the def plays silence |
| No sample files | nothing reads a file into a `LocalBuf` |
| A reload discards the recording | the buffer belongs to the synth |

## Proposal

The sidecar names buffers. `scsy` allocates them before it starts the synth, and the def reads them by number.

```text
// @buf 0 samples/loop.wav
// @buf 1 alloc 10 2
// @p1 rate -2 2
```

- `@buf N path` reads a sound file into buffer N with `/b_allocRead`. A relative path is relative to the def's folder.

- `@buf N alloc SECONDS [CHANNELS]` allocates an empty buffer for recording with `/b_alloc`. The frame count is SECONDS times the current rate. CHANNELS defaults to 1.

- N is 0..63. The def uses the same number, for example `PlayBuf.ar(buffer_id=0, ...)`.

Explicit numbers over buffers named by a control: the def needs no extra control, and a def written for sclang, with `PlayBuf.ar(1, 0)`, works unchanged. A compiler such as sapf-sc can assign the numbers itself.

`ScriptEngine::parseSpecs` skips any comment line without `@p`, so earlier `scsy` versions ignore `@buf` lines.

### Load order

`ScWorld::loadDef` already sends `/d_recv`, waits for `/done`, then starts the synth and waits for `/synced`. Buffers go before `/s_new`:

1. For each `@buf` line, send `/b_allocRead` or `/b_alloc` and wait for `/done` with that command as its first argument. A `/fail` fails the load, with scsynth's reason, such as `File '...' could not be opened`. The running synth keeps playing.

2. `/d_recv`, then `/s_new`, as now.

The file is read on scsynth's NRT thread. That thread runs on core 0 at normal priority (see [Device threads](scsynth-module.md#spike-results)), so loading never touches an audio core. Loading still blocks the `scsy` worker thread for the length of the read. Card read speed is not measured.

### Keeping buffers

`ScWorld` keeps a table of the buffers it holds: number, path or alloc size, file modification time.

- **Reload with an unchanged line:** skip it. Live-coding a def does not re-read its samples, and a recording in an `alloc` buffer survives.

- **Reload with a changed line, or a new number:** reallocate. scsynth swaps the data on the RT thread and frees the old data on the NRT thread, so a running synth never reads freed memory (*unverified*; SC's normal `/b_allocRead` behaviour).

- **Number no longer named:** `/b_free` once the new synth runs.

- **Rate change:** `prepare` reopens the World, which loses every buffer. `ScriptEngine::prepare` then reloads the program, which reallocates them. `alloc` buffers lose their recordings.

### Limits

- **Memory.** `/b_allocRead` uses ordinary `malloc`, not the real-time pool. A 10-minute stereo file at 48 kHz takes 230 MB as 32-bit float. `scsy` should refuse a load whose buffers would exceed a fixed total, say 256 MB per instance, before sending anything. The SSP's free memory is not measured.

- **Sample rate.** scsynth does not resample. A def plays a 44.1 kHz file at the right speed only with `BufRateScale`.

- **No sharing.** Each instance runs its own World, so two instances that name one file hold two copies. A process-wide cache that each World refers to would remove that. It is out of scope.

- **No saving.** Writing an `alloc` buffer to the card (`/b_write`) is out of scope.

## Separate change: the real-time pool

Setting `o.mRealTimeMemorySize` in `ScWorld::open` raises the `LocalBuf` limit for every def. It is one line and independent of the above. The value depends on how many instances you expect: each World allocates its pool when it opens, whether or not a def uses it.

## Tests

In `plugins/scsynth/tests/scsynth_test.cpp`, with defs from `tests/defs/defs.py` and a short WAV in `tests/defs`:

| Test | Checks |
|-|-|
| file | `@buf 0 click.wav` and a `PlayBuf` def: the output holds the file's samples |
| alloc | `@buf 1 alloc 1` with `RecordBuf` from input 1 and `BufRd`: the second second repeats the first |
| missing file | the load fails with the file's name in the error, and the previous def keeps playing |
| reload | reloading the def with an unchanged line keeps the `alloc` buffer's recording |
| memory cap | a sidecar over the cap fails before anything is sent |

## Open

- Does `/b_allocRead` work in the SSP build? libscsynth is compiled with the sndfile headers and without `NO_LIBSNDFILE`, and `scsy` links `libsndfile.a`, so it should (*unverified*).

- The memory cap, after measuring free memory on the SSP with Synthor running.
