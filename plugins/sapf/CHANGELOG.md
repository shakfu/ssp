# Changelog

sapf follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(SAPF VERSION ...)`).

## [Unreleased]

First version: the sapf-sc interpreter and its own scsynth on the SSP, for live coding from a USB keyboard or over ssh. A multi-line editor evaluates blocks with Ctrl-Enter; Ctrl-. interrupts and stops. The prelude loads at start, definitions persist, and the code is saved with the patch. Ctrl-O lists the files in `BOOT/sapf`, among them sapf's examples, which install with `sapf`. Eight inputs and outputs, as `scsy`'s. GPL-3.0, as sapf.

`ola` voices start on the World's sample clock, not the wall clock, so they stay in time when the host's audio drifts or stalls. The keyboard moves between `sapf` and `ttyp` with the screen; see [docs/dev/sapf-plugin.md](../../docs/dev/sapf-plugin.md).
