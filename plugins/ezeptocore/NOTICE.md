# Licence notice

`ezep` is GPL-3.0-only, not the AGPL-3.0 of the rest of this repository.

`ezep.so` contains the EZEPTOCORE firmware from [schollz/_core](https://github.com/schollz/_core) at `e702abc` (v8.1.0), Copyright Zack Scholl, GPL-3.0-only. It is built from:

- `lib/` and `main.c`: the firmware, through `lib/core_engine/prepare.py`, _core's generator of its desktop engine;
- `rack/src/Storage.cpp`: _core's reader of sample-manager card folders, from its VCV Rack module;
- `lib/pcg_basic.c`: the PCG random number generator, Copyright 2014 Melissa O'Neill, Apache License 2.0, in [LICENSE-pcg-Apache-2.0.txt](LICENSE-pcg-Apache-2.0.txt).

[`scripts/fetch_core.sh`](../../scripts/fetch_core.sh) fetches these unchanged. It appends [`Source/ssp_api.inc`](Source/ssp_api.inc) to the generator's firmware source before the generator's second stage. That file follows the firmware's Tap-held branches, so it is a derived work, also GPL-3.0-only.

GPLv3 and AGPL-3.0 section 13 each allow a work under one to be combined with a work under the other. The files in `Source/` are distributed under GPL-3.0-only too. The framework in `plugins/common`, which `ezep.so` links, keeps its AGPL-3.0.

`make install-ezep` downloads _core's EZEPTOCORE default banks to the card. They are not part of this repository or its releases.

The corresponding source is this repository at the release's tag, with the _core sources that `scripts/fetch_core.sh` fetches at the commit above. The full GPLv3 text is in [LICENSE](LICENSE).
