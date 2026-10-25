# Changelog

mlsp follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(MLSP VERSION ...)`).

## [Unreleased]

First version. ModuLisp, the language of the uSEQ module, on uSEQ's signal engine at b628d0e, through the fork shakfu/useq, loaded from the card or typed on a USB keyboard. GPL-3.0, for uSEQ. Experimental: releases leave it out until uSEQ's software licence is formally clarified.

- Jacks: `d1`-`d4` on Out 1-4 as gates, `a1`-`a4` on Out 5-8 as 0-5 V. The program reads In 1-8 as `in1`-`in8` (gates) and `ain1`-`ain8` (0-5 V), and the encoders as `p1`-`p16`.

- Time: the program's tempo, or `; @clock [N]` for pulses on In 2, or `; @clock midi`; `; @reset` resets on In 1. The repo's `Clock` drives it, as for `alda` and `gseq`.

- Ctrl-Enter runs a program only if all of it compiles on a fresh engine, then applies it to the running one, so outputs both assign keep their state. uSEQ's module applies the forms before an error; a typo then leaves half a program running. Ctrl-E runs forms on the running engine, as uSEQ's editor does.

- The fork's SSP commit adds the inputs and controls, and two bound checks. Cells are indexed by symbol ID, and the interner is process-wide and never shrinks: without the check, the 513th distinct name in one Synthor session wrote past the cell array. A form over 255 tokens was truncated without an error.
