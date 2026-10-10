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

## Next session

1. One module at a time, with Ethernet in:
   - Load `fm-midi.csd`, set MIDI IN to `midisend`, channel OMNI, "Note In" lit.
   - Send `echo "on 60 100" > /tmp/midisend` over ssh, and check the status panel's `MIDI notes`.
   - Repeat for `midi.ck`, `poly.dsp`, `midi_saw.scsyndef`.
   - `midisend` lives in `/tmp`: after a reboot, run `make midisend`.
2. If `chuk`, `fstr` or `scsy` still get no notes while `csnd` does:
   - Is `MIDI notes` shown? If it is, the note arrived and the program is at fault; if not, delivery is.
   - Does "Note In" survive state restore? `midiFromXml` defaults it to false (`BaseProcessor.cpp:110`), and the constructor's `noteInput(true)` comes before any restore.
   - Several modules in one process: JUCE's ALSA client is shared. The log showed one input port for several modules. Check whether a second module's `openDevice` replaces the first one's port.
3. `csnd` unstable audio with the piano: measure DSP load. Check whether note bursts or aftertouch overflow the 256-entry queue, and whether `fm-midi.csd` voices pile up (`madsr` release).
4. Fix the inverted `midiDeviceChangeLock.test_and_set()` in `setMidiInDevice` and `checkMidiDevices` (`BaseProcessor.cpp:271`, `:322`). `checkMidiDevices` returns when the flag is clear and leaves it set. `setMidiInDevice` proceeds when it is held. So the two can run `connectMidiInDevice` at once.
5. Optional, offered: show MIDI IN and "Note In" in each status panel; make the `chuk`, `fstr` and `scsy` built-ins play notes, as `csnd`'s does.

## Open from before

- Voltmeter check of CV output levels (TODO.md).
- `fstr`: whether `mydsp_poly::keyOn` allocates on the audio thread is unchecked.
