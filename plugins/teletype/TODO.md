# ttyp TODO

## Done on the SSP

- `tests/run_on_ssp.sh` passes (2026-10-12): the engine and firmware, without Synthor.
- In Synthor (user, 2026-10-13): listed after a restart; a USB keyboard through a USB 2.0 hub types into it; TR 1 and TR 2 patched to Trig 1 and Trig 2 run scripts 1 and 2, by `TR.P` and from the metro.
- Disk in Synthor (user, 2026-10-13): wrote 32 scenes, read `examples/tt00.txt` into slot 0; loaded with Esc and clocked, TR 1, TR 2 and CV 1 changed as the scene says.

## CV test

Checks CV out, In, both ranges, and the SSP's output ceiling. Done so far (user, 2026-10-13, voltmeter on CV 1, plain cable): readings match the status panel up to 5 V, and `V 5`, `V 6` and `V 10` read 5 V, so outputs reach 5 V and clip there. A patch cable with LEDs read about 0.5 V low; measure with a plain cable. In: `IN` read 4877 (2.98 V) with about 3 V applied, within 1%. In is the 9th input; patched to input 1 (Trig 1), it read nothing. Whether the SSP could reach 5.2 V is untested: `ttyp` limits the signal to 1.0.

**Setup.** One `ttyp`, both ranges at `0-5V` (button 2 for the range page, encoder 1 for CV 1; In's range is encoder 2 on the main page). Patch CV 1 (output 5) to In (input 9). Optional: a voltmeter on the CV 1 jack. Type in live mode.

1. **Out to In.** Type `CV 1 V 3` then `CV 2 IN`.
   - Pass: the status panel shows CV 1 about 3.00 V and CV 2 about 3.00 V. `IN` alone prints about 4915.
   - Voltmeter: 3.00 V at CV 1.
2. **Zero.** `CV 1 0` then `CV 2 IN`. Pass: CV 2 about 0.00 V, `IN` about 0.
3. **Ceiling.** `CV 1 V 7` then `CV 2 IN`.
   - Pass: CV 1 shows 7.00 V (teletype's value); CV 2 about 5.0 V; `IN` about 8191.
   - Voltmeter: 5.0 V. Record whether it is 5.0 or 5.2 V. If 5.2 V, the output limit in `PluginProcessor::processBlock` (1.0) could be 1.04.
4. **Shifted range.** CV 1's range and In's range to `-5-5V`. `CV 1 V 7` then `CV 2 IN`.
   - Pass: CV 2 about 7.00 V.
   - Voltmeter: 2.0 V at CV 1 (7 - 5).
5. **Shifted ends.** Still shifted: `CV 1 0` gives -5 V at the jack and CV 2 about 0 V. `CV 1 V 10` gives +5 V and CV 2 about 10 V.
6. **Mismatched ranges.** CV 1 at `-5-5V`, In at `0-5V`. `CV 1 V 3`.
   - Pass: -2 V at the jack; In clips negative input to 0, so CV 2 about 0 V.
7. **Moving signal.** Ranges back to `0-5V`. M script: `CV 1 V RAND 4` and `CV 2 IN`; then `M 250`, `M.ACT 1`.
   - Pass: CV 2 follows CV 1 within one tick. Stop with `M.ACT 0`.
8. **Slew.** `CV.SLEW 1 1000` then `CV 1 V 4`.
   - Pass: CV 1 rises to 4 V over about 1 s, in steps of at most one audio block.
9. **PARAM.** M script: `CV 3 PARAM`; then `M 50`, `M.ACT 1`, and turn encoder 1. (Typed in live mode, `CV 3 PARAM` would read PARAM once.)
   - Pass: CV 3 in the status panel goes from 0 V at Param 0 to 10 V at 1; the jack clips at 5 V.

## Verify on the SSP

- [ ] Held keys repeat; unplug and replug the keyboard.
- [ ] Two instances: the keyboard follows the one on screen (`onSSPTimer` sets the target; does Synthor call it for hidden editors?).
- [ ] No keys reach the Linux console or SysRq (`EVIOCGRAB`).
- [ ] Preset save and reload in Synthor and in rack; the compact editor in rack.
- [ ] Front button (button 1) and the range page (button 2).
- [ ] Disk: how long the UI pauses while it writes the 32 files.
- [ ] DSP load in Synthor's display; the firmware thread's CPU in `top`.
- [ ] Unload: Synthor unloading `ttyp.so` with the key thread stopped.
- [ ] A keyboard on the DWC2 port without a hub, after swapping the Ethernet dongle to the other port.

## Planned

- [ ] Falling-edge scripts (`$.POL`): the mock raises an interrupt on rising edges only.

## Open

- [ ] Licence: teletype and monome-rack are GPL-2 with no "or later" grant; this repo is AGPL-3. Until it is settled, `UNRELEASED` in `scripts/release.py` keeps `ttyp` out of release packages.
