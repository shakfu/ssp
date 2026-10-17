# Changelog

alda follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(ALDA VERSION ...)`).

## [Unreleased]

First version: an Alda score, from the card or a USB keyboard, plays in a loop as gates on outs 1-4 and 1 V/octave pitch CV on outs 5-8, one mono voice per part. In 1 restarts the loop. The loop is the score rounded up to a 4/4 bar, or `# @loop N` quarter notes. A new score starts when the running loop ends; one that fails to parse leaves the old one playing. The code is coloured by psnd's Alda grammar. GPL-3.0, as psnd.

MIDI out, to an ALSA port of its own, `alda`, which other modules such as `dexd` choose as MIDI IN, and to the general panel's MIDI OUT device: every part on Alda's channels, with its velocities, programs, track volume and pan. The outputs are labelled `Gate 1`-`Gate 4` and `CV 1`-`CV 4`, a pair per voice; `ScriptProcessor` takes a plugin's own bus names for this. A restart releases sounding notes; removing the module sends All Notes Off. The audio thread queues the messages lock-free and a thread of the plugin's sends them every 1 ms, since ALSA writes can block.

psnd's Alda core builds unchanged: a stand-in header replaces libremidi, which it includes only for pointer fields it leaves NULL, and empty functions satisfy the link for psnd's MIDI player, which the plugin never calls. psnd's shared suite (20 scores) and conformance run (40 examples against Alda's output) pass against that build. Design: [docs/dev/dsl-alda.md](../../docs/dev/dsl-alda.md).

Ctrl-. stops the score at once, and the next score starts at once instead of at the loop's end; a score with no notes stops too. Ctrl-L empties the code view, Ctrl-R resets (stop, empty, no program file). A `# @clock` comment follows a clock on In 2, at 4 or N pulses per quarter note, or MIDI clock with Start, Stop and Continue (`# @clock midi`): each pulse moves the score to the next pulse's position and it never runs past the next, so it stays locked and halts with the clock.

The Code view's status line lists `alda`'s keys, and shows the bar and tempo, or `stopped`, in place of `running`.
