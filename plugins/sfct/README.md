# sfct

`sfct` is a Percussa SSP looper plugin based on monome's [softcut](https://github.com/monome/softcut-lib) record/playback engine of the monome norns: each voice is a read/write head that loops over a buffer at any rate, forward or reverse, and crossfades whenever it jumps.

sfct has eight mono voices, grouped into four stereo tracks. It records the two audio inputs, plays back loaded WAV files, and saves its buffers back to WAV.

## Install

Copy `sfct.so` to the `plugins` folder on the SSP's SD card. The version shows at the top right of the screen; [CHANGELOG.md](CHANGELOG.md) lists what each version changed.

## Concepts

- **Voice**: one mono head. It plays, records, or both, over one buffer.

- **Track**: two voices, L and R. Track 1 is voices 1 and 2, track 2 is voices 3 and 4, and so on. By default L voices record In L and R voices record In R; a track's `input` setting changes this (see Global view).

- **Buffer**: mono audio memory, 43.7 s long at 48 kHz.

- **Mode** (Global view) decides which buffers the tracks use:

  - `norns` (default): all tracks share two buffers, L and R. Four tracks are four stereo playheads over one recording.

  - `4 loop`: each track has its own L and R buffers. Four tracks are four independent loops.

- **Link**: a linked track's L and R voices move together, with pan mirrored. Unlink a track to set its two sides apart, for example different rates for a stereo effect.

Tracks 1 and 2 start on, tracks 3 and 4 off. With the defaults, track 1 records the input at rate 1, and track 2 replays it at -0.5 (reverse, an octave down) through a lowpass filter.

## Quick start

**Record a loop.** Patch audio into In L and In R. For a mono source, patch In L only and set `T1 input` to `L` (Global view, page 2), so both sides record it. On track 1, press rec (button 2). The track records over its loop, 0 to 4 s by default, and plays it back. Press rec again to stop recording; the loop keeps playing. Track 2 is already replaying it in reverse.

**Play a sample.** Press Load (button 5), choose a WAV file with the encoders and arrows, and press Load again. A stereo file fills the track's L and R buffers; a mono file fills the current voice's buffer. The voices loop the whole file.

**Make a stereo effect.** On a track, turn link (button 7) off. Hold EN+ to switch to the R voice, and change its rate or loop end.

**Keep the result.** Press Save (button 8), keep or edit the name, and press Save again.

## Navigation

| Control | Action |
|-|-|
| TRK- / TRK+ | previous / next track; after track 4, the Global view |
| TRK- / TRK+, held | TRK- returns to track 1; TRK+ jumps to the Global view |
| EN- / EN+ | previous / next page. Within a track, the L voice's eight pages come first, then the R voice's |
| EN- / EN+, held | switch to the other side of the track, at the same page |
| Encoder | turn to change the value |
| Encoder, held while turning | fine steps |
| Encoder, pressed and released | reset to the default |
| RS (CLR), held | clear: the current voice's loop in a track, everything in the Global view (see Clear) |
| RS + LS | the general panel (MIDI), as in all technobear modules |

The header shows the track, the side, the page and its name, for example `Track 2 R 5/8 filter`.

A held button acts as soon as it has been held long enough, without waiting for release. Clear is the exception: it acts on release (see Clear).

## Voice pages

| Page | Name | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|-|
| 1 | play | rate | start | end | level |
| 2 | rec | rec level | pre level | in gain | pan |
| 3 | fade | fade | slew | rec shape | pre shape |
| 4 | fade 2 | rec delay | pre window | rec offset | |
| 5 | filter | lpf | Q | LP | dry |
| 6 | filter 2 | HP | BP | BR | |
| 7 | in filter | in FC | in Q | in track | in dry |
| 8 | in filter 2 | in LP | in HP | in BP | in BR |

Pages 1-3 hold the everyday controls. Pages 4-8 set softcut's crossfade and filters in detail; their defaults are softcut's own.

| Parameter | Range | Default | Step (fine) | What it does |
|-|-|-|-|-|
| rate | -4 to 4 | T1 1, T2 -0.5, T3 2, T4 0.5 | 0.05 (0.001) | playback speed and direction; 2 is an octave up, negative is reverse |
| start | 0 to 43 s | 0 | 1 (0.01) | loop start |
| end | 0 to 43 s | 4 | 1 (0.01) | loop end. Ignored while the track is synced |
| level | 0 to 1 | T1 0.8, others 0.6 | 0.1 (0.01) | output level |
| rec level | 0 to 1 | 1 | 0.1 (0.01) | level of new material when recording |
| pre level | 0 to 1 | 0.5 | 0.1 (0.01) | level of old material kept when recording: 0 replaces, 1 overdubs |
| in gain | 0 to 1 | T1 1, others 0 | 0.1 (0.01) | input level into the voice |
| pan | -1 to 1 | L -1, R 1 | 0.1 (0.01) | position in Out L / Out R |
| fade | 0.001 to 1 s | 0.05 | 0.01 (0.001) | crossfade time at loop points and cuts |
| slew | 0 to 4 s | 0.1 | 0.1 (0.01) | glide time for rate changes |
| rec shape | linear, sine, raised | raised | 1 | curve of the record level across a crossfade |
| pre shape | linear, sine, raised | linear | 1 | curve of the kept level across a crossfade |
| rec delay | 0 to 0.5 | 0.0078 | 0.01 (0.001) | share of the crossfade, from its start, before the record level starts to rise |
| pre window | 0 to 1 | 0.125 | 0.05 (0.005) | share of the crossfade, from its start, over which the kept level falls from full to pre level |
| rec offset | -10 to 10 ms | -0.17 | 0.1 (0.01) | how far the write position trails the read position |
| lpf | 20 to 20000 Hz | T2 2000, others 8000 | 500 (10) | output filter cutoff |
| Q | 0.05 to 4 | 4 | 0.1 (0.01) | output filter damping: low values resonate, 4 is smooth |
| LP, HP, BP, BR | 0 to 1 | LP: T2 1, others 0; others 0 | 0.1 (0.01) | output filter levels: lowpass, highpass, bandpass, band reject |
| dry | 0 to 1 | T2 0, others 1 | 0.1 (0.01) | unfiltered output level |
| in FC | 20 to 20000 Hz | 16000 | 500 (10) | input filter cutoff, applied before recording |
| in Q | 0.05 to 4 | 4 | 0.1 (0.01) | input filter damping |
| in track | 0 to 1 | 1 | 0.1 (0.01) | how far the input cutoff follows the rate: at 1, rate 0.5 halves it. This stops a slow recording from aliasing |
| in LP, HP, BP, BR | 0 to 1 | LP 1, others 0 | 0.1 (0.01) | input filter levels |
| in dry | 0 to 1 | 0 | 0.1 (0.01) | unfiltered input level |

The output filter's levels are independent: LP and dry both at 1 sum the filtered and dry signals.

## Buttons

| Button | Name | Action |
|-|-|-|
| 1 | play | play on/off |
| 2 | rec | record on/off. Hold to record once (see below) |
| 3 | loop | on: loop between start and end; off: play to the end once |
| 4 | cut | jump to the loop start, on press |
| 5 | Load | open the file browser; press again to load the selection |
| 6 | on | voice on/off. An off voice is silent, records nothing and uses no CPU |
| 7 | link | link the track's L and R voices |
| 8 | Save | open the name editor; press again to save |

In the file browser and the name editor, button 7 is Cancel.

**Record once**: hold rec. The voice records exactly one pass of its loop, starting at its next loop point, then stops recording by itself. A stopped voice starts at its loop start. On a linked track both voices record once.

## Linking

A linked track's two voices share every setting except pan, which is mirrored. An edit to either voice changes both. Their playheads stay sample-locked.

When you relink a track, the R voice takes the L voice's settings and both jump to the loop start.

Turning `on` off for one voice of a linked track turns off both. Unlink first to silence one side.

## Clear

RS is labelled CLR. Hold it, then release it to clear; a short press does nothing. Clear waits for the release because RS also opens the general panel together with LS: holding RS before pressing LS must not clear.

**In a track**, it clears the current voice's loop region. When the track is linked, both buffers are cleared. The first and last 5 ms of the region fade out rather than cut. In `norns` mode the buffers are shared, so a clear also silences other tracks that loop over the same region.

**In the Global view**, it clears everything and starts over, as if the module were new: every parameter returns to its default, all buffers are silenced, the file references are dropped, the mode returns to `norns` and the `4 loop` buffers' memory is released. Every playhead restarts at its loop start. MIDI settings are kept.

## Global view

Press TRK+ after track 4.

| Page | Name | Contents |
|-|-|-|
| 1 | mode | mode: `norns` or `4 loop` |
| 2 | input | T1 input, T2 input, T3 input, T4 input |
| 3 | feedback 1-2 | T1 fb src, T1 fb amt, T2 fb src, T2 fb amt |
| 4 | feedback 3-4 | T3 fb src, T3 fb amt, T4 fb src, T4 fb amt |
| 5 | sync | T1 sync, T2 sync, T3 sync, T4 sync |
| 6 | phase 1-2 | T1 phase Q, T1 phase ofs, T2 phase Q, T2 phase ofs |
| 7 | phase 3-4 | T3 phase Q, T3 phase ofs, T4 phase Q, T4 phase ofs |

Switching mode keeps every buffer's contents. The first switch to `4 loop` uses 48 MB more memory.

**Input** chooses what a track records. The SSP's inputs are separate channels: an unpatched In R is silence, not a copy of In L.

| Value | L voice records | R voice records |
|-|-|-|
| `stereo` (default) | In L | In R |
| `L` | In L | In L |
| `R` | In R | In R |
| `L+R` | (In L + In R) / 2 | (In L + In R) / 2 |

`L+R` is halved so that two full-scale inputs do not clip.

**Feedback** sends one track's output into another track's input: L into L, R into R. Set `fb src` to `off` or a source track, which may be the same track, and `fb amt` to the amount, 0 to 1 (default 0.5). The signal is taken after the source's filter and before its level, so turning a source's level down does not stop its feedback. Feedback is recorded only while the receiving track records.

**Sync** sets a track's loop length from MIDI clock: `off`, or 1, 2, 3, 4, 6, 8, 12, 16, 24 or 32 beats. The loop runs from `start` for that many beats; `end` is ignored. The length is in buffer time, so at rate 0.5 the loop lasts twice as many beats. The sync page header shows the tempo, or `no clock`.

- Select a MIDI input in the general panel (RS + LS). Clock is read by default.

- The tempo is averaged over 4 beats, so a tempo change takes 4 beats to arrive. Changes under 0.2% are ignored, so clock jitter does not move the loop.

- With MIDI transport enabled in the general panel, MIDI Start sends every synced track to its loop start.

**Phase** drives the `Tn Phase` outputs. `phase Q` is a time grid in seconds, 0 to 16 (0 is off); `phase ofs` shifts the grid, 0 to 43 s. The output pulses each time the track's L voice crosses a grid line. For one pulse per loop, set `phase Q` to the loop length and `phase ofs` to the loop start.

## Inputs and outputs

| Input | Signal |
|-|-|
| In L, In R | audio. Each track's `input` setting chooses which one its voices record |
| T1-T4 Rate | V/oct. +1 V doubles the track's rate, -1 V halves it. Reverse stays reverse |
| T1-T4 Pos | moves the track's loop 1 s per volt, keeping its length. It stops at 0 s and at the buffer end |
| T1-T4 Rec | gate. The track records while the gate is above 2.5 V, as well as when rec is on |
| T1-T4 Cut | trigger. The track jumps to its loop start on each rising edge above 2.5 V |

| Output | Signal |
|-|-|
| Out L, Out R | all voices, mixed and panned |
| Voice 1-8 | each voice alone, after its level, before pan |
| T1-T4 Phase | trigger, 2.7 ms long, at each `phase Q` grid line the track's L voice crosses (see Global view) |

CV acts on both voices of a track. It is read every 2.7 ms.

## Files

**Load** opens a browser at the folder of the buffer's current file, otherwise at `/media/BOOT/samples`. sfct reads WAV and AIFF at any sample rate and converts them to 48 kHz. Files longer than 43.7 s are cut at that length.

- A stereo file fills the current track's L and R buffers.

- A mono file fills the current voice's buffer only.

After a load, every voice that plays the loaded buffers loops the whole file, with rec off. In `norns` mode that is every voice; in `4 loop` mode, the current track.

**Save** writes a 24-bit WAV, from 0 s to the end of the waveform view. A linked track saves a stereo file of its two buffers; an unlinked voice saves its own buffer as mono. The default name is `sfct-YYMMDD-HHMMSS`. The file goes in the folder of the buffer's current file, otherwise in `/media/BOOT/samples`. Save never overwrites an existing file; choose another name.

**Presets** store the file each buffer was last loaded from or saved to, and reload it. Recordings and clears made since then are not stored: save first to keep them.

The line under the waveform reports each save and clear.

## Display

The waveform shows the current track's L and R buffers, from 0 s to the furthest loop end of the enabled voices on them.

- **Shaded region**: a voice's loop as it plays, in its track's colour. The current voice is brighter. Pos CV moves it, and sync sets its length.

- **Vertical line**: a playhead. Each voice has two, which crossfade at loop points and cuts. Each is as bright as its volume. A recording playhead is thicker.

- **Curves at the loop edges**: the crossfades. The solid curve is the playback level, from the bottom of the lane (silent) to near the top (full). While the current voice records, a dashed curve shows the level it records at and a grey curve the level of old material it keeps.

The header shows `DSP`: the CPU time sfct takes per audio block, as an average and a peak. With all eight voices on in `norns` mode it averages about 10%, with peaks near 20%. A voice that is off uses none.

## Limits

- Buffers are 43.7 s, against 349 s on norns.

- Recording during a save can leave small seams in the saved file: the save copies the buffer over 0.17 s while the recording continues.

- CV and cut triggers are read every 2.7 ms, not every sample.

- Files at rates above 48 kHz can alias when converted.

- The compact UI has no Load, Save, Clear, record once or waveform. It has every parameter page, in order.

- Phase pulses are timed to the 2.7 ms block, not the sample.

## Differences from norns softcut

- sfct has eight voices; norns has six.

- Recording keeps the input's polarity. norns softcut inverts it, which cancels against the dry signal when both are mixed.

- A voice that starts from stopped begins at its loop start.

## Credits

sfct is by [shakfu](https://github.com/shakfu). It is built on [softcut](https://github.com/monome/softcut-lib) by monome (GPL-3.0) and TheTechnobear's SSP module framework (AGPL-3.0). Implementation notes are in [docs/dev/sfct.md](../../docs/dev/sfct.md).
