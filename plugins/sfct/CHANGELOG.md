# Changelog

sfct follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(SFCT VERSION ...)`) and shown at the top right of the screen.

## [0.2.0] - 2026-10-24

- `norns` mode has three tracks, six voices, as norns softcut has. Track 4 keeps its settings, is silent, and is skipped by TRK+; `4 loop` keeps four. A preset that plays track 4 in `norns` mode loses it.

- A mono file loaded on a linked track fills its L and R buffers. It filled only the current voice's, and no action copied it to the other side. An unlinked voice still loads into its own buffer, as Save and Clear treat it.

- Holding EN- or EN+ moves to the previous or next voice, at the same page, through every track's L and R voices. It switched between the two sides of one track.

- A preset naming channel 2 or above of a file with more than two channels crashed: the check used the file's channel count, but only the first two channels are read. The load now fails and the preset keeps its file name.

## [0.1.0] - 2026-10-01

First release. Eight mono softcut voices in four stereo tracks, over two shared buffers (`norns` mode) or a pair per track (`4 loop` mode). Per voice: rate, loop, record and overdub levels, crossfade shapes, and input and output state-variable filters with all of softcut's settings. Linked L/R voices stay sample-locked. Per track: input routing (stereo, L, R or L+R), CV for rate, position, record gate and cut, feedback from any track, loop length synced to MIDI clock, and a phase trigger output. Load and save WAV files; clear a loop region, or everything; record a single pass. See [README.md](README.md).

softcut-lib is patched to fix upstream's polarity-inverted recording, and to skip per-sample work with byte-identical output (21% less time per block on x86).
