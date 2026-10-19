# ezep TODO

## Verify on the SSP (0.1.0)

Done 2026-10-19 with `tests/run_on_ssp.sh`, outside Synthor: the golden hash, the engine tests, and the default banks from RAM (see README, Sound and timing).

In Synthor:

1. `make install MOD=ezep` and `make install-ezep`; restart. `ezep` is in the module list.
2. A new `ezep` loads `BOOT/ezep/default`: the status panel shows `default`, bank 1, a tempo, playing. Out L/R sound.
3. Each knob on the play and tap pages changes the sound. Volume past the middle adds saturation, then fuzz.
4. Buttons 1-4 and 6: Tap taps tempo; Mode changes the trigger mode on the panel; Mult changes the clock division; Bank changes bank at the loop's end; Play stops and starts.
5. A 2 PPQN clock on In 1 sets the tempo. Trig and Clock Out drive another module.
6. Amen, Break and Sample CV on In 9-11, patched and unpatched.
7. Load: a card folder, a bank folder, a file in a bank, `BOOT/ezep`, and a folder without a card (the card playing stays).
8. Save a preset after changing bank, tempo, division and trigger mode; reload it.
9. DSP readout with two instances; RAM with `free` before and after.
10. Delete the module while it plays (unload).

## v2

- The LED ring and mode LEDs, from `core_engine_display`.
- Mode held: Sample, Break and Amen set chaos, saturation and Amiga bit depth (`INCLUDE_EZEPTOCORE`).
- Tap + Mode: reset to beat 1 with a clock, the sample's tempo without one.
- The Grimoire editor: the seven effect banks, and each effect's parameters.
- The card's settings as controls: CV polarity, Amen CV behaviour, sample CV mapping, clock stop, reset input.
- MIDI in, which the Rack module also leaves out.
- Tempo as a control, not only tap and clock.
- Sample names from `N.name.json`.
- Inputs 7 and 8.

## Open

- A Play press during the firmware's start-up restart is ignored, as on the hardware. The test triggers after 2 s.
- The bench's 3.7 ms blocks at normal priority: preemption beside Synthor (none at FIFO priority). Check Synthor's DSP peak in step 9.
- The resampler is 4-point Hermite. The Rack module uses Speex at quality 8. Compare by ear.
