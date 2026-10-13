# ttyp

`ttyp` runs [monome teletype](https://github.com/monome/teletype) 5 on the Percussa SSP. It is the unmodified teletype firmware, with its live, edit, pattern, preset and help modes, typed into from a USB keyboard. Scripts react to trigger inputs and drive trigger and CV outputs.

The firmware runs against [monome-rack](https://github.com/Dewb/monome-rack)'s mock hardware layer, as in VCV Rack. The design is in [docs/dev/teletype.md](../../docs/dev/teletype.md); open work is in [TODO.md](TODO.md).

`ttyp` makes no audio. Like the hardware, it is a control module: triggers in, triggers and CV out.

## Install

Copy `ttyp.so` to the `plugins` folder on the SSP's SD card and restart the SSP. The firmware is inside `ttyp.so`. `ttyp` is listed under Modulation.

## Keyboard

### USB

Plug the keyboard into a **USB 2.0 hub** on the SSP's free USB port. A keyboard plugged straight into that port does not enumerate: the port's controller (EHCI) handles only high-speed devices, and keyboards are low or full speed. A USB 2.0 hub converts between them. A USB 1.1 hub does not.

The keyboard goes to the `ttyp` instance whose screen was shown last. `ttyp` takes the keyboard for itself, so the Linux console does not see it.

### Network

`ttyp` also listens on 127.0.0.1, port 7770. Forward the port from your computer, then type with [`tools/ttyp_type.py`](../../tools/ttyp_type.py):

```
ssh -L 7770:127.0.0.1:7770 root@192.168.1.6
tools/ttyp_type.py lines.txt        # or: echo "TR.P 1" | tools/ttyp_type.py
```

Each newline is Enter. The text goes to whichever mode is active: live mode runs each line, edit mode adds it to the script.

### Keys

Keys follow teletype's [manual](https://monome.org/docs/teletype/manual/). The ones to start with:

| Key | Action |
|-|-|
| Tab | live, edit, pattern mode, in turn |
| Esc | scene read (load); Esc again returns |
| Alt+Esc | scene write (save) |
| `[` `]` | previous and next script, in edit mode |
| Alt+F1 to F8, F9, F10 | edit script 1-8, M, I |
| F1 to F8, F9, F10 | run script 1-8, M, I |
| Enter | run the line (live), or store it (edit) |

## Inputs and outputs

| SSP | teletype | Behaviour |
|-|-|-|
| Trig 1-8 | trigger inputs | A rising edge above 2.2 V runs script 1-8. `STATE n` reads the input's level. `MUTE n` stops it running its script |
| In | IN | `IN` reads it as 0-16383. Read when a script uses it; a change runs nothing |
| TR 1-4 | TR outputs | 0 or 5 V. `TR n x` sets the level, `TR.P n` pulses for `TR.TIME n` ms (default 100) |
| CV 1-4 | CV outputs | `CV n x`, with `V`, `N` and the other teletype scalings; `CV.SLEW` slews |

The metro (`M`) and delays (`DEL`) run scripts without any input.

The line under the screen shows In in teletype's volts and Trig 1-8, lit while high. An input Synthor reports as unpatched reads 0 and stays dark. The status panel shows the outputs.

## Controls

| Control | Main page | Range page |
|-|-|-|
| Encoder 1 | PARAM, teletype's knob (`PARAM`, 0-16383) | CV 1 range |
| Encoder 2 | In range | CV 2 range |
| Encoder 3 | | CV 3 range |
| Encoder 4 | | CV 4 range |
| Button 1 | teletype's front panel button | |
| Button 2 | range page | main page |
| Button 3 | Disk: scene files, as teletype's USB stick | |

The compact view in rack shows the screen and has the main page's encoders only.

### Range

The SSP's outputs and inputs span about +/-5 V. Teletype's span 0-10 V. Each CV output and In has a range:

- `0-5V`: volts as teletype writes them, clipped above 5 V. `V` and `N` stay correct up to 5 V, five octaves of 1 V/oct.
- `-5-5V`: teletype's 0-10 V shifted down by 5 V. The full range, at the wrong absolute voltage: `V 5` gives 0 V. On In, the shift is added back.

## Saving programs

A teletype program is a **scene**: eight scripts, M and I, patterns, and a description. Teletype keeps 32 scenes.

- **Save a scene:** Alt+Esc, choose a slot, Enter.
- **Load a scene:** Esc, choose a slot, Enter. This replaces the scripts, patterns and description, then runs the scene's I script.
- **`SCENE n`** loads a scene from a script without running I. Variables, CV and TR outputs and the metro carry over, so a running scene can hand over to another mid-performance. Add `$ 10` to run the new I too. Put `SCENE n` last in its script: lines after it come from the new scene. `SCENE.P n` keeps the patterns; `SCENE.G n` keeps the grid state.
- **Synthor presets** save all 32 scenes and the scene being edited, so a preset is a complete teletype. A preset made by another `ttyp` version whose teletype stores scenes differently is refused, and the module keeps its current scenes.

### Scene files

**Disk** (button 3) does what plugging a USB stick into teletype does, with `/media/BOOT/teletype` as the stick:

1. Every scene is written to `tt00s.txt` to `tt31s.txt`, replacing those files.
2. Each `tt00.txt` to `tt31.txt` that exists is read into its slot, replacing that scene.

The status panel shows the counts and any error. The scene being edited is unchanged: load a slot with Esc to use what was read. A file's name without the `s` is never written, so files you write are never overwritten.

This is how to write programs ahead of time, or use scenes others have shared: put `ttNN.txt` files in `/media/BOOT/teletype`, press Disk, then load the slot. [`examples/tt00.txt`](examples/tt00.txt) is one to start from. To keep a scene you made on the SSP, press Disk and rename its `ttNNs.txt` to `ttNN.txt`.

The format is teletype's own:

```
CLOCK DIVIDER
TRIG 1: CLOCK IN

#1
EVERY 2: TR.P 1
EVERY 4: TR.P 2

#I
TR.TIME 1 20
```

- The lines before the first `#` are the description, at most 32 lines of 31 characters. A longer line runs into the next.
- `#1` to `#8`, `#M` and `#I` hold the scripts, at most 6 lines each. `#P` holds the patterns and `#G` the grid, in the format teletype writes; see any `ttNNs.txt`.
- A section left out stays empty. Case does not matter.
- A file over 64 KB is refused.

## Constraints on the SSP

Compared with the hardware:

- **Voltage.** CV outputs reach 5 V, not 10 V; see Range. TR outputs give 5 V, not 10 V. In reads about +/-5 V.
- **Timing.**
  - The firmware runs on a 1 ms tick, as on the hardware.
  - Outputs change once per audio block, 2.7 ms at 128 samples and 48 kHz. A pulse shorter than a block can be missed: keep `TR.TIME` above about 5 ms.
  - A trigger reaches its script within one tick plus one block.
  - A long script delays teletype, as on the hardware. It never stalls Synthor's audio: the firmware has its own thread.
- **Falling edges.** Scripts run on rising edges only. `$.POL` settings that run a script on a falling edge do nothing; monome-rack's mock raises no interrupt on a falling edge.
- **Not connected:** i2c devices (Ansible, Just Friends, TELEX, ER-301, W/, crow, Disting, i2c2midi), grid and arc, and USB MIDI. Their ops parse and run, but nothing answers them. Disk replaces the USB stick.
- **PARAM** is an encoder, not a jack. It cannot be modulated by CV.
- **Keyboard.**
  - Keys are read as a US layout.
  - A key released within 47 ms is missed, as on the hardware: the firmware polls the keyboard at that interval.
  - One keyboard for all instances; it follows the screen shown last.
  - Some shortcuts need function keys. On a compact keyboard, use its Fn layer.
- **Network typing** with `tools/ttyp_type.py` sends taps: no held keys, and no modifier but Shift. The port takes one client at a time.
- **Instances.** Each instance runs its own copy of the firmware, about 1 MB with its scene memory, and its own 1 ms thread.
- **Releases.** `ttyp` is not in the release package yet: teletype is GPL-2 and this repository AGPL-3, and that is unresolved.

## Troubleshooting

- **Not in the module list:** restart the SSP; Synthor scans plugins at startup.
- **Keyboard does nothing:** check the status panel says `keyboard: connected`. If not, plug the keyboard in through a USB 2.0 hub. `dmesg` on the SSP shows whether it enumerated.
- **Typing goes to the wrong instance:** show the instance you want to type into; the keyboard follows the screen.
- **A trigger runs nothing:** check its number lights under the screen while the signal is high. If it does not, check the patch goes to that Trig input and rises above 2.2 V. If it does, check the input is not muted (Ctrl+F1 to F8 toggles mutes).
- **In reads nothing:** In is the 9th input, after Trig 1-8. The line under the screen shows its value.
