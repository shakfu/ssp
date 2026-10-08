# Changelog

bard follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(BARD VERSION ...)`) and shown at the top right of the screen.

## 0.1.0 - 2026-10-08

First release, as the module `bard`. Two spoken-word decks over a library of shelves of `.wav` and `.raw` books, streamed from the card. Bookmarks from a text sidecar, or placed automatically; read, recite and wander sequencing; positions resumed across sessions. Per deck: rate with pitch keep, voice colour, room, seam fade, and ducking of the other deck. CV for book, mark, volume and next; envelope and bookmark-gate outputs. See [README.md](README.md).
