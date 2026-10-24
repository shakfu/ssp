# ttyp

`ttyp` runs [monome teletype](https://github.com/monome/teletype) 5 on the Percussa SSP. It is the unmodified teletype firmware, with its live, edit, pattern, preset and help modes, typed into from a USB keyboard.

New to `ttyp`? Start with [README-getting-started.md](README-getting-started.md).

The firmware runs against [monome-rack](https://github.com/Dewb/monome-rack)'s mock hardware layer, as in VCV Rack.

## Install

Copy `ttyp.so` to `BOOT/plugins` (from source: `make install MOD=ttyp`) and restart the SSP. The firmware is inside `ttyp.so`. `ttyp` is listed under Modulation.

`ttyp` reads scene files from `BOOT/teletype`. The release's `BOOT/teletype/examples` holds three example scenes; copy that folder too.

It needs a USB keyboard on a USB 2.0 hub; see [Keyboard](#keyboard).

## First use

Follow [README-getting-started.md](README-getting-started.md). It patches `ttyp` ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)), runs a first command, and saves a scene.

## The language

### What it is

Teletype runs short scripts of one-line commands; trigger input n runs script n. Commands use prefix notation, evaluated right to left: `CV 1 N 12` sets CV 1 to note 12. A scene holds scripts 1-8, a metro script M, an init script I, four 64-step patterns, and integer variables. See the [manual](https://monome.org/docs/teletype/manual/) and the [studies](https://monome.org/docs/teletype/studies-1/); [README-getting-started.md](README-getting-started.md) is a fuller tutorial here.

### A first program

Press Alt+F1 to edit script 1, type each line, and press Enter after it:

```text
CV 1 N RAND 24
TR.P 1
```

- `RAND 24`: a random integer, 0 to 24.
- `N`: a note number as a CV value, 1 V/oct. `CV 1` sets Out 5.
- `TR.P 1`: a 100 ms pulse on Out 1 (`TR.TIME 1` sets the length).
- A rising edge on In 1, or F1, runs the script.

### How it maps to the SSP

| SSP | In teletype |
|-|-|
| Audio in | n/a: In 1-8 are triggers, In 9 is CV |
| Audio out | n/a: Out 1-8 carry triggers and CV |
| Encoders | `PARAM`: encoder 1, 0-16383 ([Controls](#controls)) |
| CV in | `IN`: In 9, 0-16383 ([Jacks](#jacks), [Range](#range)) |
| CV out | `CV 1`-`CV 4`: Out 5-8, 0-5 V by default ([Range](#range)) |
| Gates and triggers | In 1-8 run scripts 1-8; `TR 1`-`TR 4` on Out 1-4 ([Jacks](#jacks)) |
| MIDI in | n/a: `MI` ops need teletype's USB port ([Constraints](#constraints-on-the-ssp)) |
| MIDI out | n/a: only through i2c2midi, not connected |
| Clock and tempo | `M` metro, `DEL`, `BPM`; a 1 ms tick; Synthor's tempo unused |
| Sample rate and block size | outputs change once per block, 2.7 ms ([Constraints](#constraints-on-the-ssp)) |
| Files on the card | `BOOT/teletype`: `ttNN.txt` scenes, read by Disk ([Scene files](#scene-files)) |
| Not available on the SSP | i2c ops, grid, arc, USB MIDI, falling-edge `$.POL` ([Constraints](#constraints-on-the-ssp)) |

## Keyboard

### USB

Plug the keyboard into a USB 2.0 hub on the SSP's free USB port. Plugged in directly, or through a USB 1.1 hub, it is not detected ([Using](../../docs/USING.md#usb-devices)).

The keyboard goes to the keyboard module whose screen was shown last: a `ttyp` instance, `sapf`, or the Code view of `fstr`, `csnd`, `chuk` or `alda`. With none on screen, whichever holds it keeps it. The module takes the keyboard for itself, so the Linux console does not see it.

### Network

`ttyp` also listens on 127.0.0.1, port 7770. Forward the port from your computer, then type with [`tools/ttyp_type.py`](../../tools/ttyp_type.py):

```text
ssh -L 7770:127.0.0.1:7770 root@<ssp-address>
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

## Jacks

| Jack | Name | teletype | Behaviour |
|-|-|-|-|
| In 1-8 | `Trig 1`-`Trig 8` | trigger inputs | A rising edge above 2.2 V runs script 1-8. `STATE n` reads the input's level. `MUTE n` stops it running its script |
| In 9 | `In` | IN | `IN` reads 0-16383 for 0-10 V; with the default In range, 0-5 V reads about 0-8191. Read when a script uses it; a change runs nothing |
| Out 1-4 | `TR 1`-`TR 4` | TR outputs | 0 or 5 V. `TR n x` sets the level, `TR.P n` pulses for `TR.TIME n` ms (default 100) |
| Out 5-8 | `CV 1`-`CV 4` | CV outputs | `CV n x`, with `V`, `N` and the other teletype scalings; `CV.SLEW` slews |

The metro (`M`) and delays (`DEL`) run scripts without any input.

The status panel, right of the screen, shows the inputs above the outputs: In in teletype's volts, and Trig 1-8. A Trig number is:

- dark grey while unpatched;
- white while patched and low;
- amber while high.

In is dimmed while unpatched; it then reads 0.

Inputs 1-8 are always triggers and input 9 is always In: teletype's trigger inputs are digital, and it has one CV input. A voltage patched into a Trig input is seen only as above or below 2.2 V.

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

The SSP's outputs and inputs span about +/-5 V. Teletype's span 0-10 V. Each CV output and In has a range, `0-5V` by default:

- `0-5V`: volts as teletype writes them, clipped above 5 V. `V` and `N` stay correct up to 5 V, five octaves of 1 V/oct.

- `-5-5V` (-5 V to +5 V): teletype's 0-10 V shifted down by 5 V. It gives the full range, offset by 5 V: `V 5` gives 0 V. On In, 5 V is added back.

## Saving programs

A teletype program is a **scene**: eight scripts, M and I, patterns, and a description. Teletype keeps 32 scenes.

- **Save a scene:** Alt+Esc, choose a slot, Enter.

- **Load a scene:** Esc, choose a slot, Enter. This replaces the scripts, patterns and description, then runs the scene's I script.

- **`SCENE n`** loads a scene from a script without running I. Variables, CV and TR outputs and the metro carry over, so a running scene can hand over to another mid-performance. Add `$ 10` to run the new I too. Put `SCENE n` last in its script: lines after it come from the new scene. `SCENE.P n` keeps the patterns; `SCENE.G n` keeps the grid state.

- **Synthor presets** save all 32 scenes and the scene being edited, so a preset is a complete teletype. A preset made by another `ttyp` version whose teletype stores scenes differently is refused, and the module keeps its current scenes.

### Scene files

**Disk** (button 3) does what plugging a USB stick into teletype does, with `BOOT/teletype` ([card paths](../../docs/USING.md#card-paths)) as the stick:

1. Every scene is written to `tt00s.txt` to `tt31s.txt`, replacing those files.

2. Each `tt00.txt` to `tt31.txt` that exists is read into its slot, replacing that scene.

The status panel shows the counts and any error. Disk leaves the scene being edited unchanged; load a slot with Esc to use what it read. Disk never writes a `ttNN.txt`, so your own files are safe.

To load a scene written elsewhere:

1. Put it in `BOOT/teletype` as `ttNN.txt`, where NN is the slot.
2. Press Disk.
3. Load slot NN with Esc.

To keep a scene made on the SSP, press Disk and rename its `ttNNs.txt` to `ttNN.txt`.

The release puts three scenes in `BOOT/teletype/examples`, where Disk does not read them. Copy one up a level to load it.

- [`tt00.txt`](examples/tt00.txt): a clock divider.
- [`tt01.txt`](examples/tt01.txt): a sample and hold in semitones.
- [`tt02.txt`](examples/tt02.txt): random gates from the metro.

The format is teletype's own:

```text
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

- **Timing:** the firmware runs on a 1 ms tick, as on the hardware.

- **Output rate:** outputs change once per audio block, 2.7 ms at 128 samples and 48 kHz. A pulse shorter than a block can be missed: keep `TR.TIME` above about 5 ms.

- **Trigger latency:** a trigger reaches its script within one tick plus one block.

- **Long scripts** delay teletype, as on the hardware. They never stall Synthor's audio.

- **Falling edges.** Scripts run on rising edges only. `$.POL` settings that run a script on a falling edge do nothing; monome-rack's mock raises no interrupt on a falling edge.

- **Not connected:** i2c devices (Ansible, Just Friends, TELEX, ER-301, W/, crow, Disting, i2c2midi), grid and arc, and USB MIDI. Their ops parse and run, but nothing answers them. Disk replaces the USB stick.

- **PARAM** is an encoder, not a jack. It cannot be modulated by CV.

- **Keyboard layout:** keys are read as a US layout.

- **Short taps:** a key released within 47 ms is missed, as on the hardware. The firmware polls the keyboard at that interval.

- **One keyboard** serves all instances and keyboard modules. It follows the screen shown last.

- **Function keys:** some shortcuts need them. On a compact keyboard, use its Fn layer.

- **Network typing** with `tools/ttyp_type.py` sends taps: no held keys, and no modifier but Shift. The port takes one client at a time: a new connection replaces the old.

- **Instances.** Each instance runs its own copy of the firmware, about 1 MB with its scene memory, and its own 1 ms thread.

## Troubleshooting

- **Not in the module list:** restart the SSP; Synthor scans plugins at startup.

- **Keyboard does nothing:** check the status panel says `keyboard: connected`. If not, plug the keyboard in through a USB 2.0 hub. `dmesg` on the SSP shows whether it enumerated.

- **Typing goes to the wrong instance:** show the instance you want to type into; the keyboard follows the screen.

- **A trigger runs nothing:** check its number lights in the status panel while the signal is high. If it does not, check the patch goes to that Trig input and rises above 2.2 V. If it does, check the input is not muted (Ctrl+F1 to F8 toggles mutes).

- **In reads nothing:** In is the 9th input, after Trig 1-8. The status panel shows its value.

## Licence

`ttyp` is GPL-3.0, unlike the AGPL-3.0 of the rest of this repository. [NOTICE.md](NOTICE.md) lists what the embedded firmware is built from and on what terms; [LICENSE](LICENSE) is the GPL-3.0 text.
