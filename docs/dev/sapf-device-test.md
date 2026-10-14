# sapf: device test

Step 4 of [sapf-plugin.md](sapf-plugin.md). Needs `sapf.so` in `BOOT/plugins`, and `BOOT/sapf/ugens` and `BOOT/sapf/sapf-prelude.txt` (`make install-sapf`). A USB keyboard through a USB 2.0 hub, or port 7771 over ssh.

Each step lists what to type, then what should happen. Ctrl-Enter evaluates the block at the cursor.

| # | Do | Expect |
|-|-|-|
| 1 | Add `sapf` to a patch; show it | status `loading the prelude`, then `ready`; output ends `done loading file` |
| 2 | `440 0 sinosc .1 * play` | a sine on output 1 |
| 3 | Ctrl-. | silence; output `stopped` |
| 4 | `2 3 +` | output `stack: 5` |
| 5 | `1 2 ]` | a red `error: syntax` line |
| 6 | `\[ 4 = n (4 3 7 rand 6h xline 0 impulse 4 9c 9u xline * 1m decay) (n 4h 84h nrands) (n nurands) (n .01 .11 nrands) klank rpan2 3 1m fadeout] .6 1.2 randz 1 2 ola play` | metallic pings on outputs 1 and 2, at random times; Ctrl-. stops them |
| 7 | `ord 1e12 N +/`, then Ctrl-. | status `evaluating`; after Ctrl-., `error: interrupted`, `stopped`, status `ready` |
| 8 | `ord 1e9 N reverse` | within seconds, `error: memory grew past 256 MB; interrupted`; Synthor keeps running |
| 9 | Patch a signal into input 1; `0 soundin play` | the input on output 1 |
| 10 | Type `[440` / blank line / `550] 0 sinosc .1 * play` on three lines; Ctrl-Enter on the blank line | two sines; the three lines flash |
| 11 | Ctrl-S; change the code; Ctrl-O | output `saved ...`, then `opened ...`; the saved code is back |
| 12 | Save the patch; reload it | the code is back |
| 13 | Add `ttyp`; switch between `ttyp` and `sapf` | the keyboard follows the screen |
| 14 | Play step 6's texture; remove `sapf` from the patch; add it again | no crash; the new `sapf` loads the prelude and plays |
| 15 | Add a second `sapf` | its status says another sapf runs; it stays silent |

Note the DSP figure on the status line during step 6.
