# gseq TODO

## Record from a MIDI keyboard

- [x] Built 2026-10-22; usage in the README (Recording). Device test: docs/dev/gseq-device-test.md, steps 20-25.

## Scales

- [ ] A scale and root per row, for one row or several.

Open:

1. Snap on entry or on playback? On entry, encoder 1 moves a step's note by scale degrees and the stored notes stay in the scale. On playback, stored notes keep their pitch and play quantized to the scale, so changing the scale re-harmonizes a pattern without touching it. Both can be had: degrees on entry, quantize on playback.
2. Recorded notes: quantize to the row's scale, or keep what was played?
3. Several rows at once: copy the cursor row's scale to every row (a held button), or one pattern scale that each row can override.
4. Where: the row page's four encoders are taken (channel, voice, velocity CV, division). A second row page, with Row pressed again to reach it, can hold scale and root.
