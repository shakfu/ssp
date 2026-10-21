# Changelog

brks follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(BREAKS VERSION ...)`).

## [0.1.0] - 2026-10-19

First version: the breakbeat slicer firmware of `_core` v8.1.0 on the SSP, built through `_core`'s desktop engine with one hook added to its firmware. It plays sample-manager card folders: 16 banks of 16 sliced samples, one bank in RAM. The panel's five knobs, Tap, Mode, Clock, and the Amen, Break and Sample CVs work as on the hardware. Trig, Clock Out and stereo audio are its outputs. GPL-3.0-only; see [NOTICE.md](NOTICE.md).

The firmware's Tap-held functions are encoders of their own: Volume, Filter and Gating, plus Play (Tap + Mult) on button 3. Pressing Tap through the firmware's panel also taps the tempo, so two edits within a second would set it. `Source/ssp_api.inc` instead applies the state the firmware's Tap branches apply, without a press. Other button combinations are not reachable.

Mult is the Clock Out encoder, and Bank is Left/Right (PG-/PG+) and In 5. The firmware counts a Mult or Bank press as a tap only if it is released within 200 ms, and most presses on the SSP's buttons take longer, so the on-screen Bank button did nothing. Its tap also only steps forward. The bank step instead runs the tap's branch inside the firmware's input loop, through a hook `scripts/fetch_core.sh` adds; see [docs/dev/breaks.md](../../docs/dev/breaks.md). The LED ring, MIDI and the Grimoire editor are not included.

Tempo sets the tempo as Tap does, and follows it from Tap, In 1 and the card. Its default is not applied, so a new instance plays at the card's tempo. In 1 takes 2 pulses a beat, as on the hardware; another rate needs a clock divider before it.

The status panel names the effects the Grimoire bank allows and the ones playing. The firmware numbers the two lists differently, so numbers did not match the card's `effectN` files.

The engine is compiled Thumb-2 on the SSP. clang 18 and 20 miscompile it in ARM mode, and the SSP build crashed at its first instance; see [docs/dev/breaks.md](../../docs/dev/breaks.md).
