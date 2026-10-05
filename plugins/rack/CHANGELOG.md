# Changelog

rack is a fork of TheTechnobear's trax 1.0.1. It will follow [semantic versioning](https://semver.org) from its first release. Design notes: [docs/dev/rack-design.md](../../docs/dev/rack-design.md).

## Unreleased

Renamed from trax: the plugin is `rack.so` (plugin code `RACK`, manufacturer `SF00`), and presets live in `/rack_presets`. The binary preset format is unchanged, so binary presets load in either.

Presets can be JSON, loaded and saved on the device. A preset name ending in `.json` saves as JSON. See [README-json-presets.md](README-json-presets.md).

A track's routing in a JSON preset is a gain matrix: rows are source jacks, columns destination jacks, and each nonzero cell is a wire. A `dc` row adds offsets. JSON saves write the matrix; presets that list `wires` still load.

```json
"matrix": { "rows": ["in:0", "1:Out L"], "cols": ["1:In L", "out:0"], "gain": [[1, 0], [0, 0.5]] }
```

Modules run in wiring order, not slot order. Previously, a wire from a higher slot to a lower one read the previous block, a 128-sample delay. Now only a wire on a feedback loop does, and the loop is broken at its lowest slot. Presets with a backward wire that is not part of a loop sound slightly different.

A read-only routing view shows each track's wiring as a module grid in execution order, with the wires of the selected cell alongside. Wires that read the previous block are orange; self-wires, which carry nothing, are red. On the track page, Down opens it and Up returns.

Fixed four data races between the audio thread and the UI or preset loading. The first three could crash. Changing a module removed its wires without the track lock, while the audio thread iterated them. The audio thread cleared and summed into a module's buffer without that module's lock, while loading resized the buffer. Loading a binary preset rebuilt the wire list without the track lock. Track mute and level were plain variables written by the UI and read by the audio thread. `tests/test_host.py` runs `Track` under ThreadSanitizer to catch regressions.
