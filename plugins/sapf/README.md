# sapf

`sapf` runs [sapf](https://github.com/lfnoise/sapf), "a Forth-like language for audio synthesis using lazy lists and APL-like auto-mapping", for live coding: type code on a USB keyboard, evaluate it, and hear it. It embeds the fork [shakfu/sapf](https://github.com/shakfu/sapf); sapf's own UGens (unit generators) make the sound.

## Install

Copy `sapf.so` to `BOOT/plugins` (from source: `make install MOD=sapf`, to a mounted card) and restart the SSP. See [Using](../../docs/USING.md#installing).

`BOOT/sapf` holds `sapf-prelude.txt`, which sapf loads, and `sapf-examples.txt`, sapf's own examples. A release has them there. From source, `make install-sapf` copies them to a mounted card, or over ssh to `SSP_HOST`.

`sapf` needs a USB keyboard, or its key port; see [Keyboard](#keyboard).

## First use

1. Add `sapf` and patch Out 1 and Out 2 to outputs. Synthor runs a module only while a jack is patched ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)). The built-in sends In 1-2 to Out 1-2.
2. Press Load (button 5), select `sapf-examples.txt`, and press Load again. Load runs the whole file.
3. Press Ctrl-. to stop every sound. The Code view opens.
4. Move the cursor to an example and press Ctrl-E.

## The language

### What it is

sapf is a Forth-like stack language for audio. Words take arguments from a stack and push results onto it. A signal is a lazy list of samples, computed when played. Operators auto-map over lists: a filter on a list of two signals gives two filters. The language is in sapf's [README](https://github.com/lfnoise/sapf#readme); [sapf-examples.txt](https://github.com/lfnoise/sapf/blob/master/sapf-examples.txt) is the tutorial by example.

### A first program

```text
; In 1 and In 2 through a low-pass filter, to Out 1 and Out 2
; encoder 1 (Mouse X): cutoff, 200 Hz to 12 kHz
; encoder 2 (Mouse Y): level, 0 to 1
[0 adc 1 adc] 200 12k xmousex lpf  0 1 mousey *  play
```

- `[0 adc 1 adc]`: a list of two signals, In 1 and In 2.
- `200 12k xmousex`: encoder 1, mapped exponentially from 200 to 12000 Hz.
- `lpf` and `*` auto-map over the list: one filter and one gain per channel.
- `play`: list element k goes to Out k+1.

### How it maps to the SSP

| SSP | In sapf |
|-|-|
| Audio in | `0 adc`..`7 adc`: In 1-8; `8 adcn` reads all ([Evaluating](#evaluating)) |
| Audio out | `play`: a list of up to 8 signals to Out 1-8 |
| Encoders | `mousex`, `mousey` and their forms: encoders 1 and 2 ([Use](#use)) |
| CV in | `adc`: 1.0 is 5 V ([CV](../../docs/script-editor.md#cv)) |
| CV out | `play` a slow signal: 1.0 is 5 V |
| Gates and triggers | no gate type: `adc` reads a gate; `play` writes one |
| MIDI in | n/a: the module opens no MIDI port |
| MIDI out | n/a: the module opens no MIDI port |
| Clock and tempo | n/a: no host clock; code sets rates in Hz |
| Sample rate and block size | Synthor's rate at first load; 128-frame blocks ([Sound](#sound)) |
| Files on the card | `BOOT/sapf`: prelude, examples, `>sf` output ([Files](#files)) |
| Not available on the SSP | MIDI words, the Manta ([Limits](#limits)); file viewers ([Evaluating](#evaluating)) |

## Use

The module opens on its pages: the mouse page and sapf's output on the left, its state and render load on the right, and meters below them. Load (button 5) opens a browser; choose a `.sapf` or `.txt` file and press Load again to run it. With no file, a built-in sends In 1-2 to Out 1-2.

The SSP has no mouse, so encoders 1 and 2 are sapf's: Mouse X and Mouse Y (p1 and p2), each 0 to 1, start in the middle. Pressing an encoder returns it to the middle ([Encoders](../../docs/script-editor.md#encoders)). `mousex`, `mousey`, `xmousex`, `xmousey` and their `1` forms read them, as sapf's examples do. Code reads the inputs with `adc` (see [Evaluating](#evaluating)).

## Keyboard

The [shared editor](../../docs/script-editor.md) edits the code, on key port 7771. Ctrl-S on the built-in writes `BOOT/sapf/live.sapf`. Ctrl-1..0 do nothing. These keys differ:

| Key | Does |
|-|-|
| Ctrl-Enter | stops every sound, then evaluates the whole code |
| Ctrl-E | evaluates the block around the cursor beside what plays |
| Ctrl-. | interrupts the evaluation, and stops every sound |
| Ctrl-R | stops every sound and empties the code view, with no file |

- Built-in words are cyan; words you define stay white.
- The last three lines of output show above the status line.

## Files

Copy `.sapf` files into `BOOT/sapf` on the card or by scp, then open them with Load or Ctrl-O. Load runs the whole file; [First use](#first-use) shows how to play one block of it.

## Evaluating

Ctrl-E evaluates a block as [Keys](../../docs/script-editor.md#keys) defines it.

- Definitions persist from one evaluation to the next, across Ctrl-Enter too. The stack starts empty each time; what an evaluation leaves on it is printed after `stack:`, then dropped.

- `play` plays through outputs 1 and 2 for a stereo signal. `stop` (or Ctrl-.) stops everything.

- `0 adc` reads input 1, audio or CV: 1.0 is 5 V. Inputs 1-8 are channels 0-7; `8 adcn` reads all eight.

- `>sf` and `>sfo` write to `BOOT/sapf`; `sgram` writes to `/tmp`. Nothing is opened in a viewer.

- One evaluation runs at a time; Ctrl-E while one runs is refused. One that grows the SSP's memory by more than 256 MB is interrupted.

- sapf reports errors in its output, in red, after the evaluation starts: the status line shows `ready` for code that failed.

One `sapf` runs per patch; a second shows a message and stays silent.

## Sound

- Code that renders slower than real time makes sapf's sound break up. The status then shows `late` and the milliseconds of sound missed since the start. The rest of the patch plays on.
- `render` in the status is the share of real time sapf takes to compute its sound. `dsp` is the audio callback's share, which only copies.
- On the pages, meters show In 1-2 and Out 1-2: each one's peak, averaged over about 0.3 s, from -70 to +6 dB. CV shows too, with 0 dB at 5 V.
- Below -40 dB (50 mV) a meter reads nothing, so an unplugged input stays dark. Synthor's own view shows all eight.
- Input through `adc` arrives about 27 ms late.

`setseed` fixes everything an evaluation plays, noise and `ola` voices included: the same seeded code sounds the same each time. Noise UGens draw random values as they compute, 128 frames at a time; sapf on a desktop computes 512, so the same seed gives other noise there.

## Limits

- p3-p16 exist, but sapf code cannot read them.

- sapf is built without its MIDI backend, so MIDI words such as `mctl` and `mgate` have no device. The Manta controller is off.

- An evaluation stops at Ctrl-. between interpreter steps; a single long built-in operation, such as sorting a huge list, finishes first.

## Licence

GPL-3.0, as sapf; see [LICENSE](LICENSE). The repo is AGPL-3.0.
