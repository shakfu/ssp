# alda

`alda` plays a score in [Alda](https://alda.io) notation as gates and pitch CV, and as MIDI, in a loop. It makes no sound itself: patch the outputs into oscillators and envelopes, or send the MIDI to a synth. The score is text, written in the module from a USB keyboard or loaded from the card.

It embeds the C implementation of Alda in [psnd](https://github.com/shakfu/psnd) (`source/langs/alda`), fetched by `scripts/fetch_alda.sh`. Design: [docs/dev/dsl-alda.md](../../docs/dev/dsl-alda.md).

## Install

Copy `alda.so` to the `plugins` folder on the SD card, and `examples/` to `BOOT/alda`, where the browser opens.

## Outputs and inputs

The first four parts, in the order the score declares them, are mono voices:

| Part | Gate | Pitch CV |
|-|-|-|
| 1 | out 1, `Gate 1` | out 5, `CV 1` |
| 2 | out 2, `Gate 2` | out 6, `CV 2` |
| 3 | out 3, `Gate 3` | out 7, `CV 3` |
| 4 | out 4, `Gate 4` | out 8, `CV 4` |

Synthor labels the jacks by name: `Gate k` and `CV k` are voice k's pair; the inputs are `Reset` (In 1), `Clock` (In 2) and `In 3`-`In 8`, unused.

- Gate: 5 V while a note sounds. Alda shortens each note to 90% of its length by default (`(quant 90)`), so repeated notes retrigger.
- Pitch: 1 V/octave, C4 (MIDI 60) at 0 V, within the SSP's +-5 V. It holds after the gate closes.
- Chords: the last note started sets the pitch; releasing it returns to a note still held.
- Parts after the fourth do not sound.
- In 1: a rising edge restarts the loop. A gate open at the restart closes for 1 ms, so the next note retriggers.

The score's own tempo sets the speed, `(tempo! 120)` by default, unless it follows a clock.

## Controls

The control pages hold `alda`'s own controls; `@pN` comments in a score are ignored. Each starts neutral, and loading a score with Load returns them there, except Clock, which the patch keeps.

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
| midi | MIDI clock from `alda`'s MIDI IN (general panel), 24 per quarter note; Start returns to the beginning, Stop halts and releases notes, Continue resumes |
| score | the score's comment line, below; without one, free |

| Comment | Clock |
|-|-|
| `# @clock` | analog, 16ths |
| `# @clock N` | analog, N per quarter note (1-96; 24 for DIN sync) |
| `# @clock midi` | midi |

A patch keeps the Clock setting; one saved before Clock existed opens at `score`. Switching while the score plays goes on from the 16th (or pulse) it is in.

Each pulse moves the score to the next pulse's position; between pulses it runs at the rate of the last interval, but never past the next pulse. The score therefore stays locked to the clock and halts when it stops. The score starts at its beginning and plays it at once; the first pulse moves it on by one pulse. In 1 still resets. The score's `(tempo ...)` changes are ignored once two pulses have arrived.

## MIDI

`alda` opens an ALSA MIDI port named `alda` (a second instance `alda 2`, and so on). To play `dexd` or another module, choose `alda` as that module's MIDI IN in its general panel (RS + LS). To play external gear, choose a device as `alda`'s own MIDI OUT; both receive the same messages. Do not choose `alda`'s own port as its MIDI OUT: every message would arrive twice.

Every part plays, not only the first four, on the channel Alda assigns it, or the one `(midi-channel N)` pins; percussion (`midi-percussion:`) is on channel 10. Notes carry Alda's velocities; each part's program, track volume (CC 11) and pan (CC 10) precede its first note and repeat each loop. A restart (loop end, reset, new score) releases every sounding note. Removing the module sends All Notes Off on all 16 channels.

The messages leave within about 1 ms of the block that produced them, so timing follows the audio block (2.7 ms at 128 frames), not the sample. If more than 1024 messages wait at once, the rest are dropped, and the status line counts them.

## Loop

The loop is the score's length rounded up to a 4/4 bar. A comment sets it in quarter notes instead:

```alda
# @loop 6
piano: c4 d e f g a
```

Notes past the loop's end do not sound.

## Use

Synthor runs a module only while at least one of its jacks is patched in the patch, even to an input module whose hardware jack is empty. Until then `alda` stands still: no gates, no CV, no MIDI. For MIDI alone, patch any `INPT` output into one of `alda`'s inputs.

Load (button 5) opens a browser. Choose a `.alda` and press Load again. A score that fails to parse leaves the previous one playing, and the screen shows the error. With no score, a built-in two-part loop plays.

A new score, loaded or run from the keyboard, starts when the running loop ends, or at once after a stop.

## Keyboard

A USB keyboard, or port 7775 on 127.0.0.1 over ssh (`ssh -L 7775:127.0.0.1:7775 root@192.168.1.6`), edits the score as text, as in `chuk` (see [its README](../chuck/README.md#keyboard)). 

| Key | Does |
|-|-|
| Ctrl-Enter | runs the whole score: it replaces the running one when the loop ends, or starts at once after a stop |
| Ctrl-. | stops: gates close, MIDI notes are released, silence until the next score runs |
| Ctrl-L | empties the code view, which the score keeps playing; again on the empty view, brings the text back |
| Ctrl-R | resets: stops, empties the code view, and forgets the program file; Ctrl-L brings the text back |
| Ctrl-S | writes the code to its file, or `BOOT/alda/live.alda` after a reset or for the built-in |
| Ctrl-Shift-S | Save As: type a file name (beside the current file; `.alda` added if missing); Enter saves, and Ctrl-S then writes there; an existing file needs a second Enter; Esc cancels |
| Ctrl-O | opens the browser, as Load |

The Code view's status line lists these keys, and shows the bar and tempo, or `stopped`, where other modules show `running`. Ctrl-E is not supported yet. The code is coloured by psnd's Alda grammar (`external/tree-sitter-alda`).

## Not yet

- Ctrl-E: a block played once beside the loop.
- Tempo from a control.

## Licence

GPL-3.0, as psnd.
