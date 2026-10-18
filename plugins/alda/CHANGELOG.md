# Changelog

alda follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(ALDA VERSION ...)`).

## [0.1.0] - 2026-10-18

First version. An Alda score, loaded from the card or typed on a USB keyboard, plays in a loop. GPL-3.0, as psnd.

- Jacks: the first four parts are mono voices, each on `Gate k` (5 V while a note sounds) and `CV k` (1 V/octave, C4 at 0 V). `Reset` restarts the loop; `Clock` takes a clock.
- MIDI: every part, on Alda's channels with its velocities, programs, track volume and pan, to an ALSA port of its own (`alda`, `alda 2`, ...) that other modules choose as MIDI IN, and to the general panel's MIDI OUT. A restart releases sounding notes; removing the module sends All Notes Off. A thread of the plugin's sends the messages, not the audio thread, since ALSA writes can block.
- Time: the loop is the score rounded up to a 4/4 bar, or `# @loop N` quarter notes. `# @clock [N]` follows pulses on `Clock`, 4 per quarter note by default; `# @clock midi` follows MIDI clock with Start, Stop and Continue. Each pulse sets the position, so the score stays locked and halts with the clock.
- Editing: a new score starts when the running loop ends, or at once after Ctrl-. (stop). A score that fails to parse leaves the old one playing. Ctrl-R stops, empties the view and forgets the program file. The status line shows the bar and tempo, or `stopped`. The code is coloured by psnd's Alda grammar.
- Controls: Transpose, Tempo, Velocity, Swing (16ths) and Mute 1-4, neutral by default. Clock picks free, analog, MIDI, or the score's `# @clock` line (the default); a clock patched to `Clock` was ignored unless the score asked for it, with no sign of that. The clock source belongs to the patch, so the same score plays free or synced without an edit, and Load keeps it. Patches saved before Clock open at `score`; they would otherwise read it as `free`.

psnd's Alda core builds unchanged: a stand-in header replaces libremidi, which it includes only for pointer fields it leaves NULL, and empty functions satisfy the link for psnd's MIDI player, which the plugin never calls. psnd's shared suite (20 scores) and conformance run (40 examples against Alda's output) pass against that build. Design: [docs/dev/dsl-alda.md](../../docs/dev/dsl-alda.md).
