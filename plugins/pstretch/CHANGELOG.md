# Changelog

pstretch follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(PSTRETCH VERSION ...)`) and shown at the top right of the screen.

## [0.1.1] - 2026-10-08

The mix page (A/B, Route, Window) could not be reached: the editor stacked every page as a row, and the screen holds six. It now scrolls. A long Up or Down jumps between decks, a long Right to the mix page and a long Left back.

## [0.1.0] - 2026-10-08

First release, as the module `strc`. Two PaulStretch decks, each stretching its input, a capture of it, or a clip streamed from a folder. Per deck: stretch, diffusion, pitch, tone and mix; freeze and grab; an LFO or envelope follower on diffusion, stretch or tone, with LFO and gate outputs. A window of 4096 to 32768 samples. See [README.md](README.md).

Two changes to the sk-engines voice:

- A grain reads `window * pitch` input samples. sk-engines bounded the live read head and the file read-ahead by one window. Pitched up, a grain then read past the newest input into audio about 5 seconds old.
- Capture now records into a second ring while it loops the first. sk-engines stopped recording on capture, so Grab and the gate only restarted the old loop.
