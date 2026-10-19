# brks TODO

## Verify on the SSP (0.1.0)

Done 2026-10-19 with `tests/run_on_ssp.sh`, outside Synthor: the golden hash, the engine tests, and the default banks from RAM (see README, Sound and timing).

In Synthor:

1. `make install MOD=brks` and `make install-brks`; restart. `brks` is in the module list.
2. A new `brks` loads `BOOT/brks/default`: the status panel shows `default`, bank 1, a tempo, playing. Out L/R sound.
3. Each knob on the play and sound pages changes the sound; Grimoire with Break past a quarter. Volume past the middle adds saturation, then fuzz.
4. Buttons 1-3: Tap taps tempo; Mode changes the trigger mode on the panel; Play stops and starts. Left/Right change bank at the loop's end; Right twice moves two banks. Clock Out changes Clock Out's rate.
5. A 2 PPQN clock on In 1 sets the tempo, and Tempo shows it. Without one, Tempo changes the speed. Trig and Clock Out drive another module.
6. Amen, Break and Sample CV on In 9-11, patched and unpatched.
7. Load: a card folder, a bank folder, a file in a bank, `BOOT/brks`, and a folder without a card (the card playing stays).
8. Save a preset after changing bank, Tempo, Clock Out and trigger mode; reload it.
9. DSP readout with two instances; RAM with `free` before and after.
10. Delete the module while it plays (unload).

## v2

- The LED ring and mode LEDs, from `core_engine_display`.
- Mode held: Sample, Break and Amen set chaos, saturation and Amiga bit depth.
- Tap + Mode: reset to beat 1 with a clock, the sample's tempo without one.
- The Grimoire editor: the seven effect banks, and each effect's parameters.
- The card's settings as controls: CV polarity, Amen CV behaviour, sample CV mapping, clock stop, reset input.
- MIDI in, which the Rack module also leaves out.
- Sample names from `N.name.json`.
- Inputs 7 and 8.

## Open

- A Play press during the firmware's start-up restart is ignored, as on the hardware. The test triggers after 2 s.
- The bench's 3.7 ms blocks at normal priority: preemption beside Synthor (none at FIFO priority). Check Synthor's DSP peak in step 9.
- The resampler is 4-point Hermite. The Rack module uses Speex at quality 8. Compare by ear.
