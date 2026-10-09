# Changelog

rack is a fork of TheTechnobear's trax 1.0.1 and numbers its own versions, independent of trax's, and follows [semantic versioning](https://semver.org) from 0.2.0, its first release. The version is set in `CMakeLists.txt` (`project(RACK VERSION ...)`) and shown on the SSP. Design notes: [docs/dev/rack-design.md](../../docs/dev/rack-design.md).

## [0.2.0] - 2026-10-09

Renamed from trax: the plugin is `rack.so` (plugin code `RACK`, manufacturer `SF00`), and presets live in `/media/BOOT/rack_presets`. The binary preset format is unchanged, so binary presets load in either.

Presets moved from `/rack_presets` on the root filesystem to the BOOT partition. The root filesystem is ext4 and owned by root, so copying presets from a desktop needed sudo, and macOS and Windows cannot mount it; BOOT is FAT. Move any presets saved on the device from `/rack_presets` to `/media/BOOT/rack_presets`.

Presets can be JSON, loaded and saved on the device. A preset name ending in `.json` saves as JSON. See [README-json-presets.md](README-json-presets.md).

A track's routing in a JSON preset is a gain matrix: rows are source jacks, columns destination jacks, and each nonzero cell is a wire. A `dc` row adds offsets. JSON saves write the matrix; presets that list `wires` still load.

```json
"matrix": { "rows": ["in:0", "1:Out L"], "cols": ["1:In L", "out:0"], "gain": [[1, 0], [0, 0.5]] }
```

Modules run in wiring order, not slot order. Previously, a wire from a higher slot to a lower one read the previous block, a 128-sample delay. Now only a wire on a feedback loop does, and the loop is broken at its lowest slot. Presets with a backward wire that is not part of a loop sound slightly different.

A routing view shows each track's wiring as a module grid in execution order: rows are sources, columns destinations, headed by module name. Beside it, a jack matrix shows the selected module pair: source outputs by destination inputs, with gain as brightness. Encoders 3 and 4 pick a source and destination jack. `Level` switches them to offset and gain; gain is the connection, so turning it up from 0 adds the wire and turning it to 0 removes it. Device edits keep gain between 0 and 1. Wires that read the previous block are orange; self-wires, which carry nothing, are red. On the track page, Down opens it and Up returns. Full screen, the routing view uses the whole 1600 x 480 screen rather than the compact layout. Its button box is gone (the title line shows soft key 1's Level state), so the module grid runs to the bottom. The grid leaves out empty slots, which have no jacks, so its cells grow as a track uses fewer modules. The jack matrix starts where the grid ends, so a track with few modules gives wide pairs more room. Jack cells print their gain when it fits at a readable size, and the cursor is a thick frame.

In `Level` mode, offset and gain move 0.1 a detent, or 0.01 while the encoder is held, as parameters do elsewhere; 0.01 alone made patching slow. Encoder 4's press, which connects or disconnects, now acts on release, so a fine adjustment does not toggle the wire.

Fixed a crash raising gain on the SSP; the same steps no longer crash (2026-10-09). The cause is inferred, since this change both limits inputs and clamps offsets. A module's input is the sum of its wires, and nothing limited it: a feedback loop near unity gain grows to infinity and NaN, and a module that indexes tables with its input, such as `clds`, can then read out of bounds. rack now sets non-finite input samples to 0 and limits the rest to +-2 (+-10 V) before each module runs. Offsets set on the device, which had no limit, stay between -1 and 1 like gain's 0 to 1.

Fixed the jack matrix showing every wire near full brightness on the SSP. The cells were filled with a translucent colour, and the view is painted over its previous frame, so the fills built up within a few frames. Cells are now painted opaque.

Fixed JSON saves of wires at gain 0. They were written as 0 cells, which read back as no wire. A gain-0 wire with an offset also wrote a `dc` value into a column with no wire, and the reload then rejected the whole matrix, leaving the track unrouted. Saves now leave out cells that sum to 0. A `dc` with no wire left in its column is dropped and logged.

Fixed four data races between the audio thread and the UI or preset loading. The first three could crash. Changing a module removed its wires without the track lock, while the audio thread iterated them. The audio thread cleared and summed into a module's buffer without that module's lock, while loading resized the buffer. Loading a binary preset rebuilt the wire list without the track lock. Track mute and level were plain variables written by the UI and read by the audio thread. `tests/test_host.py` runs `Track` under ThreadSanitizer to catch regressions.
