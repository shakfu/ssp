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

## Launchpad Mini MK3

- [ ] Design 2026-10-23: docs/dev/gseq-launchpad.md. All decisions made; step 1 is a device check.
- [ ] Launchpad X, velocity-sensitive key pads: built 2026-10-24, device steps 36b-36c pending.

## Piano roll mode

- [x] Built 2026-10-23: docs/dev/gseq-roll.md; usage in the README (Roll mode). Device test: docs/dev/gseq-device-test.md steps 37-45.

## Monome grid

- [ ] 16 x 8 grid built 2026-10-25: usage in the README (Monome grid), design in docs/dev/monome.md. Device test: docs/dev/gseq-device-test.md steps 47-56. Needs Synthor stopped to deploy (`make deploy-mod MOD=gseq`).
- [ ] Pin the libmonome fork's `LATENCY_MS 4` commit in scripts/build_monome.sh once pushed; the SSP build used the local checkout (`LIBMONOME_DIR`).
