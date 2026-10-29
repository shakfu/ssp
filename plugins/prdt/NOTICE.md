# Licence notice

`prdt` is AGPL-3.0, as the rest of this repository is. It links these libraries, whose notices go with every copy
of `prdt.so`:

- [libpd](https://github.com/libpd/libpd) 0.16.1, in `external/libpd`: BSD-3-Clause, [LICENSE-libpd.txt](LICENSE-libpd.txt).
- [Pure Data](https://puredata.info) 0.56-5, by Miller Puckette and others, in `external/libpd/pure-data`:
  BSD-3-Clause, [LICENSE-pure-data.txt](LICENSE-pure-data.txt).
- [cyclone](https://github.com/porres/pd-cyclone) 0.9-5, by Krzysztof Czaja, Alexandre Porres and others, in
  `external/pd-cyclone`: BSD-3-Clause, [LICENSE-cyclone.txt](LICENSE-cyclone.txt).
- [ELSE](https://github.com/porres/pd-else) 1.0-0 RC-15, by Alexandre Torres Porres and others, in
  `external/pd-else`: mostly WTFPL-2, [LICENSE-else.txt](LICENSE-else.txt). Some sources carry their own licence:
  `conv~` GPL-3.0-or-later, `fdn.rev~` and `giga.rev~` GPL-2.0-or-later, both of which AGPL-3.0 can combine with;
  the `bl.*` oscillators BSD-2-Clause; `osc.*`, `bassdrum~`, cJSON and FFTease (`pvretune~`) MIT. The texts are in
  `external/pd-else/LICENSE`.

The copies of Pure Data, cyclone and ELSE carry local patches, listed in `external/libpd/PATCHES.md`,
`external/pd-cyclone/PATCHES.md` and `external/pd-else/PATCHES.md`.
