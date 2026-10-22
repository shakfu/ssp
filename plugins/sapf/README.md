# sapf

[sapf](https://github.com/lfnoise/sapf) is "a Forth-like language for audio synthesis using lazy lists and APL-like auto-mapping". The `sapf` plugin makes this available on the Percussa SSP for live coding: type code on a USB keyboard, evaluate it, and hear it. It embeds the fork [shakfu/sapf](https://github.com/shakfu/sapf); sapf's own UGens make the sound. The design is in [docs/dev/sapf-plugin.md](../../docs/dev/sapf-plugin.md).

## Install

Copy `sapf.so` to the `plugins` folder on the SSP's SD card, run `make install-sapf` with the card mounted, and restart the SSP. `make install-sapf` copies the prelude and sapf's examples to `BOOT/sapf`.

## Use

The module opens on its pages: sapf's output on the left, its state and the render thread's load on the right, and meters below them. Load (button 5) opens a browser; choose a `.sapf` or `.txt` file and press Load again to run it. With no file, a built-in sends inputs 1-2 to outputs 1-2.

sapf has no controls: code reads the inputs with `adc` (see [Evaluating](#evaluating)). The encoders do nothing on the pages.

## Keyboard

Just as for `ttyp`: a USB keyboard through a USB 2.0 hub, or port 7771 on 127.0.0.1 over ssh (`ssh -L 7771:127.0.0.1:7771 root@192.168.1.6`). The first key typed opens the Code view; Code (button 6) or Esc returns to the pages. The keyboard goes to the keyboard module on screen: `sapf` and `ttyp` can share a patch. With neither on screen, whichever holds it keeps it.

| Key | Does |
|-|-|
| Ctrl-Enter | stops every sound, then evaluates the whole code |
| Ctrl-E | evaluates the block around the cursor beside what plays |
| Ctrl-. | interrupts the evaluation, and stops every sound |
| Ctrl-S | writes the code to the program's file; for the built-in, to `BOOT/sapf/live.sapf` |
| Ctrl-Shift-S | Save As: type a file name (beside the current file; `.sapf` added if missing), Enter saves; an existing file needs a second Enter; Esc cancels |
| Ctrl-O | opens the browser, as Load |
| Ctrl-L | empties the code view; again on the empty view, brings the text back |
| Ctrl-R | stops every sound and empties the code view, with no file |
| Esc | returns to the pages |
| arrows, Home, End, PageUp, PageDown | move; with Ctrl, Home and End go to the start and end |

The code is coloured by syntax; built-in words are cyan, words you define stay white. The last three lines of output show above the status line. Encoder 1 moves the cursor by rows, encoder 2 by columns. A held key repeats. Tab inserts spaces to the next multiple of 4. The layout is US.

The preset stores the code last run, and the text when it holds an edit not yet run; the code runs when the patch loads. Patches from 0.1.0 bring their text back without running it.

## Files

Code longer than a line is easier to bring than to type: copy `.sapf` files into `BOOT/sapf` by card or scp, then open them with Load or Ctrl-O. `sapf-examples.txt`, sapf's own examples, is there after `make install-sapf`. Load runs a whole file, so open the examples, press Ctrl-. to stop them, move to an example, and press Ctrl-E.

## Evaluating

A block is the lines around the cursor up to blank lines; a blank line inside brackets or a string does not end it. On a blank line, Ctrl-E evaluates the block above.

- Definitions persist from one evaluation to the next, across Ctrl-Enter too. The stack starts empty each time; what an evaluation leaves on it is printed after `stack:`, then dropped.

- `play` plays through outputs 1 and 2 for a stereo signal. `stop` (or Ctrl-.) stops everything.

- `0 adc` reads input 1, audio or CV: 1.0 is 5 V. Inputs 1-8 are channels 0-7; `8 adcn` reads all eight.

- `>sf` and `>sfo` write to `BOOT/sapf`; `sgram` writes to `/tmp`. Nothing is opened in a viewer.

- One evaluation runs at a time; Ctrl-E while one runs is refused. One that grows the SSP's memory by more than 256 MB is interrupted.

- sapf reports errors in its output, in red, after the evaluation starts: the status line shows `ready` for code that failed.

One `sapf` runs per patch; a second shows a message and stays silent.

## Inputs and outputs

Eight inputs, `In 1`..`In 8`, and eight outputs, `Out 1`..`Out 8`.

## Sound

A thread of its own renders sapf 10.7 ms ahead of the SSP's audio. Code that renders slower than real time makes sapf's sound break up, and the status shows `late` and the milliseconds of sound missed since the start; the rest of the patch plays on. `render` in the status is the render thread's share of real time; `dsp` is the audio callback's, which only copies. On the pages, meters show inputs 1-2 and outputs 1-2: each one's peak, averaged over about 0.3 s, from -70 to +6 dB. CV shows too, with 0 dB at 5 V; below -40 dB (50 mV) a meter reads nothing, so an unplugged input stays dark. Synthor's own view shows all eight. Input through `adc` arrives about 27 ms late: the FIFO, the same again so the render thread can run ahead, and two blocks.

`setseed` fixes everything an evaluation plays, noise and `ola` voices included: the same seeded code sounds the same each time. Noise UGens draw random values as they compute, 128 frames at a time; sapf on a desktop computes 512, so the same seed gives other noise there.

## Limits

- No MIDI. No controls: p1-p16 exist, but sapf code cannot read them.

- An evaluation stops at Ctrl-. between interpreter steps; a single long built-in operation, such as sorting a huge list, finishes first.

## Licence

GPL-3.0, as sapf; see [LICENSE](LICENSE). The repo is AGPL-3.0.
