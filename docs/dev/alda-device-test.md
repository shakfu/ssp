# alda: device test

Step 3 of [dsl-alda.md](dsl-alda.md). Step 2 passed on 2026-10-17 (`plugins/alda/tests/run_on_ssp.sh`, Synthor running): psnd's shared suite (20) and conformance run (40), and `alda_test`, all pass on the SSP. The slowest example score builds in 5.1 ms (`all-instruments.alda`).

## Setup

- Install, with Synthor stopped (`kill -9 $(pidof SYNTHOR)`, then power-cycle after): `make deploy-mod MOD=alda`.
- Examples: `ssh root@192.168.1.6 mkdir -p /media/BOOT/alda && scp -O plugins/alda/examples/*.alda root@192.168.1.6:/media/BOOT/alda/`.
- Keyboard: USB through a hub, or port 7775 over ssh (`ssh -L 7775:127.0.0.1:7775 root@192.168.1.6`).
- At hand: a multimeter or scope; an oscillator with a 1 V/oct input; an envelope; a trigger source.
- For step 20: `dexd`. For steps 20c-26: a USB MIDI interface on the SSP, cabled to a synth, or to a computer running a MIDI monitor (`aseqdump -p <port>` on Linux).

Each step lists what to do, then what should happen. Code goes in the Code view; Ctrl-Enter runs it. Voltages are at the jacks.

| # | Do | Expect |
|-|-|-|
| 1 | Add `alda` to a patch; show it | listed as `alda`; inputs labelled `Reset`, `Clock`, `In 3`-`In 8`, outputs `Gate 1`-`Gate 4`, `CV 1`-`CV 4`; status `bar 1/2  120 bpm` and `lead bass`; the bar count moves |
| 2 | Scope outs 1, 2, 5, 6 | gates 0 / 5 V on outs 1-2; stepped pitch CV on outs 5-6 |
| 3 | `(quant 100)` / `piano: o5 c1`; Ctrl-Enter | after the running loop ends: out 1 at 5 V, out 5 at +1.000 V |
| 4 | `piano: o3 c1 \| o4 c1 \| o5 c1 \| o6 c1` | out 5 steps -1, 0, +1, +2 V, each within 5 mV; an oscillator on out 5 plays four Cs in octave tune |
| 5 | `piano: o0 c1 \| o9 c1 \| o9 b1` | out 5 at -4 V (MIDI 12), +5 V (MIDI 120), then +5 V again: Alda caps B9 at MIDI 127, which the plugin clamps to the SSP's +5 V |
| 6 | Load (button 5), `four-voices.alda` | four gates on outs 1-4, four CVs on outs 5-8; status `lead bass arp drone` |
| 7 | Patch out 1 to an envelope; `piano: c8 c c c c c c c` | eight distinct envelopes: Alda's 90% notes leave a gap |
| 8 | `(quant 100)` / `piano: c8 c c c c c c c` | one long gate: a repeated note with no gap does not retrigger (expected; note it) |
| 9 | `piano: c4 (((` | an error with `live.alda:1:` and the column; the previous score keeps playing |
| 10 | `piano: c4 x9` | `cannot interpret the score`; the previous score keeps playing |
| 11 | `# @loop 2` / `piano: c4 d e f` | only c and d play, then repeat; status `bar 1/1` |
| 12 | Patch a trigger into in 1 during a sustained note of `piano: c1 d1` | the loop restarts at each trigger: c again; the envelope on out 1 retriggers (the 1 ms gap) |
| 13 | Ctrl-E on any line | `runs whole programs only: Ctrl-Enter` |
| 14 | Ctrl-S | `BOOT/alda/live.alda` written; Load lists it |
| 15 | Save the patch; reload it | the score is back and plays |
| 16 | Add a second `alda` with a different score | both play, each on its own outputs |
| 17 | Remove one `alda` while it plays; add it again | no crash; the new one plays the built-in |
| 18 | Note the DSP figure with `four-voices.alda` playing | record it; expect well under 1% (inference: one event check and eight writes per sample) |
| 19 | Turn the encoders on the control pages | nothing changes: `p1`..`p16` do nothing yet |
| 20 | Add `dexd`; in its general panel (RS + LS), MIDI IN; patch an `INPT` output to one of `alda`'s inputs | the list shows `alda` beside the USB devices; choose it, and `dexd` plays `alda`'s score. Passed 2026-10-17. Without the patch Synthor does not run `alda`, and nothing plays |
| 20b | Add a second `alda` | `dexd`'s MIDI IN list also shows `alda 2` |
| 20c | `alda`'s general panel: MIDI OUT to the interface; the built-in plays | on the monitor, per loop: program change, CC 11 = 100, CC 10 = 64, then notes on two channels |
| 21 | Load `four-voices.alda` | notes on four channels; each note-on has its note-off; velocities about 69 (Alda's default mf) |
| 22 | `midi-percussion: o2 c8 d c d` | notes on channel 10 |
| 23 | `piano: c1 d1`; a trigger into in 1 during a note | a note-off for the sounding note, then program, CCs and c again |
| 24 | During a long note, Ctrl-Enter a new score; wait for the loop's end | the old note ends at the loop's end; no note hangs on the synth |
| 25 | Remove `alda` from the patch while it plays | All Notes Off (CC 123) on channels 1-16; nothing hangs |
| 26 | Unplug the MIDI interface while it plays; plug it in again | no crash; within a few seconds MIDI resumes (the general panel's reconnection) |
| 27 | With `dexd` playing `alda`'s score, Ctrl-. | silence at once; status `stopped`; no note hangs |
| 28 | Load `four-voices.alda` while stopped | it starts at once, not at a loop's end |
| 28b | Open the Code view | the status line shows `bar n/m  bpm` (or `stopped`), and ends `C-Enter run  C-. stop  C-L clear  C-R reset  C-S save  C-O open  Esc pages`, not cut off at the right |
| 29 | Ctrl-L; then Ctrl-L again | the view empties and the score plays on; then the text is back |
| 30 | Ctrl-R; then Ctrl-S | silence and an empty view; Ctrl-S writes `BOOT/alda/live.alda` |
| 30b | Ctrl-Shift-S, Backspace the name, type `mine`, Enter | `saved .../mine.alda`; Ctrl-S then writes `mine.alda`; Load lists it |
| 30c | Ctrl-Shift-S, type an existing name, Enter | `exists: Enter again overwrites`; Esc leaves the file as it was |
| 30d | Control page 1: Transpose +12 | the CV and the MIDI notes an octave up; notes held when it moved still end |
| 30e | Tempo x2; Velocity x0.5; Swing 66% | twice as fast; `dexd` quieter; a shuffle on 16ths |
| 30f | Page 2: Mute 1 on | voice 1's gate silent, its MIDI part silent; the other voices play |
| 31 | `# @clock` / `piano: c4 d e f`; a clock (16ths) into In 2 | one note every 4 pulses; the status bpm follows the clock's; stop the clock: the score halts; change its rate: the notes follow |
| 32 | `# @clock 24` with a 24 ppqn clock on In 2 | as step 31, at 24 pulses a note |
| 32b | No `# @clock` line; page 3 Clock to `analog`, a clock on In 2 | as step 31; Clock to `free`: the score's tempo, the clock ignored; back to `score`: free, as the score has no line |
| 32c | Clock `analog` while a score plays free | no jump back: it goes on from the 16th it is in, at the clock's rate |
| 33 | `# @clock midi`; a DAW or sequencer sending MIDI clock to `alda`'s MIDI IN (general panel) | Start plays from the beginning in time; Stop halts and releases notes; Continue resumes where it stopped |
| 27 | With `dexd` playing `alda`'s score, Ctrl-. | silence at once; status `stopped`; no note hangs |
| 28 | Load `four-voices.alda` while stopped | it starts at once, not at a loop's end |
| 29 | Ctrl-L; then Ctrl-L again | the view empties and the score plays on; then the text is back |
| 30 | Ctrl-R; then Ctrl-S | silence and an empty view; Ctrl-S writes `BOOT/alda/live.alda` |
| 31 | `# @clock` / `piano: c4 d e f`; a clock (16ths) into In 2 | one note every 4 pulses; the status bpm follows the clock's; stop the clock: the score halts; change its rate: the notes follow |
| 32 | `# @clock 24` with a 24 ppqn clock on In 2 | as step 31, at 24 pulses a note |
| 33 | `# @clock midi`; a DAW or sequencer sending MIDI clock to `alda`'s MIDI IN (general panel) | Start plays from the beginning in time; Stop halts and releases notes; Continue resumes where it stopped |

Optional, with the monitor's timestamps: note the spread of note-on times in `piano: c16 c c c c c c c` at 120 bpm. Expect the 125 ms steps within about 3.7 ms (a 128-frame block plus the 1 ms poll; inferred).

## Results

2026-10-18, user: step 31 passes. Without `# @clock`, a clock on In 2 is ignored with no sign of it; the user expected it to drive the tempo.
