# sapf

[sapf](https://github.com/lfnoise/sapf) is "a Forth-like language for audio synthesis using lazy lists and APL-like auto-mapping". The `sapf` plugin makes this available on the Percussa SSP for live coding: type code on a USB keyboard, evaluate it, and hear it. It embeds the fork [shakfu/sapf](https://github.com/shakfu/sapf); sapf's own UGens make the sound. The design is in [docs/dev/sapf-plugin.md](../../docs/dev/sapf-plugin.md).

## Install

Copy `sapf.so` to the `plugins` folder on the SSP's SD card, run `make install-sapf` with the card mounted, and restart the SSP. `make install-sapf` copies the prelude and sapf's examples to `BOOT/sapf`.

## Keyboard

Just as for `ttyp`: a USB keyboard through a USB 2.0 hub, or port 7771 on 127.0.0.1 over ssh (`ssh -L 7771:127.0.0.1:7771 root@192.168.1.6`). The keyboard goes to the keyboard module on screen: `sapf` and `ttyp` can share a patch. With neither on screen, whichever holds it keeps it.

| Key | Does |
|-|-|
| Ctrl-Enter | evaluates the block around the cursor |
| Ctrl-. or Esc | interrupts the evaluation, and stops every sound |
| Ctrl-S | saves the code to the file opened last, `live.sapf` at first, in `BOOT/sapf` |
| Ctrl-O | lists the `.sapf` and `.txt` files in `BOOT/sapf`; Enter opens one in place of the code, Esc closes the list |
| arrows, Home, End, PageUp, PageDown | move; with Ctrl, Home and End go to the start and end |

The code is coloured by syntax; built-in words are cyan, words you define stay white. Encoder 1 moves the cursor by rows, encoder 2 by columns. A held key repeats. Tab inserts spaces to the next multiple of 4. The layout is US.

## Files

Code longer than a line is easier to bring than to type: copy `.sapf` files into `BOOT/sapf` by card or scp, then open them with Ctrl-O. `sapf-examples.txt`, sapf's own examples, is there after `make install-sapf`: open it, move to an example, and press Ctrl-Enter. Opening a file replaces the code; Ctrl-S first keeps it. The status line shows the current file and line.

## Evaluating

A block is the lines around the cursor up to blank lines; a blank line inside brackets or a string does not end it. On a blank line, Ctrl-Enter evaluates the block above.

- Definitions persist from one evaluation to the next. The stack starts empty each time; what an evaluation leaves on it is printed after `stack:`, then dropped.

- `play` plays through outputs 1 and 2 for a stereo signal. `stop` (or Ctrl-.) stops everything.

- `0 adc` reads input 1, audio or CV: 1.0 is 5 V. Inputs 1-8 are channels 0-7; `8 adcn` reads all eight.

- `>sf` and `>sfo` write to `BOOT/sapf`; `sgram` writes to `/tmp`. Nothing is opened in a viewer.

- One evaluation runs at a time. One that grows the SSP's memory by more than 256 MB is interrupted.

The code is saved with the patch. One `sapf` runs per patch; a second shows a message and stays silent.

## Sound

A thread of its own renders sapf 10.7 ms ahead of the SSP's audio. Code that renders slower than real time makes sapf's sound break up, and the status line shows `late` and the milliseconds of sound missed since the start; the rest of the patch plays on. `dsp` on the status line is the render thread's share of real time. Right of the output, meters show inputs 1-2 and outputs 1-2: each one's peak, averaged over about 0.3 s, from -70 to +6 dB. CV shows too, with 0 dB at 5 V; below -40 dB (50 mV) a meter reads nothing, so an unplugged input stays dark. Synthor's own view shows all eight. Input through `adc` arrives about 27 ms late: the FIFO, the same again so the render thread can run ahead, and two blocks.

`setseed` fixes everything an evaluation plays, noise and `ola` voices included: the same seeded code sounds the same each time. Noise UGens draw random values as they compute, 128 frames at a time; sapf on a desktop computes 512, so the same seed gives other noise there.

## Limits

- No MIDI. The encoders do not set controls yet.

- An evaluation stops at Ctrl-. between interpreter steps; a single long built-in operation, such as sorting a huge list, finishes first.
