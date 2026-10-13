# Changelog

ttyp follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(TELETYPE VERSION ...)`).

## [Unreleased]

First version: teletype `e390bac` on monome-rack's mock hardware, embedded in the plugin and loaded from memory once per instance. A USB keyboard or a localhost TCP port types into it. Eight trigger inputs, IN, four TR and four CV outputs, PARAM on an encoder, and a range per CV output and for IN, for the SSP's +/-5 V. Presets save the scenes. The status panel shows In and the trigger inputs' states beside teletype's display. See [README.md](README.md).

Disk (button 3) reads and writes scenes as teletype's text files in `/media/BOOT/teletype`, as plugging a USB stick into teletype does: all 32 scenes out to `ttNNs.txt`, then each `ttNN.txt` into its slot. Programs can be written ahead of time this way. `examples/tt00.txt` is one.

Keys from a USB keyboard reach the firmware as HID boot reports, which its own HID timer polls, so key repeat is teletype's. monome-rack's `hidMessage` calls the key handler directly instead; `ttyp` uses that path only for typed text from the TCP port.
