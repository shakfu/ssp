# sfct

Softcut looper with eight mono voices in four stereo tracks, built on the vendored
[softcut-lib](../../external/softcut-lib/README.md).

Track n is voices 2n-1 (L) and 2n (R). L voices record In L, R voices record In R.
Each buffer is mono, 2^21 frames: 43.7 s at 48 kHz.

## Modes

`mode` (Global view) selects how tracks map to buffers:

- `norns` (default): every track shares two buffers, L and R, as norns softcut does. Tracks are
  four stereo playheads over one recording.
- `4 loop`: each track has its own L and R buffers, so the tracks are independent loops.
  The six extra buffers (48 MB) are allocated on the first switch and kept afterwards.

Switching keeps every buffer's contents. In `norns` mode, tracks 2-4 read the shared buffers again.

| IO | |
|-|-|
| In | In L, In R |
| Out | Out L, Out R (panned mix), Voice 1-8 (post level, pre pan) |

## Navigation

TRK-/TRK+ step through tracks 1-4, then the Global view. EN-/EN+ page through a track as one
sequence: the L voice's three pages, then the R voice's. The header shows the track, side and page.
Rate moves 0.05 per detent, 0.001 with the encoder held. Each voice's pages:

1. rate, start, end, level
2. rec level, pre level, in gain, pan
3. fade, slew, lpf, lp mix

Buttons: play, rec, loop, cut (momentary, jumps to loop start), Load, on, link, Save.

Tracks 1 and 2 start on, tracks 3 and 4 off. A voice that is off is not processed: it is silent,
records nothing and costs no CPU. It resumes from where it stopped.

Each track's L and R voices are linked by default (`link12` .. `link78`). While linked, an edit to
either voice is copied to its partner with pan mirrored, and the partner's playhead stays
sample-locked. Delink a track to give its voices different rates or loop points for stereo effects.
Relinking copies the L voice to the R voice and cuts both to the loop start.

## Files

Load (button 5) opens a file browser at `/media/BOOT/samples`; Load again loads the selection,
Cancel (button 7) returns. A mono file fills the current voice's buffer; a stereo file fills its
track's L and R buffers. The file is resampled to the engine rate and truncated at the buffer
length. Every voice that plays those buffers is set to loop the file, with rec off: all of them in
`norns` mode, the current track in `4 loop` mode. The preset stores each buffer's file and channel
and reloads them. The compact UI has no Load button.

Save (button 8) opens a name editor with a timestamped default (`sfct-YYMMDD-HHMMSS`); Save again
writes the file, Cancel (button 7) returns. It saves the buffers from 0 s to the end of the waveform
view as a 24-bit WAV: the track's L and R for a linked track, else the current voice's buffer as mono.
The file goes next to the buffer's current file, else to `/media/BOOT/samples`. Save never overwrites
an existing file. Afterwards the preset references the saved file, so it restores the recording. The
audio thread copies the buffers 32768 frames per block, so a voice recording during a save leaves
seams up to 0.17 s apart in the file. The status line under the waveform reports the result.

## Display

The waveform shows the current track's L and R buffers over the furthest loop end among enabled
voices that use them, at least 0.5 s. Each such voice draws its loop region in its track's colour,
the current voice brighter. Each voice has two playheads, which crossfade at cuts and loop points;
each is drawn as bright as its playback gain. A recording head is drawn thicker. Peaks are rescanned
on the audio thread, 4096 frames per block, so a 4 s view refreshes in 0.25 s.

The DSP readout shows `processBlock` time as a percentage of the block duration, on one core.
CPU scales with enabled voices: two tracks measured 10% on the SSP.

## Differences from norns softcut

- Quirks are fixed (`Voice(true)`). Upstream records polarity-inverted, which cancels against the dry signal in a patch.
- A voice starting from stopped cuts to its loop start. A softcut subhead stays silent until its first cut.
- As upstream, the record path soft-clips with a linear gain of 1.2 below 0.68.
- norns has six voices on 2^24-frame buffers (349 s); sfct has eight on 2^21 frames (43.7 s).

`make test` builds `tests/engine_test.cpp` natively and runs it.
