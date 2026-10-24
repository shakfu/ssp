# alda

`alda` plays a score in [Alda](https://alda.io) notation as gates and pitch CV, and as MIDI, in a loop. It makes no sound itself: patch the outputs into oscillators and envelopes, or send the MIDI to a synth. The score is text, written in the module from a USB keyboard or loaded from the card.

It embeds the C implementation of Alda in [psnd](https://github.com/shakfu/psnd) (`source/langs/alda`).

## Install

Copy `alda.so` to `BOOT/plugins` (from source: `make install MOD=alda`, to a mounted card) and restart the SSP. See [Using](../../docs/USING.md#installing).

The example scores go in `BOOT/alda`, where the browser opens. A release has them there. From source, copy `examples/` to `BOOT/alda` by hand.

A USB keyboard edits the score; it is optional ([Keyboard](#keyboard)).

## First use

1. Add `alda`. Patch Gate 1 (Out 1) to an envelope and CV 1 (Out 5) to an oscillator's 1 V/oct input. Synthor runs a module only while a jack is patched ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)). The built-in two-part loop plays.
2. For MIDI alone, patch any `INPT` output into one of `alda`'s inputs, and choose `alda` as a synth module's MIDI IN; see [MIDI](#midi).
3. Press Load (button 5), select `four-voices.alda`, and press Load again. It starts when the running loop ends.
4. Patch Gate 2-4 and CV 2-4 to hear its other three parts.

## The language

### What it is

Alda is a text notation for music. A score declares parts by instrument name, then lists their notes. A note is a letter with an optional duration: `c8` is an eighth-note C. `o4` sets the octave, `>` and `<` move it, and `|` marks a bar. Attributes in parentheses, such as `(tempo! 100)`, set tempo, volume and other values. See the [docs](https://github.com/alda-lang/alda/blob/master/doc/index.md), the [tutorial](https://alda.io/tutorial/) and the [cheat sheet](https://alda.io/cheat-sheet/).

### A first program

```alda
(tempo! 100)

piano:
  o4 c8 d e f g4 g | a8 a g4 c2/e/g

upright-bass:
  o2 c2 g | f2 c
```

- `(tempo! 100)`: 100 quarter notes a minute, for every part.
- `piano:` is part 1: Gate 1 and CV 1, and MIDI channel 1. `upright-bass:` is part 2: Gate 2, CV 2, channel 2.
- `c2/e/g`: a half-note chord. MIDI plays all three notes; CV 1 holds one ([Inputs and outputs](#inputs-and-outputs)).
- The loop is two 4/4 bars ([Loop](#loop)).

### How it maps to the SSP

| SSP | In Alda |
|-|-|
| Audio in | n/a: `alda` reads no audio |
| Audio out | n/a: `alda` makes no sound |
| Encoders | fixed controls; `@pN` comments are ignored ([Controls](#controls)) |
| CV in | n/a: In 3-8 are unused |
| CV out | parts 1-4: pitch on Out 5-8, 1 V/oct ([Inputs and outputs](#inputs-and-outputs)) |
| Gates and triggers | parts 1-4: gates on Out 1-4; In 1 resets the loop |
| MIDI in | `# @clock midi`: clock and transport only ([Clock](#clock)) |
| MIDI out | every part, channels 1-16 in declaration order; 10 is percussion ([MIDI](#midi)) |
| Clock and tempo | `(tempo! N)`, or `# @clock` for In 2 ([Clock](#clock)) |
| Sample rate and block size | gates and CV per sample; MIDI per block ([MIDI](#midi)) |
| Files on the card | `BOOT/alda`: `.alda` scores, `live.alda` ([Install](#install)) |
| Not available on the SSP | Alda's CLI and REPL; gates and CV for parts 5 and on |

## Use

Load (button 5) opens a browser. Choose a `.alda` and press Load again. A score that fails to parse leaves the previous one playing, and the screen shows the error. With no score, a built-in two-part loop plays.

A new score, loaded or run from the keyboard, starts when the running loop ends, or at once after a stop.

## Inputs and outputs

The first four parts, in the order the score declares them, are mono voices:

| Part | Gate | Pitch CV |
|-|-|-|
| 1 | Out 1, `Gate 1` | Out 5, `CV 1` |
| 2 | Out 2, `Gate 2` | Out 6, `CV 2` |
| 3 | Out 3, `Gate 3` | Out 7, `CV 3` |
| 4 | Out 4, `Gate 4` | Out 8, `CV 4` |

Synthor labels the jacks by name: `Gate k` and `CV k` are voice k's pair; the inputs are `Reset` (In 1), `Clock` (In 2) and `In 3`-`In 8`, unused.

- Gate: 5 V while a note sounds. Alda shortens each note to 90% of its length by default (`(quant 90)`), so repeated notes retrigger.

- Pitch: 1 V/octave, C4 (MIDI 60) at 0 V, within the SSP's +-5 V. It holds after the gate closes.

- Chords: the last note started sets the pitch; releasing it returns to a note still held.

- Parts after the fourth do not sound.

- In 1: a rising edge restarts the loop. A gate open at the restart closes for 1 ms, so the next note retriggers.

The score's own tempo sets the speed, `(tempo! 120)` by default, unless it follows a clock.

## Controls

The control pages hold `alda`'s own controls; `@pN` comments in a score are ignored. Each starts neutral, and loading a score with Load returns them there, except Clock, which keeps its setting. Pressing an encoder sets its control to the start of its range, such as -24 semitones, not to neutral ([Encoders](../../docs/script-editor.md#encoders)).

| Page | Control | Range | Does |
|-|-|-|-|
| 1 | Transpose | -24 to +24 semitones | moves the pitch CV at once, and MIDI notes from their next note-on |
| 1 | Tempo | x0.25 to x4 | scales the score's tempo; ignored while a clock drives it |
| 1 | Velocity | x0 to x2 | scales MIDI velocities; at 0 no MIDI notes are sent, though the gates play |
| 1 | Swing | 50% to 75% | delays every second 16th: 50% is straight, 66% triplet feel, 75% the most |
| 2 | Mute 1-4 | off below the middle, on above | closes voice k's gate and drops its part's MIDI notes; the CV still follows |
| 3 | Clock | free, analog, midi, score | what drives the time; see Clock. `score`, the default, follows the score's `# @clock` line |

## Clock

The Clock control picks what drives the time:

| Clock | Time from |
|-|-|
| free | the score's tempo; clocks are ignored |
| analog | rising edges on In 2 (`Clock`), 4 per quarter note (16ths), or N with `# @clock N` |
| midi | MIDI clock from `alda`'s MIDI IN in the general panel (LS + RS, see [Using](../../docs/USING.md#the-general-panel)), 24 per quarter note; Start returns to the beginning, Stop halts and releases notes, Continue resumes |
| score | the score's comment line, below; without one, free |

| Comment | Clock |
|-|-|
| `# @clock` | analog, 16ths |
| `# @clock N` | analog, N per quarter note (1-96; 24 for DIN sync) |
| `# @clock midi` | midi |

A preset keeps the Clock setting; one saved before Clock existed opens at `score`. Switching while the score plays goes on from the 16th (or pulse) it is in.

- Each pulse moves the score to the next pulse's position.
- Between pulses the score runs at the rate of the last interval, but never past the next pulse. It stays locked to the clock and halts when the clock stops.
- The score starts at its beginning and plays it at once. The first pulse moves it on by one pulse.
- In 1 still resets.
- The score's `(tempo ...)` changes are ignored once two pulses have arrived.

## MIDI

`alda` opens an ALSA MIDI port named `alda` (a second instance `alda 2`, and so on). To play `dexd` or another module, choose `alda` as that module's MIDI IN in its general panel. To play external gear, choose a device as `alda`'s own MIDI OUT; both receive the same messages. Do not choose `alda`'s own port as its MIDI OUT: every message would arrive twice.

Every part plays, not only the first four, on the channel Alda assigns it, or the one `(midi-channel N)` pins; percussion (`midi-percussion:`) is on channel 10. Notes carry Alda's velocities; each part's program, track volume (CC 11) and pan (CC 10) precede its first note and repeat each loop. A restart (loop end, reset, new score) releases every sounding note. Removing the module sends All Notes Off on all 16 channels.

The messages leave within about 1 ms of the block that produced them, so timing follows the audio block (2.7 ms at 128 frames), not the sample. If more than 1024 messages wait at once, the rest are dropped, and the status line counts them.

## Loop

The loop is the score's length rounded up to a 4/4 bar. A comment sets it in quarter notes instead:

```alda
# @loop 6
piano: c4 d e f g a
```

Notes past the loop's end do not sound.

## Keyboard

The [shared editor](../../docs/script-editor.md) edits the score, on key port 7775. Ctrl-S writes `BOOT/alda/live.alda` after a reset or for the built-in. Ctrl-E and Ctrl-1..0 do nothing. These keys differ:

| Key | Does |
|-|-|
| Ctrl-Enter | runs the whole score: it replaces the running one when the loop ends, or starts at once after a stop |
| Ctrl-. | stops: gates close, MIDI notes are released, silence until the next score runs |
| Ctrl-L | empties the code view, which the score keeps playing; again on the empty view, brings the text back |
| Ctrl-R | resets: stops, empties the code view, and forgets the program file; Ctrl-L brings the text back |

The Code view's status line shows the bar and tempo, or `stopped`, where other modules show `running`. The code is coloured by psnd's Alda grammar (`external/tree-sitter-alda`).

## Not yet

- Ctrl-E: a block played once beside the loop.

- A tempo control in BPM that replaces the score's tempo. Tempo today scales it, x0.25 to x4.

## Licence

GPL-3.0, as psnd.
