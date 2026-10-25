# mlsp

`mlsp` runs ModuLisp, the language of the [uSEQ](https://github.com/Emute-Lab-Instruments/uSEQ) eurorack module, as gates and CV. Each output is a function of time: `(d1 (sqr beat))` is a gate on every beat. The module makes no sound itself: patch the outputs into envelopes, oscillators and other modules. The program is text, written in the module from a USB keyboard or loaded from the card.

It embeds uSEQ's signal engine through the fork [shakfu/useq](https://github.com/shakfu/useq), which adds the SSP's eight inputs and keeps only the engine's source.

**Experimental.** `mlsp` is not in releases until the licence of uSEQ's software is formally clarified. Build it from source.

## Install

Build from source, then `make install MOD=mlsp` to a mounted card, and restart the SSP. Releases do not include it. See [Using](../../docs/USING.md#installing).

The example programs go in `BOOT/mlsp`, where the browser opens. Copy `examples/` there by hand.

A USB keyboard edits the program; it is optional ([Keyboard](#keyboard)).

## First use

1. Add `mlsp`. Patch `d1` (Out 1) to an envelope's gate and `a1` (Out 5) to a filter's cutoff. Synthor runs a module only while a jack is patched ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)). The built-in program runs: `d1` gates on every beat at 120 bpm.
2. Patch `d2`-`d4` and `a2`-`a4` to hear the rest of the built-in.
3. Press Load (button 5), select `clocked-euclid.useq`, and press Load again. It follows a clock on In 2, so patch one there, 4 pulses a quarter note.

## The language

### What it is

ModuLisp is a Lisp in which a program assigns expressions to outputs. Time is the input: `t` is seconds, and `beat`, `bar`, `phrase` and `section` are phasors, ramps from 0 to 1 over their length. Functions such as `sqr`, `seq`, `gates` and `euclid` turn a phasor into a gate pattern or a sequence of values. An output reassigned keeps running without a break. See uSEQ's [docs](https://github.com/Emute-Lab-Instruments/uSEQ/tree/main/docs) and its [function spec](https://github.com/Emute-Lab-Instruments/uSEQ/blob/main/docs/specs/functions.md).

### A first program

```clojure
; @p1 depth 0 1
(set-bpm 100)
(d1 (sqr beat))
(d2 (euclid 3 8 bar))
(a1 (seq [0 0.25 0.5 0.75] bar))
(a2 (* p1 (usin phrase)))
```

- `(set-bpm 100)`: 100 beats a minute.
- `(d1 (sqr beat))`: Out 1 high for the first half of each beat.
- `(d2 (euclid 3 8 bar))`: three hits spread over eight steps a bar, on Out 2.
- `(a1 (seq ...))`: four steps a bar on Out 5: 0, 1.25, 2.5 and 3.75 V.
- `(a2 (* p1 (usin phrase)))`: a sine over a four-bar phrase on Out 6, scaled by encoder 1.

### How it maps to the SSP

| SSP | In ModuLisp |
|-|-|
| Audio in | n/a: `mlsp` samples its inputs as CV |
| Audio out | n/a: `mlsp` makes no sound |
| Encoders | `p1`-`p16`, declared by `; @pN` lines ([Controls](#controls)) |
| CV in | `ain1`-`ain8`: In 1-8, 0 to 5 V as 0 to 1 |
| CV out | `a1`-`a4`: Out 5-8, 0 to 1 as 0 to 5 V |
| Gates and triggers | `d1`-`d4`: Out 1-4; `in1`-`in8`: In 1-8 as gates |
| MIDI in | `; @clock midi`: clock and transport only ([Clock](#clock)) |
| MIDI out | n/a ([Not yet](#not-yet)) |
| Clock and tempo | `(set-bpm N)`, or `; @clock` for In 2 ([Clock](#clock)) |
| Sample rate and block size | the outputs update every 16 frames, 3 kHz at 48 kHz |
| Files on the card | `BOOT/mlsp`: `.useq` programs, `live.useq` ([Install](#install)) |
| Not available on the SSP | uSEQ's serial protocol, editor and flash storage; `a5`-`a8`, `d5`-`d8`, `s1`-`s8` have no jack |

## Use

Load (button 5) opens a browser. Choose a `.useq` and press Load again. With no program, the built-in runs.

A program runs only if all of it compiles. One with an error leaves the previous program running, and the screen shows the error and its line. A new program replaces the running one at once. An output both programs assign keeps its state, such as a `slew` or a counter; an output only the old one assigned goes to 0 V.

Every name a program defines is checked when it runs, so a program that uses a name only Ctrl-E defined fails. Put the definition in the program.

## Inputs and outputs

| Jack | Name | In ModuLisp |
|-|-|-|
| In 1-8 | `In 1`-`In 8` | `in1`-`in8`: 1 above 1 V, else 0. `ain1`-`ain8`: 0 to 5 V as 0 to 1, clamped |
| Out 1-4 | `d1`-`d4` | `d1`-`d4`: 5 V while the value is above 0.5, else 0 V |
| Out 5-8 | `a1`-`a4` | `a1`-`a4`: 0 to 1 as 0 to 5 V, clamped |

- An output the program does not assign is 0 V.
- In 1 also resets the time with `; @reset`, and In 2 takes a clock with `; @clock` ([Clock](#clock)). Both still read as `in1`, `ain1`, `in2` and `ain2`.
- The inputs and outputs update every 16 frames, 0.33 ms at 48 kHz. A gate edge lands within that.

## Controls

The control pages show the controls the program declares with `; @pN` lines, read in the program as `p1`-`p16`. The form is in the [script editor guide](../../docs/script-editor.md#pn-lines):

```clojure
; @p1 rate 1 8
; @p2 depth 0 1 cv 3
(d1 (sqr (fast p1 beat)))
```

A control reads its value in its declared range, 0 to 1 without one, and follows its CV input with `cv N`.

## Clock

`t` is seconds since the module started, at the program's tempo. `(set-bpm N)` sets the tempo, 120 by default. A comment line makes the time follow a clock instead:

| Comment | Time from |
|-|-|
| `; @clock` | rising edges on In 2, 4 per quarter note (16ths) |
| `; @clock N` | rising edges on In 2, N per quarter note (1-96; 24 for DIN sync) |
| `; @clock midi` | MIDI clock from `mlsp`'s MIDI IN in the general panel (LS + RS, see [Using](../../docs/USING.md#the-general-panel)), 24 per quarter note; Start returns to 0, Stop halts, Continue resumes |
| `; @reset` | a rising edge on In 1 sets `t` to 0 |

- With a clock, `beat` moves one beat per quarter note of the clock. Each pulse moves the time to the next pulse's position; between pulses it runs at the last interval's rate, never past the next pulse, so it halts when the clock stops.
- `bpm` stays the program's tempo, so `t` is seconds at that tempo, not wall-clock seconds.
- The status line shows the clock's measured tempo.
- `(useq-pause)` holds every output, `(useq-play)` resumes. Ctrl-Enter of a program also resumes.

## Keyboard

The [shared editor](../../docs/script-editor.md) edits the program, on key port 7777. Ctrl-S writes `BOOT/mlsp/live.useq` after a reset or for the built-in. Ctrl-1..0 show `no parts to remove`. These keys differ:

| Key | Does |
|-|-|
| Ctrl-Enter | runs the whole program in place of the running one, if all of it compiles |
| Ctrl-E | runs the forms of the block at the cursor beside the program, as uSEQ's editor sends them. A form that fails leaves the forms before it applied |
| Ctrl-. | clears every output: all go to 0 V until the next Ctrl-Enter or Ctrl-E |
| Ctrl-R | resets: clears every output, empties the code view, and forgets the program file; Ctrl-L brings the text back |

Ctrl-E shows the last form's value, such as `= 3` for `(+ 1 2)`, where other modules show their result. The Code view's status line shows the bar and tempo, or `paused`. The code is not coloured.

## Limits

- One SSP session holds about 360 distinct names across every `mlsp` instance, beyond uSEQ's built-in ones. Past that, a new name is an error, `Too many names`, until Synthor restarts. uSEQ keeps its names in one process-wide table.
- A form holds at most 255 tokens; a longer one is an error, `Too many tokens in one form`.
- uSEQ's own limits apply: 1024 graph nodes and 64-entry vectors.

## Not yet

- MIDI out: four voices on `d5`-`d8`, `a5`-`a8` and `s5`-`s8`.
- Colours for the code.

## Licence

GPL-3.0, for uSEQ, pending formal clarification of uSEQ's software licence.
