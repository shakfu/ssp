# Changelog

ezep follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(EZEPTOCORE VERSION ...)`).

## [0.1.0] - 2026-10-19

First version: the EZEPTOCORE firmware of _core v8.1.0 on the SSP, built unchanged through _core's desktop engine. It plays sample-manager card folders: 16 banks of 16 sliced samples, one bank in RAM. The panel's five knobs, its four buttons, Clock, and the Amen, Break and Sample CVs work as on the hardware. Trig, Clock Out and stereo audio are its outputs. GPL-3.0-only; see [NOTICE.md](NOTICE.md).

The firmware's Tap-held functions are encoders of their own: Volume, Filter and Gating, plus Play (Tap + Mult) on button 6. Pressing Tap through the firmware's panel also taps the tempo, so two edits within a second would set it. `Source/ssp_api.inc` instead applies the state the firmware's Tap branches apply, without a press. Other button combinations are not reachable. The LED ring, MIDI and the Grimoire editor are not included.

The engine is compiled Thumb-2 on the SSP. clang 18 and 20 miscompile it in ARM mode, and the SSP build crashed at its first instance; see [docs/dev/ezeptocore.md](../../docs/dev/ezeptocore.md).
