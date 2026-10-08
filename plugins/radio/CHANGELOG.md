# Changelog

radio follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(RADIO VERSION ...)`) and shown at the top right of the screen.

## 0.1.1 - 2026-10-08

A long Up or Down jumps between decks, a long Right (Prog +) to the mix page and a long Left back.

## 0.1.0 - 2026-10-07

First release, as the module `rdio`. Two decks over a library of banks of `.wav` and `.raw` stations, streamed from the card on a free-running clock. Per deck: station, start, speed, static, level and bank, with CV for station, start, speed (V/oct) and reset. A/B crossfade, three stereo routes, and a separate output per deck. Every switch fades; Start can jump at once. Banks and stations sort with numbers by value. A Radio Music `SETTINGS.TXT` in the chosen root sets the fade and the Start modes. See [README.md](README.md).
