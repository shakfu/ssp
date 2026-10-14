# sapf

`sapf` runs [sapf](https://github.com/lfnoise/sapf) on the Percussa SSP for live coding: type code on a USB keyboard, evaluate it, and hear it. The interpreter is [sapf-sc](https://github.com/shakfu/sapf-sc), which compiles sapf's signals to SuperCollider UGens; `sapf` plays them through its own scsynth. The design is in [docs/dev/sapf-plugin.md](../../docs/dev/sapf-plugin.md).

## Install

Copy `sapf.so` to the `plugins` folder on the SSP's SD card, run `make install-sapf` with the card mounted, and restart the SSP. `make install-sapf` copies the UGen plugins to `BOOT/sapf/ugens` and the prelude to `BOOT/sapf/sapf-prelude.txt`.

## Keyboard

As for `ttyp`: a USB keyboard through a USB 2.0 hub, or port 7771 on 127.0.0.1 over ssh (`ssh -L 7771:127.0.0.1:7771 root@192.168.1.6`). The keyboard goes to the keyboard module on screen: `sapf` and `ttyp` can share a patch. With neither on screen, whichever holds it keeps it.

| Key | Does |
|-|-|
| Ctrl-Enter | evaluates the block around the cursor |
| Ctrl-. or Esc | interrupts the evaluation, and stops every sound |
| Ctrl-S | saves the code to `BOOT/sapf/live.sapf` |
| Ctrl-O | opens `BOOT/sapf/live.sapf` |
| arrows, Home, End, PageUp, PageDown | move; with Ctrl, Home and End go to the start and end |

Encoder 1 moves the cursor by rows, encoder 2 by columns. A held key repeats. Tab inserts spaces to the next multiple of 4. The layout is US.

## Evaluating

A block is the lines around the cursor up to blank lines; a blank line inside brackets or a string does not end it. On a blank line, Ctrl-Enter evaluates the block above.

- Definitions persist from one evaluation to the next. The stack starts empty each time; what an evaluation leaves on it is printed after `stack:`, then dropped.
- `play` plays through outputs 1 and 2 for a stereo signal. `stop` (or Ctrl-.) stops everything.
- `0 soundin` reads input 1, audio or CV: 1.0 is 5 V. Inputs 1-8 are channels 0-7.
- `>sd` and `>nrt` write to `BOOT/sapf`. `>sf` does not work in `sapf`.
- One evaluation runs at a time. One that grows the SSP's memory by more than 256 MB is interrupted.

The code is saved with the patch. One `sapf` runs per patch; a second shows a message and stays silent.

## Limits

- No MIDI. The encoders do not set controls yet.
- An evaluation stops at Ctrl-. between interpreter steps; a single long built-in operation, such as sorting a huge list, finishes first.
