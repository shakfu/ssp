# Changelog

ttyp follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(TELETYPE VERSION ...)`).

## [Unreleased]

- An empty audio block read the sample before the input buffer.

## [0.1.0] - 2026-10-13

First release: monome teletype 5 (`e390bac`) on the SSP, typed into from a USB keyboard through a USB 2.0 hub, or over ssh from a computer. Eight trigger inputs, In, four TR and four CV outputs, PARAM on an encoder, and a range per CV output and for In for the SSP's +/-5 V. The status panel shows the inputs, patched or not, and the outputs. Presets save all 32 scenes. GPL-3.0; see [NOTICE.md](NOTICE.md). Start with [README-getting-started.md](README-getting-started.md).

Disk (button 3) reads and writes scenes as teletype's text files in `/media/BOOT/teletype`, as plugging a USB stick into teletype does: every slot out to `ttNNs.txt`, then each `ttNN.txt` into its slot. Three example scenes ship in `teletype/examples`.

The firmware is built from upstream sources against monome-rack's mock hardware and embedded in `ttyp.so`; each instance loads its own copy from memory, since the firmware keeps its state in globals. Keys from a USB keyboard reach it as HID boot reports, so key repeat and modifiers are the firmware's own. Replacement headers stand in for the Atmel ASF headers monome-rack compiles, whose licence restricts redistribution to Atmel microcontrollers.
