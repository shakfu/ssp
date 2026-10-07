# TODO

## Engine ports

From sk-engines (`~/projects/sk-engines/src/engine`), on `plugins/common/engine` as radio, csound and chuck are; see [docs/dev/engines.md](docs/dev/engines.md). In this order. Each module name is at most four characters and its `PLUGIN_CODE` is the name in capitals.

1. **edrums -> `edrm`**. Euclidean drum machine with synthesized voices.
   - Clock: the Daisy platform transport has no SSP equivalent. Use MIDI clock (as sfct's `Tempo.h`), a clock CV input, or an internal tempo.
   - Pattern storage: replace `daisy::PersistentStorage` with the plugin state.
2. **pstretch -> `strc`**. Real-time PaulStretch. Brings its own `fft.h`. The SSP may allow larger windows than the Daisy.
3. **bard -> `bard`**. Spoken-word player with bookmarks and WSOLA pitch-keep. Reuses radio's file streaming: move `Station` and `Stream` from `plugins/radio` into `plugins/common/engine` first.
4. **glitch -> `gltc`**. 12 lo-fi noise algorithms from Noisferatu. GPLv3, compatible with this repo's AGPL-3.0; keep its licence header.

Per port: native engine tests and a ThreadSanitizer run, a `plugins/common/tests` entry, README and CHANGELOG, then a check on the SSP.

## Device checks

- MIDI on `csnd` and `chuk`: see `plugins/csound/TODO.md` and `plugins/chuck/TODO.md`.
