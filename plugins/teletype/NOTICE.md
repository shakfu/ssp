# Licence notice

`ttyp` is GPLv3, not the AGPL-3.0 of the rest of this repository.

`ttyp.so` contains, as an embedded library, the teletype firmware built from:

- [monome teletype](https://github.com/monome/teletype) at `e390bac` and its [libavr32](https://github.com/monome/libavr32) at `b37dc3f`, by monome and contributors;
- the mock hardware layer of [monome-rack](https://github.com/Dewb/monome-rack) at `1f5a9ca`, by Michael Dewberry and contributors;
- `firmware/ttyp_hid.c` and `firmware/gitversion.c` from this repository.

Each upstream project ships the GNU GPL version 2 text and states no version in its source files. Under section 9 of GPL version 2, a program that specifies no version may be used under any version published by the Free Software Foundation; `ttyp` uses version 3. GPLv3 and AGPL-3.0 section 13 each allow a work under one to be combined with a work under the other, so the files in `Source/` are distributed under GPLv3 too.

The corresponding source is this repository at the release's tag, with the upstream sources that [`scripts/fetch_teletype.sh`](../../scripts/fetch_teletype.sh) fetches at the commits above. The full GPLv3 text is in [LICENSE](LICENSE).
