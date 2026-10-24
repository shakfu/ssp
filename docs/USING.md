# Using the modules

What every module in this repo shares on the SSP. Each module's README covers only its own controls.

## Installing

1. Download `shakfu-ssp-plugins-<VERSION>.zip` from the [releases](https://github.com/shakfu/ssp/releases).
2. Copy the contents of its `BOOT/` folder onto the SD card's BOOT partition. `BOOT/plugins` holds the modules; other folders hold the files some modules read, such as `BOOT/csound` and `BOOT/sapf`.
3. Restart the SSP. Synthor lists the new modules under their 4-letter names (`gseq`, `sfct`, ...).

A module's README names any other files it needs. From a source build, `make install MOD=<name>` copies one module to a mounted card, and the `make install-*` targets copy the extra files ([BUILDING.md](BUILDING.md)).

## Card paths

The card's BOOT partition is `/media/BOOT` on the SSP, and `/media/$USER/BOOT` when the card is mounted on Linux. The READMEs write a path on it as `BOOT/<folder>`.

## A module runs only while a jack is patched

Synthor processes a module only while at least one of its jacks is patched. An unpatched module makes no sound, keeps no time and sends no MIDI. A virtual patch is enough: for a module that only sends MIDI, patch `INPT` IN1 to its first input.

## Buttons

The screen labels the arrow buttons: Up and Down `EN-` and `EN+`, Left and Right `PG-` and `PG+`. Some modules relabel them for their own use, such as `TRK-`/`TRK+` in `sfct` and `ROW-`/`ROW+`/`STP-`/`STP+` in `gseq`.

| Button | Does |
|-|-|
| Up, Down | the previous or next page |
| long Up, long Down | the first page of the previous or next page group, where a module has groups |
| Left, Right | module-specific. In modules with global pages, long Right goes to them and long Left returns |
| LS and RS together | open or close the general panel |
| buttons 1-8 | the eight soft keys under the screen, labelled on screen |

Encoders 1-4 edit the four controls on the page. Turning while pressing gives finer steps where a module supports it.

## The general panel

LS and RS pressed together open the general panel, which every module has. It holds the module's MIDI settings: MIDI channel, MIDI IN, MIDI OUT, Note In, and the MIDI automation that maps a CC, note or pressure to a parameter. A preset saves them. Inside `rack`, a module's compact view opens the panel with Up and Down pressed together.

## USB devices

The SSP's left USB port takes MIDI controllers and keyboards only through a powered USB hub. A USB keyboard needs a USB 2.0 hub: plugged in directly, or through a USB 1.1 hub, it is not detected.
