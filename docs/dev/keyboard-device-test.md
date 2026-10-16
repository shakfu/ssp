# Keyboard modules: device test

The code view of `fstr`, `csnd` and `chuk`, and the keyboard shared with `sapf` and `ttyp`. Needs the five `.so` files from one build in `BOOT/plugins`, the examples in `BOOT/chuck`, `BOOT/csound` and `BOOT/faust`, and `BOOT/faust/libraries` (`make install-faust`). A USB keyboard through a USB 2.0 hub. Listen on outputs 1 and 2.

Each step lists what to do, then what should happen. Code typed on one line here is one line in the editor; "a blank line" means press Enter on an empty line. The status line under the code reads `<file>:<row>   <state>   dsp N%   keys usb|tcp :PORT`.

## chuk: code view

| # | Do | Expect |
|-|-|-|
| 1 | Add `chuk` to a patch; show it; press Down | the Code view opens on the built-in drone's code; status `built-in:2   running ... keys usb` |
| 2 | Esc; then Down; then button 6 (Code) | pages; Code view; pages. The Code label is hidden while Load's browser is open |
| 3 | In the Code view, turn encoders 1 and 2 | the cursor moves by rows, then by columns |
| 4 | On the `(0.15 + p2 * 0.85) * 0.3 => s.gain;` line, change `0.3` to `0.1`; Ctrl-Enter | status `compiling`, then `running`; the drone is quieter; the file shows as `live.ck`. Note any gap in the sound |
| 5 | Change `s.gain` to `s.gainx`; Ctrl-Enter | an orange `live.ck:...` error; the quieter drone keeps playing |
| 6 | Undo step 5's change; Ctrl-S | `saved /media/BOOT/chuck/live.ck` for 3 s; `ssh root@192.168.1.6 cat /media/BOOT/chuck/live.ck` shows the code with `0.1` |
| 7 | Ctrl-O; choose `lfo.ck`; Load; press Down | the pages show `lfo.ck`'s controls at its defaults; the Code view shows `lfo.ck` |
| 8 | Change a number in `lfo.ck`; Ctrl-Enter; save the patch in Synthor; load it again | the edited code plays and is in the view; `BOOT/chuck/lfo.ck` is unchanged |
| 9 | Type a comment line `// not run` without running it; save the patch; load it | the comment is back in the view; the sound is step 8's |

## chuk: Ctrl-E shreds

Start from step 8's program, or Ctrl-O `filter.ck`. Put each block below the program, after a blank line.

| # | Do | Expect |
|-|-|-|
| 10 | `SinOsc a => dac.chan(0); 0.05 => a.gain; 660 => a.freq; while (true) 1::second => now;`; Ctrl-E | the line flashes; a 660 Hz tone joins on output 1; note `shred 1: SinOsc a => dac.chan(0);` (labels stop at 24 characters); a row above the status line reads `shreds  1 SinOsc a => dac.chan(0);`. Note any click when it starts |
| 11 | A blank line, then `SinOsc b => dac.chan(1); 0.05 => b.gain; 990 => b.freq; while (true) 1::second => now;`; Ctrl-E | 990 Hz on output 2; `shreds  1 ...   2 ...` |
| 12 | Ctrl-1 | 660 Hz stops with no gap in the rest; the list is `1 SinOsc b ...` |
| 13 | Ctrl-- | 990 Hz stops; the shreds row goes |
| 14 | A block `SinOsc c => dac.chan(0); 0.05 => c.gain; 1::second => now;`; Ctrl-E | a tone for 1 s; it shows in the list, then leaves it |
| 15 | A block `SinOsc d => nosuch;`; Ctrl-E | an orange error; the program plays on |
| 16 | Steps 10 and 11 again; Ctrl-. | silence, the program included; note `cleared every shred; Ctrl-Enter runs the program again` |
| 17 | Delete step 15's line; Ctrl-Enter | the whole buffer runs as the program, steps 10, 11 and 14's blocks with it |
| 18 | Optional: run 16 blocks with Ctrl-E, then one more | the 17th reports `16 shreds running: remove one first` |
| 19 | With shreds running, remove `chuk` from the patch; add it again | no crash; the new `chuk` plays the built-in |

## csnd

| # | Do | Expect |
|-|-|-|
| 20 | Add `csnd`; show it; press Down | the Code view shows the built-in `.csd`; the drone plays |
| 21 | After `schedule "Drone", 0, -1`, a blank line, then `instr 2` / `a1 vco2 0.1, 660` / `outs a1, a1` / `endin` / `schedule 2, 0, 2` on five lines; Ctrl-E inside them | note `orchestra code compiled`; a 2 s 660 Hz tone over the drone, which does not restart |
| 22 | Change `660` to `880`; Ctrl-E | a 2 s 880 Hz tone: the new definition |
| 23 | After `</CsInstruments>`: `<CsScore>`, a blank line, `i 2 0 1`, a blank line, `</CsScore>`; Ctrl-E on `i 2 0 1` | note `events sent`; a 1 s 880 Hz tone |
| 24 | A block `instr 3` / `a1 nosuch 1` / `endin`; Ctrl-E | an orange error; the drone plays on |
| 25 | Ctrl-- and Ctrl-. | `no parts to remove`, `no parts to clear`; nothing else changes |
| 26 | `ssh root@192.168.1.6 'grep VmRSS /proc/$(pidof SYNTHOR)/status'`; repeat step 22 fifty times, changing the pitch each time; read VmRSS again | the growth per redefinition. Csound may keep replaced definitions; record the figure |
| 27 | Delete step 24's block; Ctrl-Enter | a full restart: the drone restarts, and the score's `i 2 0 1` plays step 22's instrument |

## fstr

| # | Do | Expect |
|-|-|-|
| 28 | Add `fstr`; show it; Ctrl-O; open `osc.dsp`; press Down | the Code view shows `osc.dsp` |
| 29 | Change a number; Ctrl-Enter | status `compiling` for the JIT's time, then `running` with the change. Record how long `compiling` shows |
| 30 | Ctrl-E | an orange `runs whole programs only: Ctrl-Enter`; the sound is unchanged |
| 31 | Break the code; Ctrl-Enter | Faust's error in orange; the previous program plays on |

## Keyboard between modules

| # | Do | Expect |
|-|-|-|
| 32 | One patch with `sapf`, `ttyp`, `chuk`, `csnd` and `fstr`. Show each in turn and type a character | it goes to the module on screen only |
| 33 | Leave every module's screen (Synthor's main page); type `zz`; show `chuk` | the `zz` typed off screen may appear in the code: the last module shown keeps the keyboard and queues up to 256 keys. Record where it lands |
| 34 | Show `chuk`; unplug the keyboard; plug it in again | status `keys tcp :7774`, then `keys usb` |
| 35 | With `chuk` on screen, `ssh root@192.168.1.6 "python3 -c 'import socket; socket.create_connection((\"127.0.0.1\", 7774)).sendall(bytes([2, 4, 0, 0]))'"` | an `a` at the cursor. Ports 7772 (`fstr`) and 7773 (`csnd`) work the same way when shown |
| 36 | Add a second `chuk`; show each | keys go to the one on screen |
| 37 | Remove all five modules from the patch | no crash |

## sapf after the move

`sapf`'s editor now comes from `plugins/common/keys`. Run steps 2, 10, 11 and 13 of [sapf-device-test.md](sapf-device-test.md): sound, the flash of the three lines, Ctrl-S and Ctrl-O, the keyboard moving to `ttyp`.

Record the gap in the sound in steps 4, 10 and 21 (the audio stops while a compile holds the engine), the `compiling` time in step 29, and the VmRSS growth in step 26.
