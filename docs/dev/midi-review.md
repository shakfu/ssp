# MIDI review: csnd, chuk, fstr, scsy

Status as of 2026-10-10. MIDI notes from a module's general panel input, in the four script modules.

## Done (uncommitted)

| Module | Notes reach the program as | Status panel | Example |
|-|-|-|-|
| `csnd` | Csound MIDI opcodes (unchanged) | `MIDI notes: N` (new) | `fm-midi.csd` |
| `chuk` | globals `midiNotes[128]` (note << 8 \| velocity), `midiCount`, `midiEvent` | `MIDI notes: N` | `midi.ck` (rewritten) |
| `fstr` | Faust polyphony (`mydsp_poly`, ungrouped) when options declare `[nvoices:N]` | `MIDI notes: N` | `poly.dsp` (new) |
| `scsy` | a voice per note for a def with `gate` | `MIDI notes: N, held: H` | `midi_saw.scsyndef` |

- Host tests: `make test`, 161 passed. Engine tests send notes and check outputs. The `chuk` and `fstr` race tests add a MIDI thread.

- `chuk`: 6 new `tsan.supp` entries for ChucK globals. They rest on reading ChucK's single-producer ring, not on an instrumented libchuck.

- Built for the SSP; deployed to the card on 2026-10-10 (md5 checked). READMEs and changelogs updated.

## Device results (user, 2026-10-10)

1. Controller test, with Synthor's `MIDI` module as MIDI-to-CV:
   - `quadosc.csd` tracked pitch, through `cv 1`.

   - `midi.ck` cutoff moved, through `cv 1` on cutoff.

   - `fm-midi.csd` was silent.

   - The kernel log had no `midi in connected` line, so no module had a MIDI IN device. Only CV reached the modules.

2. MIDI IN set in each module's general panel:
   - Piano (ALSA `24-0`): `csnd` played, with unstable audio. `chuk`, `fstr` and `scsy` did not play. Which program each had loaded was not recorded.

   - `midisend` (`130-0`): reported as not working. No notes were written to `/tmp/midisend` during that test, so this is likely untested, not failed.

3. The left USB port does not recognise MIDI controllers. The right port then cannot take the Ethernet dongle, so a controller test has no ssh. A USB hub would allow both.

Kernel log: `connectMidiInDevice` logged 26 connects on one thread, alternating `24-0` and `130-0`. After the test, a single JUCE client (131) had one port, subscribed to `midisend`.

## Device results (user, 2026-10-11)

`midisend` as each module's MIDI IN, channel OMNI, "Note In" lit; note 60, then a C major chord, sent over ssh:

| Module | Program | Heard | Status |
|-|-|-|-|
| `csnd` | `fm-midi.csd` | note, then chord | `MIDI notes` rose by 4 per round |
| `chuk` | `midi.ck` | note, then chord | `MIDI notes: 8` |
| `fstr` | `poly.dsp` | note, then chord; encoders show no note controls | `MIDI notes: 4` |
| `scsy` | `midi_saw.scsyndef` | note, chord, then a 20-note cluster with 16 voices | `MIDI notes: 24, held: 0` after release |

So all four pass. On 2026-10-10 the `midisend` test sent no notes, and the `chuk`, `fstr` and `scsy` built-ins ignore notes. One `chuk` attempt played the built-in drone because `midi.ck` was not loaded yet.

`fstr` crashed Synthor while `poly.dsp` was being chosen in the Load browser, and again on patch reload. gdb on the SSP: libfaust's `assert(x.lo() > 0)` in `intervalPow.cpp:89` aborted on its compile thread, whose FPSCR was `0x63000093` (FZ and DN set). Fixed: `FaustRuntime::compile` runs in `FE_DFL_ENV`; `testFlushToZero` reproduces it on x86 with DAZ. Deployed 2026-10-11: `poly.dsp` loads and plays.

## SD card corruption (2026-10-11)

After a later `fstr` crash (no gdb attached; cause unknown), the kernel set BOOT (`mmcblk1p6`, FAT32) read-only: `fat_free_clusters: deleting FAT entry beyond EOF`. The user imaged it (`~/Backups/ssp-boot-backup.img`, 20,971,520,000 bytes) and ran `fsck.vfat -a`.

- Damaged: macOS `.Spotlight-V100` and `.Trashes` (Sep 2026), `samples/AMEN/cw_amen08_165.wav` (second copy renamed `FSCK0000.000`), 588 orphaned clusters (9.6 MB, freed), dirty bit.

- Not listed: `plugins/`, `faust/`, `chuck/`, `csound/`, `scsy/`, `presets/`, `rack_presets/`.

- After repair, `fsck.vfat -n` reports only the boot sector's NT dirty flag (offset 65) differing from its backup.

Re-tested with gdb attached: the crash recurred when opening `chuk` after many modules, with no `fstr` loaded. SIGSEGV with the program counter on an unmapped address. Cause: `~BaseProcessor` detached its async thread and slept 100 ms, so the thread could outlive the object and the `.so`; `rack` detached its track threads the same way. Both now join. After deploying all 12 plugins, opening and closing many modules (28 module destructions) ran without a crash (user, 2026-10-11).

## Next session (2026-10-10 plan; steps 1-2 done 2026-10-11)

1. One module at a time, with Ethernet in:
   - Load `fm-midi.csd`, set MIDI IN to `midisend`, channel OMNI, "Note In" lit.

   - Send `echo "on 60 100" > /tmp/midisend` over ssh, and check the status panel's `MIDI notes`.

   - Repeat for `midi.ck`, `poly.dsp`, `midi_saw.scsyndef`.

   - `midisend` lives in `/tmp`: after a reboot, run `make midisend`.

2. If `chuk`, `fstr` or `scsy` still get no notes while `csnd` does:
   - Is `MIDI notes` shown? If it is, the note arrived and the program is at fault; if not, delivery is.

   - Does "Note In" survive state restore? `midiFromXml` defaults it to false (`BaseProcessor.cpp:110`), and the constructor's `noteInput(true)` comes before any restore.

   - Several modules in one process: JUCE's ALSA client is shared. The log showed one input port for several modules. Check whether a second module's `openDevice` replaces the first one's port.

3. `csnd` with the piano: works without issues on a later test (user, 2026-10-11).

4. Inverted `midiDeviceChangeLock`: replaced by a `std::mutex` (2026-10-11). On the SSP, 11 rapid MIDI IN switches between `midisend` and NONE each reconnected, with no crash, and the module then played a chord. The unplug-and-replug reconnect is untested: the SSP's left USB port does not take MIDI controllers, and a USB hub did not work.

5. Optional, offered: show MIDI IN and "Note In" in each status panel; make the `chuk`, `fstr` and `scsy` built-ins play notes, as `csnd`'s does.

## Open from before

- Voltmeter check of CV output levels: all four modules pass (user, 2026-10-11).

- `fstr`: whether `mydsp_poly::keyOn` allocates on the audio thread is unchecked.
