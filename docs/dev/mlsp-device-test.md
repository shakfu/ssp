# mlsp: device test

Step 3 of [dsl-useq.md](dsl-useq.md). Not run yet. Step 2 (uSEQ's suites and `mlsp_test` cross-built and run on the SSP, `eval_cold` timed) is not done either; steps 18-19 below cover the timing it was to measure.

## Setup

- Install, with Synthor stopped (`kill -9 $(pidof SYNTHOR)`, then power-cycle after): `make deploy-mod MOD=mlsp`.

- Examples: `ssh root@192.168.1.6 mkdir -p /media/BOOT/mlsp && scp -O plugins/mlsp/examples/*.useq root@192.168.1.6:/media/BOOT/mlsp/`.

- Keyboard: USB through a hub, or port 7777 over ssh (`ssh -L 7777:127.0.0.1:7777 root@192.168.1.6`).

- At hand: a multimeter or scope; an envelope; a clock source (16ths); a trigger source; a CV source.

Each step lists what to do, then what should happen. Code goes in the Code view; Ctrl-Enter runs it. Voltages are at the jacks.

| # | Do | Expect |
|-|-|-|
| 1 | Add `mlsp`; patch out 1; show it | listed as `mlsp`; inputs `In 1`-`In 8`, outputs `d1`-`d4`, `a1`-`a4`; status `bar N  120 bpm` and `d1 d2 d3 d4 a1 a2 a3 a4`; the bar count moves |
| 2 | Scope outs 1-8 | gates 0 / 5 V on outs 1-4; ramps and steps 0-5 V on outs 5-8 |
| 3 | `(a1 0.5)` / `(a2 1)` / `(a3 2)`; Ctrl-Enter | out 5 at 2.5 V, out 6 and out 7 at 5.0 V, each within 10 mV; outs 1-4 and 8 at 0 V |
| 4 | `(set-bpm 60)` / `(d1 (sqr beat))` | out 1: 0.5 s high, 0.5 s low |
| 5 | `(d1 (sqr nope))` | `line 1: Unknown 'nope' ...`; the previous program keeps running |
| 6 | `(d1 (sqr beat)` | `line 1: form not closed`; the previous program keeps running |
| 7 | Below the program, a blank line, then `(a4 0.2)`; cursor on it; Ctrl-E | `ok`; out 8 at 1.0 V; out 1 keeps its gate without a glitch |
| 8 | Ctrl-E on `(+ 1 2)` | `= 3` |
| 9 | Ctrl-. | `cleared`; every out at 0 V; status `no outputs` |
| 10 | `; @p1 level 0 1` / `(a1 p1)`; turn encoder 1 | the page shows `level`; out 5 follows, 0 to 5 V |
| 11 | `(a1 ain3)` / `(d1 in3)`; a CV into in 3, 0 to 5 V | out 5 follows in 3; out 1 goes high above 1 V |
| 12 | `; @clock` / `(d1 (sqr beat))`; a 16th clock into in 2 | out 1 gates once per 4 pulses; status `clock In 2 x4` and the clock's tempo; stopping the clock halts the gate |
| 13 | `; @reset` / `(d1 (sqr bar))`; triggers into in 1 | each trigger restarts the bar: out 1 goes high at the trigger |
| 14 | `; @clock midi`; MIDI clock and Start/Stop into the general panel's MIDI IN | beat follows the clock; Start returns to bar 1; Stop halts |
| 15 | Ctrl-E on `(useq-pause)`, then `(useq-play)` | status `paused`; outputs hold; then resume |
| 16 | Ctrl-S; save the preset; reload it | `BOOT/mlsp/live.useq` written; the program is back and runs |
| 17 | Add a second `mlsp` with a different program | both run, each on its own outputs; a name defined in one is unknown in the other |
| 18 | Note the DSP figure with the built-in running | record it; the engine runs 3000 ticks a second |
| 19 | Run `clocked-euclid.useq` repeatedly with Ctrl-Enter while scoping out 5 | no step or dropout at each run; if there is, `eval_cold` holds a block (Threads in dsl-useq.md) |
| 20 | Remove one `mlsp` while it runs; add it again | no crash; the new one runs the built-in |
