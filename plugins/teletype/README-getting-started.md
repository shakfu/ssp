# Getting started with ttyp

This guide sets up `ttyp`, runs its first commands and scripts, and saves a program. [README.md](README.md) is the reference: keys, inputs and outputs, controls, scene files and constraints. Teletype's own [documentation](https://monome.org/docs/teletype/) covers the language.

## 1. What you need

- A USB keyboard, and a **USB 2.0 hub**. The SSP's free USB port does not enumerate a keyboard plugged in directly.
- Optional: a clock source in Synthor, such as an LFO's square output, and a voltmeter.

## 2. Install

1. Copy `BOOT/plugins/ttyp.so` and the `BOOT/teletype` folder from the release onto the card's BOOT partition.
2. Restart the SSP. Synthor scans plugins at startup.
3. Plug the hub into the free USB port and the keyboard into the hub.

## 3. Add the module and patch it

Add `ttyp` from the module list; it is under Modulation. Its jacks follow teletype's panel:

| `ttyp` | Kind |
|-|-|
| inputs 1-8: Trig 1-8 | trigger inputs |
| input 9: In | CV input |
| outputs 1-4: TR 1-4 | trigger outputs |
| outputs 5-8: CV 1-4 | CV outputs |

Open the module's screen. The left half is teletype's display. The status panel on the right shows `keyboard: connected`, the inputs, and the outputs. Inputs that are not patched are dark grey.

## 4. First commands

Teletype starts in live mode: each line you type runs when you press Enter. Try:

```
TR.P 1
CV 1 V 3
X 5
X
```

- `TR.P 1` pulses TR 1 for 100 ms. The status panel shows `TR 1 on` while it is high.
- `CV 1 V 3` sets CV 1 to 3 V.
- `X 5` sets the variable X; `X` alone prints it.

## 5. A script, run by a trigger

A script runs when its trigger input rises. Patch TR 1 to Trig 1 in Synthor, so `ttyp` triggers itself.

1. Press Alt+F1 to edit script 1. Type `X + X 1` and press Enter.
2. Press Tab until the screen shows live mode again. Tab cycles live, edit and pattern mode.
3. Type `TR.P 1`. Trig 1 lights amber in the status panel, and script 1 runs.
4. Type `X`. It prints 1, and counts up with each `TR.P 1`.

## 6. The metro

The metro runs the M script at a fixed rate, with nothing patched.

1. Press Alt+F9 to edit M. Type `TR.P 1` and press Enter. Return to live mode.
2. Type `M 500`, then `M.ACT 1`. TR 1 pulses every 500 ms; with the patch from step 5, X counts up.
3. `M.ACT 0` stops it.

## 7. Reading In

In is the one input whose value teletype reads. A script reads it with `IN` when it runs; a change on In runs nothing. Patch a voltage to input 9 and type `IN`. It prints 0-16383 for 0-10 V: about 1638 per volt.

For a sample and hold, put `CV 1 IN` in script 1 and clock Trig 1. To read In in semitones instead, type `IN.SCALE 0 120` once; `CV 1 N IN` then gives CV 1 the nearest semitone. The example scene `tt01.txt` does this.

The SSP's jacks span about +/-5 V; teletype's span 0-10 V. Button 2 opens the range page, where each CV output can shift down by 5 V. See Range in [README.md](README.md).

## 8. Save your work

- **Alt+Esc** saves the scene being edited to one of 32 slots: choose a slot and press Enter.
- **Esc** loads a slot: choose it and press Enter. Loading runs the scene's I script, its setup.
- A **Synthor preset** saves all 32 slots and the scene being edited.

## 9. Write scenes on a computer

Disk (button 3) reads and writes scenes as teletype's text files in `/media/BOOT/teletype`, as a USB stick does on the hardware.

1. Press Disk. Every slot is written to `tt00s.txt` to `tt31s.txt`. The status panel shows `disk: wrote 32, read 0`.
2. Copy a scene file to `/media/BOOT/teletype` as `ttNN.txt`, where NN is the slot to load it into. The release's examples are in `/media/BOOT/teletype/examples`:

   | File | Scene | Patch |
   |-|-|-|
   | `tt00.txt` | clock divider: TR 1 every 2nd clock, TR 2 every 4th, a random CV on CV 1 | a clock into Trig 1 |
   | `tt01.txt` | sample and hold in semitones: In to CV 1 on each clock | a clock into Trig 1, a CV into In |
   | `tt02.txt` | random gates and notes from the metro | none |

   For example, copy `examples/tt02.txt` to `tt02.txt`.
3. Press Disk. The status panel shows `read 1`.
4. Press Esc, choose the slot, and press Enter. `tt02` starts at once: its I script starts the metro.

A scene file is plain text:

```
RANDOM GATES

#M
TR.P RRAND 1 4
CV 1 N RAND 24

#I
M 200
M.ACT 1
```

The lines before the first `#` describe the scene, at most 31 characters each. `#1` to `#8`, `#M` and `#I` hold the scripts, at most 6 lines each. To keep a scene made on the SSP, press Disk and rename its `ttNNs.txt` to `ttNN.txt`. The files without an `s` are never overwritten.

## 10. Type from a computer

`ttyp` listens for keys on port 7770 of the SSP itself. From a computer on the SSP's network, forward the port over ssh and type a file in with [`tools/ttyp_type.py`](../../tools/ttyp_type.py) from the repository:

```
ssh -L 7770:127.0.0.1:7770 root@<ssp-address>
tools/ttyp_type.py lines.txt
```

Each line goes to the mode on screen: in live mode it runs, in edit mode it is added to the script.

## Next

- [README.md](README.md): every key, control and constraint.
- Teletype's [documentation](https://monome.org/docs/teletype/): the manual, the op reference and the studies.
