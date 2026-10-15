# sapf: device test

Step 3 of [sapf-plugin.md](sapf-plugin.md). Needs `sapf.so` in `BOOT/plugins`, and `sapf-prelude.txt` and `sapf-examples.txt` in `BOOT/sapf` (`make install-sapf`). `BOOT/sapf/ugens`, from the sapf-sc version, is no longer used and can be deleted. Steps 6-10's code is long: put it in `BOOT/sapf/live.sapf` and open that with Ctrl-O. A USB keyboard through a USB 2.0 hub, or port 7771 over ssh.

Each step lists what to type, then what should happen. Ctrl-Enter evaluates the block at the cursor.

| # | Do | Expect |
|-|-|-|
| 1 | Add `sapf` to a patch; show it | status `loading the prelude`, then `ready`; output ends `done loading file` |
| 2 | `440 0 sinosc .1 * play` | a sine on output 1; no `late` on the status line |
| 3 | Ctrl-. | silence; output `stopped` |
| 4 | `2 3 +` | output `stack: 5` |
| 5 | `1 2 ]` | a red `error: syntax` line |
| 6 | `\[ ((4h 2k rand) 0 sinosc) ((1 11 rand) 0 (0 .7 rand) lfpulse) 4m lag 4c * * rpan2 4 quadenv *] .5 1 2 ola play` | overlapping random beeps on outputs 1 and 2; Ctrl-. stops them |
| 7 | `ord 1e12 N +/`, then Ctrl-. | status `evaluating`; after Ctrl-., `error: interrupted`, `stopped`, status `ready` |
| 8 | `ord 1e9 N reverse` | within seconds, `error: memory grew past 256 MB; interrupted`; Synthor keeps running |
| 9 | Patch a signal into input 1; `0 adc play` | the input on output 1, about 27 ms late (inferred; measure it if a scope is at hand) |
| 10 | Type `[440` / blank line / `550] 0 sinosc .1 * play` on three lines; Ctrl-Enter on the blank line | two sines; the three lines flash |
| 11 | Ctrl-S; change the code; Ctrl-O, Enter on `live.sapf` | output `saved ...`; the list shows `live.sapf` selected, `sapf-examples.txt` and the prelude; then `opened ...` and the saved code is back |
| 12 | Save the patch; reload it | the code is back |
| 13 | Add `ttyp`; switch between `ttyp` and `sapf` | the keyboard follows the screen |
| 14 | Play step 6's texture; remove `sapf` from the patch; add it again | no crash; the new `sapf` loads the prelude and plays |
| 15 | Add a second `sapf` | its status says another sapf runs; it stays silent |
| 16 | Ctrl-O, open `sapf-examples.txt`; PageDown to an example with `play`; Ctrl-Enter | the example plays; the status shows `sapf-examples.txt:` and the line |
| 17 | `440 0 sinosc .1 * 1 T "t" >sfo` | output `wrote file '.../BOOT/sapf/t.wav'`; nothing else happens: `openFiles` is off |
| 18 | With another module sounding, open `sapf-examples.txt` at `;;;; rain` (16 voices with reverb, over real time on the SSP) and play it | the status line's `late` grows and sapf's sound breaks up; the other module plays on without a gap |
| 19 | Patch audio into input 1 and a CV into input 2; `0 adc play` | `in` meters 1 and 2 move with the signals, CV included; `out` meter 1 follows input 1 |

Note the `dsp` figure (the render thread's share of real time) during steps 6 and 18, and whether `late` appears in steps 2-16: it would mean the 4-block FIFO is too shallow with Synthor's other threads running.
