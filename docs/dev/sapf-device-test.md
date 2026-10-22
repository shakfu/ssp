# sapf: device test

Step 3 of [sapf-plugin.md](sapf-plugin.md), on the shared script editor ([sapf-editor.md](sapf-editor.md)). Needs `sapf.so` in `BOOT/plugins`, and `sapf-prelude.txt` and `sapf-examples.txt` in `BOOT/sapf` (`make install-sapf`). `BOOT/sapf/ugens`, from the sapf-sc version, is no longer used and can be deleted. Steps 7-11's code is long: put it in `BOOT/sapf/live.sapf` and open that with Ctrl-O. A USB keyboard through a USB 2.0 hub, or port 7771 over ssh.

Each step lists what to type, then what should happen. Ctrl-E evaluates the block at the cursor; Ctrl-Enter stops every sound and evaluates the whole code.

| # | Do | Expect |
|-|-|-|
| 1 | Add `sapf` to a patch; show it | the pages: output on the left ends `done loading file`; the status panel shows `built-in`, `ready` and `render`; meters below it |
| 2 | Patch audio into input 1 and a CV into input 2 | the built-in: input 1 on output 1, input 2 on output 2; `in` meters 1 and 2 move, CV included; `out` meters follow |
| 3 | Type a key | the Code view, with the built-in's text; the last output lines above the status line |
| 4 | Ctrl-L; `440 0 sinosc .1 * play`; Ctrl-Enter | the built-in stops; a sine on output 1; no `late` on the status line |
| 5 | Ctrl-. | silence; output `stopped` |
| 6 | On new lines, `2 3 +`, Ctrl-E; then `1 2 ]`, Ctrl-E | output `stack: 5`; then a red `error: syntax` line |
| 7 | `\[ ((4h 2k rand) 0 sinosc) ((1 11 rand) 0 (0 .7 rand) lfpulse) 4m lag 4c * * rpan2 4 quadenv *] .5 1 2 ola play`, Ctrl-E | overlapping random beeps on outputs 1 and 2; Ctrl-. stops them |
| 8 | `ord 1e12 N +/`, Ctrl-E, then Ctrl-. | status `evaluating`; after Ctrl-., `error: interrupted`, `stopped`, status `ready` |
| 9 | `ord 1e9 N reverse`, Ctrl-E | within seconds, `error: memory grew past 256 MB; interrupted`; Synthor keeps running |
| 10 | `0 adc play`, Ctrl-E, with a signal on input 1 | the input on output 1, about 27 ms late (inferred; measure it if a scope is at hand) |
| 11 | Type `[440` / blank line / `550] 0 sinosc .1 * play` on three lines; Ctrl-E on the blank line | two sines beside step 10's input; the three lines flash |
| 12 | Ctrl-Enter on a text holding steps 10 and 11's blocks | everything stops, then both play again |
| 13 | Esc; Code (button 6) | the pages, sound unchanged; then the Code view again |
| 14 | Ctrl-S; change the code; Ctrl-O, Load on `live.sapf` | `saved ...`; the browser opens; the saved code is back and runs |
| 15 | Save the patch; reload it | the code last run plays once the prelude loads; an edit not run is back as text |
| 16 | Load a patch saved with sapf 0.1.0 | its text is back, nothing plays; Ctrl-S writes its old file |
| 17 | Add `ttyp`; switch between `ttyp` and `sapf` | the keyboard follows the screen |
| 18 | Play step 7's texture; remove `sapf` from the patch; add it again | no crash; the new `sapf` loads the prelude and runs the built-in |
| 19 | Add a second `sapf` | an orange error: another sapf runs; it stays silent |
| 20 | Ctrl-O, Load `sapf-examples.txt` | every example starts; Ctrl-. stops them; PageDown to an example with `play`; Ctrl-E plays it; the status shows `sapf-examples.txt:` and the line |
| 21 | `440 0 sinosc .1 * 1 T "t" >sfo`, Ctrl-E | output `wrote file '.../BOOT/sapf/t.wav'`; nothing else happens: `openFiles` is off |
| 22 | With another module sounding, play `;;;; rain` from `sapf-examples.txt` (16 voices with reverb, over real time on the SSP) | `late` grows and sapf's sound breaks up; the other module plays on without a gap |
| 23 | In rack, sapf's compact view | the state and the last output lines |

Note the `render` figure (the render thread's share of real time) during steps 7 and 22, and whether `late` appears in steps 4-18: it would mean the 4-block FIFO is too shallow with Synthor's other threads running. Step 20's full load also runs the number puzzles, which take minutes on the SSP: Ctrl-. stops them.
